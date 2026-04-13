param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$Compiler = "",
    [ValidateSet("arm64-v8a", "armeabi-v7a", "x86_64", "x86", "riscv64", "all")]
    [string]$Abi = "arm64-v8a",
    [switch]$Clean,
    [switch]$Bundle,
    [switch]$NoDaemon,
    [switch]$SkipNativeBuild,
    [switch]$SkipJavaInstall,
    [string]$JavaDownloadUrl = "https://api.adoptium.net/v3/binary/latest/17/ga/windows/x64/jdk/hotspot/normal/eclipse",
    [switch]$SkipSdkInstall,
    [string]$AndroidCmdlineToolsUrl = "https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip",
    [string]$NdkVersion = "26.3.11579264",
    [int]$ApiLevel = 24,
    [int]$NativeJobs = 4,
    [switch]$NoSignApk,
    [string]$KeystorePath = "",
    [string]$KeystoreAlias = "",
    [string]$KeystorePassword = "",
    [string]$KeyPassword = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$androidDir = Join-Path $repoRoot "Android"
$gradleWrapper = Join-Path $androidDir "gradlew.bat"

if (-not (Test-Path $gradleWrapper)) {
    throw "Android Gradle wrapper not found: $gradleWrapper"
}

function Test-JavaAvailable {
    $cmd = Get-Command java -ErrorAction SilentlyContinue
    if (-not $cmd) { return $false }
    $prevEap = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        & java -version 2>$null | Out-Null
        return ($LASTEXITCODE -eq 0)
    } finally {
        $ErrorActionPreference = $prevEap
    }
}

function Use-LocalJavaIfPresent {
    $localJavaExe = Get-ChildItem -Path (Join-Path $repoRoot ".tools\java") -Recurse -Filter "java.exe" -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $localJavaExe) { return $false }
    $javaHome = Split-Path -Parent (Split-Path -Parent $localJavaExe.FullName)
    $javaBin = Join-Path $javaHome "bin"
    $env:JAVA_HOME = $javaHome
    if ($env:Path -notlike "*$javaBin*") {
        $env:Path = "$javaBin;$env:Path"
    }
    return $true
}

function Ensure-Java {
    Use-LocalJavaIfPresent | Out-Null
    if (Test-JavaAvailable) { return }
    if ($SkipJavaInstall) {
        throw "Java is required but not installed, and -SkipJavaInstall was set."
    }

    $toolsDir = Join-Path $repoRoot ".tools"
    $javaDir = Join-Path $toolsDir "java"
    $downloadPath = Join-Path $toolsDir "jdk17.zip"
    $downloadMetaPath = "$downloadPath.meta"
    $javaCacheTag = "jdk17-cache-v2|$JavaDownloadUrl"
    $extractDir = Join-Path $javaDir "extract"

    New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
    New-Item -ItemType Directory -Force -Path $javaDir | Out-Null
    Remove-Item -Recurse -Force $extractDir -ErrorAction SilentlyContinue

    $useCachedJavaZip = (Test-Path $downloadPath) -and (Test-Path $downloadMetaPath) -and ((Get-Content -Path $downloadMetaPath -Raw -ErrorAction SilentlyContinue).Trim() -eq $javaCacheTag)
    if ($useCachedJavaZip) {
        Write-Host "[INFO] Reusing cached JDK archive: $downloadPath"
    } else {
        Write-Host "[INFO] Java not found. Downloading JDK 17 from website..."
        try {
            Invoke-WebRequest -Uri $JavaDownloadUrl -OutFile $downloadPath -MaximumRedirection 10
            Set-Content -Path $downloadMetaPath -Value $javaCacheTag -Encoding ascii
        } catch {
            throw "Java download failed from $JavaDownloadUrl. $_"
        }
    }

    Write-Host "[INFO] Extracting JDK..."
    try {
        Expand-Archive -Path $downloadPath -DestinationPath $extractDir -Force
    } catch {
        throw "Downloaded JDK archive could not be extracted. $_"
    }

    $javaExe = Get-ChildItem -Path $extractDir -Recurse -Filter "java.exe" -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $javaExe) {
        throw "Could not find java.exe after extraction."
    }

    $javaHome = Split-Path -Parent (Split-Path -Parent $javaExe.FullName)
    $javaBin = Join-Path $javaHome "bin"

    $env:JAVA_HOME = $javaHome
    if ($env:Path -notlike "*$javaBin*") {
        $env:Path = "$javaBin;$env:Path"
    }

    [Environment]::SetEnvironmentVariable("JAVA_HOME", $javaHome, "User")
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    if (-not $userPath) { $userPath = "" }
    if ($userPath -notlike "*$javaBin*") {
        $newUserPath = if ([string]::IsNullOrWhiteSpace($userPath)) { $javaBin } else { "$javaBin;$userPath" }
        [Environment]::SetEnvironmentVariable("Path", $newUserPath, "User")
    }

    if (-not (Test-JavaAvailable)) {
        throw "Java setup completed, but java is still not available in this shell. Open a new terminal and rerun."
    }
}

function Resolve-AndroidSdkDir {
    $candidates = @()
    if ($env:ANDROID_SDK_ROOT) { $candidates += $env:ANDROID_SDK_ROOT }
    if ($env:ANDROID_HOME) { $candidates += $env:ANDROID_HOME }
    $candidates += @(
        (Join-Path $env:LOCALAPPDATA "Android\Sdk"),
        (Join-Path $env:USERPROFILE "AppData\Local\Android\Sdk"),
        "C:\Android\Sdk"
    )
    foreach ($p in $candidates) {
        if ([string]::IsNullOrWhiteSpace($p)) { continue }
        if ((Test-Path (Join-Path $p "platform-tools")) -or (Test-Path (Join-Path $p "cmdline-tools"))) {
            return $p
        }
    }
    return $null
}

function Install-AndroidSdk {
    if ($SkipSdkInstall) {
        throw "Android SDK not found and -SkipSdkInstall was set."
    }

    $toolsDir = Join-Path $repoRoot ".tools"
    $sdkDir = Join-Path $toolsDir "android-sdk"
    $zipPath = Join-Path $toolsDir "android-cmdline-tools.zip"
    $zipMetaPath = "$zipPath.meta"
    $sdkCacheTag = "android-cmdline-tools-cache-v2|$AndroidCmdlineToolsUrl"
    $extractRoot = Join-Path $toolsDir "android-cmdline-tools-extract"
    $latestDir = Join-Path $sdkDir "cmdline-tools\latest"
    $latestTagPath = Join-Path $latestDir ".cache_tag"

    New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
    New-Item -ItemType Directory -Force -Path $sdkDir | Out-Null
    $hasPatchedCmdlineTools = (Test-Path (Join-Path $latestDir "bin\sdkmanager.bat")) -and (Test-Path $latestTagPath) -and ((Get-Content -Path $latestTagPath -Raw -ErrorAction SilentlyContinue).Trim() -eq $sdkCacheTag)
    if (-not $hasPatchedCmdlineTools) {
        Remove-Item -Recurse -Force $extractRoot -ErrorAction SilentlyContinue
        Remove-Item -Recurse -Force $latestDir -ErrorAction SilentlyContinue

        $useCachedSdkZip = (Test-Path $zipPath) -and (Test-Path $zipMetaPath) -and ((Get-Content -Path $zipMetaPath -Raw -ErrorAction SilentlyContinue).Trim() -eq $sdkCacheTag)
        if ($useCachedSdkZip) {
            Write-Host "[INFO] Reusing cached Android cmdline-tools archive: $zipPath"
        } else {
            Write-Host "[INFO] Android SDK not found. Downloading Android command-line tools..."
            try {
                Invoke-WebRequest -Uri $AndroidCmdlineToolsUrl -OutFile $zipPath -MaximumRedirection 10
                Set-Content -Path $zipMetaPath -Value $sdkCacheTag -Encoding ascii
            } catch {
                throw "Android command-line tools download failed from $AndroidCmdlineToolsUrl. $_"
            }
        }

        Write-Host "[INFO] Extracting Android command-line tools..."
        Expand-Archive -Path $zipPath -DestinationPath $extractRoot -Force

        $srcCmdlineTools = Join-Path $extractRoot "cmdline-tools"
        if (-not (Test-Path $srcCmdlineTools)) {
            throw "Extracted Android tools folder not found at $srcCmdlineTools"
        }

        New-Item -ItemType Directory -Force -Path (Join-Path $sdkDir "cmdline-tools") | Out-Null
        Copy-Item -Path $srcCmdlineTools -Destination $latestDir -Recurse -Force
        Set-Content -Path $latestTagPath -Value $sdkCacheTag -Encoding ascii
    } else {
        Write-Host "[INFO] Reusing existing Android cmdline-tools at $latestDir"
    }

    $sdkManager = Join-Path $latestDir "bin\sdkmanager.bat"
    if (-not (Test-Path $sdkManager)) {
        throw "sdkmanager.bat not found at $sdkManager"
    }

    $env:ANDROID_HOME = $sdkDir
    $env:ANDROID_SDK_ROOT = $sdkDir

    Write-Host "[INFO] Accepting Android SDK licenses..."
    1..200 | ForEach-Object { "y" } | & $sdkManager "--sdk_root=$sdkDir" "--licenses" | Out-Null

    Write-Host "[INFO] Installing Android SDK packages..."
    & $sdkManager "--sdk_root=$sdkDir" "platform-tools" "platforms;android-34" "build-tools;34.0.0" "ndk;$NdkVersion"
    if ($LASTEXITCODE -ne 0) {
        throw "sdkmanager package install failed with exit code $LASTEXITCODE."
    }

    [Environment]::SetEnvironmentVariable("ANDROID_HOME", $sdkDir, "User")
    [Environment]::SetEnvironmentVariable("ANDROID_SDK_ROOT", $sdkDir, "User")
}

function Ensure-AndroidSdk {
    $sdkDir = Resolve-AndroidSdkDir
    if (-not $sdkDir) {
        Install-AndroidSdk
        $sdkDir = Resolve-AndroidSdkDir
        if (-not $sdkDir) {
            throw "Android SDK install attempted, but SDK path still not found."
        }
    }

    $env:ANDROID_HOME = $sdkDir
    $env:ANDROID_SDK_ROOT = $sdkDir

    $localPropsPath = Join-Path $androidDir "local.properties"
    $sdkDirForProps = $sdkDir -replace "\\", "/"
    $newSdkLine = "sdk.dir=$sdkDirForProps"

    $lines = @()
    if (Test-Path $localPropsPath) {
        $lines = @(Get-Content -Path $localPropsPath)
    }
    $found = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match "^\s*sdk\.dir\s*=") {
            $lines[$i] = $newSdkLine
            $found = $true
            break
        }
    }
    if (-not $found) {
        $lines += $newSdkLine
    }
    Set-Content -Path $localPropsPath -Value $lines -Encoding ascii
}

function Resolve-AndroidNdkDir {
    $candidates = @()
    if ($env:ANDROID_NDK_HOME) { $candidates += $env:ANDROID_NDK_HOME }
    if ($env:ANDROID_NDK_ROOT) { $candidates += $env:ANDROID_NDK_ROOT }
    if ($env:ANDROID_SDK_ROOT) {
        $ndkRoot = Join-Path $env:ANDROID_SDK_ROOT "ndk"
        if (Test-Path $ndkRoot) {
            $candidates += (Get-ChildItem -Path $ndkRoot -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | ForEach-Object { $_.FullName })
        }
    }
    if ($env:ANDROID_HOME) {
        $ndkRoot = Join-Path $env:ANDROID_HOME "ndk"
        if (Test-Path $ndkRoot) {
            $candidates += (Get-ChildItem -Path $ndkRoot -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | ForEach-Object { $_.FullName })
        }
    }

    foreach ($p in $candidates) {
        if ([string]::IsNullOrWhiteSpace($p)) { continue }
        if (Test-Path (Join-Path $p "toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe")) {
            return $p
        }
    }
    return $null
}

function Ensure-AndroidNdk {
    $ndkDir = Resolve-AndroidNdkDir
    if ($ndkDir) {
        $env:ANDROID_NDK_HOME = $ndkDir
        $env:ANDROID_NDK_ROOT = $ndkDir
        return $ndkDir
    }

    if ($SkipSdkInstall) {
        throw "Android NDK not found and -SkipSdkInstall was set."
    }

    $sdkManager = Join-Path $env:ANDROID_SDK_ROOT "cmdline-tools\latest\bin\sdkmanager.bat"
    if (-not (Test-Path $sdkManager)) {
        throw "sdkmanager not found at $sdkManager"
    }

    Write-Host "[INFO] Installing Android NDK $NdkVersion..."
    & $sdkManager "--sdk_root=$env:ANDROID_SDK_ROOT" "ndk;$NdkVersion" | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "NDK install failed with exit code $LASTEXITCODE."
    }

    $ndkDir = Resolve-AndroidNdkDir
    if (-not $ndkDir) {
        throw "Android NDK install attempted, but no usable NDK was found."
    }
    $env:ANDROID_NDK_HOME = $ndkDir
    $env:ANDROID_NDK_ROOT = $ndkDir
    return $ndkDir
}

function Resolve-LatestBuildToolsDir {
    $root = Join-Path $env:ANDROID_SDK_ROOT "build-tools"
    if (-not (Test-Path $root)) { return $null }
    $dir = Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        Select-Object -First 1
    if (-not $dir) { return $null }
    return $dir.FullName
}

function Ensure-DebugKeystore {
    $signDir = Join-Path $repoRoot ".tools\android-signing"
    $debugKeystore = Join-Path $signDir "debug.keystore"
    if (Test-Path $debugKeystore) { return $debugKeystore }

    New-Item -ItemType Directory -Force -Path $signDir | Out-Null
    $keytool = Get-Command keytool -ErrorAction SilentlyContinue
    if (-not $keytool) {
        $candidate = Join-Path $env:JAVA_HOME "bin\keytool.exe"
        if (Test-Path $candidate) { $keytool = Get-Item $candidate }
    }
    if (-not $keytool) {
        throw "keytool not found. Cannot create debug keystore for APK signing."
    }

    & $keytool.Source -genkeypair `
        -keystore $debugKeystore `
        -storepass "android" `
        -keypass "android" `
        -alias "androiddebugkey" `
        -keyalg "RSA" `
        -keysize 2048 `
        -validity 10000 `
        -dname "CN=Android Debug,O=Android,C=US"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to create debug keystore."
    }
    return $debugKeystore
}

function Ensure-SdlIncludeShims {
    param(
        [string]$SdlRoot
    )
    $shimRoot = Join-Path $repoRoot ".build\include"
    $sdlLower = Join-Path $shimRoot "sdl3"
    $sdlImageNs = Join-Path $shimRoot "SDL3_image"
    $sdlTtfNs = Join-Path $shimRoot "SDL3_ttf"
    $sdlMixerNs = Join-Path $shimRoot "SDL3_mixer"

    New-Item -ItemType Directory -Force -Path $sdlLower | Out-Null
    New-Item -ItemType Directory -Force -Path $sdlImageNs | Out-Null
    New-Item -ItemType Directory -Force -Path $sdlTtfNs | Out-Null
    New-Item -ItemType Directory -Force -Path $sdlMixerNs | Out-Null

    $copyShimHeader = {
        param(
            [string]$SourcePath,
            [string]$DestinationPath
        )
        if (-not (Test-Path $SourcePath)) { return }
        if (Test-Path $DestinationPath) { return }
        try {
            Copy-Item -Path $SourcePath -Destination $DestinationPath -Force
        } catch {
            Write-Host "[WARN] Could not update shim header (in use): $DestinationPath"
        }
    }

    $sdlHeaderDir = Join-Path $SdlRoot "include\SDL3"
    if (Test-Path $sdlHeaderDir) {
        Get-ChildItem -Path $sdlHeaderDir -Filter *.h -File -ErrorAction SilentlyContinue | ForEach-Object {
            & $copyShimHeader $_.FullName (Join-Path $sdlLower $_.Name)
        }
    }
    $imageHeader = Join-Path $sdlHeaderDir "SDL_image.h"
    $ttfHeader = Join-Path $sdlHeaderDir "SDL_ttf.h"
    $mixerHeader = Join-Path $sdlHeaderDir "SDL_mixer.h"
    & $copyShimHeader $imageHeader (Join-Path $sdlImageNs "SDL_image.h")
    & $copyShimHeader $ttfHeader (Join-Path $sdlTtfNs "SDL_ttf.h")
    & $copyShimHeader $mixerHeader (Join-Path $sdlMixerNs "SDL_mixer.h")

    return $shimRoot
}

function Get-PlatformerSourceList {
    $cmakePath = Join-Path $repoRoot "CMakeLists.txt"
    if (-not (Test-Path $cmakePath)) {
        throw "CMakeLists.txt not found: $cmakePath"
    }

    $lines = Get-Content -Path $cmakePath
    $inPlatformerBlock = $false
    $srcList = New-Object 'System.Collections.Generic.List[string]'

    foreach ($line in $lines) {
        $trimmed = $line.Trim()
        if (-not $inPlatformerBlock) {
            if ($trimmed -match "^add_executable\s*\(\s*platformer\b") {
                $inPlatformerBlock = $true
                continue
            }
            continue
        }

        if ($trimmed -eq ")") {
            break
        }

        if ($trimmed -match "^(src/[A-Za-z0-9_\-./]+\.cpp)\s*$") {
            $src = $Matches[1] -replace "/", "\"
            $src = $src -replace "\\", "/"
            if ($src -notin $srcList) {
                $srcList.Add($src)
            }
        }
    }

    if ($srcList.Count -eq 0) {
        throw "Could not extract platformer sources from CMakeLists.txt."
    }

    return @($srcList)
}

function Build-NativeAndroidWindows {
    param(
        [string]$OneAbi,
        [string]$NdkDir
    )

    $abiPrefixMap = @{
        "arm64-v8a"   = "aarch64-linux-android"
        "armeabi-v7a" = "armv7a-linux-androideabi"
        "x86_64"      = "x86_64-linux-android"
        "x86"         = "i686-linux-android"
        "riscv64"     = "riscv64-linux-android"
    }
    if (-not $abiPrefixMap.ContainsKey($OneAbi)) {
        throw "Unsupported ABI for Windows native build: $OneAbi"
    }

    $toolBin = Join-Path $NdkDir "toolchains\llvm\prebuilt\windows-x86_64\bin"
    $target = "$($abiPrefixMap[$OneAbi])$ApiLevel"
    $cxx = Join-Path $toolBin "$target-clang++.cmd"
    if (-not (Test-Path $cxx)) { $cxx = Join-Path $toolBin "$target-clang++.exe" }
    if (-not (Test-Path $cxx)) {
        throw "NDK compiler not found for ABI=$OneAbi at $toolBin"
    }

    $sdlRoot = Join-Path $repoRoot "deps\android"
    $sdlLibDir = Join-Path $sdlRoot "lib\$OneAbi"
    if (-not (Test-Path (Join-Path $sdlRoot "include\SDL3\SDL.h"))) {
        throw "Missing staged SDL headers at $sdlRoot\include\SDL3\SDL.h"
    }
    if (-not (Test-Path $sdlLibDir)) {
        throw "Missing staged SDL libs for ABI=$OneAbi at $sdlLibDir"
    }

    $jsonRoot = Join-Path $repoRoot "third_party"
    if (-not (Test-Path (Join-Path $jsonRoot "nlohmann\json.hpp"))) {
        throw "Missing nlohmann json header: $jsonRoot\nlohmann\json.hpp"
    }

    $shimRoot = Ensure-SdlIncludeShims -SdlRoot $sdlRoot
    $outDir = Join-Path $repoRoot "build\android\$OneAbi"
    $objDir = Join-Path $outDir "obj-win"
    New-Item -ItemType Directory -Force -Path $objDir | Out-Null

    $curlRoot = Join-Path $repoRoot "deps\android-curl"
    $curlLibDir = Join-Path $curlRoot "lib\$OneAbi"
    $hasCurl = (Test-Path (Join-Path $curlRoot "include\curl\curl.h")) -and (Test-Path (Join-Path $curlLibDir "libcurl.so"))

    $cppFlags = @(
        "-I$jsonRoot",
        "-I$(Join-Path $sdlRoot 'include')",
        "-I$shimRoot",
        "-I$(Join-Path $shimRoot 'sdl3')"
    )
    if ($hasCurl) {
        $cppFlags += "-I$(Join-Path $curlRoot 'include')"
        $cppFlags += "-DHAVE_CURL=1"
    } else {
        $cppFlags += "-DHAVE_CURL=0"
    }

    $cxxFlags = @(
        "-std=c++17",
        "-Oz",
        "-DNDEBUG",
        "-DSDL_ENABLE_OLD_NAMES=1",
        "-DPLATFORMER_HAS_SDL3_MIXER=1",
        "-fPIC",
        "-ffunction-sections",
        "-fdata-sections"
    )
    $srcList = Get-PlatformerSourceList

    $maxJobs = if ($NativeJobs -gt 0) { $NativeJobs } else { [Math]::Max(1, [Environment]::ProcessorCount) }
    $jobs = @()
    $objects = @()
    $claimedFiles = New-Object 'System.Collections.Generic.HashSet[string]'
    $slotFiles = @{}
    for ($i = 1; $i -le $maxJobs; $i++) {
        $slotFiles[$i] = New-Object 'System.Collections.Generic.List[string]'
    }
    $nextSlot = 1

    foreach ($srcRel in $srcList) {
        if ($claimedFiles.Contains($srcRel)) {
            Write-Host "[INFO] Skipping already-claimed file: $srcRel ($OneAbi)"
            continue
        }
        [void]$claimedFiles.Add($srcRel)
        while ($jobs.Count -ge $maxJobs) {
            $remaining = @()
            $failedJob = $null
            foreach ($j in $jobs) {
                if ($j.Process.HasExited) {
                    if ($j.Process.ExitCode -ne 0) {
                        $failedJob = $j
                        break
                    }
                } else {
                    $remaining += $j
                }
            }
            if ($failedJob) {
                $retryArgs = @($failedJob.Args)
                $retryOutput = & $failedJob.CompilerPath @retryArgs 2>&1
                if ($LASTEXITCODE -eq 0) {
                    $jobs = $remaining
                    continue
                }
                if (Test-Path $failedJob.StdErrPath) {
                    Write-Host (Get-Content -Path $failedJob.StdErrPath -Raw)
                }
                if (Test-Path $failedJob.StdOutPath) {
                    $outText = Get-Content -Path $failedJob.StdOutPath -Raw
                    if (-not [string]::IsNullOrWhiteSpace($outText)) {
                        Write-Host $outText
                    }
                }
                if ($retryOutput) {
                    $retryText = ($retryOutput | Out-String).Trim()
                    if (-not [string]::IsNullOrWhiteSpace($retryText)) {
                        Write-Host $retryText
                    }
                }
                throw "Compile failed: $($failedJob.SourceRel) for ABI=$OneAbi (thread $($failedJob.SlotId))"
            }
            $jobs = $remaining
            if ($jobs.Count -ge $maxJobs) {
                Start-Sleep -Milliseconds 40
            }
        }
        $src = Join-Path $repoRoot $srcRel
        $obj = Join-Path $objDir (($srcRel -replace "/", "_") -replace "\.cpp$", ".o")
        $objects += $obj
        $slotId = $nextSlot
        $nextSlot++
        if ($nextSlot -gt $maxJobs) { $nextSlot = 1 }
        $slotFiles[$slotId].Add($srcRel)
        Write-Host "[INFO] [T$slotId] Compiling $srcRel ($OneAbi)"
        $args = @()
        $args += $cxxFlags
        $args += $cppFlags
        $args += @("-c", $src, "-o", $obj)
        $logBase = Join-Path $objDir (($srcRel -replace "/", "_") -replace "\.cpp$", "")
        $stdoutLog = "$logBase.stdout.log"
        $stderrLog = "$logBase.stderr.log"
        Remove-Item -Path $stdoutLog, $stderrLog -ErrorAction SilentlyContinue
        $proc = Start-Process -FilePath $cxx -ArgumentList $args -NoNewWindow -PassThru -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog
        $jobs += [PSCustomObject]@{
            Process = $proc
            SourceRel = $srcRel
            StdOutPath = $stdoutLog
            StdErrPath = $stderrLog
            SlotId = $slotId
            CompilerPath = $cxx
            Args = @($args)
        }
    }
    while ($jobs.Count -gt 0) {
        $remaining = @()
        $failedJob = $null
        foreach ($j in $jobs) {
            if ($j.Process.HasExited) {
                if ($j.Process.ExitCode -ne 0) {
                    $failedJob = $j
                    break
                }
            } else {
                $remaining += $j
            }
        }
        if ($failedJob) {
            $retryArgs = @($failedJob.Args)
            $retryOutput = & $failedJob.CompilerPath @retryArgs 2>&1
            if ($LASTEXITCODE -eq 0) {
                $jobs = $remaining
                continue
            }
            if (Test-Path $failedJob.StdErrPath) {
                Write-Host (Get-Content -Path $failedJob.StdErrPath -Raw)
            }
            if (Test-Path $failedJob.StdOutPath) {
                $outText = Get-Content -Path $failedJob.StdOutPath -Raw
                if (-not [string]::IsNullOrWhiteSpace($outText)) {
                    Write-Host $outText
                }
            }
            if ($retryOutput) {
                $retryText = ($retryOutput | Out-String).Trim()
                if (-not [string]::IsNullOrWhiteSpace($retryText)) {
                    Write-Host $retryText
                }
            }
            throw "Compile failed: $($failedJob.SourceRel) for ABI=$OneAbi (thread $($failedJob.SlotId))"
        }
        $jobs = $remaining
        if ($jobs.Count -gt 0) {
            Start-Sleep -Milliseconds 40
        }
    }
    for ($i = 1; $i -le $maxJobs; $i++) {
        if ($slotFiles[$i].Count -gt 0) {
            Write-Host "[INFO] [T$i] Files taken: $($slotFiles[$i] -join ', ')"
        }
    }

    $requiredStatic = @("libSDL3.a", "libSDL3_image.a", "libSDL3_ttf.a", "libSDL3_mixer.a")
    foreach ($lib in $requiredStatic) {
        $p = Join-Path $sdlLibDir $lib
        if (-not (Test-Path $p)) {
            throw "Missing required static SDL lib for ABI=${OneAbi}: $p"
        }
    }
    $allSdlStatic = Get-ChildItem -Path $sdlLibDir -Filter "libSDL*.a" -File -ErrorAction SilentlyContinue |
        Sort-Object Name |
        Select-Object -ExpandProperty FullName
    if (-not $allSdlStatic -or $allSdlStatic.Count -eq 0) {
        throw "No SDL static archives found for ABI=${OneAbi} at $sdlLibDir"
    }
    $nonSdlStatic = Get-ChildItem -Path $sdlLibDir -Filter "lib*.a" -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notlike "libSDL*.a" } |
        Sort-Object Name |
        Select-Object -ExpandProperty FullName

    $ldFlags = @(
        "-L$sdlLibDir",
        "-landroid", "-lOpenSLES", "-llog", "-lGLESv2", "-lEGL",
        "-Wl,--gc-sections",
        "-Wl,--no-undefined",
        "-Wl,--export-dynamic-symbol=Java_org_libsdl_app_SDLActivity_nativeAllowRecreateActivity",
        "-Wl,--export-dynamic-symbol=Java_org_libsdl_app_SDLActivity_nativeCheckSDLThreadCounter"
    )
    if ($hasCurl) {
        $ldFlags += "-L$curlLibDir"
        $ldFlags += "-lcurl"
    }

    $outLib = Join-Path $outDir "libplatformer.so"
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    & $cxx "-shared" @objects "-Wl,--start-group" @allSdlStatic @nonSdlStatic "-Wl,--end-group" @ldFlags "-o" $outLib
    if ($LASTEXITCODE -ne 0) {
        throw "Link failed for ABI=$OneAbi"
    }
    $stripExe = Join-Path $toolBin "llvm-strip.exe"
    if (Test-Path $stripExe) {
        # Keep JNI export symbols (required by SDLActivity native method lookup).
        # --strip-all can remove them and cause UnsatisfiedLinkError at app startup.
        & $stripExe --strip-debug $outLib
        if ($LASTEXITCODE -ne 0) {
            throw "Stripping native lib failed for ABI=$OneAbi"
        }
    }
    Write-Host "[OK] Built native lib: $outLib"
}

function Resolve-TargetAbis {
    if ($Abi -ne "all") {
        return @($Abi)
    }
    $candidates = @("arm64-v8a", "armeabi-v7a", "x86_64", "x86", "riscv64")
    $available = @()
    foreach ($candidate in $candidates) {
        $libDir = Join-Path $repoRoot "deps\android\lib\$candidate"
        if (Test-Path (Join-Path $libDir "libSDL3.a")) {
            $available += $candidate
        }
    }
    if ($available.Count -eq 0) {
        throw "Abi=all was requested, but no staged SDL ABI libs were found under deps/android/lib."
    }
    return $available
}

function Sync-SharedLibrariesForAbi {
    param(
        [string]$OneAbi,
        [string]$DestinationDir,
        [string]$NdkDir
    )

    $searchDirs = @(
        (Join-Path $repoRoot "deps\android\lib\$OneAbi"),
        (Join-Path $repoRoot "deps\android-curl\lib\$OneAbi")
    )

    $copied = @{}
    foreach ($dir in $searchDirs) {
        if (-not (Test-Path $dir)) { continue }
        $libs = Get-ChildItem -Path $dir -Filter "lib*.so" -File -ErrorAction SilentlyContinue
        foreach ($lib in $libs) {
            if ($lib.Name -ieq "libplatformer.so") { continue }
            $dst = Join-Path $DestinationDir $lib.Name
            Copy-Item -Path $lib.FullName -Destination $dst -Force
            $copied[$lib.Name] = $true
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($NdkDir)) {
        $ndkTripleByAbi = @{
            "arm64-v8a"   = "aarch64-linux-android"
            "armeabi-v7a" = "arm-linux-androideabi"
            "x86_64"      = "x86_64-linux-android"
            "x86"         = "i686-linux-android"
            "riscv64"     = "riscv64-linux-android"
        }
        if ($ndkTripleByAbi.ContainsKey($OneAbi)) {
            $triple = $ndkTripleByAbi[$OneAbi]
            $ndkLibCandidates = @(
                (Join-Path $NdkDir "toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\$triple\libc++_shared.so"),
                (Join-Path $NdkDir "toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\$triple\$ApiLevel\libc++_shared.so")
            )
            foreach ($cand in $ndkLibCandidates) {
                if (Test-Path $cand) {
                    $dst = Join-Path $DestinationDir "libc++_shared.so"
                    Copy-Item -Path $cand -Destination $dst -Force
                    $copied["libc++_shared.so"] = $true
                    break
                }
            }
        }
    }

    if ($copied.Count -gt 0) {
        $names = @($copied.Keys) | Sort-Object
        Write-Host "[INFO] Synced shared libs ($OneAbi): $($names -join ', ')"
    }
}

function Invoke-NativeGameBuild {
    if ($SkipNativeBuild) {
        Write-Host "[INFO] Skipping native game code build (-SkipNativeBuild)."
        return
    }

    Write-Host "[INFO] Building native game code for Android (ABI=$Abi)..."

    $isWindowsHost = ($env:OS -eq "Windows_NT")
    if ($isWindowsHost) {
        $ndkDir = Ensure-AndroidNdk
        Write-Host "[INFO] Building native game code on Windows via NDK: $ndkDir"
        $abis = Resolve-TargetAbis

        $jniRoot = Join-Path $androidDir "app\src\main\jniLibs"
        New-Item -ItemType Directory -Force -Path $jniRoot | Out-Null

        foreach ($oneAbi in $abis) {
            Build-NativeAndroidWindows -OneAbi $oneAbi -NdkDir $ndkDir
            $srcSo = Join-Path $repoRoot "build\android\$oneAbi\libplatformer.so"
            $dstDir = Join-Path $jniRoot $oneAbi
            New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
            Copy-Item -Path $srcSo -Destination (Join-Path $dstDir "libplatformer.so") -Force
            Write-Host "[INFO] Synced native lib: $oneAbi"
            Sync-SharedLibrariesForAbi -OneAbi $oneAbi -DestinationDir $dstDir -NdkDir $ndkDir
        }

        $assetsSrc = Join-Path $repoRoot "assets"
        $assetsDst = Join-Path $androidDir "app\src\main\assets"
        if (Test-Path $assetsSrc) {
            Remove-Item -Recurse -Force $assetsDst -ErrorAction SilentlyContinue
            Copy-Item -Path $assetsSrc -Destination $assetsDst -Recurse -Force
            Write-Host "[INFO] Synced assets."
        }
        return
    }

    $bash = Get-Command bash -ErrorAction SilentlyContinue
    if (-not $bash) {
        throw "bash is required to compile native Android game code. Install bash, or run with -SkipNativeBuild."
    }

    Push-Location $repoRoot
    try {
        $env:ABI = $Abi
        & bash "./build/android-prod.sh"
        if ($LASTEXITCODE -ne 0) {
            throw "Native Android game code build failed with exit code $LASTEXITCODE."
        }
        & bash "./build/update-android-app.sh"
        if ($LASTEXITCODE -ne 0) {
            throw "Android app sync failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Remove-Item Env:\ABI -ErrorAction SilentlyContinue
        Pop-Location
    }
}

Ensure-Java
Ensure-AndroidSdk
Invoke-NativeGameBuild

$buildTask = if ($Configuration -eq "Debug") { "assembleDebug" } else { "assembleRelease" }
$bundleTask = if ($Configuration -eq "Debug") { "bundleDebug" } else { "bundleRelease" }

$args = @()
if ($Clean) {
    $args += "clean"
}
$args += $buildTask
if ($Bundle) {
    $args += $bundleTask
}
if ($NoDaemon) {
    $args += "--no-daemon"
}

Push-Location $androidDir
try {
    $maxGradleAttempts = 3
    $gradleAttempt = 0
    while ($true) {
        $gradleAttempt++
        $prevEap = $ErrorActionPreference
        try {
            # Gradle can emit stderr on expected task failures; handle via exit code + retry logic below.
            $ErrorActionPreference = "Continue"
            $joinedArgs = ($args | ForEach-Object {
                if ($_ -match '\s') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
            }) -join ' '
            $cmdLine = '"' + $gradleWrapper + '" ' + $joinedArgs + ' 2>&1'
            & cmd.exe /d /c $cmdLine | Tee-Object -FilePath (Join-Path $androidDir ("build\gradle-last-attempt-" + $gradleAttempt + ".log")) | Out-Host
        } finally {
            $ErrorActionPreference = $prevEap
        }
        $gradleExit = $LASTEXITCODE
        if ($gradleExit -eq 0) { break }

        $logPath = Join-Path $androidDir ("build\gradle-last-attempt-" + $gradleAttempt + ".log")
        $logText = ""
        if (Test-Path $logPath) {
            $logText = Get-Content -Path $logPath -Raw -ErrorAction SilentlyContinue
        }
        $isR8DexLock = ($logText -match "minifyReleaseWithR8") -and
                       ($logText -match "classes\.dex") -and
                       ($logText -match "being used by another process")
        if (-not $isR8DexLock -or $gradleAttempt -ge $maxGradleAttempts) {
            throw "Android Gradle build failed with exit code $gradleExit."
        }

        Write-Host "[WARN] Gradle hit a transient R8 dex file lock. Retrying ($gradleAttempt/$maxGradleAttempts)..."
        & $gradleWrapper --stop | Out-Null
        $lockedDex = Join-Path $androidDir "app\build\intermediates\dex\release\minifyReleaseWithR8\classes.dex"
        if (Test-Path $lockedDex) {
            Remove-Item -Path $lockedDex -Force -ErrorAction SilentlyContinue
        }
        Start-Sleep -Seconds (2 * $gradleAttempt)
    }
}
finally {
    Pop-Location
}

$apkBase = Join-Path $androidDir "app\build\outputs\apk"
$aabBase = Join-Path $androidDir "app\build\outputs\bundle"
$variant = $Configuration.ToLowerInvariant()
$apkVariantDir = Join-Path $apkBase $variant
$aabVariantDir = Join-Path $aabBase $variant
$apkPaths = @()
$signedApkPaths = @()
$aabPath = $null
function Test-IsSignedApkPath([string]$path) {
    if ([string]::IsNullOrWhiteSpace($path)) { return $false }
    $name = [System.IO.Path]::GetFileNameWithoutExtension($path)
    return $name.EndsWith("-signed", [System.StringComparison]::OrdinalIgnoreCase)
}
function Remove-IdsigForApk([string]$apkPath) {
    if ([string]::IsNullOrWhiteSpace($apkPath)) { return }
    $idsig = "$apkPath.idsig"
    if (Test-Path $idsig) {
        Remove-Item -Path $idsig -Force -ErrorAction SilentlyContinue
    }
}

if (Test-Path $apkVariantDir) {
    $apkPaths = Get-ChildItem -Path $apkVariantDir -File -Filter "*.apk" -Recurse -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -ExpandProperty FullName
}
if (Test-Path $aabVariantDir) {
    $aabCandidate = Get-ChildItem -Path $aabVariantDir -File -Filter "*.aab" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($aabCandidate) { $aabPath = $aabCandidate.FullName }
}

if (-not $NoSignApk -and $apkPaths.Count -gt 0) {
    $buildToolsDir = Resolve-LatestBuildToolsDir
    if (-not $buildToolsDir) {
        throw "Android build-tools not found. Cannot sign APK."
    }
    $apksigner = Join-Path $buildToolsDir "apksigner.bat"
    if (-not (Test-Path $apksigner)) {
        throw "apksigner not found at $apksigner"
    }

    $ks = $KeystorePath
    $alias = $KeystoreAlias
    $storePass = $KeystorePassword
    $keyPass = $KeyPassword
    if ([string]::IsNullOrWhiteSpace($ks)) {
        $ks = Ensure-DebugKeystore
        $alias = "androiddebugkey"
        $storePass = "android"
        $keyPass = "android"
    } else {
        if ([string]::IsNullOrWhiteSpace($alias)) { throw "KeystoreAlias is required when KeystorePath is set." }
        if ([string]::IsNullOrWhiteSpace($storePass)) { throw "KeystorePassword is required when KeystorePath is set." }
        if ([string]::IsNullOrWhiteSpace($keyPass)) { $keyPass = $storePass }
        if (-not (Test-Path $ks)) { throw "Keystore not found: $ks" }
    }

    $zipalign = Join-Path $buildToolsDir "zipalign.exe"
    foreach ($apkPath in $apkPaths) {
        $apkInput = $apkPath
        if (Test-Path $zipalign) {
            $aligned = [System.IO.Path]::Combine(
                [System.IO.Path]::GetDirectoryName($apkPath),
                ([System.IO.Path]::GetFileNameWithoutExtension($apkPath) + "-aligned.apk")
            )
            & $zipalign -f 4 $apkPath $aligned
            if ($LASTEXITCODE -eq 0 -and (Test-Path $aligned)) {
                $apkInput = $aligned
            }
        }

        $signedApk = $apkInput
        if ($signedApk.EndsWith("-unsigned.apk", [System.StringComparison]::OrdinalIgnoreCase)) {
            $signedApk = $signedApk.Substring(0, $signedApk.Length - "-unsigned.apk".Length) + "-signed.apk"
        } elseif (-not $signedApk.EndsWith("-signed.apk", [System.StringComparison]::OrdinalIgnoreCase)) {
            $signedApk = [System.IO.Path]::Combine(
                [System.IO.Path]::GetDirectoryName($signedApk),
                ([System.IO.Path]::GetFileNameWithoutExtension($signedApk) + "-signed.apk")
            )
        }

        & $apksigner sign `
            --ks $ks `
            --ks-key-alias $alias `
            --ks-pass "pass:$storePass" `
            --key-pass "pass:$keyPass" `
            --out $signedApk `
            $apkInput
        if ($LASTEXITCODE -ne 0) {
            throw "APK signing failed: $apkPath"
        }

        & $apksigner verify --verbose $signedApk | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Signed APK verification failed: $signedApk"
        }
        Remove-IdsigForApk $signedApk
        $signedApkPaths += $signedApk

        # Keep only the signed output; remove unsigned/intermediate APKs.
        if ($apkPath -ne $signedApk -and (Test-Path $apkPath)) {
            Remove-Item -Path $apkPath -Force -ErrorAction SilentlyContinue
        }
        if ($apkInput -ne $signedApk -and $apkInput -ne $apkPath -and (Test-Path $apkInput)) {
            Remove-Item -Path $apkInput -Force -ErrorAction SilentlyContinue
        }
    }
} else {
    # If signing is skipped, do not treat unsigned APKs as final outputs.
    $signedApkPaths = $apkPaths | Where-Object { Test-IsSignedApkPath $_ }
}
if (Test-Path $apkVariantDir) {
    Get-ChildItem -Path $apkVariantDir -File -Filter "*.apk" -Recurse -ErrorAction SilentlyContinue |
        Where-Object { -not (Test-IsSignedApkPath $_.FullName) } |
        Remove-Item -Force -ErrorAction SilentlyContinue
    Get-ChildItem -Path $apkVariantDir -File -Filter "*.idsig" -Recurse -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
}

$finalDir = Join-Path $repoRoot "final"
New-Item -ItemType Directory -Force -Path $finalDir | Out-Null
# Final folder should only contain signed APKs.
Get-ChildItem -Path $finalDir -File -Filter "*.apk" -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue
if ($signedApkPaths.Count -gt 0) {
    foreach ($signedApk in $signedApkPaths) {
        if (Test-Path $signedApk) {
            $dstApk = Join-Path $finalDir ([System.IO.Path]::GetFileName($signedApk))
            Copy-Item -Path $signedApk -Destination $dstApk -Force
            Remove-IdsigForApk $dstApk
        }
    }
}

Write-Host ""
if ($signedApkPaths.Count -gt 0) {
    Write-Host "[OK] Signed APK(s):"
    foreach ($signedApk in $signedApkPaths) {
        Write-Host "  - $signedApk"
    }
    Write-Host "[OK] Final APK folder: $finalDir"
} else {
    Write-Host "[WARN] APK not found under: $apkVariantDir"
}

if ($Bundle) {
    if ($aabPath -and (Test-Path $aabPath)) {
        Write-Host "[OK] AAB: $aabPath"
    } else {
        Write-Host "[WARN] AAB not found under: $aabVariantDir"
    }
}

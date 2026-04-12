param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [string]$BuildDir = "build-Windows",
    [string]$Triplet = "x64-windows",
    [string]$Version = "",
    [string]$VersionId = "",
    [string]$Generator = "",
    [switch]$SkipVcpkgInstall,
    [switch]$SkipVcRedistDownload
)

$ErrorActionPreference = "Stop"

function Require-Tool([string]$name) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        if ($name -eq "cmake") {
            $common = @(
                "C:\Program Files\CMake\bin\cmake.exe",
                "C:\Program Files (x86)\CMake\bin\cmake.exe"
            )
            foreach ($p in $common) {
                if (Test-Path $p) {
                    $binDir = Split-Path -Parent $p
                    if (-not (($env:Path -split ";") -contains $binDir)) {
                        $env:Path = "$binDir;$env:Path"
                    }
                    break
                }
            }
            if (Get-Command cmake -ErrorAction SilentlyContinue) {
                return
            }
        }
        throw "$name is not available on PATH."
    }
}

function Get-VsInstallPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        return ""
    }

    $installPath = & $vswhere `
        -latest `
        -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null

    if ($installPath -and (Test-Path $installPath.Trim())) {
        return $installPath.Trim()
    }
    return ""
}

function Get-PreferredVisualStudioGenerator {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        return "Visual Studio 17 2022"
    }

    $productLineVersion = & $vswhere `
        -latest `
        -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property catalog_productLineVersion 2>$null

    switch ($productLineVersion.Trim()) {
        "18" { return "Visual Studio 18 2026" }
        "17" { return "Visual Studio 17 2022" }
        "16" { return "Visual Studio 16 2019" }
        "15" { return "Visual Studio 15 2017" }
        default { return "Visual Studio 17 2022" }
    }
}

function Test-PathCompilerAvailable {
    if (Get-Command cl -ErrorAction SilentlyContinue) { return $true }
    if (Get-Command clang++ -ErrorAction SilentlyContinue) { return $true }
    if (Get-Command g++ -ErrorAction SilentlyContinue) { return $true }
    return $false
}

function Ensure-NinjaAvailable {
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        return $true
    }

    $ninjaDir = Join-Path $repoRoot ".tools\ninja"
    $ninjaExe = Join-Path $ninjaDir "ninja.exe"
    if (Test-Path $ninjaExe) {
        if (-not (($env:Path -split ";") -contains $ninjaDir)) {
            $env:Path = "$ninjaDir;$env:Path"
        }
        return $true
    }

    return $false
}

function Import-VsDevCmdEnvironment {
    $vsInstall = Get-VsInstallPath
    if (-not $vsInstall) {
        return $false
    }

    $vsDevCmd = Join-Path $vsInstall "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path $vsDevCmd)) {
        return $false
    }

    Write-Host "[STEP] Importing Visual Studio developer environment"
    $envDump = & cmd.exe /s /c """$vsDevCmd"" -arch=x64 -host_arch=x64 >nul && set"
    foreach ($line in $envDump) {
        if ($line -match "^([^=]+)=(.*)$") {
            [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
        }
    }
    return $true
}

function Install-VcpkgFallback([string]$targetDir) {
    $zipPath = Join-Path ([System.IO.Path]::GetTempPath()) "vcpkg-master.zip"
    $extractRoot = Join-Path ([System.IO.Path]::GetTempPath()) "vcpkg-master-extract"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    if (Test-Path $extractRoot) { Remove-Item $extractRoot -Recurse -Force }

    Write-Host "[STEP] Downloading vcpkg source archive"
    Invoke-WebRequest -Uri "https://github.com/microsoft/vcpkg/archive/refs/heads/master.zip" -OutFile $zipPath
    Expand-Archive -Path $zipPath -DestinationPath $extractRoot -Force

    $extracted = Join-Path $extractRoot "vcpkg-master"
    if (-not (Test-Path $extracted)) {
        throw "Failed to extract vcpkg archive."
    }
    Move-Item -Path $extracted -Destination $targetDir
}

function Resolve-IsccPath() {
    if ($env:INNO_SETUP_PATH) {
        $candidate = Join-Path $env:INNO_SETUP_PATH "ISCC.exe"
        if (Test-Path $candidate) { return $candidate }
    }
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 6\ISCC.exe",
        "C:\Program Files (x86)\Inno Setup 5\ISCC.exe",
        "C:\Program Files\Inno Setup 5\ISCC.exe"
    )
    foreach ($p in $candidates) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

function Copy-DllTree([string]$sourceDir, [string]$destDir) {
    if (-not (Test-Path $sourceDir)) { return }
    Get-ChildItem $sourceDir -Filter "*.dll" -File -Recurse -ErrorAction SilentlyContinue |
        Copy-Item -Destination $destDir -Force
}

function Get-LocalSdl3MixerPrefix([string]$repoRootPath) {
    $searchRoot = Join-Path $repoRootPath "deps\windows-sdl3-mixer"
    if (-not (Test-Path $searchRoot)) { return $null }
    $cfg = Get-ChildItem -Path $searchRoot -Recurse -Filter "SDL3_mixerConfig.cmake" -File -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $cfg) { return $null }
    $p1 = Split-Path $cfg.FullName -Parent
    $p2 = Split-Path $p1 -Parent
    if ($p2 -and (Test-Path $p2)) { return $p2 }
    return $null
}

function Get-LocalSdl3MixerRuntimeDirs([string]$repoRootPath, [string]$tripletName) {
    $base = Join-Path $repoRootPath "deps\windows-sdl3-mixer\SDL3_mixer-3.1.2\lib"
    if (-not (Test-Path $base)) {
        return @()
    }

    $arch = switch -Regex ($tripletName) {
        "^x64-" { "x64"; break }
        "^x86-" { "x86"; break }
        "^arm64-" { "arm64"; break }
        default { "" }
    }
    if (-not $arch) {
        return @()
    }

    $dirs = @()
    $mainDir = Join-Path $base $arch
    if (Test-Path $mainDir) {
        $dirs += $mainDir
        $optionalDir = Join-Path $mainDir "optional"
        if (Test-Path $optionalDir) {
            $dirs += $optionalDir
        }
    }
    return $dirs
}

function Resolve-BuildOutput([string]$repoRootPath, [string]$buildDirName, [string]$configName, [string]$fileName) {
    $candidates = @(
        (Join-Path $repoRootPath "$buildDirName\$configName\$fileName"),
        (Join-Path $repoRootPath "$buildDirName\$fileName")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    return $candidates[0]
}

function Get-AppVersionFromConfig([string]$repoRootPath) {
    $configPath = Join-Path $repoRootPath "assets\config.json"
    if (-not (Test-Path $configPath)) {
        return ""
    }
    try {
        $config = Get-Content $configPath -Raw | ConvertFrom-Json
        if ($config.version) {
            return [string]$config.version
        }
    } catch {
    }
    return ""
}

function Get-AppVersionIdFromConfig([string]$repoRootPath) {
    $configPath = Join-Path $repoRootPath "assets\config.json"
    if (-not (Test-Path $configPath)) {
        return ""
    }
    try {
        $config = Get-Content $configPath -Raw | ConvertFrom-Json
        if ($null -ne $config.version_id) {
            return [string]$config.version_id
        }
    } catch {
    }
    return ""
}

function Normalize-PathForCompare([string]$p) {
    if (-not $p) { return "" }
    try {
        $full = [System.IO.Path]::GetFullPath($p)
    } catch {
        $full = $p
    }
    return ($full -replace "\\", "/").ToLowerInvariant().TrimEnd("/")
}

function Get-ConfigureFingerprint([string]$repoRootPath) {
    $fingerprintFiles = @(
        (Join-Path $repoRootPath "CMakeLists.txt"),
        (Join-Path $repoRootPath "build\build-windows-installer.ps1"),
        (Join-Path $repoRootPath "cmake\WindowsVersionResource.rc.in")
    )

    $parts = New-Object System.Collections.Generic.List[string]
    foreach ($file in $fingerprintFiles) {
        if (-not (Test-Path $file)) { continue }
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash
        $parts.Add(((Normalize-PathForCompare $file) + "=" + $hash))
    }
    return ($parts -join ";")
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$vcpkgRoot = Join-Path $repoRoot "vcpkg"
$vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"
$env:VCPKG_ROOT = $vcpkgRoot
$stageRoot = Join-Path $repoRoot "dist\windows-installer"
$appDir = Join-Path $stageRoot "app"
$rootDir = Join-Path $stageRoot "root"
$installerScript = Join-Path $repoRoot "installer\windows\df-platformer.iss"
$vcRedist = Join-Path $stageRoot "vc_redist.x64.exe"

if (-not $Version -or -not $Version.Trim()) {
    $Version = Get-AppVersionFromConfig -repoRootPath $repoRoot
    if (-not $Version) {
        $Version = "local"
    }
    Write-Host "[STEP] Using app version: $Version"
}
if (-not $VersionId -or -not $VersionId.Trim()) {
    $VersionId = Get-AppVersionIdFromConfig -repoRootPath $repoRoot
    if (-not $VersionId) {
        $VersionId = "0"
    }
    Write-Host "[STEP] Using app version id: $VersionId"
}

Require-Tool "cmake"

if (-not $Generator -or -not $Generator.Trim()) {
    $hasPathCompiler = Test-PathCompilerAvailable
    $hasNinja = Ensure-NinjaAvailable
    if ($hasNinja -and $hasPathCompiler) {
        $Generator = "Ninja"
    } elseif (Get-VsInstallPath) {
        $Generator = Get-PreferredVisualStudioGenerator
    } elseif ($hasNinja -and $hasPathCompiler) {
        $Generator = "Ninja"
    } else {
        throw "No usable Windows C++ toolchain found. Run build/init-windows-dev.ps1 first or install Visual Studio Build Tools."
    }
    Write-Host "[STEP] Auto-selected generator: $Generator"
}

if ($Generator -like "Visual Studio*") {
    if (-not (Import-VsDevCmdEnvironment)) {
        throw "Visual Studio generator requested but the developer environment could not be loaded."
    }
} elseif ($Generator -eq "Ninja" -and -not (Ensure-NinjaAvailable)) {
    throw "Ninja generator requested but ninja.exe is not available."
}

$buildDirPath = Join-Path $repoRoot $BuildDir
$cachePath = Join-Path $buildDirPath "CMakeCache.txt"
$fingerprintPath = Join-Path $buildDirPath ".configure-fingerprint"
$currentFingerprint = Get-ConfigureFingerprint -repoRootPath $repoRoot
if (Test-Path $cachePath) {
    $cachedGenerator = ""
    $cachedGeneratorInstance = ""
    foreach ($line in (Get-Content $cachePath)) {
        if ($line.StartsWith("CMAKE_GENERATOR:INTERNAL=")) {
            $cachedGenerator = $line.Substring("CMAKE_GENERATOR:INTERNAL=".Length).Trim()
        } elseif ($line -match "^CMAKE_GENERATOR_INSTANCE:(?:INTERNAL|UNINITIALIZED)=") {
            $cachedGeneratorInstance = $line.Substring($line.IndexOf("=") + 1).Trim()
        }
    }

    $resetCache = $false
    if ($cachedGenerator -and $cachedGenerator -ne $Generator) {
        Write-Host "[STEP] Resetting stale CMake cache for generator switch ($cachedGenerator -> $Generator)"
        $resetCache = $true
    }
    if (($Generator -like "Visual Studio*") -and $cachedGeneratorInstance -and (-not (Test-Path $cachedGeneratorInstance))) {
        Write-Host "[STEP] Resetting stale CMake cache because the cached Visual Studio instance is missing"
        $resetCache = $true
    }
    if ((Test-Path $fingerprintPath) -and ((Get-Content $fingerprintPath -Raw) -ne $currentFingerprint)) {
        Write-Host "[STEP] Resetting stale CMake cache because the build configuration changed"
        $resetCache = $true
    }
    if ($resetCache) {
        Remove-Item $cachePath -Force -ErrorAction SilentlyContinue
        $cmakeFilesDir = Join-Path $buildDirPath "CMakeFiles"
        if (Test-Path $cmakeFilesDir) {
            Remove-Item $cmakeFilesDir -Recurse -Force -ErrorAction SilentlyContinue
        }
        Remove-Item $fingerprintPath -Force -ErrorAction SilentlyContinue
    }
}

if (-not (Test-Path $vcpkgRoot)) {
    if (Get-Command git -ErrorAction SilentlyContinue) {
        Write-Host "[STEP] Cloning vcpkg"
        & git clone https://github.com/microsoft/vcpkg $vcpkgRoot
    } else {
        Write-Host "[INFO] git not found; using archive download fallback."
        Install-VcpkgFallback -targetDir $vcpkgRoot
    }
}

if (-not (Test-Path $vcpkgExe)) {
    Write-Host "[STEP] Bootstrapping vcpkg"
    & "$vcpkgRoot\bootstrap-vcpkg.bat" -disableMetrics
}

if (-not $SkipVcpkgInstall) {
    Write-Host "[STEP] Installing dependencies with vcpkg ($Triplet)"
    $localMixerPrefix = Get-LocalSdl3MixerPrefix -repoRootPath $repoRoot
    if ($localMixerPrefix) {
        $env:SDL3_mixer_DIR = Join-Path $localMixerPrefix "cmake"
        $env:CMAKE_PREFIX_PATH = if ($env:CMAKE_PREFIX_PATH) { "$localMixerPrefix;$env:CMAKE_PREFIX_PATH" } else { $localMixerPrefix }
        Write-Host "[STEP] Using bundled SDL3_mixer package: $localMixerPrefix"
        & $vcpkgExe install sdl3 sdl3-image sdl3-ttf curl nlohmann-json --triplet $Triplet
    } else {
        & $vcpkgExe install sdl3 sdl3-image sdl3-ttf sdl3-mixer curl nlohmann-json --triplet $Triplet
    }
}

Write-Host "[STEP] Configuring CMake"
$cmakeArgs = @(
    "-S", $repoRoot,
    "-B", $buildDirPath,
    "-G", $Generator,
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DCMAKE_TOOLCHAIN_FILE=$vcpkgRoot\scripts\buildsystems\vcpkg.cmake",
    "-DVCPKG_TARGET_TRIPLET=$Triplet",
    "-DPLATFORMER_REQUIRE_SDL3_MIXER=ON"
)
if ($Generator -like "Visual Studio*") {
    $cmakeArgs += @("-A", "x64")
    $vsInstall = Get-VsInstallPath
    if ($vsInstall) {
        $cmakeArgs += "-DCMAKE_GENERATOR_INSTANCE=$vsInstall"
    }
}
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed."
}
New-Item -ItemType Directory -Force -Path $buildDirPath | Out-Null
Set-Content -LiteralPath $fingerprintPath -Value $currentFingerprint -NoNewline

Write-Host "[STEP] Building binaries"
& cmake --build $buildDirPath --config $Config --parallel

Write-Host "[STEP] Staging installer content"
if (Test-Path $stageRoot) {
    Remove-Item $stageRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $appDir | Out-Null
New-Item -ItemType Directory -Force -Path $rootDir | Out-Null

Copy-Item (Resolve-BuildOutput -repoRootPath $repoRoot -buildDirName $BuildDir -configName $Config -fileName "platformer.exe") -Destination $appDir -Force
$launcherExe = Resolve-BuildOutput -repoRootPath $repoRoot -buildDirName $BuildDir -configName $Config -fileName "df-launcher.exe"
if (Test-Path $launcherExe) {
    Copy-Item $launcherExe -Destination $rootDir -Force
}
$trayExe = Resolve-BuildOutput -repoRootPath $repoRoot -buildDirName $BuildDir -configName $Config -fileName "df-tray.exe"
if (Test-Path $trayExe) {
    Copy-Item $trayExe -Destination $rootDir -Force
}
$sheetConfigExe = Resolve-BuildOutput -repoRootPath $repoRoot -buildDirName $BuildDir -configName $Config -fileName "sheet_config.exe"
if (Test-Path $sheetConfigExe) {
    Copy-Item $sheetConfigExe -Destination $appDir -Force
}
$updaterExe = Resolve-BuildOutput -repoRootPath $repoRoot -buildDirName $BuildDir -configName $Config -fileName "df-updater.exe"
if (Test-Path $updaterExe) {
    Copy-Item $updaterExe -Destination $appDir -Force
}

Copy-Item (Join-Path $repoRoot "assets") -Destination (Join-Path $appDir "assets") -Recurse -Force
Copy-Item (Join-Path $repoRoot "LICENSE") -Destination $appDir -Force
Copy-Item (Join-Path $repoRoot "README.md") -Destination $appDir -Force

$objectTypeMap = Join-Path $repoRoot "object_type_map.json"
if (Test-Path $objectTypeMap) {
    Copy-Item $objectTypeMap -Destination $appDir -Force
}

Set-Content -Path (Join-Path $rootDir "current_version.txt") -Value $VersionId -NoNewline

$vcpkgBin = Join-Path $vcpkgRoot "installed\$Triplet\bin"
if (Test-Path $vcpkgBin) {
    Copy-DllTree -sourceDir $vcpkgBin -destDir $appDir
}
foreach ($runtimeDir in (Get-LocalSdl3MixerRuntimeDirs -repoRootPath $repoRoot -tripletName $Triplet)) {
    Copy-DllTree -sourceDir $runtimeDir -destDir $appDir
}

$rootRuntimeFiles = @(
    "SDL3.dll"
)
foreach ($runtimeName in $rootRuntimeFiles) {
    $stagedRuntime = Join-Path $appDir $runtimeName
    if (Test-Path $stagedRuntime) {
        Copy-Item $stagedRuntime -Destination $rootDir -Force
    }
}

if (-not $SkipVcRedistDownload) {
    Write-Host "[STEP] Downloading VC++ Redistributable"
    Invoke-WebRequest -Uri "https://aka.ms/vs/17/release/vc_redist.x64.exe" -OutFile $vcRedist
} else {
    Write-Host "[STEP] Skipping VC++ Redistributable download"
}

$iscc = Resolve-IsccPath
if (-not $iscc) {
    throw "Inno Setup not found. Install it from https://jrsoftware.org/isdl.php or set INNO_SETUP_PATH."
}

Write-Host "[STEP] Building installer"
& $iscc "/DMyAppVersion=$Version" "/DMyAppVersionId=$VersionId" $installerScript

$outputDir = Join-Path $stageRoot "output"
Write-Host "[DONE] Installer build complete."
Write-Host "Output folder: $outputDir"

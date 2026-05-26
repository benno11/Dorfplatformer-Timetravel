param(
    [string]$BuildDir = ".build",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [string]$Generator = "",
    [ValidateSet("x64", "x86")]
    [string]$WindowsArch = "x64",
    [switch]$UseVcpkg
)

$ErrorActionPreference = "Stop"
$script:PreferredVcvars = ""

function Get-VsInstallPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        return ""
    }

    $vsInstall = & $vswhere `
        -latest `
        -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null

    if ($vsInstall -and (Test-Path $vsInstall.Trim())) {
        return $vsInstall.Trim()
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

function Test-VSBuildToolsInstalled {
    return -not [string]::IsNullOrWhiteSpace((Get-VsInstallPath))
}

function Test-PathCompilerAvailable {
    if (Get-Command cl -ErrorAction SilentlyContinue) { return $true }
    if (Get-Command clang++ -ErrorAction SilentlyContinue) { return $true }
    if (Get-Command g++ -ErrorAction SilentlyContinue) { return $true }
    return $false
}

function Get-WindowsTriplet {
    if ($WindowsArch -eq "x86") {
        return "x86-windows"
    }
    return "x64-windows"
}

function Get-VsTargetArchitecture {
    if ($WindowsArch -eq "x86") {
        return "x86"
    }
    return "x64"
}

function Ensure-NinjaAvailable {
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        return $true
    }

    $ninjaDir = Join-Path $PSScriptRoot "..\.tools\ninja"
    $ninjaExe = Join-Path $ninjaDir "ninja.exe"
    if (Test-Path $ninjaExe) {
        if (-not (($env:Path -split ";") -contains $ninjaDir)) {
            $env:Path = "$ninjaDir;$env:Path"
        }
        return $true
    }

    return $false
}

function Get-VsPlatformToolset {
    $vsInstall = Get-VsInstallPath
    if (-not $vsInstall) {
        return ""
    }

    $toolsetRoot = Join-Path $vsInstall "VC\Auxiliary\Build"
    if (-not (Test-Path $toolsetRoot)) {
        return ""
    }

    $preferred = Get-ChildItem $toolsetRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^v\d+$' } |
        Sort-Object Name -Descending |
        Select-Object -First 1
    if ($preferred) {
        return $preferred.Name
    }
    return ""
}

function Import-VsDevCmdEnvironment {
    $vsInstall = Get-VsInstallPath
    if (-not $vsInstall) {
        return
    }

    $vsDevCmd = Join-Path $vsInstall "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path $vsDevCmd)) {
        return
    }

    $vcToolsRoot = Join-Path $vsInstall "VC\Tools\MSVC"
    $preferredVcvars = ""
    if (Test-Path $vcToolsRoot) {
        $usable = Get-ChildItem $vcToolsRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            Where-Object {
                (Test-Path (Join-Path $_.FullName "bin\Hostx64\x64\cl.exe")) -and
                (Test-Path (Join-Path $_.FullName "lib\x64\msvcrtd.lib"))
            } |
            Select-Object -First 1
        if ($usable) {
            $parts = $usable.Name.Split(".")
            if ($parts.Length -ge 2) {
                $preferredVcvars = "$($parts[0]).$($parts[1])"
            }
        }
    }

    Write-Host "Importing Visual Studio developer environment from: $vsDevCmd"
    $devArgs = "-arch=$(Get-VsTargetArchitecture) -host_arch=x64"
    if ($preferredVcvars) {
        $devArgs += " -vcvars_ver=$preferredVcvars"
        Write-Host "Using MSVC toolset preference: $preferredVcvars"
    }
    $envDump = & cmd.exe /s /c """$vsDevCmd"" $devArgs >nul && set"
    foreach ($line in $envDump) {
        if ($line -match "^([^=]+)=(.*)$") {
            [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
        }
    }
}

function Get-PreferredMsvcBinDir {
    $vsInstall = Get-VsInstallPath
    if (-not $vsInstall) {
        return ""
    }
    $vcToolsRoot = Join-Path $vsInstall "VC\Tools\MSVC"
    if (-not (Test-Path $vcToolsRoot)) {
        return ""
    }

    $targetArch = if ($WindowsArch -eq "x86") { "x86" } else { "x64" }
    $usable = Get-ChildItem $vcToolsRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        Where-Object {
            (Test-Path (Join-Path $_.FullName "bin\Hostx64\$targetArch\cl.exe")) -and
            (Test-Path (Join-Path $_.FullName "bin\Hostx64\$targetArch\link.exe")) -and
            (Test-Path (Join-Path $_.FullName "lib\$targetArch\msvcrtd.lib"))
        } |
        Select-Object -First 1
    if (-not $usable) {
        return ""
    }
    return (Join-Path $usable.FullName "bin\Hostx64\$targetArch")
}

function Require-Tool([string]$name) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        if ($name -eq "cmake") {
            $common = @(
                "C:\Program Files\CMake\bin\cmake.exe",
                "C:\Program Files (x86)\CMake\bin\cmake.exe"
            )
            foreach ($p in $common) {
                if (Test-Path $p) {
                    $cmakeBin = Split-Path -Parent $p
                    if (-not ($env:Path -split ";" | Where-Object { $_ -eq $cmakeBin })) {
                        $env:Path = "$cmakeBin;$env:Path"
                    }
                    break
                }
            }
            if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
                throw "cmake is not available on this system. Install it with: winget install -e --id Kitware.CMake"
            }
            return
        }
        throw "$name is not available on this system."
    }
}

Require-Tool "cmake"

if (-not $Generator -or -not $Generator.Trim()) {
    $hasPathCompiler = Test-PathCompilerAvailable
    $hasNinja = Ensure-NinjaAvailable
    if ($hasNinja -and $hasPathCompiler) {
        $Generator = "Ninja"
    } elseif (Test-VSBuildToolsInstalled) {
        $Generator = Get-PreferredVisualStudioGenerator
    } elseif ($hasNinja -and $hasPathCompiler) {
        $Generator = "Ninja"
    } else {
        throw "No usable build generator/toolchain found. Install Visual Studio Build Tools or run from a shell with clang++, cl, or g++ on PATH."
    }
    Write-Host "Auto-selected generator: $Generator"
}

$cmakeArgs = @(
    "-S", ".",
    "-B", $BuildDir,
    "-DCMAKE_BUILD_TYPE=$Config"
)

function Normalize-PathForCompare([string]$p) {
    if (-not $p) { return "" }
    try {
        $full = [System.IO.Path]::GetFullPath($p)
    } catch {
        $full = $p
    }
    return ($full -replace "\\", "/").ToLowerInvariant().TrimEnd("/")
}

function Get-ConfigureFingerprint {
    $repoRoot = [System.IO.Path]::GetFullPath(".")
    $fingerprintFiles = @(
        (Join-Path $repoRoot "CMakeLists.txt"),
        (Join-Path $repoRoot "build\setup-build-env.ps1"),
        (Join-Path $repoRoot "cmake\WindowsVersionResource.rc.in"),
        (Join-Path $repoRoot "cmake\WindowsAppManifest.xml.in")
    )

    $parts = New-Object System.Collections.Generic.List[string]
    foreach ($file in $fingerprintFiles) {
        if (-not (Test-Path $file)) { continue }
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash
        $parts.Add(((Normalize-PathForCompare $file) + "=" + $hash))
    }
    return ($parts -join ";")
}

$buildDirFull = [System.IO.Path]::GetFullPath($BuildDir)
$sourceDirFull = [System.IO.Path]::GetFullPath(".")
$cachePath = Join-Path $buildDirFull "CMakeCache.txt"
$fingerprintPath = Join-Path $buildDirFull ".configure-fingerprint"
$currentFingerprint = Get-ConfigureFingerprint
$requestedPlatform = ""
if ($Generator -and $Generator.Trim() -and ($Generator -like "Visual Studio*")) {
    $requestedPlatform = if ($WindowsArch -eq "x86") { "Win32" } else { "x64" }
}
if ($Generator -and $Generator.Trim() -and ($Generator -like "Visual Studio*")) {
    Import-VsDevCmdEnvironment
    $script:PreferredVcvars = $env:VCToolsVersion
    if ($script:PreferredVcvars -match '^(\d+\.\d+)') {
        $script:PreferredVcvars = $matches[1]
    }
}
if (Test-Path $cachePath) {
    $cacheText = Get-Content $cachePath -Raw
    $cachedSource = ""
    $cachedBuild = ""
    $cachedGenerator = ""
    $cachedPlatform = ""
    $cachedToolset = ""
    $cachedGeneratorInstance = ""
    foreach ($line in ($cacheText -split "`r?`n")) {
        if ($line.StartsWith("CMAKE_HOME_DIRECTORY:INTERNAL=")) {
            $cachedSource = $line.Substring("CMAKE_HOME_DIRECTORY:INTERNAL=".Length)
        } elseif ($line.StartsWith("CMAKE_CACHEFILE_DIR:INTERNAL=")) {
            $cachedBuild = $line.Substring("CMAKE_CACHEFILE_DIR:INTERNAL=".Length)
        } elseif ($line.StartsWith("CMAKE_GENERATOR:INTERNAL=")) {
            $cachedGenerator = $line.Substring("CMAKE_GENERATOR:INTERNAL=".Length)
        } elseif ($line.StartsWith("CMAKE_GENERATOR_PLATFORM:INTERNAL=")) {
            $cachedPlatform = $line.Substring("CMAKE_GENERATOR_PLATFORM:INTERNAL=".Length)
        } elseif ($line.StartsWith("CMAKE_GENERATOR_TOOLSET:INTERNAL=")) {
            $cachedToolset = $line.Substring("CMAKE_GENERATOR_TOOLSET:INTERNAL=".Length)
        } elseif ($line -match "^CMAKE_GENERATOR_INSTANCE:(?:INTERNAL|UNINITIALIZED)=") {
            $cachedGeneratorInstance = $line.Substring($line.IndexOf("=") + 1)
        }
    }

    $sourceMismatch = (Normalize-PathForCompare $cachedSource) -ne (Normalize-PathForCompare $sourceDirFull)
    $buildMismatch = $cachedBuild -and ((Normalize-PathForCompare $cachedBuild) -ne (Normalize-PathForCompare $buildDirFull))
    $generatorMismatch = $Generator -and $cachedGenerator -and ($cachedGenerator -ne $Generator)
    $platformMismatch = $requestedPlatform -and ($cachedPlatform -ne $requestedPlatform)
    $requestedToolset = ""
    if ($Generator -and ($Generator -like "Visual Studio*") -and $script:PreferredVcvars) {
        $vsPlatformToolset = Get-VsPlatformToolset
        if ($vsPlatformToolset) {
            $requestedToolset = "$vsPlatformToolset,version=$script:PreferredVcvars"
        }
    }
    $toolsetMismatch = $requestedToolset -and ($cachedToolset -ne $requestedToolset)
    $generatorInstanceMismatch = $false
    if ($Generator -and ($Generator -like "Visual Studio*")) {
        $requestedVsInstall = Get-VsInstallPath
        if ($cachedGeneratorInstance -and (-not (Test-Path $cachedGeneratorInstance))) {
            $generatorInstanceMismatch = $true
        } elseif ($requestedVsInstall -and $cachedGeneratorInstance -and ((Normalize-PathForCompare $cachedGeneratorInstance) -ne (Normalize-PathForCompare $requestedVsInstall))) {
            $generatorInstanceMismatch = $true
        }
    }
    $fingerprintMismatch = $false
    if (Test-Path $fingerprintPath) {
        $cachedFingerprint = Get-Content $fingerprintPath -Raw
        $fingerprintMismatch = $cachedFingerprint -ne $currentFingerprint
    }
    if ($sourceMismatch -or $buildMismatch -or $generatorMismatch -or $platformMismatch -or $toolsetMismatch -or $generatorInstanceMismatch -or $fingerprintMismatch) {
        Write-Host "Detected stale CMake cache from a different path/environment. Resetting cache in $buildDirFull..."
        Remove-Item $cachePath -Force -ErrorAction SilentlyContinue
        $cmakeFilesDir = Join-Path $buildDirFull "CMakeFiles"
        if (Test-Path $cmakeFilesDir) {
            Remove-Item $cmakeFilesDir -Recurse -Force -ErrorAction SilentlyContinue
        }
        Remove-Item $fingerprintPath -Force -ErrorAction SilentlyContinue
    }
}

if ($Generator -and $Generator.Trim()) {
    $cmakeArgs += @("-G", $Generator)
    if ($Generator -like "Visual Studio*") {
        $cmakeArgs += @("-A", ($(if ($WindowsArch -eq "x86") { "Win32" } else { "x64" })))
        $vsPlatformToolset = Get-VsPlatformToolset
        if ($script:PreferredVcvars -and $vsPlatformToolset) {
            $cmakeArgs += @("-T", "$vsPlatformToolset,version=$script:PreferredVcvars")
        }
        $preferredMsvcBin = Get-PreferredMsvcBinDir
        if ($preferredMsvcBin) {
            $clPath = Join-Path $preferredMsvcBin "cl.exe"
            $linkPath = Join-Path $preferredMsvcBin "link.exe"
            $cmakeArgs += @("-DCMAKE_CXX_COMPILER=$clPath")
            $cmakeArgs += @("-DCMAKE_LINKER=$linkPath")
            Write-Host "Forcing MSVC compiler: $clPath"
        }
        $vsInstall = Get-VsInstallPath
        if ($vsInstall) {
            $cmakeArgs += "-DCMAKE_GENERATOR_INSTANCE=$vsInstall"
        }
    }
}

$localVcpkg = Join-Path (Get-Location) "vcpkg"
$localVcpkgToolchain = Join-Path $localVcpkg "scripts/buildsystems/vcpkg.cmake"
if (Test-Path $localVcpkgToolchain) {
    $vcpkgRoot = $localVcpkg
    $env:VCPKG_ROOT = $localVcpkg
} else {
    $vcpkgRoot = $env:VCPKG_ROOT
}
$toolchainFile = if ($vcpkgRoot) { Join-Path $vcpkgRoot "scripts/buildsystems/vcpkg.cmake" } else { "" }
$useVcpkgNow = $UseVcpkg.IsPresent -or (($toolchainFile -ne "") -and (Test-Path $toolchainFile))
if ($useVcpkgNow) {
    if (-not $toolchainFile -or -not (Test-Path $toolchainFile)) {
        throw "VCPKG_ROOT is not set to a valid vcpkg installation."
    }
    $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile"

    if (-not $env:VCPKG_DEFAULT_TRIPLET) {
        $env:VCPKG_DEFAULT_TRIPLET = Get-WindowsTriplet
    }
    $cmakeArgs += "-DVCPKG_TARGET_TRIPLET=$($env:VCPKG_DEFAULT_TRIPLET)"

    Write-Host "Using vcpkg toolchain: $toolchainFile"
    Write-Host "Using vcpkg triplet: $($env:VCPKG_DEFAULT_TRIPLET)"
} else {
    Write-Host "Vcpkg not detected. CMake will use system-installed packages."
}

Write-Host "Configuring project..."
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed. Ensure Visual Studio Build Tools C++ workload is installed and dependencies are available (local vcpkg is auto-detected if present)."
}

New-Item -ItemType Directory -Force -Path $buildDirFull | Out-Null
Set-Content -LiteralPath $fingerprintPath -Value $currentFingerprint -NoNewline

Write-Host "Build environment ready."
Write-Host "Next: cmake --build $BuildDir --config $Config"

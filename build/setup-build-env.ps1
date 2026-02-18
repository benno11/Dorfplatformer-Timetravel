param(
    [string]$BuildDir = ".build",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [string]$Generator = "",
    [switch]$UseVcpkg
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

$buildDirFull = [System.IO.Path]::GetFullPath($BuildDir)
$sourceDirFull = [System.IO.Path]::GetFullPath(".")
$cachePath = Join-Path $buildDirFull "CMakeCache.txt"
if (Test-Path $cachePath) {
    $cacheText = Get-Content $cachePath -Raw
    $cachedSource = ""
    $cachedBuild = ""
    foreach ($line in ($cacheText -split "`r?`n")) {
        if ($line.StartsWith("CMAKE_HOME_DIRECTORY:INTERNAL=")) {
            $cachedSource = $line.Substring("CMAKE_HOME_DIRECTORY:INTERNAL=".Length)
        } elseif ($line.StartsWith("CMAKE_CACHEFILE_DIR:INTERNAL=")) {
            $cachedBuild = $line.Substring("CMAKE_CACHEFILE_DIR:INTERNAL=".Length)
        }
    }

    $sourceMismatch = (Normalize-PathForCompare $cachedSource) -ne (Normalize-PathForCompare $sourceDirFull)
    $buildMismatch = $cachedBuild -and ((Normalize-PathForCompare $cachedBuild) -ne (Normalize-PathForCompare $buildDirFull))
    if ($sourceMismatch -or $buildMismatch) {
        Write-Host "Detected stale CMake cache from a different path/environment. Resetting cache in $buildDirFull..."
        Remove-Item $cachePath -Force -ErrorAction SilentlyContinue
        $cmakeFilesDir = Join-Path $buildDirFull "CMakeFiles"
        if (Test-Path $cmakeFilesDir) {
            Remove-Item $cmakeFilesDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

if ($Generator -and $Generator.Trim()) {
    $cmakeArgs += @("-G", $Generator)
}

$vcpkgRoot = $env:VCPKG_ROOT
$toolchainFile = if ($vcpkgRoot) { Join-Path $vcpkgRoot "scripts/buildsystems/vcpkg.cmake" } else { "" }
$useVcpkgNow = $UseVcpkg.IsPresent -or (($toolchainFile -ne "") -and (Test-Path $toolchainFile))
if ($useVcpkgNow) {
    if (-not $toolchainFile -or -not (Test-Path $toolchainFile)) {
        throw "VCPKG_ROOT is not set to a valid vcpkg installation."
    }
    $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile"

    if (-not $env:VCPKG_DEFAULT_TRIPLET) {
        if ([Environment]::Is64BitOperatingSystem) {
            $env:VCPKG_DEFAULT_TRIPLET = "x64-windows"
        } else {
            $env:VCPKG_DEFAULT_TRIPLET = "x86-windows"
        }
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
    throw "CMake configure failed. If compiler/toolchain is missing, install Visual Studio 2022 Build Tools with C++ workload: winget install -e --id Microsoft.VisualStudio.2022.BuildTools"
}

Write-Host "Build environment ready."
Write-Host "Next: cmake --build $BuildDir --config $Config"

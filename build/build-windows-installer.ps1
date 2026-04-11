param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [string]$BuildDir = "build-Windows",
    [string]$Triplet = "x64-windows",
    [string]$Version = "local",
    [switch]$SkipVcpkgInstall,
    [switch]$SkipVcRedistDownload
)

$ErrorActionPreference = "Stop"

function Require-Tool([string]$name) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        throw "$name is not available on PATH."
    }
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
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 6\ISCC.exe"
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

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$vcpkgRoot = Join-Path $repoRoot "vcpkg"
$vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"
$stageRoot = Join-Path $repoRoot "dist\windows-installer"
$appDir = Join-Path $stageRoot "app"
$installerScript = Join-Path $repoRoot "installer\windows\df-platformer.iss"
$vcRedist = Join-Path $stageRoot "vc_redist.x64.exe"

Require-Tool "cmake"

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
    & $vcpkgExe install sdl3 sdl3-image sdl3-ttf sdl3-mixer curl nlohmann-json --triplet $Triplet
}

Write-Host "[STEP] Configuring CMake"
& cmake `
    -S $repoRoot `
    -B (Join-Path $repoRoot $BuildDir) `
    -G Ninja `
    -DCMAKE_BUILD_TYPE=$Config `
    -DCMAKE_TOOLCHAIN_FILE="$vcpkgRoot\scripts\buildsystems\vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=$Triplet `
    -DPLATFORMER_REQUIRE_SDL3_MIXER=ON

Write-Host "[STEP] Building binaries"
& cmake --build (Join-Path $repoRoot $BuildDir) --config $Config --parallel

Write-Host "[STEP] Staging installer content"
if (Test-Path $stageRoot) {
    Remove-Item $stageRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $appDir | Out-Null

Copy-Item (Join-Path $repoRoot "$BuildDir\platformer.exe") -Destination $appDir -Force
$sheetConfigExe = Join-Path $repoRoot "$BuildDir\sheet_config.exe"
if (Test-Path $sheetConfigExe) {
    Copy-Item $sheetConfigExe -Destination $appDir -Force
}

Copy-Item (Join-Path $repoRoot "assets") -Destination (Join-Path $appDir "assets") -Recurse -Force
Copy-Item (Join-Path $repoRoot "LICENSE") -Destination $appDir -Force
Copy-Item (Join-Path $repoRoot "README.md") -Destination $appDir -Force

$objectTypeMap = Join-Path $repoRoot "object_type_map.json"
if (Test-Path $objectTypeMap) {
    Copy-Item $objectTypeMap -Destination $appDir -Force
}

$vcpkgBin = Join-Path $vcpkgRoot "installed\$Triplet\bin"
if (Test-Path $vcpkgBin) {
    Copy-DllTree -sourceDir $vcpkgBin -destDir $appDir
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
& $iscc "/DMyAppVersion=$Version" $installerScript

$outputDir = Join-Path $stageRoot "output"
Write-Host "[DONE] Installer build complete."
Write-Host "Output folder: $outputDir"

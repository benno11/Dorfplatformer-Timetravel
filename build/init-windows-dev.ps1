param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [string]$BuildDir = ".build",
    [string]$Generator = "",
    [ValidateSet("llvm", "msvc", "auto")]
    [string]$Compiler = "llvm",
    [switch]$UseVcpkg
)

$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

function Ensure-CMakeAvailable {
    if (Get-Command cmake -ErrorAction SilentlyContinue) {
        return
    }
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
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw "cmake is not available. Install it with: winget install -e --id Kitware.CMake"
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

function Test-CompilerAvailable {
    if (Get-Command clang++ -ErrorAction SilentlyContinue) { return $true }
    if (Get-Command cl -ErrorAction SilentlyContinue) { return $true }
    if (Get-Command g++ -ErrorAction SilentlyContinue) { return $true }
    return $false
}

function Test-LlvmInstalled {
    if (Get-Command clang++ -ErrorAction SilentlyContinue) {
        return $true
    }
    if (Test-Path "C:\Program Files\LLVM\bin\clang++.exe") {
        $llvmBin = "C:\Program Files\LLVM\bin"
        if (-not (($env:Path -split ";") -contains $llvmBin)) {
            $env:Path = "$llvmBin;$env:Path"
        }
        return $true
    }
    return $false
}

function Ensure-LlvmInstalled {
    if (Test-LlvmInstalled) {
        return
    }

    if (Get-Command winget -ErrorAction SilentlyContinue) {
        Write-Host "[STEP] Installing LLVM"
        & winget install -e --id LLVM.LLVM --accept-package-agreements --accept-source-agreements
        if (($LASTEXITCODE -ne 0) -and ($LASTEXITCODE -ne 3010)) {
            throw "LLVM installation failed."
        }
    } else {
        throw "LLVM is missing and winget is not available to install it automatically."
    }

    if (-not (Test-LlvmInstalled)) {
        throw "LLVM installation completed, but clang++ is still not available."
    }
}

function Ensure-VsBuildToolsInstalled {
    if (Get-VsInstallPath) {
        return
    }

    if (Get-Command winget -ErrorAction SilentlyContinue) {
        Write-Host "[STEP] Installing Visual Studio Build Tools (C++ workload)"
        & winget install -e --id Microsoft.VisualStudio.2022.BuildTools `
            --accept-package-agreements `
            --accept-source-agreements `
            --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
        if (($LASTEXITCODE -ne 0) -and ($LASTEXITCODE -ne 3010)) {
            throw "Visual Studio Build Tools installation failed."
        }
    } else {
        throw "Visual Studio Build Tools are missing and winget is not available to install them automatically."
    }

    if (-not (Get-VsInstallPath)) {
        throw "Visual Studio Build Tools installation completed, but no usable C++ workload was detected."
    }
}

function Ensure-NinjaAvailable {
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        return
    }

    $ninjaDir = Join-Path (Get-Location) ".tools\ninja"
    $ninjaExe = Join-Path $ninjaDir "ninja.exe"
    if (Test-Path $ninjaExe) {
        if (-not (($env:Path -split ";") -contains $ninjaDir)) {
            $env:Path = "$ninjaDir;$env:Path"
        }
        return
    }

    Write-Host "[STEP] Downloading local ninja.exe"
    $zipPath = Join-Path $env:TEMP "ninja-win.zip"
    New-Item -ItemType Directory -Path $ninjaDir -Force | Out-Null
    Invoke-WebRequest -Uri "https://github.com/ninja-build/ninja/releases/latest/download/ninja-win.zip" -OutFile $zipPath
    Expand-Archive -Path $zipPath -DestinationPath $ninjaDir -Force
    if (-not (($env:Path -split ";") -contains $ninjaDir)) {
        $env:Path = "$ninjaDir;$env:Path"
    }
}

function Resolve-IsccPath {
    $candidates = @()
    if ($env:INNO_SETUP_PATH) {
        $candidates += (Join-Path $env:INNO_SETUP_PATH "ISCC.exe")
    }
    $candidates += @(
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 6\ISCC.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    return ""
}

Ensure-CMakeAvailable
Ensure-NinjaAvailable

if ($Compiler -eq "llvm") {
    Ensure-LlvmInstalled
} elseif ($Compiler -eq "msvc") {
    Ensure-VsBuildToolsInstalled
} else {
    if (-not (Test-CompilerAvailable)) {
        Ensure-LlvmInstalled
    }
}

Write-Host "[STEP] Preparing CMake build environment"
$setupArgs = @(
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $PSScriptRoot "setup-build-env.ps1"),
    "-BuildDir", $BuildDir,
    "-Config", $Config
)
if ($Generator -and $Generator.Trim()) {
    $setupArgs += @("-Generator", $Generator)
}
if ($UseVcpkg) {
    $setupArgs += "-UseVcpkg"
}
& powershell @setupArgs
if ($LASTEXITCODE -ne 0) {
    throw "setup-build-env.ps1 failed."
}

$iscc = Resolve-IsccPath
if ($iscc) {
    Write-Host "[OK] Inno Setup detected: $iscc"
} else {
    Write-Host "[INFO] Inno Setup not found. Installer packaging will stay unavailable until ISCC.exe is installed."
}

Write-Host "[DONE] Windows build environment is ready."
Write-Host "Next steps:"
Write-Host "  .\build-local.ps1 -Compiler $Compiler"
Write-Host "  .\build\build-windows-installer.ps1 -Version <your-version>"

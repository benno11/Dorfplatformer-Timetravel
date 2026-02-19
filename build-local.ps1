param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [string]$BuildDir = ".build",
    [string]$Generator = "",
    [ValidateSet("llvm", "msvc")]
    [string]$Compiler = "llvm",
    [switch]$NoCompilerAutoInstall,
    [switch]$Run,
    [switch]$UseVcpkg
)

$ErrorActionPreference = "Stop"

Set-Location $PSScriptRoot

function Test-VSBuildToolsInstalled {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        return $false
    }

    $installPath = & $vswhere `
        -latest `
        -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null

    return -not [string]::IsNullOrWhiteSpace($installPath)
}

function Test-CxxCompilerAvailable {
    if (Get-Command cl -ErrorAction SilentlyContinue) { return $true }
    if (Get-Command g++ -ErrorAction SilentlyContinue) { return $true }
    if (Get-Command clang++ -ErrorAction SilentlyContinue) { return $true }
    if (Test-VSBuildToolsInstalled) { return $true }
    return $false
}

function Test-LlvmInstalled {
    if (Get-Command clang++ -ErrorAction SilentlyContinue) { return $true }
    if (Test-Path "C:\Program Files\LLVM\bin\clang++.exe") { return $true }
    return $false
}

function Get-ClangMajorVersion {
    $clangCmd = Get-Command clang++ -ErrorAction SilentlyContinue
    if (-not $clangCmd) {
        $clangPath = "C:\Program Files\LLVM\bin\clang++.exe"
        if (Test-Path $clangPath) {
            $clangCmd = Get-Item $clangPath
        }
    }
    if (-not $clangCmd) {
        return $null
    }

    $versionText = & $clangCmd.Source --version 2>$null | Select-Object -First 1
    if (-not $versionText) {
        return $null
    }

    $m = [regex]::Match($versionText, "clang version\s+(\d+)\.")
    if ($m.Success) {
        return [int]$m.Groups[1].Value
    }
    return $null
}

function Ensure-NinjaAvailable {
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        return
    }

    $ninjaDir = Join-Path $PSScriptRoot ".tools\ninja"
    $ninjaExe = Join-Path $ninjaDir "ninja.exe"
    if (Test-Path $ninjaExe) {
        if (-not (($env:Path -split ";") -contains $ninjaDir)) {
            $env:Path = "$ninjaDir;$env:Path"
        }
        return
    }

    if (Get-Command winget -ErrorAction SilentlyContinue) {
        & winget install -e --id Ninja-build.Ninja --accept-package-agreements --accept-source-agreements
        if (($LASTEXITCODE -eq 0) -or ($LASTEXITCODE -eq 3010)) {
            return
        }
    }

    $tempDir = Join-Path $env:TEMP "df-ninja-installer"
    $zipPath = Join-Path $tempDir "ninja-win.zip"
    New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
    Invoke-WebRequest -Uri "https://github.com/ninja-build/ninja/releases/latest/download/ninja-win.zip" -OutFile $zipPath

    New-Item -ItemType Directory -Path $ninjaDir -Force | Out-Null
    Expand-Archive -Path $zipPath -DestinationPath $ninjaDir -Force
    if (-not (($env:Path -split ";") -contains $ninjaDir)) {
        $env:Path = "$ninjaDir;$env:Path"
    }
}

function Install-LlvmCompilerIfMissing {
    if (Test-LlvmInstalled) {
        return
    }

    Write-Host "No LLVM/Clang compiler detected. Installing LLVM..."
    if (Get-Command winget -ErrorAction SilentlyContinue) {
        & winget install -e --id LLVM.LLVM --accept-package-agreements --accept-source-agreements
        if (($LASTEXITCODE -ne 0) -and ($LASTEXITCODE -ne 3010)) {
            throw "Automatic LLVM installation via winget failed."
        }
        return
    }

    $tempDir = Join-Path $env:TEMP "df-llvm-installer"
    $installer = Join-Path $tempDir "LLVM-installer.exe"
    New-Item -ItemType Directory -Path $tempDir -Force | Out-Null

    # Stable direct download target from official LLVM releases.
    $llvmUrl = "https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/LLVM-18.1.8-win64.exe"
    Invoke-WebRequest -Uri $llvmUrl -OutFile $installer

    $args = @("/S")
    $isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).
        IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        $proc = Start-Process -FilePath $installer -ArgumentList $args -PassThru -Wait -Verb RunAs
    } else {
        $proc = Start-Process -FilePath $installer -ArgumentList $args -PassThru -Wait
    }

    if (($proc.ExitCode -ne 0) -and ($proc.ExitCode -ne 3010)) {
        throw "Automatic LLVM installation failed (exit code: $($proc.ExitCode))."
    }
}

function Install-MsvcCompilerIfMissing {
    if (Test-CxxCompilerAvailable) {
        return
    }

    if ($NoCompilerAutoInstall) {
        throw "No C++ compiler/toolchain found and auto-install is disabled (-NoCompilerAutoInstall)."
    }

    Write-Host "No compiler/toolchain detected. Installing Visual Studio 2022 Build Tools (C++ workload)..."

    if (Get-Command winget -ErrorAction SilentlyContinue) {
        & winget install -e --id Microsoft.VisualStudio.2022.BuildTools `
            --accept-package-agreements `
            --accept-source-agreements `
            --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"

        if (($LASTEXITCODE -ne 0) -and ($LASTEXITCODE -ne 3010)) {
            throw "Automatic compiler installation via winget failed."
        }
    } else {
        $tempDir = Join-Path $env:TEMP "df-buildtools-installer"
        $bootstrapper = Join-Path $tempDir "vs_BuildTools.exe"
        $installPath = Join-Path $tempDir "vs-buildtools"
        New-Item -ItemType Directory -Path $tempDir -Force | Out-Null

        Write-Host "winget not found. Downloading Visual Studio Build Tools bootstrapper..."
        Invoke-WebRequest -Uri "https://aka.ms/vs/17/release/vs_BuildTools.exe" -OutFile $bootstrapper

        $args = @(
            "--quiet",
            "--wait",
            "--norestart",
            "--nocache",
            "--installPath", $installPath,
            "--add", "Microsoft.VisualStudio.Workload.VCTools",
            "--includeRecommended"
        )
        $retryArgs = @(
            "--quiet",
            "--wait",
            "--norestart",
            "--add", "Microsoft.VisualStudio.Workload.VCTools"
        )
        $interactiveArgs = @(
            "--wait",
            "--installPath", $installPath,
            "--add", "Microsoft.VisualStudio.Workload.VCTools",
            "--includeRecommended"
        )

        $isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).
            IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
        $proc = $null
        if (-not $isAdmin) {
            Write-Host "Requesting administrator privileges for Build Tools installation..."
            $proc = Start-Process -FilePath $bootstrapper -ArgumentList $args -PassThru -Wait -Verb RunAs
        } else {
            $proc = Start-Process -FilePath $bootstrapper -ArgumentList $args -PassThru -Wait
        }

        if ($proc.ExitCode -eq 87) {
            Write-Host "Installer rejected one or more flags. Retrying with minimal arguments..."
            if (-not $isAdmin) {
                $proc = Start-Process -FilePath $bootstrapper -ArgumentList $retryArgs -PassThru -Wait -Verb RunAs
            } else {
                $proc = Start-Process -FilePath $bootstrapper -ArgumentList $retryArgs -PassThru -Wait
            }
        }

        if ($proc.ExitCode -eq 1) {
            $installerLog = Get-ChildItem "$env:TEMP\dd_installer*.log" -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTime -Descending |
                Select-Object -First 1
            $requiresInteractiveMigration = $false
            if ($installerLog) {
                $requiresInteractiveMigration = Select-String -Path $installerLog.FullName -Pattern "automatic migration cannot be started in quiet or passive mode" -Quiet
            }
            if ($requiresInteractiveMigration) {
                Write-Host "Visual Studio Installer requires interactive migration. Retrying in interactive mode..."
                if (-not $isAdmin) {
                    $proc = Start-Process -FilePath $bootstrapper -ArgumentList $interactiveArgs -PassThru -Wait -Verb RunAs
                } else {
                    $proc = Start-Process -FilePath $bootstrapper -ArgumentList $interactiveArgs -PassThru -Wait
                }
            }
        }

        if (($proc.ExitCode -ne 0) -and ($proc.ExitCode -ne 3010)) {
            throw "Automatic compiler installation via Visual Studio bootstrapper failed (exit code: $($proc.ExitCode)). Check %TEMP%\\dd_bootstrapper*.log"
        }
    }

    if (-not (Test-CxxCompilerAvailable)) {
        throw "Compiler installation finished, but no C++ toolchain was detected. Reboot or open a new terminal, then run .\build-local.ps1 again."
    }
}

function Ensure-ProjectDepsAvailable {
    $triplet = if ([Environment]::Is64BitOperatingSystem) { "x64-windows" } else { "x86-windows" }
    $vcpkgRoot = Join-Path $PSScriptRoot "vcpkg"
    $vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"
    $prefix = Join-Path $vcpkgRoot "installed\$triplet"
    $sdlConfig = Join-Path $prefix "share\sdl3\SDL3Config.cmake"

    if (Test-Path $sdlConfig) {
        $env:CMAKE_PREFIX_PATH = if ($env:CMAKE_PREFIX_PATH) { "$prefix;$env:CMAKE_PREFIX_PATH" } else { $prefix }
        Write-Host "Using dependency prefix: $prefix"
        return
    }

    if (-not (Test-Path $vcpkgExe)) {
        return
    }

    Write-Host "Missing SDL dev packages. Installing dependencies via local vcpkg..."
    & $vcpkgExe install sdl3 sdl3-image sdl3-ttf curl nlohmann-json "--triplet=$triplet"
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg dependency install failed."
    }

    if (-not (Test-Path $sdlConfig)) {
        throw "vcpkg finished but SDL3Config.cmake is still missing at $sdlConfig"
    }

    $env:CMAKE_PREFIX_PATH = if ($env:CMAKE_PREFIX_PATH) { "$prefix;$env:CMAKE_PREFIX_PATH" } else { $prefix }
    Write-Host "Using dependency prefix: $prefix"
}

if ($NoCompilerAutoInstall) {
    if (-not (Test-CxxCompilerAvailable)) {
        throw "No C++ compiler/toolchain found and auto-install is disabled (-NoCompilerAutoInstall)."
    }
} else {
    if ($Compiler -eq "llvm") {
        Install-LlvmCompilerIfMissing
        Ensure-NinjaAvailable

        # Ensure current process can resolve freshly installed LLVM binaries.
        if ((Test-Path "C:\Program Files\LLVM\bin") -and (-not (($env:Path -split ";") -contains "C:\Program Files\LLVM\bin"))) {
            $env:Path = "C:\Program Files\LLVM\bin;$env:Path"
        }

        $clangMajor = Get-ClangMajorVersion
        if ($clangMajor -and $clangMajor -lt 19 -and (Test-VSBuildToolsInstalled)) {
            Write-Host "Detected Clang $clangMajor, but current MSVC STL requires Clang 19+."
            Write-Host "Falling back to MSVC compiler for compatibility."
            $Compiler = "msvc"
            Remove-Item Env:CC -ErrorAction SilentlyContinue
            Remove-Item Env:CXX -ErrorAction SilentlyContinue
            Install-MsvcCompilerIfMissing
        }
    } else {
        Install-MsvcCompilerIfMissing
    }
}

Ensure-ProjectDepsAvailable

if (-not $Generator) {
    if ($Compiler -eq "msvc") {
        $Generator = "Visual Studio 17 2022"
    } elseif ($Compiler -eq "llvm") {
        $Generator = "Ninja"
    } else {
        $hasNinja = [bool](Get-Command ninja -ErrorAction SilentlyContinue)
        $hasPathCompiler = [bool](Get-Command cl -ErrorAction SilentlyContinue) -or `
            [bool](Get-Command g++ -ErrorAction SilentlyContinue) -or `
            [bool](Get-Command clang++ -ErrorAction SilentlyContinue)

        if ($hasNinja -and $hasPathCompiler) {
            $Generator = "Ninja"
        } else {
            $Generator = "Visual Studio 17 2022"
        }
    }
}

if ($Compiler -eq "llvm") {
    $env:CC = "clang"
    $env:CXX = "clang++"
} else {
    Remove-Item Env:CC -ErrorAction SilentlyContinue
    Remove-Item Env:CXX -ErrorAction SilentlyContinue
}

# CMake cannot switch generators in-place. Reset cache if needed.
$cachePath = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $cachePath) {
    $cachedGenerator = ""
    foreach ($line in (Get-Content $cachePath)) {
        if ($line.StartsWith("CMAKE_GENERATOR:INTERNAL=")) {
            $cachedGenerator = $line.Substring("CMAKE_GENERATOR:INTERNAL=".Length).Trim()
            break
        }
    }
    if ($cachedGenerator -and $cachedGenerator -ne $Generator) {
        Write-Host "Switching generator from '$cachedGenerator' to '$Generator'. Resetting build cache..."
        Remove-Item $cachePath -Force -ErrorAction SilentlyContinue
        $cmakeFilesDir = Join-Path $BuildDir "CMakeFiles"
        if (Test-Path $cmakeFilesDir) {
            Remove-Item $cmakeFilesDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

Write-Host "[1/2] Configure build environment..."
$setupArgs = @(
    "-ExecutionPolicy", "Bypass",
    "-File", "build/setup-build-env.ps1",
    "-BuildDir", $BuildDir,
    "-Config", $Config,
    "-Generator", $Generator
)
if ($UseVcpkg) {
    $setupArgs += "-UseVcpkg"
}
& powershell @setupArgs
if ($LASTEXITCODE -ne 0) {
    throw "Failed to configure the project."
}

Write-Host "[2/2] Build targets..."
& cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) {
    throw "Build failed."
}

if ($Run) {
    $exeCandidates = @(
        (Join-Path $BuildDir $Config "platformer.exe"),
        (Join-Path $BuildDir "platformer.exe")
    )
    $exePath = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $exePath) {
        throw "Build succeeded but platformer.exe was not found in '$BuildDir'."
    }
    Write-Host "Launching $exePath"
    & $exePath
}

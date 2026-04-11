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

function Test-VSBuildToolsInstalled {
    return -not [string]::IsNullOrWhiteSpace((Get-VsInstallPath))
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

function Get-LocalSdl3MixerPrefix {
    $searchRoot = Join-Path $PSScriptRoot "deps\windows-sdl3-mixer"
    if (-not (Test-Path $searchRoot)) { return $null }
    $cfg = Get-ChildItem -Path $searchRoot -Recurse -Filter "SDL3_mixerConfig.cmake" -File -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $cfg) { return $null }
    $p1 = Split-Path $cfg.FullName -Parent
    $p2 = Split-Path $p1 -Parent
    if ($p2 -and (Test-Path $p2)) { return $p2 }
    return $null
}

function Install-Sdl3MixerDevelPackage {
    $existingPrefix = Get-LocalSdl3MixerPrefix
    if ($existingPrefix) { return $existingPrefix }

    $installRoot = Join-Path $PSScriptRoot "deps\windows-sdl3-mixer"
    New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
    $zipPath = Join-Path $env:TEMP "SDL3_mixer-devel-latest-VC.zip"
    $downloadUrl = $null

    try {
        try {
            [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        } catch { }

        try {
            $headers = @{ "User-Agent" = "DF-New-Bootstrap" }
            $rels = Invoke-RestMethod -Headers $headers -Uri "https://api.github.com/repos/libsdl-org/SDL_mixer/releases?per_page=20" -UseBasicParsing
            if ($rels) {
                foreach ($rel in $rels) {
                    if (-not $rel.assets) { continue }
                    $asset = $rel.assets | Where-Object {
                        $_.name -and $_.name -match "^SDL3_mixer-devel-.*-VC\.zip$"
                    } | Select-Object -First 1
                    if ($asset -and $asset.browser_download_url) {
                        $downloadUrl = $asset.browser_download_url
                        break
                    }
                }
            }
        } catch { }

        if (-not $downloadUrl) {
            Write-Host "Could not resolve latest SDL3_mixer VC package URL."
            return $null
        }

        try {
            Invoke-WebRequest -Uri $downloadUrl -OutFile $zipPath -UseBasicParsing
        } catch {
            $curlExe = Get-Command curl.exe -ErrorAction SilentlyContinue
            if ($curlExe) {
                & $curlExe.Source -L --retry 3 --fail --output $zipPath $downloadUrl
            } else {
                throw
            }
        }

        if (-not (Test-Path $zipPath)) {
            return $null
        }

        Expand-Archive -Path $zipPath -DestinationPath $installRoot -Force
        return (Get-LocalSdl3MixerPrefix)
    } catch {
        Write-Host "SDL3_mixer devel package install failed: $($_.Exception.Message)"
        return $null
    }
}

function Ensure-LocalVcpkgExecutable {
    param(
        [string]$VcpkgRoot
    )

    $vcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
    if (Test-Path $vcpkgExe) {
        return $vcpkgExe
    }

    $bootstrapScript = Join-Path $VcpkgRoot "bootstrap-vcpkg.bat"
    $tlsDownloader = Join-Path $VcpkgRoot "scripts\tls12-download.exe"
    $toolMetadata = Join-Path $VcpkgRoot "scripts\vcpkg-tool-metadata.txt"

    if ((Test-Path $bootstrapScript) -and (Test-Path $tlsDownloader)) {
        Write-Host "Bootstrapping local vcpkg..."
        Push-Location $VcpkgRoot
        try {
            & cmd.exe /c "`"$bootstrapScript`""
        } finally {
            Pop-Location
        }
        if (($LASTEXITCODE -eq 0) -and (Test-Path $vcpkgExe)) {
            return $vcpkgExe
        }
    }

    if (Test-Path $toolMetadata) {
        Write-Host "Downloading pinned vcpkg.exe for local toolchain..."
        $versionTag = Select-String -Path $toolMetadata -Pattern "^VCPKG_TOOL_RELEASE_TAG=(.+)$" |
            ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() } |
            Select-Object -First 1
        if ($versionTag) {
            $downloadUrl = "https://github.com/microsoft/vcpkg-tool/releases/download/$versionTag/vcpkg.exe"
            try {
                Invoke-WebRequest -Uri $downloadUrl -OutFile $vcpkgExe -UseBasicParsing
            } catch {
                $curlExe = Get-Command curl.exe -ErrorAction SilentlyContinue
                if ($curlExe) {
                    & $curlExe.Source -L --retry 3 --fail --output $vcpkgExe $downloadUrl
                } else {
                    throw
                }
            }
        }
    }

    if (Test-Path $vcpkgExe) {
        return $vcpkgExe
    }
    throw "Unable to prepare local vcpkg executable."
}

function Ensure-ProjectDepsAvailable {
    $triplet = if ([Environment]::Is64BitOperatingSystem) { "x64-windows" } else { "x86-windows" }
    $vcpkgRoot = Join-Path $PSScriptRoot "vcpkg"
    $vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"
    $prefix = Join-Path $vcpkgRoot "installed\$triplet"
    $sdlConfig = Join-Path $prefix "share\sdl3\SDL3Config.cmake"
    $sdlMixerConfigA = Join-Path $prefix "share\sdl3-mixer\SDL3_mixerConfig.cmake"
    $sdlMixerConfigB = Join-Path $prefix "share\SDL3_mixer\SDL3_mixerConfig.cmake"
    $hasSdlMixer = (Test-Path $sdlMixerConfigA) -or (Test-Path $sdlMixerConfigB)
    $localMixerPrefix = Get-LocalSdl3MixerPrefix
    $preferredMixerPrefix = if ($hasSdlMixer) { $prefix } else { $localMixerPrefix }

    if ((Test-Path $sdlConfig) -and ($hasSdlMixer -or $localMixerPrefix)) {
        $prefixes = @($prefix)
        if ((-not $hasSdlMixer) -and $localMixerPrefix) { $prefixes += $localMixerPrefix }
        $joinedPrefixes = ($prefixes -join ";")
        if ($preferredMixerPrefix) {
            $env:SDL3_mixer_DIR = Join-Path $preferredMixerPrefix "cmake"
        }
        $env:CMAKE_PREFIX_PATH = if ($env:CMAKE_PREFIX_PATH) { "$joinedPrefixes;$env:CMAKE_PREFIX_PATH" } else { $joinedPrefixes }
        Write-Host "Using dependency prefix: $prefix"
        if ($preferredMixerPrefix) { Write-Host "Using SDL3_mixer prefix: $preferredMixerPrefix" }
        return
    }

    if (-not (Test-Path $vcpkgExe)) {
        $vcpkgExe = Ensure-LocalVcpkgExecutable -VcpkgRoot $vcpkgRoot
    }

    Write-Host "Missing SDL dev packages (including SDL3_mixer). Installing dependencies via local vcpkg..."
    $depArgs = @("install", "sdl3", "sdl3-image", "sdl3-ttf", "sdl3-mixer", "curl", "nlohmann-json", "--triplet=$triplet")
    $depArgsNoMixer = @("install", "sdl3", "sdl3-image", "sdl3-ttf", "curl", "nlohmann-json", "--triplet=$triplet")
    $mixerOptionalMode = $false
    $oldEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $depOut = & $vcpkgExe @depArgs 2>&1
        $depRc = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldEap
    }
    if ($depRc -ne 0) {
        $depText = ($depOut | Out-String)
        $missingMixerPort = $depText -match "sdl3-mixer does not exist"
        if ($missingMixerPort) {
            $mixerOptionalMode = $true
            Write-Host "Local vcpkg baseline does not include sdl3-mixer; attempting auto-update and retry..."
            $gitExe = $null
            $gitCmd = Get-Command git -ErrorAction SilentlyContinue
            if ($gitCmd) {
                $gitExe = $gitCmd.Source
            } else {
                $gitCandidates = @(
                    (Join-Path $env:ProgramFiles "Git\\cmd\\git.exe"),
                    (Join-Path $env:ProgramFiles "Git\\bin\\git.exe"),
                    (Join-Path $env:ProgramW6432 "Git\\cmd\\git.exe"),
                    (Join-Path $env:LocalAppData "Programs\\Git\\cmd\\git.exe")
                ) | Where-Object { $_ -and $_.Trim().Length -gt 0 }
                foreach ($candidate in $gitCandidates) {
                    if (Test-Path $candidate) {
                        $gitExe = $candidate
                        break
                    }
                }
            }
            if ($gitExe) {
                & $gitExe -C $vcpkgRoot pull --ff-only
                if ($LASTEXITCODE -eq 0) {
                    $oldEap = $ErrorActionPreference
                    $ErrorActionPreference = "Continue"
                    try {
                        & $vcpkgExe @depArgs
                        $depRc = $LASTEXITCODE
                    } finally {
                        $ErrorActionPreference = $oldEap
                    }
                } else {
                    Write-Host "vcpkg auto-update failed; continuing with original error."
                }
            } else {
                Write-Host "git is not available to auto-update vcpkg. Attempting to install Git via winget..."
                $wingetCmd = Get-Command winget -ErrorAction SilentlyContinue
                $gitInstalledViaWinget = $false
                if ($wingetCmd) {
                    & $wingetCmd.Source install --id Git.Git --exact --silent --accept-package-agreements --accept-source-agreements
                    if ($LASTEXITCODE -eq 0) {
                        $gitInstalledViaWinget = $true
                        $gitPostInstallCandidates = @(
                            (Join-Path $env:ProgramFiles "Git\\cmd\\git.exe"),
                            (Join-Path $env:ProgramFiles "Git\\bin\\git.exe"),
                            (Join-Path $env:ProgramW6432 "Git\\cmd\\git.exe"),
                            (Join-Path $env:LocalAppData "Programs\\Git\\cmd\\git.exe")
                        ) | Where-Object { $_ -and $_.Trim().Length -gt 0 }
                        foreach ($candidate in $gitPostInstallCandidates) {
                            if (Test-Path $candidate) {
                                $gitExe = $candidate
                                break
                            }
                        }
                        if ($gitExe) {
                            Write-Host "Git installed successfully; retrying vcpkg update..."
                            & $gitExe -C $vcpkgRoot pull --ff-only
                            if ($LASTEXITCODE -eq 0) {
                                $oldEap = $ErrorActionPreference
                                $ErrorActionPreference = "Continue"
                                try {
                                    & $vcpkgExe @depArgs
                                    $depRc = $LASTEXITCODE
                                } finally {
                                    $ErrorActionPreference = $oldEap
                                }
                            } else {
                                Write-Host "vcpkg auto-update failed even after installing Git."
                            }
                        } else {
                            Write-Host "Git install reported success, but git.exe was not found in expected paths."
                        }
                    } else {
                        Write-Host "winget failed to install Git."
                    }
                }
                if (-not $gitExe) {
                    if (-not $wingetCmd) {
                        Write-Host "winget is not available to install Git automatically."
                    } elseif ($gitInstalledViaWinget) {
                        Write-Host "Git installation completed but git.exe was not detected. Trying direct installer..."
                    }
                    Write-Host "Attempting direct Git installer download..."
                    $gitInstaller = Join-Path $env:TEMP "Git-latest-64-bit.exe"
                    try {
                        try {
                            [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
                        } catch { }
                        $gitInstallerUrl = $null
                        try {
                            $gitLatest = Invoke-RestMethod -Uri "https://api.github.com/repos/git-for-windows/git/releases/latest" -UseBasicParsing
                            if ($gitLatest -and $gitLatest.assets) {
                                $asset = $gitLatest.assets | Where-Object {
                                    $_.name -and $_.name -match "64-bit\.exe$" -and $_.name -notmatch "Portable"
                                } | Select-Object -First 1
                                if ($asset -and $asset.browser_download_url) {
                                    $gitInstallerUrl = $asset.browser_download_url
                                }
                            }
                        } catch { }
                        if (-not $gitInstallerUrl) {
                            $gitInstallerUrl = "https://github.com/git-for-windows/git/releases/latest/download/Git-64-bit.exe"
                        }
                        Invoke-WebRequest -Uri $gitInstallerUrl -OutFile $gitInstaller -UseBasicParsing
                    } catch {
                        $curlExe = Get-Command curl.exe -ErrorAction SilentlyContinue
                        if ($curlExe) {
                            if (-not $gitInstallerUrl) {
                                $gitInstallerUrl = "https://github.com/git-for-windows/git/releases/latest/download/Git-64-bit.exe"
                            }
                            & $curlExe.Source -L --retry 3 --fail --output $gitInstaller $gitInstallerUrl
                        } else {
                            throw
                        }
                    }
                    try {
                        if (Test-Path $gitInstaller) {
                            $installProc = Start-Process -FilePath $gitInstaller -ArgumentList "/VERYSILENT","/NORESTART","/SP-" -PassThru -Wait
                            if ($installProc.ExitCode -eq 0) {
                                $gitPostInstallCandidates = @(
                                    (Join-Path $env:ProgramFiles "Git\\cmd\\git.exe"),
                                    (Join-Path $env:ProgramFiles "Git\\bin\\git.exe"),
                                    (Join-Path $env:ProgramW6432 "Git\\cmd\\git.exe"),
                                    (Join-Path $env:LocalAppData "Programs\\Git\\cmd\\git.exe")
                                ) | Where-Object { $_ -and $_.Trim().Length -gt 0 }
                                foreach ($candidate in $gitPostInstallCandidates) {
                                    if (Test-Path $candidate) {
                                        $gitExe = $candidate
                                        break
                                    }
                                }
                                if ($gitExe) {
                                    Write-Host "Git installed successfully via direct installer; retrying vcpkg update..."
                                    & $gitExe -C $vcpkgRoot pull --ff-only
                                    if ($LASTEXITCODE -eq 0) {
                                        $oldEap = $ErrorActionPreference
                                        $ErrorActionPreference = "Continue"
                                        try {
                                            & $vcpkgExe @depArgs
                                            $depRc = $LASTEXITCODE
                                        } finally {
                                            $ErrorActionPreference = $oldEap
                                        }
                                    } else {
                                        Write-Host "vcpkg auto-update failed after direct Git install."
                                    }
                                } else {
                                    Write-Host "Direct Git install reported success, but git.exe was not found in expected paths."
                                }
                            } else {
                                Write-Host "Direct Git installer failed with exit code $($installProc.ExitCode)."
                            }
                        } else {
                            Write-Host "Direct Git installer download failed."
                        }
                    } catch {
                        Write-Host "Direct Git installer failed: $($_.Exception.Message)"
                    }
                }
            }
            if ($depRc -ne 0) {
                Write-Host "Retrying dependency install without SDL3_mixer (audio mixer support will be disabled)..."
                $oldEap = $ErrorActionPreference
                $ErrorActionPreference = "Continue"
                try {
                    & $vcpkgExe @depArgsNoMixer
                    $depRc = $LASTEXITCODE
                } finally {
                    $ErrorActionPreference = $oldEap
                }
            }
        }
    }
    if ($depRc -ne 0) {
        throw "vcpkg dependency install failed."
    }

    if (-not (Test-Path $sdlConfig)) {
        throw "vcpkg finished but SDL3Config.cmake is still missing at $sdlConfig"
    }
    $hasSdlMixer = (Test-Path $sdlMixerConfigA) -or (Test-Path $sdlMixerConfigB)
    if (-not $hasSdlMixer) {
        $localMixerPrefix = Install-Sdl3MixerDevelPackage
        if ($localMixerPrefix) {
            Write-Host "Installed SDL3_mixer development package: $localMixerPrefix"
            $hasSdlMixer = $true
        }
    }

    if ((-not $mixerOptionalMode) -and (-not $hasSdlMixer)) {
        throw "vcpkg finished but SDL3_mixerConfig.cmake is still missing under $prefix\\share\\sdl3-mixer"
    } elseif ($mixerOptionalMode -and (-not $hasSdlMixer)) {
        Write-Host "SDL3_mixer is unavailable in this local vcpkg baseline; continuing without mixer link target."
    }

    $preferredMixerPrefix = if ($hasSdlMixer) { $prefix } else { $localMixerPrefix }
    $prefixes = @($prefix)
    if ((-not $hasSdlMixer) -and $localMixerPrefix) { $prefixes += $localMixerPrefix }
    $joinedPrefixes = ($prefixes -join ";")
    if ($preferredMixerPrefix) { $env:SDL3_mixer_DIR = Join-Path $preferredMixerPrefix "cmake" }
    $env:CMAKE_PREFIX_PATH = if ($env:CMAKE_PREFIX_PATH) { "$joinedPrefixes;$env:CMAKE_PREFIX_PATH" } else { $joinedPrefixes }
    Write-Host "Using dependency prefix: $prefix"
    if ($preferredMixerPrefix) { Write-Host "Using SDL3_mixer prefix: $preferredMixerPrefix" }
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
        $Generator = Get-PreferredVisualStudioGenerator
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
            $Generator = Get-PreferredVisualStudioGenerator
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
        Write-Host "Switching generator from '$cachedGenerator' to '$Generator'. Resetting build cache..."
        $resetCache = $true
    }
    if (($Generator -like "Visual Studio*") -and $cachedGeneratorInstance -and (-not (Test-Path $cachedGeneratorInstance))) {
        Write-Host "Cached Visual Studio instance is missing: $cachedGeneratorInstance"
        $resetCache = $true
    }
    if ($resetCache) {
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

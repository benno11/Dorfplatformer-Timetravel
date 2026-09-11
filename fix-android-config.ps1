param(
    [string]$RepoRoot = $PSScriptRoot
)
$ErrorActionPreference = "Stop"

# Repair metadata consumed by Gradle and the direct NDK compiler before building.
$configPath = Join-Path $RepoRoot "assets/config.json"
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
if ($config.version -isnot [string] -or $config.version -notmatch '^\d+\.\d+\.\d+(?:[.\-+][A-Za-z0-9.\-]+)?$') {
    throw "assets/config.json must contain a valid version string."
}
$versionCode = 0
if (-not [int]::TryParse([string]$config.version_id, [ref]$versionCode) -or
    $versionCode -lt 1 -or $versionCode -gt 2100000000) {
    throw "assets/config.json version_id must be an integer from 1 to 2100000000."
}

$gradlePath = Join-Path $RepoRoot "Android/app/build.gradle.kts"
$gradle = [IO.File]::ReadAllText($gradlePath)
$codePattern = '(?m)^(\s*versionCode\s*=\s*)[^\r\n]+'
$namePattern = '(?m)^(\s*versionName\s*=\s*)[^\r\n]+'
foreach ($pattern in @($codePattern, $namePattern)) {
    if ([regex]::Matches($gradle, $pattern).Count -ne 1) {
        throw "Expected exactly one versionCode and versionName in Android/app/build.gradle.kts."
    }
}
$fixed = [regex]::Replace($gradle, $codePattern, { param($m) $m.Groups[1].Value + $versionCode })
$fixed = [regex]::Replace($fixed, $namePattern, { param($m) $m.Groups[1].Value + '"' + $config.version + '"' })

$generatedDir = Join-Path $RepoRoot "build/android/generated"
$template = [IO.File]::ReadAllText((Join-Path $RepoRoot "cmake/BuildInfo.h.in"))
$header = $template.Replace('@PLATFORMER_BUILD_TIMESTAMP@', (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
$header = $header.Replace('@PLATFORMER_BUILD_TIMEZONE@', (Get-Date -Format 'zzz'))
$header = $header.Replace('@PLATFORMER_APP_VERSION_STRING@', [string]$config.version)
$header = $header.Replace('@PLATFORMER_APP_VERSION_ID@', [string]$versionCode)
if ($header -match '@PLATFORMER_\w+@') { throw "Unresolved Android build metadata placeholder." }

$utf8 = New-Object System.Text.UTF8Encoding($false)
New-Item -ItemType Directory -Force -Path $generatedDir | Out-Null
[IO.File]::WriteAllText((Join-Path $generatedDir "BuildInfo.h"), $header, $utf8)
if ($fixed -cne $gradle) {
    [IO.File]::WriteAllText($gradlePath, $fixed, $utf8)
    Write-Host "[FIX] Android app version synchronized to $($config.version) ($versionCode)."
}
Write-Host "[OK] Android compiler build metadata generated."

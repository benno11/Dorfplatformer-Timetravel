$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path $repo ("build/config-fixer-test-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path "$fixture/assets","$fixture/Android/app","$fixture/cmake" | Out-Null
Copy-Item -LiteralPath "$repo/cmake/BuildInfo.h.in" -Destination "$fixture/cmake/BuildInfo.h.in"
$gradlePath = "$fixture/Android/app/build.gradle.kts"
[IO.File]::WriteAllText($gradlePath, "versionCode = 1`nversionName = `"0.0.1`"`n// preserved")
$configPath = "$fixture/assets/config.json"
[IO.File]::WriteAllText($configPath, '{"version":"2.3.1","version_id":27}')
& "$repo/fix-android-config.ps1" -RepoRoot $fixture
$fixed = [IO.File]::ReadAllText($gradlePath)
if ($fixed -notmatch 'versionCode = 27' -or $fixed -notmatch 'versionName = "2.3.1"' -or $fixed -notmatch '// preserved') {
    throw "Metadata synchronization failed."
}
if ((Get-Content "$fixture/build/android/generated/BuildInfo.h" -Raw) -match '@PLATFORMER_') {
    throw "Header still contains placeholders."
}
$header = Get-Content "$fixture/build/android/generated/BuildInfo.h" -Raw
if ($header -notmatch 'PLATFORMER_CLIENT_VERSION "2.3.1"' -or $header -notmatch 'PLATFORMER_CLIENT_VERSION_ID "27"') {
    throw "Compiled client identity is missing or incorrect."
}
& "$repo/fix-android-config.ps1" -RepoRoot $fixture
if ([IO.File]::ReadAllText($gradlePath) -cne $fixed) { throw "Repeated repair changed Gradle config." }
foreach ($invalid in @('{"version":"bad","version_id":27}', '{"version":"2.3.1","version_id":0}', '{"version":"2.3.1","version_id":2.5}', '{')) {
    [IO.File]::WriteAllText($configPath, $invalid)
    $rejected = $false
    try { & "$repo/fix-android-config.ps1" -RepoRoot $fixture } catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid metadata was accepted." }
    if ([IO.File]::ReadAllText($gradlePath) -cne $fixed) { throw "Invalid metadata changed Gradle config." }
}
Write-Host "Android compiler config fixer tests passed."

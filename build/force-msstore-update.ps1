$ErrorActionPreference = "Stop"

if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw "winget is not available on this system."
}

Write-Host "Refreshing winget sources..."
winget source update

Write-Host "Updating Microsoft Store packages via winget..."
winget upgrade --all --source msstore --accept-source-agreements --accept-package-agreements

Write-Host "Done."

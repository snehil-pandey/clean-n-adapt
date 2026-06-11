param(
    [string]$InstallDir = "",
    [ValidateSet("User", "Machine")]
    [string]$PathScope = "User",
    [switch]$NoPath
)

$ErrorActionPreference = "Stop"

$SourceDir = $PSScriptRoot
if (-not $InstallDir) {
    $InstallDir = $SourceDir
}

$SourceExe = Join-Path $SourceDir "cna.exe"
if (-not (Test-Path -LiteralPath $SourceExe)) {
    throw "cna.exe was not found beside install.ps1. Extract the release ZIP first, then run install.ps1 from that folder."
}

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $InstallDir ".state") | Out-Null

$TargetExe = Join-Path $InstallDir "cna.exe"
if ((Resolve-Path -LiteralPath $SourceExe).Path -ine (Join-Path (Resolve-Path -LiteralPath $InstallDir).Path "cna.exe")) {
    Copy-Item -LiteralPath $SourceExe -Destination $TargetExe -Force
}

$Cmd = Join-Path $InstallDir "cna.cmd"
Set-Content -LiteralPath $Cmd -Encoding ASCII -Value "@echo off`r`nset ""APP_DIR=%~dp0""`r`nset ""CNA_STATE_DIR=%APP_DIR%.state""`r`n""%APP_DIR%cna.exe"" %*`r`n"

if (-not $NoPath) {
    & (Join-Path $SourceDir "add-to-path.ps1") -InstallDir $InstallDir -Scope $PathScope
}

Write-Host ""
Write-Host "Installed clean-n-adapt to $InstallDir" -ForegroundColor Green
Write-Host "State DB folder: $InstallDir\.state"
Write-Host "Open a new terminal and run:"
Write-Host "  cna"

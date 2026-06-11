$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)

$InstallDir = "C:\Program Files\cleanNadapt"

if (-not (Test-Path ".venv")) {
    python -m venv .venv
}

& .\.venv\Scripts\python.exe -m pip install --upgrade pip
& .\.venv\Scripts\python.exe -m pip install -e ".[build]"
& .\.venv\Scripts\pyinstaller.exe --onefile --name cna --clean src\clean_n_adapt\__main__.py

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $InstallDir ".state") | Out-Null
Copy-Item -LiteralPath ".\dist\cna.exe" -Destination (Join-Path $InstallDir "cna.exe") -Force

$Cmd = Join-Path $InstallDir "cna.cmd"
Set-Content -LiteralPath $Cmd -Encoding ASCII -Value "@echo off`r`nset ""APP_DIR=%~dp0""`r`nset ""CNA_STATE_DIR=%APP_DIR%.state""`r`n""%APP_DIR%cna.exe"" %*`r`n"

& (Join-Path $PSScriptRoot "add-to-path.ps1") -InstallDir $InstallDir -Scope Machine

Write-Host ""
Write-Host "Installed to $InstallDir" -ForegroundColor Green
Write-Host "State DB folder: <install folder>\.state"
Write-Host "Open a new terminal and run:"
Write-Host "  cna"

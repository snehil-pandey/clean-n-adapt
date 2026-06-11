$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)

if (-not (Test-Path ".venv")) {
    python -m venv .venv
}

& .\.venv\Scripts\python.exe -m pip install --upgrade pip
& .\.venv\Scripts\python.exe -m pip install -e .
Write-Host ""
Write-Host "Installed. Run:" -ForegroundColor Green
Write-Host "  .\.venv\Scripts\cna.exe --help"

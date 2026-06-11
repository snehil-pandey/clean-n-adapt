$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)

if (-not (Test-Path ".venv")) {
    python -m venv .venv
}

& .\.venv\Scripts\python.exe -m pip install --upgrade pip
& .\.venv\Scripts\python.exe -m pip install -e ".[build]"
& .\.venv\Scripts\pyinstaller.exe --onefile --name cna --clean src\clean_n_adapt\__main__.py
Write-Host ""
Write-Host "Built dist\cna.exe" -ForegroundColor Green

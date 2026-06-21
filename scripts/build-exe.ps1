$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)

function Invoke-Step {
    param([scriptblock]$Command)
    & $Command
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if (-not (Test-Path ".venv")) {
    Invoke-Step { python -m venv .venv }
}

Invoke-Step { & .\.venv\Scripts\python.exe -m pip install --upgrade pip }
Invoke-Step { & .\.venv\Scripts\python.exe -m pip install -e ".[build]" }
Invoke-Step { & .\.venv\Scripts\python.exe -m nuitka `
    --standalone `
    --onefile `
    --windows-uac-admin `
    --assume-yes-for-downloads `
    --enable-plugin=pyside6 `
    --output-filename=cna.exe `
    --output-dir=dist `
    --python-flag=-m `
    src\clean_n_adapt }
Write-Host ""
Write-Host "Built dist\cna.exe" -ForegroundColor Green
Write-Host "Install with:"
Write-Host "  powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1"

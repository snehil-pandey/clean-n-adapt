@echo off
setlocal
cd /d "%~dp0\.."

if not exist ".venv" (
    python -m venv .venv
)

".venv\Scripts\python.exe" -m pip install --upgrade pip
".venv\Scripts\python.exe" -m pip install -e .
echo.
echo Installed. Run:
echo   .venv\Scripts\cna.exe --help

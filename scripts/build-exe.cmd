@echo off
setlocal
cd /d "%~dp0\.."

if not exist ".venv" (
    python -m venv .venv
)

".venv\Scripts\python.exe" -m pip install --upgrade pip
".venv\Scripts\python.exe" -m pip install -e ".[build]"
".venv\Scripts\pyinstaller.exe" --onefile --name cna --clean src\clean_n_adapt\__main__.py
echo.
echo Built dist\cna.exe

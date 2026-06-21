@echo off
setlocal
cd /d "%~dp0\.."

if not exist ".venv" (
    python -m venv .venv
    if errorlevel 1 exit /b 1
)

".venv\Scripts\python.exe" -m pip install --upgrade pip
if errorlevel 1 exit /b 1
".venv\Scripts\python.exe" -m pip install -e ".[build]"
if errorlevel 1 exit /b 1
".venv\Scripts\python.exe" -m nuitka ^
  --standalone ^
  --onefile ^
  --windows-uac-admin ^
  --assume-yes-for-downloads ^
  --enable-plugin=pyside6 ^
  --output-filename=cna.exe ^
  --output-dir=dist ^
  --python-flag=-m ^
  src\clean_n_adapt
if errorlevel 1 exit /b 1
echo.
echo Built dist\cna.exe
echo Install with:
echo   scripts\install.cmd

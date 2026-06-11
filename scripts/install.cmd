@echo off
setlocal
cd /d "%~dp0\.."

set "INSTALL_DIR=C:\Program Files\cleanNadapt"
set "STATE_DIR=%INSTALL_DIR%\.state"

if not exist ".venv" (
    python -m venv .venv
)

".venv\Scripts\python.exe" -m pip install --upgrade pip
".venv\Scripts\python.exe" -m pip install -e ".[build]"
".venv\Scripts\pyinstaller.exe" --onefile --name cna --clean src\clean_n_adapt\__main__.py

mkdir "%INSTALL_DIR%" 2>nul
mkdir "%STATE_DIR%" 2>nul
copy /Y "dist\cna.exe" "%INSTALL_DIR%\cna.exe" >nul
(
    echo @echo off
    echo set CNA_STATE_DIR=%STATE_DIR%
    echo "%INSTALL_DIR%\cna.exe" %%*
) > "%INSTALL_DIR%\cna.cmd"

setx /M PATH "%PATH%;%INSTALL_DIR%" >nul
echo.
echo Installed to %INSTALL_DIR%
echo State DB folder: %STATE_DIR%
echo Open a new terminal and run:
echo   cna

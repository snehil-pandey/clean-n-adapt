@echo off
setlocal
cd /d "%~dp0\.."

set "INSTALL_DIR=C:\Program Files\cleanNadapt"

if not exist ".venv" (
    python -m venv .venv
)

".venv\Scripts\python.exe" -m pip install --upgrade pip
".venv\Scripts\python.exe" -m pip install -e ".[build]"
".venv\Scripts\pyinstaller.exe" --onefile --name cna --clean src\clean_n_adapt\__main__.py

mkdir "%INSTALL_DIR%" 2>nul
mkdir "%INSTALL_DIR%\.state" 2>nul
copy /Y "dist\cna.exe" "%INSTALL_DIR%\cna.exe" >nul
(
    echo @echo off
    echo set "APP_DIR=%%~dp0"
    echo set "CNA_STATE_DIR=%%APP_DIR%%.state"
    echo "%%APP_DIR%%cna.exe" %%*
) > "%INSTALL_DIR%\cna.cmd"

call "%~dp0add-to-path.cmd" "%INSTALL_DIR%"
echo.
echo Installed to %INSTALL_DIR%
echo State DB folder: ^<install folder^>\.state
echo Open a new terminal and run:
echo   cna

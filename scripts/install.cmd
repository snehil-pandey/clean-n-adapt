@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

set "INSTALL_DIR=%~1"
if "%INSTALL_DIR%"=="" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%\install.ps1"
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%\install.ps1" -InstallDir "%INSTALL_DIR%"
)

exit /b %errorlevel%

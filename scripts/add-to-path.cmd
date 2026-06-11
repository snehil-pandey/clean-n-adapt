@echo off
setlocal

set "INSTALL_DIR=%~dp0"
if "%INSTALL_DIR:~-1%"=="\" set "INSTALL_DIR=%INSTALL_DIR:~0,-1%"
if not "%~1"=="" set "INSTALL_DIR=%~1"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0add-to-path.ps1" -InstallDir "%INSTALL_DIR%" -Scope User
exit /b %errorlevel%

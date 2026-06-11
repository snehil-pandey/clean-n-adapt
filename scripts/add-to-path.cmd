@echo off
setlocal

set "INSTALL_DIR=C:\Program Files\cleanNadapt"
if not "%~1"=="" set "INSTALL_DIR=%~1"

echo %PATH% | find /I "%INSTALL_DIR%" >nul
if not errorlevel 1 (
    echo Already in PATH: %INSTALL_DIR%
    echo Open a new terminal if cna is still not found.
    exit /b 0
)

setx /M PATH "%PATH%;%INSTALL_DIR%" >nul
if errorlevel 1 (
    echo Failed to update machine PATH. Run this from an elevated terminal.
    exit /b 1
)

echo Added to machine PATH: %INSTALL_DIR%
echo Open a new terminal for PATH changes to apply.

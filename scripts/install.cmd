@echo off
setlocal

set "SOURCE_DIR=%~dp0"
if "%SOURCE_DIR:~-1%"=="\" set "SOURCE_DIR=%SOURCE_DIR:~0,-1%"

set "INSTALL_DIR=%~1"
if "%INSTALL_DIR%"=="" set "INSTALL_DIR=%SOURCE_DIR%"

set "SOURCE_EXE=%SOURCE_DIR%\cna.exe"
if not exist "%SOURCE_EXE%" (
    echo cna.exe was not found beside install.cmd.
    echo Extract the release ZIP first, then run install.cmd from that folder.
    exit /b 1
)

mkdir "%INSTALL_DIR%" 2>nul
mkdir "%INSTALL_DIR%\.state" 2>nul

if /I not "%SOURCE_EXE%"=="%INSTALL_DIR%\cna.exe" (
    copy /Y "%SOURCE_EXE%" "%INSTALL_DIR%\cna.exe" >nul
)

(
    echo @echo off
    echo set "APP_DIR=%%~dp0"
    echo set "CNA_STATE_DIR=%%APP_DIR%%.state"
    echo "%%APP_DIR%%cna.exe" %%*
) > "%INSTALL_DIR%\cna.cmd"

if /I not "%~2"=="--no-path" (
    call "%SOURCE_DIR%\add-to-path.cmd" "%INSTALL_DIR%"
    if errorlevel 1 exit /b 1
)

echo.
echo Installed clean-n-adapt to %INSTALL_DIR%
echo State DB folder: %INSTALL_DIR%\.state
echo Open a new terminal and run:
echo   cna
echo Or run directly now:
echo   "%INSTALL_DIR%\cna.cmd"

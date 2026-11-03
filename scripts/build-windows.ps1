<#
.SYNOPSIS
  Builds cna.exe, cna-widget.exe and the installer (dist\Clean-n-Adapt-Setup-<version>.exe) on Windows.

.DESCRIPTION
  Needs: CMake, a C++17 compiler (Visual Studio Build Tools "Desktop development with C++", or MinGW-w64),
  and NSIS for the installer:
      winget install Kitware.CMake
      winget install NSIS.NSIS
  Run from any folder:   powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1
  Options:               -Config Debug      -Clean
#>
param(
  [string]$Config = "Release",
  [switch]$Clean
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
  throw "CMake was not found. Install it with:  winget install Kitware.CMake  (then open a new terminal)"
}
$nsis = @((Get-Command makensis -ErrorAction SilentlyContinue).Source,
          "$env:ProgramFiles\NSIS\makensis.exe",
          "${env:ProgramFiles(x86)}\NSIS\makensis.exe") | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $nsis) {
  Write-Warning "NSIS was not found - the installer will NOT be built. Install it with:  winget install NSIS.NSIS"
}

if ($Clean -and (Test-Path build)) { Remove-Item build -Recurse -Force }
cmake -S . -B build
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
cmake --build build --config $Config
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host ""
Write-Host "Output in $root\dist :" -ForegroundColor Green
Get-ChildItem "$root\dist" | Format-Table Name, @{n="Size (KB)"; e={[math]::Round($_.Length/1KB)}} -AutoSize

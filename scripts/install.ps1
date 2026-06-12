param(
    [string]$InstallDir = "$env:ProgramFiles\cleanNadapt",
    [ValidateSet("User", "Machine")]
    [string]$PathScope = "Machine",
    [switch]$NoPath,
    [switch]$DesktopShortcut
)

$ErrorActionPreference = "Stop"

function Test-Admin {
    $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function New-Shortcut([string]$Path, [string]$Target, [string]$WorkingDirectory, [string]$Description) {
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($Path)
    $shortcut.TargetPath = $Target
    $shortcut.WorkingDirectory = $WorkingDirectory
    $shortcut.Description = $Description
    $shortcut.IconLocation = "$Target,0"
    $shortcut.Save()
}

$SourceDir = $PSScriptRoot
$RepoRoot = Split-Path -Parent $SourceDir
$SourceCandidates = @(
    (Join-Path $SourceDir "cna.exe"),
    (Join-Path $RepoRoot "dist\cna.exe"),
    (Join-Path (Get-Location) "dist\cna.exe")
)
$SourceExe = $SourceCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $SourceExe) {
    throw @"
cna.exe was not found.

Expected one of:
  $($SourceCandidates -join "`n  ")

Release ZIP layout:
  install.ps1 beside cna.exe

Repo/dev layout:
  run scripts\build-exe.cmd first so dist\cna.exe exists,
  then run scripts\install.cmd or scripts\install.ps1.
"@
}

$NeedsAdmin = ($PathScope -eq "Machine") -or $InstallDir.StartsWith($env:ProgramFiles, [System.StringComparison]::OrdinalIgnoreCase)
if ($NeedsAdmin -and -not (Test-Admin)) {
    $argList = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"", "-InstallDir", "`"$InstallDir`"", "-PathScope", $PathScope)
    if ($NoPath) { $argList += "-NoPath" }
    if ($DesktopShortcut) { $argList += "-DesktopShortcut" }
    Write-Host "Clean-n-Adapt needs administrator permission to install into:" -ForegroundColor Yellow
    Write-Host "  $InstallDir"
    Write-Host "Accept the UAC prompt. This window will wait and verify the install."
    $proc = Start-Process powershell -Verb RunAs -ArgumentList ($argList -join " ") -Wait -PassThru
    $ExpectedLauncher = Join-Path $InstallDir "cna.cmd"
    if ($proc.ExitCode -eq 0 -and (Test-Path -LiteralPath $ExpectedLauncher)) {
        Write-Host ""
        Write-Host "Installed Clean-n-Adapt to $InstallDir" -ForegroundColor Green
        Write-Host "Run it directly now:"
        Write-Host "  `"$ExpectedLauncher`""
        Write-Host "Or open a new terminal and run:"
        Write-Host "  cna"
        exit 0
    }
    Write-Host ""
    Write-Host "Install did not complete at the expected C: location:" -ForegroundColor Red
    Write-Host "  $InstallDir"
    Write-Host "Expected launcher missing:"
    Write-Host "  $ExpectedLauncher"
    Write-Host "Try running PowerShell as Administrator, then run:"
    Write-Host "  powershell -ExecutionPolicy Bypass -File .\install.ps1"
    exit 1
}

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $InstallDir ".state") | Out-Null

$TargetExe = Join-Path $InstallDir "cna.exe"
Copy-Item -LiteralPath $SourceExe -Destination $TargetExe -Force

$Cmd = Join-Path $InstallDir "cna.cmd"
Set-Content -LiteralPath $Cmd -Encoding ASCII -Value "@echo off`r`nset ""APP_DIR=%~dp0""`r`nset ""CNA_STATE_DIR=%APP_DIR%.state""`r`n""%APP_DIR%cna.exe"" %*`r`n"

if (-not $NoPath) {
    & (Join-Path $SourceDir "add-to-path.ps1") -InstallDir $InstallDir -Scope $PathScope
}

$IsAdmin = Test-Admin
$Programs = if ($IsAdmin) { [Environment]::GetFolderPath("CommonPrograms") } else { [Environment]::GetFolderPath("Programs") }
$ShortcutDir = Join-Path $Programs "Clean-n-Adapt"
try {
    New-Item -ItemType Directory -Force -Path $ShortcutDir | Out-Null
    New-Shortcut -Path (Join-Path $ShortcutDir "Clean-n-Adapt.lnk") -Target $Cmd -WorkingDirectory $InstallDir -Description "Open Clean-n-Adapt dashboard"
} catch {
    Write-Host "Warning: could not create Start Menu shortcut: $($_.Exception.Message)" -ForegroundColor Yellow
}

if ($DesktopShortcut) {
    $Desktop = if ($IsAdmin) { [Environment]::GetFolderPath("CommonDesktopDirectory") } else { [Environment]::GetFolderPath("Desktop") }
    try {
        New-Shortcut -Path (Join-Path $Desktop "Clean-n-Adapt.lnk") -Target $Cmd -WorkingDirectory $InstallDir -Description "Open Clean-n-Adapt dashboard"
    } catch {
        Write-Host "Warning: could not create Desktop shortcut: $($_.Exception.Message)" -ForegroundColor Yellow
    }
}

try {
    $TerminalRoot = Join-Path $env:LOCALAPPDATA "Microsoft\Windows Terminal\Fragments\CleanNAdapt"
    New-Item -ItemType Directory -Force -Path $TerminalRoot | Out-Null
    $TerminalProfile = @{
        profiles = @(
            @{
                name = "Clean-n-Adapt"
                commandline = "`"$Cmd`""
                startingDirectory = $InstallDir
                icon = $TargetExe
                hidden = $false
            }
        )
    } | ConvertTo-Json -Depth 5
    Set-Content -LiteralPath (Join-Path $TerminalRoot "clean-n-adapt.json") -Encoding UTF8 -Value $TerminalProfile
} catch {
    Write-Host "Warning: could not create Windows Terminal profile: $($_.Exception.Message)" -ForegroundColor Yellow
}

$UninstallRoot = if ($PathScope -eq "Machine") {
    "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\CleanNAdapt"
} else {
    "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\CleanNAdapt"
}
try {
    New-Item -Path $UninstallRoot -Force | Out-Null
    Set-ItemProperty -Path $UninstallRoot -Name DisplayName -Value "Clean-n-Adapt"
    Set-ItemProperty -Path $UninstallRoot -Name DisplayVersion -Value "1.2.1"
    Set-ItemProperty -Path $UninstallRoot -Name Publisher -Value "snehil-pandey"
    Set-ItemProperty -Path $UninstallRoot -Name InstallLocation -Value $InstallDir
    Set-ItemProperty -Path $UninstallRoot -Name DisplayIcon -Value $TargetExe
    Set-ItemProperty -Path $UninstallRoot -Name UninstallString -Value "powershell -NoProfile -ExecutionPolicy Bypass -Command `"Remove-Item -LiteralPath '$InstallDir' -Recurse -Force`""
} catch {
    Write-Host "Warning: could not register uninstall entry: $($_.Exception.Message)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Installed Clean-n-Adapt to $InstallDir" -ForegroundColor Green
Write-Host "Start Menu: Clean-n-Adapt"
Write-Host "State DB folder: $InstallDir\.state"
Write-Host "Open a new terminal and run:"
Write-Host "  cna"
Write-Host "Or run directly now:"
Write-Host "  `"$Cmd`""

if (-not (Test-Path -LiteralPath $Cmd)) {
    throw "Install verification failed. Missing launcher: $Cmd"
}

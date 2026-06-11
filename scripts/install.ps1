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
$SourceExe = Join-Path $SourceDir "cna.exe"
if (-not (Test-Path -LiteralPath $SourceExe)) {
    throw "cna.exe was not found beside install.ps1. Extract the release ZIP first, then run install.ps1 from that folder."
}

$NeedsAdmin = ($PathScope -eq "Machine") -or $InstallDir.StartsWith($env:ProgramFiles, [System.StringComparison]::OrdinalIgnoreCase)
if ($NeedsAdmin -and -not (Test-Admin)) {
    $argList = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"", "-InstallDir", "`"$InstallDir`"", "-PathScope", $PathScope)
    if ($NoPath) { $argList += "-NoPath" }
    if ($DesktopShortcut) { $argList += "-DesktopShortcut" }
    Start-Process powershell -Verb RunAs -ArgumentList ($argList -join " ")
    exit 0
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

$Programs = [Environment]::GetFolderPath("CommonPrograms")
$ShortcutDir = Join-Path $Programs "Clean-n-Adapt"
New-Item -ItemType Directory -Force -Path $ShortcutDir | Out-Null
New-Shortcut -Path (Join-Path $ShortcutDir "Clean-n-Adapt.lnk") -Target $Cmd -WorkingDirectory $InstallDir -Description "Open Clean-n-Adapt dashboard"

if ($DesktopShortcut) {
    $Desktop = [Environment]::GetFolderPath("CommonDesktopDirectory")
    New-Shortcut -Path (Join-Path $Desktop "Clean-n-Adapt.lnk") -Target $Cmd -WorkingDirectory $InstallDir -Description "Open Clean-n-Adapt dashboard"
}

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

$UninstallRoot = if ($PathScope -eq "Machine") {
    "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\CleanNAdapt"
} else {
    "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\CleanNAdapt"
}
New-Item -Path $UninstallRoot -Force | Out-Null
Set-ItemProperty -Path $UninstallRoot -Name DisplayName -Value "Clean-n-Adapt"
Set-ItemProperty -Path $UninstallRoot -Name DisplayVersion -Value "1.2.0"
Set-ItemProperty -Path $UninstallRoot -Name Publisher -Value "snehil-pandey"
Set-ItemProperty -Path $UninstallRoot -Name InstallLocation -Value $InstallDir
Set-ItemProperty -Path $UninstallRoot -Name DisplayIcon -Value $TargetExe
Set-ItemProperty -Path $UninstallRoot -Name UninstallString -Value "powershell -NoProfile -ExecutionPolicy Bypass -Command `"Remove-Item -LiteralPath '$InstallDir' -Recurse -Force`""

Write-Host ""
Write-Host "Installed Clean-n-Adapt to $InstallDir" -ForegroundColor Green
Write-Host "Start Menu: Clean-n-Adapt"
Write-Host "State DB folder: $InstallDir\.state"
Write-Host "Open a new terminal and run:"
Write-Host "  cna"
Write-Host "Or run directly now:"
Write-Host "  `"$Cmd`""

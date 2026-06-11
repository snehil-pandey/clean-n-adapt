param(
    [string]$InstallDir = $PSScriptRoot,
    [ValidateSet("Machine", "User")]
    [string]$Scope = "User"
)

$ErrorActionPreference = "Stop"

function Normalize-PathPart([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return ""
    }
    return [Environment]::ExpandEnvironmentVariables($Value).Trim().Trim('"').TrimEnd("\")
}

function Send-EnvironmentChanged {
    $signature = @"
using System;
using System.Runtime.InteropServices;
public static class EnvBroadcast {
    [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Auto)]
    public static extern IntPtr SendMessageTimeout(
        IntPtr hWnd,
        UInt32 Msg,
        UIntPtr wParam,
        string lParam,
        UInt32 fuFlags,
        UInt32 uTimeout,
        out UIntPtr lpdwResult
    );
}
"@
    if (-not ("EnvBroadcast" -as [type])) {
        Add-Type $signature
    }
    $result = [UIntPtr]::Zero
    [void][EnvBroadcast]::SendMessageTimeout([IntPtr]0xffff, 0x1A, [UIntPtr]::Zero, "Environment", 0x2, 5000, [ref]$result)
}

$ResolvedInstallDir = (Resolve-Path -LiteralPath $InstallDir).Path.TrimEnd("\")
$RegistryPath = if ($Scope -eq "Machine") {
    "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment"
} else {
    "HKCU:\Environment"
}

$CurrentPath = (Get-ItemProperty -Path $RegistryPath -Name Path -ErrorAction SilentlyContinue).Path
if ([string]::IsNullOrWhiteSpace($CurrentPath)) {
    $CurrentPath = ""
}

$InstallNorm = Normalize-PathPart $ResolvedInstallDir
$Parts = @()
$AlreadyPresent = $false
foreach ($Part in ($CurrentPath -split ";")) {
    if ([string]::IsNullOrWhiteSpace($Part)) {
        continue
    }
    $PartNorm = Normalize-PathPart $Part
    if ($PartNorm -ieq $InstallNorm) {
        $AlreadyPresent = $true
    } else {
        $Parts += $Part.Trim()
    }
}

if ($AlreadyPresent) {
    Write-Host "Already in $Scope PATH: $ResolvedInstallDir" -ForegroundColor Yellow
} else {
    $NewPath = (@($ResolvedInstallDir) + $Parts) -join ";"
    Set-ItemProperty -Path $RegistryPath -Name Path -Value $NewPath
    Write-Host "Added to $Scope PATH: $ResolvedInstallDir" -ForegroundColor Green
}

$ProcessParts = ($env:Path -split ";") | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
if (-not ($ProcessParts | Where-Object { (Normalize-PathPart $_) -ieq $InstallNorm })) {
    $env:Path = "$ResolvedInstallDir;$env:Path"
}

Send-EnvironmentChanged

$Launcher = Join-Path $ResolvedInstallDir "cna.cmd"
if (Test-Path -LiteralPath $Launcher) {
    Write-Host "Launcher found: $Launcher" -ForegroundColor Green
} else {
    Write-Host "Warning: cna.cmd was not found in $ResolvedInstallDir" -ForegroundColor Yellow
}

Write-Host "Open a new terminal for PATH changes to apply everywhere."
Write-Host "For this terminal, try:"
Write-Host "  cna --version"

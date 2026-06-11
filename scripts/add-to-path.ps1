param(
    [string]$InstallDir = $PSScriptRoot,
    [ValidateSet("Machine", "User")]
    [string]$Scope = "User"
)

$ErrorActionPreference = "Stop"

$CurrentPath = [Environment]::GetEnvironmentVariable("Path", $Scope)
if ([string]::IsNullOrWhiteSpace($CurrentPath)) {
    $CurrentPath = ""
}

$Parts = $CurrentPath -split ";" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
$AlreadyPresent = $false
foreach ($Part in $Parts) {
    if ($Part.TrimEnd("\") -ieq $InstallDir.TrimEnd("\")) {
        $AlreadyPresent = $true
        break
    }
}

if (-not $AlreadyPresent) {
    $NewPath = if ($CurrentPath.Trim()) { "$CurrentPath;$InstallDir" } else { $InstallDir }
    [Environment]::SetEnvironmentVariable("Path", $NewPath, $Scope)
    Write-Host "Added to $Scope PATH: $InstallDir" -ForegroundColor Green
} else {
    Write-Host "Already in $Scope PATH: $InstallDir" -ForegroundColor Yellow
}

Write-Host "Open a new terminal for PATH changes to apply."

$ErrorActionPreference = "Stop"

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Prefix = if ($env:THAGORE_PREFIX) { $env:THAGORE_PREFIX } else { Join-Path $env:ProgramFiles "Thagore" }
& (Join-Path $PSScriptRoot "windows.ps1") -yes

Write-Host "[thagore-installer] Installing Thagore to $Prefix..."
New-Item -ItemType Directory -Force -Path $Prefix | Out-Null
Copy-Item -Recurse -Force (Join-Path $RootDir "dist\*") $Prefix

$thagoreBin = Join-Path $Prefix "bin\thagore.exe"
$legacyThagBin = Join-Path $Prefix "bin\thag.exe"
$legacyCmdBin = Join-Path $Prefix "bin\thagore.cmd"
if (Test-Path $legacyThagBin) {
    Remove-Item -Force $legacyThagBin
}
if (Test-Path $legacyCmdBin) {
    Remove-Item -Force $legacyCmdBin
}

function Add-PathEntry {
    param(
        [Parameter(Mandatory = $true)][string]$Scope,
        [Parameter(Mandatory = $true)][string]$Entry
    )
    $cur = [Environment]::GetEnvironmentVariable("Path", $Scope)
    if ([string]::IsNullOrEmpty($cur)) {
        [Environment]::SetEnvironmentVariable("Path", $Entry, $Scope)
        return
    }
    $parts = $cur -split ";"
    if ($parts -contains $Entry) {
        return
    }
    [Environment]::SetEnvironmentVariable("Path", "$($cur.TrimEnd(';'));$Entry", $Scope)
}

$binPath = Join-Path $Prefix "bin"
Add-PathEntry -Scope "Machine" -Entry $binPath
Add-PathEntry -Scope "User" -Entry $binPath
if ($env:Path -notlike "*$binPath*") {
    $env:Path = "$env:Path;$binPath"
}

Write-Host "Thagore installed successfully."
Write-Host "Binary: $thagoreBin"
Write-Host "Legacy cleaned: $legacyThagBin, $legacyCmdBin"
Write-Host "Stdlib: $Prefix\lib\std"

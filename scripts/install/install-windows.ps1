$ErrorActionPreference = "Stop"

$candidatePackageRoot = Join-Path $PSScriptRoot ".."
$candidateRepoRoot = Join-Path $PSScriptRoot "..\.."
if ((Test-Path (Join-Path $candidatePackageRoot "bin\thagore.exe")) -and (Test-Path (Join-Path $candidatePackageRoot "lib\std"))) {
    $RootDir = (Resolve-Path $candidatePackageRoot).Path
} elseif (Test-Path (Join-Path $candidateRepoRoot "dist\bin\thagore.exe")) {
    $RootDir = (Resolve-Path $candidateRepoRoot).Path
} else {
    throw "Cannot resolve Thagore package root near $PSScriptRoot"
}
$Prefix = if ($env:THAGORE_PREFIX) { $env:THAGORE_PREFIX } else { Join-Path $env:ProgramFiles "Thagore" }
& (Join-Path $PSScriptRoot "windows.ps1") -yes -prefix $Prefix

Write-Host "[thagore-installer] Installing Thagore to $Prefix..."
New-Item -ItemType Directory -Force -Path $Prefix | Out-Null
$SourceRoot = if (Test-Path (Join-Path $RootDir "dist\bin\thagore.exe")) { Join-Path $RootDir "dist" } else { $RootDir }
Copy-Item -Recurse -Force (Join-Path $SourceRoot "*") $Prefix

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

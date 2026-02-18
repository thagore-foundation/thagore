$ErrorActionPreference = "Stop"

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Prefix = if ($env:THAGORE_PREFIX) { $env:THAGORE_PREFIX } else { Join-Path $env:ProgramFiles "Thagore" }
& (Join-Path $PSScriptRoot "windows.ps1") -yes

Write-Host "[thagore-installer] Installing Thagore to $Prefix..."
New-Item -ItemType Directory -Force -Path $Prefix | Out-Null
Copy-Item -Recurse -Force (Join-Path $RootDir "dist\*") $Prefix

$thagoreBin = Join-Path $Prefix "bin\thagore.exe"
$thagCompatBin = Join-Path $Prefix "bin\thag.exe"
$machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
if ($machinePath -notlike "*$($Prefix)\bin*") {
    [Environment]::SetEnvironmentVariable("Path", "$machinePath;$($Prefix)\bin", "Machine")
}

Write-Host "Thagore installed successfully."
Write-Host "Binary: $thagoreBin"
Write-Host "Alias: $thagCompatBin"
Write-Host "Stdlib: $Prefix\lib\std"

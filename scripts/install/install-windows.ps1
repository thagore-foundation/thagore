$ErrorActionPreference = "Stop"

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Prefix = if ($env:THAGORE_PREFIX) { $env:THAGORE_PREFIX } else { Join-Path $env:ProgramFiles "Thagore" }

function Test-LLVM {
    if (Get-Command clang.exe -ErrorAction SilentlyContinue) {
        $v = & clang.exe --version 2>$null
        if ($v -match "version 21") { return $true }
    }
    return $false
}

if (-not (Test-LLVM)) {
    Write-Host "[thagore-installer] Installing LLVM 21..."
    if (Get-Command winget.exe -ErrorAction SilentlyContinue) {
        winget install --id LLVM.LLVM --silent --accept-package-agreements --accept-source-agreements
    } else {
        throw "LLVM 21 is required. Install winget or install LLVM manually."
    }
}

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

param(
    [string]$llvmVersion = "21.1.8",
    [string]$arch = "auto",
    [string]$prefix = "",
    [switch]$yes
)

$ErrorActionPreference = "Stop"

if ($llvmVersion -ne "21.1.8") {
    throw "Only LLVM 21.1.8 is supported in release installer."
}

if ($arch -eq "auto") {
    $pa = [Environment]::GetEnvironmentVariable("PROCESSOR_ARCHITECTURE")
    if ($pa -eq "AMD64") { $arch = "x86_64" }
    elseif ($pa -eq "ARM64") { $arch = "arm64" }
    else { $arch = "x86_64" }
}
if ($arch -eq "amd64") { $arch = "x86_64" }
if ($arch -eq "aarch64") { $arch = "arm64" }

function Test-LLVM {
    if (Get-Command clang.exe -ErrorAction SilentlyContinue) {
        $v = & clang.exe --version 2>$null
        if ($v -match "21\.1\.8" -or $v -match "version 21") { return $true }
    }
    if (Test-Path "C:\Program Files\LLVM\bin\clang.exe") {
        $v = & "C:\Program Files\LLVM\bin\clang.exe" --version 2>$null
        if ($v -match "21\.1\.8" -or $v -match "version 21") { return $true }
    }
    return $false
}

function Test-LLVMAtPath([string]$llvmRoot) {
    $clangPath = Join-Path $llvmRoot "bin\clang.exe"
    if (-not (Test-Path $clangPath)) {
        return $false
    }
    $v = & $clangPath --version 2>$null
    if ($v -match "21\.1\.8" -or $v -match "version 21") { return $true }
    return $false
}

$TargetPrefix = if ([string]::IsNullOrWhiteSpace($prefix)) {
    Join-Path $env:ProgramFiles "Thagore"
} else {
    $prefix
}
$TargetLLVM = Join-Path $TargetPrefix "llvm"

if (Test-LLVMAtPath $TargetLLVM) {
    Write-Host "LLVM already available at $TargetLLVM."
    exit 0
}

if (-not $yes) {
    $ans = Read-Host "Install LLVM $llvmVersion on Windows ($arch)? [Y/n]"
    if ($ans -and $ans.ToLower() -notin @("y","yes")) {
        Write-Host "Aborted."
        exit 1
    }
}

if (Get-Command winget.exe -ErrorAction SilentlyContinue) {
    winget install --id LLVM.LLVM --version 21.1.8 --silent --accept-package-agreements --accept-source-agreements
} elseif (Get-Command choco.exe -ErrorAction SilentlyContinue) {
    choco install llvm --version=21.1.8 --no-progress -y --execution-timeout=1800
} else {
    throw "Need winget or choco to install LLVM 21.1.8."
}

if (-not (Test-LLVM)) {
    throw "LLVM verification failed after install."
}

New-Item -ItemType Directory -Force -Path $TargetLLVM | Out-Null
$systemLLVM = "C:\Program Files\LLVM"
if (-not (Test-Path (Join-Path $systemLLVM "bin\clang.exe"))) {
    $clangCmd = Get-Command clang.exe -ErrorAction SilentlyContinue
    if ($clangCmd) {
        $resolved = Split-Path -Parent (Split-Path -Parent $clangCmd.Source)
        if (Test-Path (Join-Path $resolved "bin\clang.exe")) {
            $systemLLVM = $resolved
        }
    }
}
if (-not (Test-Path (Join-Path $systemLLVM "bin\clang.exe"))) {
    throw "Unable to locate installed LLVM root after winget/choco install."
}
Copy-Item -Recurse -Force (Join-Path $systemLLVM "*") $TargetLLVM

if (-not (Test-LLVMAtPath $TargetLLVM)) {
    throw "LLVM verification failed at target path: $TargetLLVM"
}

$llvmBin = Join-Path $TargetLLVM "bin"
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

Add-PathEntry -Scope "Machine" -Entry $llvmBin
Add-PathEntry -Scope "User" -Entry $llvmBin
if ($env:Path -notlike "*$llvmBin*") {
    $env:Path = "$env:Path;$llvmBin"
}

Write-Host "[thagore-installer] LLVM $llvmVersion install done for windows/$arch at $TargetLLVM"

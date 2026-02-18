param(
    [string]$llvmVersion = "21.1.8",
    [string]$arch = "auto",
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

if (Test-LLVM) {
    Write-Host "LLVM already available."
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

Write-Host "[thagore-installer] LLVM $llvmVersion install done for windows/$arch"

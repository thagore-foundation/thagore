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

function Test-InstalledAtomicBundle {
    param(
        [Parameter(Mandatory = $true)][string]$CompilerExe,
        [Parameter(Mandatory = $true)][string]$HelperExe,
        [Parameter(Mandatory = $true)][string]$RuntimeLib
    )
    if (-not (Test-Path $CompilerExe)) { throw "Missing compiler binary: $CompilerExe" }
    if (-not (Test-Path $HelperExe)) { throw "Missing helper binary: $HelperExe" }
    if (-not (Test-Path $RuntimeLib)) { throw "Missing runtime library: $RuntimeLib" }
    $versionOut = (& $CompilerExe --version 2>&1 | Out-String).Trim()
    $helpOut = (& $CompilerExe --help 2>&1 | Out-String).Trim()
    if ([string]::IsNullOrWhiteSpace($versionOut) -or [string]::IsNullOrWhiteSpace($helpOut)) {
        throw "Installed CLI returned empty --version/--help output."
    }
    $tmpDir = Join-Path $env:TEMP ("thagore-install-helper-smoke-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null
    $src = Join-Path $tmpDir "atomic_bundle_smoke.tg"
    $ll = Join-Path $tmpDir "atomic_bundle_smoke.ll"
    $bin = Join-Path $tmpDir "atomic_bundle_smoke.exe"
    @'
func main() -> i32:
    print("atomic package smoke")
    return 0
'@ | Set-Content -Path $src -NoNewline
    Copy-Item -Force $RuntimeLib (Join-Path $tmpDir "runtime.lib")
    $oldHelper = $env:THAG_HELPER_BIN
    try {
        $env:THAG_HELPER_BIN = $HelperExe
        & $CompilerExe --emit-llvm-internal $src -o $ll | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Installed helper smoke failed: internal emit command failed."
        }
        if (-not (Test-Path $ll) -or (Get-Item $ll).Length -le 0) {
            throw "Installed helper smoke failed: no LLVM IR output."
        }
        & $CompilerExe build $src -o $bin | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Installed helper smoke failed: build command failed."
        }
        if (-not (Test-Path $bin)) {
            throw "Installed helper smoke failed: build output missing."
        }
        $runOut = (& $bin 2>&1 | Out-String)
        if ($runOut -notmatch "atomic package smoke") {
            throw "Installed helper smoke failed: runtime output mismatch."
        }
    } finally {
        if ($null -eq $oldHelper) {
            Remove-Item Env:THAG_HELPER_BIN -ErrorAction SilentlyContinue
        } else {
            $env:THAG_HELPER_BIN = $oldHelper
        }
        Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue
    }
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
Test-InstalledAtomicBundle -CompilerExe $thagoreBin -HelperExe (Join-Path $Prefix "bin\stage1.exe") -RuntimeLib (Join-Path $Prefix "lib\runtime.lib")

Write-Host "Thagore installed successfully."
Write-Host "Binary: $thagoreBin"
Write-Host "Legacy cleaned: $legacyThagBin, $legacyCmdBin"
Write-Host "Stdlib: $Prefix\lib\std"

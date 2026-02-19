param(
    [string]$mode = "check",
    [switch]$yes,
    [switch]$dryRun
)

$ErrorActionPreference = "Stop"

function Get-Semver([string]$text) {
    if ([string]::IsNullOrWhiteSpace($text)) { return "0.0.0" }
    $m = [regex]::Match($text, "(\d+\.\d+\.\d+)")
    if ($m.Success) { return $m.Groups[1].Value }
    return "0.0.0"
}

function Get-ArchTag {
    $pa = [Environment]::GetEnvironmentVariable("PROCESSOR_ARCHITECTURE")
    if ($pa -eq "ARM64") { return "arm64" }
    return "x86_64"
}

function Get-LatestRelease {
    $headers = @{
        "User-Agent" = "thagore-update"
        "Accept" = "application/vnd.github+json"
    }
    return Invoke-RestMethod -Headers $headers -Uri "https://api.github.com/repos/thagore-foundation/thagore/releases/latest" -Method Get
}

function Resolve-AssetUrl($release, [string]$arch) {
    $name = "thagore-windows-$arch.tar.gz"
    foreach ($asset in $release.assets) {
        if ($asset.name -eq $name) {
            return @{ Name = $name; Url = $asset.browser_download_url }
        }
    }
    throw "Cannot find asset '$name' in latest release."
}

$selfDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$binCandidates = @()
if (-not [string]::IsNullOrWhiteSpace($env:THAGORE_BIN)) {
    $binCandidates += $env:THAGORE_BIN
}
$binCandidates += @(
    (Join-Path $selfDir "..\bin"),
    (Join-Path $selfDir "..\..\dist\bin"),
    (Join-Path $selfDir "..\..\bin")
)
$binDir = $null
foreach ($cand in $binCandidates) {
    if (Test-Path $cand) {
        $binDir = (Resolve-Path $cand).Path
        break
    }
}
if (-not $binDir) {
    throw "Cannot resolve bin directory near $selfDir"
}
$engine = Join-Path $binDir "thag.exe"
if (-not (Test-Path $engine)) {
    throw "Cannot find engine binary: $engine"
}

$currentRaw = (& $engine --version 2>$null | Out-String).Trim()
$currentVer = Get-Semver $currentRaw
$arch = Get-ArchTag
$release = Get-LatestRelease
$latestTag = "$($release.tag_name)"
$latestVer = Get-Semver $latestTag
$asset = Resolve-AssetUrl $release $arch

if ($mode -eq "check") {
    if ($latestVer -gt $currentVer) {
        Write-Host "Update available: $latestTag (current: $currentRaw)"
        Write-Host "Run: thagore update apply"
        exit 0
    }
    Write-Host "Already up to date ($currentRaw)."
    exit 0
}

if ($mode -eq "apply") {
    if ($latestVer -le $currentVer) {
        Write-Host "Already up to date ($currentRaw)."
        exit 0
    }
    if (-not $yes) {
        $ans = Read-Host "Update to $latestTag ? [Y/n]"
        if ($ans -and $ans.ToLower() -notin @("y","yes")) {
            Write-Host "Aborted."
            exit 1
        }
    }

    $stateDir = Join-Path $env:LOCALAPPDATA "Thagore\update"
    New-Item -ItemType Directory -Force -Path $stateDir | Out-Null
    $archivePath = Join-Path $stateDir "download.tar.gz"
    $extractDir = Join-Path $stateDir "extract"
    $backupPath = Join-Path $stateDir "thag.prev.exe"
    if (Test-Path $extractDir) { Remove-Item -Recurse -Force $extractDir }
    New-Item -ItemType Directory -Force -Path $extractDir | Out-Null

    Write-Host "[update] release=$latestTag asset=$($asset.Name)"
    if ($dryRun) {
        Write-Host "[update] dry-run url=$($asset.Url)"
        Write-Host "[update] dry-run target=$engine"
        exit 0
    }

    Invoke-WebRequest -Uri $asset.Url -OutFile $archivePath
    tar -xzf $archivePath -C $extractDir
    $newEngine = Join-Path $extractDir "bin\thag.exe"
    if (-not (Test-Path $newEngine)) {
        $newEngine = Join-Path $extractDir "bin\thagore.exe"
    }
    if (-not (Test-Path $newEngine)) {
        throw "Extracted asset does not contain thag.exe/thagore.exe"
    }

    Copy-Item -Force $engine $backupPath
    Copy-Item -Force $newEngine $engine
    Write-Host "Updated to $latestTag"
    exit 0
}

if ($mode -eq "rollback") {
    $backupPath = Join-Path $env:LOCALAPPDATA "Thagore\update\thag.prev.exe"
    if (-not (Test-Path $backupPath)) {
        throw "No rollback backup found."
    }
    if (-not $yes) {
        $ans = Read-Host "Rollback current binary? [Y/n]"
        if ($ans -and $ans.ToLower() -notin @("y","yes")) {
            Write-Host "Aborted."
            exit 1
        }
    }
    Copy-Item -Force $backupPath $engine
    Write-Host "Rollback completed."
    exit 0
}

throw "Unknown update mode '$mode' (expected: check|apply|rollback)."

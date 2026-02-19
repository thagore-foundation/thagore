param(
    [string]$mode = "check",
    [switch]$yes,
    [switch]$dryRun,
    [switch]$elevated
)

$ErrorActionPreference = "Stop"

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

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

function Copy-WithRetry {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Target,
        [int]$RetryCount = 5
    )
    for ($i = 0; $i -lt $RetryCount; $i++) {
        try {
            Copy-Item -Force $Source $Target
            return
        } catch {
            if ($i -eq ($RetryCount - 1)) { throw }
            Start-Sleep -Milliseconds (300 * ($i + 1))
        }
    }
}

function Get-FileSha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -Path $Path).Hash
}

function Copy-And-VerifyHash {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Target,
        [Parameter(Mandatory = $true)][string]$ExpectedHash,
        [string]$Label = "file"
    )
    Copy-WithRetry -Source $Source -Target $Target
    $actualHash = Get-FileSha256 -Path $Target
    if ($actualHash -ne $ExpectedHash) {
        throw "$Label hash mismatch after copy: $Target"
    }
}

function Ensure-AdminForInstallPath([string]$targetPath, [string]$mode) {
    if ($mode -notin @("apply", "rollback")) { return }
    if (Test-IsAdmin) { return }

    $programFiles64 = [Environment]::GetFolderPath("ProgramFiles")
    $programFiles32 = [Environment]::GetFolderPath("ProgramFilesX86")
    $normTarget = [System.IO.Path]::GetFullPath($targetPath).ToLowerInvariant()

    $needsAdmin = $false
    foreach ($root in @($programFiles64, $programFiles32)) {
        if ([string]::IsNullOrWhiteSpace($root)) { continue }
        $normRoot = [System.IO.Path]::GetFullPath($root).ToLowerInvariant().TrimEnd('\') + "\"
        if ($normTarget.StartsWith($normRoot)) {
            $needsAdmin = $true
            break
        }
    }
    if (-not $needsAdmin) { return }
    if ($elevated) { return }

    $argList = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"", $mode, "-elevated")
    if ($yes) { $argList += "-yes" }
    if ($dryRun) { $argList += "-dryRun" }

    Write-Host "[update] Relaunching with administrator privileges..."
    $proc = Start-Process -FilePath "powershell.exe" -Verb RunAs -ArgumentList $argList -Wait -PassThru
    exit $proc.ExitCode
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
$engine = Join-Path $binDir "thagore.exe"
if (-not (Test-Path $engine)) {
    throw "Cannot find engine binary: $engine"
}
$legacyEngine = Join-Path $binDir "thag.exe"
$legacyCmdWrapper = Join-Path $binDir "thagore.cmd"
$installRoot = (Resolve-Path (Join-Path $binDir "..")).Path
Ensure-AdminForInstallPath -targetPath $engine -mode $mode

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
    $backupEngine = Join-Path $stateDir "thagore.prev.exe"
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
    $newEngine = Join-Path $extractDir "bin\thagore.exe"
    if (-not (Test-Path $newEngine)) {
        throw "Extracted asset does not contain thagore.exe"
    }
    $newInstallerDir = Join-Path $extractDir "installer"
    $newStdDir = Join-Path $extractDir "lib\std"

    Copy-WithRetry -Source $engine -Target $backupEngine

    $newEngineHash = Get-FileSha256 -Path $newEngine

    try {
        Copy-And-VerifyHash -Source $newEngine -Target $engine -ExpectedHash $newEngineHash -Label "thagore.exe"
    } catch {
        $backupEngineHash = Get-FileSha256 -Path $backupEngine
        Copy-And-VerifyHash -Source $backupEngine -Target $engine -ExpectedHash $backupEngineHash -Label "rollback thagore.exe"
        throw "Failed to install new binaries safely. Rolled back. $($_.Exception.Message)"
    }

    if (Test-Path $newInstallerDir) {
        New-Item -ItemType Directory -Force -Path (Join-Path $installRoot "installer") | Out-Null
        Copy-Item -Recurse -Force (Join-Path $newInstallerDir "*") (Join-Path $installRoot "installer")
    }
    if (Test-Path $newStdDir) {
        New-Item -ItemType Directory -Force -Path (Join-Path $installRoot "lib\std") | Out-Null
        Copy-Item -Recurse -Force (Join-Path $newStdDir "*") (Join-Path $installRoot "lib\std")
    }
    if (Test-Path $legacyEngine) {
        Remove-Item -Force $legacyEngine
    }
    if (Test-Path $legacyCmdWrapper) {
        Remove-Item -Force $legacyCmdWrapper
    }

    Write-Host "Updated to $latestTag"
    Write-Host "[update] binary replaced: $engine"
    Write-Host "[update] stale legacy binaries removed: $legacyEngine, $legacyCmdWrapper"
    exit 0
}

if ($mode -eq "rollback") {
    $backupEngine = Join-Path $env:LOCALAPPDATA "Thagore\update\thagore.prev.exe"
    if (-not (Test-Path $backupEngine)) {
        throw "No rollback backup found for thagore.exe."
    }
    if (-not $yes) {
        $ans = Read-Host "Rollback current binary? [Y/n]"
        if ($ans -and $ans.ToLower() -notin @("y","yes")) {
            Write-Host "Aborted."
            exit 1
        }
    }
    $backupEngineHash = Get-FileSha256 -Path $backupEngine
    Copy-And-VerifyHash -Source $backupEngine -Target $engine -ExpectedHash $backupEngineHash -Label "rollback thagore.exe"
    if (Test-Path $legacyEngine) {
        Remove-Item -Force $legacyEngine
    }
    if (Test-Path $legacyCmdWrapper) {
        Remove-Item -Force $legacyCmdWrapper
    }
    Write-Host "Rollback completed."
    exit 0
}

throw "Unknown update mode '$mode' (expected: check|apply|rollback)."

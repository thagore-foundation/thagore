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

function Compare-Semver([string]$left, [string]$right) {
    try {
        $lv = [version](Get-Semver $left)
        $rv = [version](Get-Semver $right)
        return $lv.CompareTo($rv)
    } catch {
        return 0
    }
}

function Get-ArchTag {
    $pa = [Environment]::GetEnvironmentVariable("PROCESSOR_ARCHITECTURE")
    if ($pa -eq "ARM64") { return "arm64" }
    return "x86_64"
}

function Get-VerifiedRelease {
    param([string]$arch)
    $headers = @{
        "User-Agent" = "thagore-update"
        "Accept" = "application/vnd.github+json"
    }
    $assetName = "thagc-core-windows.tar.gz"
    $checksumName = "SHA256SUMS-thagc-windows.txt"
    $proofName = ""
    $releases = Invoke-RestMethod -Headers $headers -Uri "https://api.github.com/repos/thagore-foundation/thagore/releases?per_page=100" -Method Get
    foreach ($rel in $releases) {
        if ($rel.draft -or $rel.prerelease) { continue }
        $tag = "$($rel.tag_name)"
        if ($tag -notmatch '^v\d+\.\d+\.\d+$') { continue }
        $names = @{}
        foreach ($a in $rel.assets) {
            $names["$($a.name)"] = $true
        }
        if ($names.ContainsKey($assetName) -and $names.ContainsKey($checksumName)) {
            return $rel
        }
    }
    throw "No official release found for windows-$arch with assets $assetName + $checksumName."
}

function Resolve-AssetUrl($release, [string]$arch) {
    $name = "thagc-core-windows.tar.gz"
    foreach ($asset in $release.assets) {
        if ($asset.name -eq $name) {
            return @{ Name = $name; Url = $asset.browser_download_url }
        }
    }
    throw "Cannot find asset '$name' in verified release."
}

function Resolve-ChecksumUrl($release, [string]$arch) {
    $name = "SHA256SUMS-thagc-windows.txt"
    foreach ($asset in $release.assets) {
        if ($asset.name -eq $name) {
            return @{ Name = $name; Url = $asset.browser_download_url }
        }
    }
    throw "Cannot find checksum asset '$name' in verified release."
}

function Get-ExpectedHashFromChecksums {
    param(
        [Parameter(Mandatory = $true)][string]$ChecksumFile,
        [Parameter(Mandatory = $true)][string]$AssetName
    )
    foreach ($line in Get-Content -Path $ChecksumFile) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $parts = $line -split '\s+', 2
        if ($parts.Count -lt 2) { continue }
        $hash = $parts[0].Trim()
        $name = $parts[1].Trim()
        if ($name.StartsWith("*")) {
            $name = $name.Substring(1)
        }
        if ($name -eq $AssetName) {
            return $hash
        }
    }
    throw "Checksum for asset '$AssetName' not found in $ChecksumFile."
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

function Assert-CliOutputHealthy([string]$ExePath) {
    $versionOut = (& $ExePath --version 2>&1 | Out-String).Trim()
    $helpOut = (& $ExePath --help 2>&1 | Out-String).Trim()
    if ([string]::IsNullOrWhiteSpace($versionOut) -or [string]::IsNullOrWhiteSpace($helpOut)) {
        throw "CLI health check failed: empty --version/--help output."
    }
    $merged = "$versionOut`n$helpOut"
    if ($merged -match "cannot read source file:\s*update|Unknown update mode 'update'|Empty file or file not found|Usage:\s*thg\.exe") {
        throw "CLI health check failed: legacy/wrapper markers detected."
    }
}

function Assert-AtomicBuildHealthy {
    param(
        [Parameter(Mandatory = $true)][string]$CompilerExe
    )
    $tmpDir = Join-Path $env:TEMP ("thagore-update-atomic-smoke-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null
    $src = Join-Path $tmpDir "atomic_smoke.tg"
    $bin = Join-Path $tmpDir "atomic_smoke.exe"
    @'
func main() -> i32:
    print("atomic smoke")
    return 0
'@ | Set-Content -Path $src -NoNewline
    try {
        & $CompilerExe build $src -o $bin | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Atomic smoke failed: build command failed."
        }
        if (-not (Test-Path $bin)) {
            throw "Atomic smoke failed: output executable missing."
        }
        $out = (& $bin 2>&1 | Out-String).Trim()
        if ($out -notmatch "atomic smoke") {
            throw "Atomic smoke failed: runtime output mismatch."
        }
    } finally {
        Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue
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
$engine = Join-Path $binDir "thagc.exe"
if (-not (Test-Path $engine)) {
    $fallbackEngine = Join-Path $binDir "thagore.exe"
    if (Test-Path $fallbackEngine) {
        $engine = $fallbackEngine
    } else {
        throw "Cannot find compiler binary (thagc.exe or thagore.exe) in $binDir"
    }
}
$thagoreCompat = Join-Path $binDir "thagore.exe"
$legacyEngine = Join-Path $binDir "thag.exe"
$legacyCmdWrapper = Join-Path $binDir "thagore.cmd"
$installRoot = (Resolve-Path (Join-Path $binDir "..")).Path
if (-not (Test-Path (Join-Path $installRoot "lib"))) {
    New-Item -ItemType Directory -Force -Path (Join-Path $installRoot "lib") | Out-Null
}
$runtimeLib = Join-Path $installRoot "lib\runtime.lib"
Ensure-AdminForInstallPath -targetPath $engine -mode $mode

$currentRaw = (& $engine --version 2>$null | Out-String).Trim()
$currentVer = Get-Semver $currentRaw
$arch = Get-ArchTag
$release = Get-VerifiedRelease -arch $arch
$latestTag = "$($release.tag_name)"
$latestVer = Get-Semver $latestTag
$asset = Resolve-AssetUrl $release $arch
$checksumAsset = Resolve-ChecksumUrl $release $arch

if ($mode -eq "check") {
    if ((Compare-Semver $latestVer $currentVer) -gt 0) {
        Write-Host "Update available: $latestTag (current: $currentRaw)"
        Write-Host "Run: thagore update apply"
        exit 0
    }
    Write-Host "Already up to date ($currentRaw)."
    exit 0
}

if ($mode -eq "apply") {
    if ((Compare-Semver $latestVer $currentVer) -le 0) {
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
    $checksumPath = Join-Path $stateDir "SHA256SUMS.txt"
    $extractDir = Join-Path $stateDir "extract"
    $backupEngine = Join-Path $stateDir "thagc.prev.exe"
    $backupRuntime = Join-Path $stateDir "runtime.prev.lib"
    if (Test-Path $extractDir) { Remove-Item -Recurse -Force $extractDir }
    New-Item -ItemType Directory -Force -Path $extractDir | Out-Null

    Write-Host "[update] release=$latestTag asset=$($asset.Name)"
    if ($dryRun) {
        Write-Host "[update] dry-run url=$($asset.Url)"
        Write-Host "[update] dry-run target=$engine"
        exit 0
    }

    Invoke-WebRequest -Uri $asset.Url -OutFile $archivePath
    Invoke-WebRequest -Uri $checksumAsset.Url -OutFile $checksumPath
    $expectedArchiveHash = Get-ExpectedHashFromChecksums -ChecksumFile $checksumPath -AssetName $asset.Name
    $actualArchiveHash = Get-FileSha256 -Path $archivePath
    if ($actualArchiveHash.ToUpperInvariant() -ne $expectedArchiveHash.ToUpperInvariant()) {
        throw "Archive checksum mismatch for $($asset.Name)."
    }
    tar -xzf $archivePath -C $extractDir
    $newEngine = Join-Path $extractDir "bin\thagc.exe"
    if (-not (Test-Path $newEngine)) {
        $fallbackNew = Join-Path $extractDir "bin\thagore.exe"
        if (Test-Path $fallbackNew) {
            $newEngine = $fallbackNew
        } else {
            throw "Extracted asset does not contain thagc.exe"
        }
    }
    $newInstallerDir = Join-Path $extractDir "installer"
    $newStdDir = Join-Path $extractDir "lib\std"
    $newRuntimeLib = Join-Path $extractDir "lib\runtime.lib"
    if (-not (Test-Path $newStdDir)) {
        throw "Extracted asset does not contain lib\\std"
    }
    if (-not (Test-Path $newRuntimeLib)) {
        throw "Extracted asset does not contain lib\\runtime.lib"
    }

    Copy-WithRetry -Source $engine -Target $backupEngine
    if (Test-Path $runtimeLib) {
        Copy-WithRetry -Source $runtimeLib -Target $backupRuntime
    }

    $newEngineHash = Get-FileSha256 -Path $newEngine
    $newRuntimeHash = Get-FileSha256 -Path $newRuntimeLib

    try {
        Copy-And-VerifyHash -Source $newEngine -Target $engine -ExpectedHash $newEngineHash -Label "thagc.exe"
        if ($engine -ne $thagoreCompat) {
            Copy-And-VerifyHash -Source $newEngine -Target $thagoreCompat -ExpectedHash $newEngineHash -Label "thagore.exe"
        }
        Copy-And-VerifyHash -Source $newRuntimeLib -Target $runtimeLib -ExpectedHash $newRuntimeHash -Label "runtime.lib"
        Assert-CliOutputHealthy -ExePath $engine
        Assert-AtomicBuildHealthy -CompilerExe $engine
    } catch {
        $backupEngineHash = Get-FileSha256 -Path $backupEngine
        Copy-And-VerifyHash -Source $backupEngine -Target $engine -ExpectedHash $backupEngineHash -Label "rollback thagc.exe"
        if ($engine -ne $thagoreCompat) {
            Copy-And-VerifyHash -Source $backupEngine -Target $thagoreCompat -ExpectedHash $backupEngineHash -Label "rollback thagore.exe"
        }
        if (Test-Path $backupRuntime) {
            $backupRuntimeHash = Get-FileSha256 -Path $backupRuntime
            Copy-And-VerifyHash -Source $backupRuntime -Target $runtimeLib -ExpectedHash $backupRuntimeHash -Label "rollback runtime.lib"
        }
        throw "Failed to install atomic bundle safely. Rolled back. $($_.Exception.Message)"
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
    Write-Host "[update] compiler replaced: $engine"
    Write-Host "[update] runtime replaced: $runtimeLib"
    Write-Host "[update] stale legacy binaries removed: $legacyEngine, $legacyCmdWrapper"
    exit 0
}

if ($mode -eq "rollback") {
    $stateDir = Join-Path $env:LOCALAPPDATA "Thagore\update"
    $backupEngine = Join-Path $stateDir "thagc.prev.exe"
    $backupRuntime = Join-Path $stateDir "runtime.prev.lib"
    if (-not (Test-Path $backupEngine)) {
        throw "No rollback backup found for thagc.exe."
    }
    if (-not $yes) {
        $ans = Read-Host "Rollback current binary? [Y/n]"
        if ($ans -and $ans.ToLower() -notin @("y","yes")) {
            Write-Host "Aborted."
            exit 1
        }
    }
    $backupEngineHash = Get-FileSha256 -Path $backupEngine
    Copy-And-VerifyHash -Source $backupEngine -Target $engine -ExpectedHash $backupEngineHash -Label "rollback thagc.exe"
    if ($engine -ne $thagoreCompat) {
        Copy-And-VerifyHash -Source $backupEngine -Target $thagoreCompat -ExpectedHash $backupEngineHash -Label "rollback thagore.exe"
    }
    if (Test-Path $backupRuntime) {
        $backupRuntimeHash = Get-FileSha256 -Path $backupRuntime
        Copy-And-VerifyHash -Source $backupRuntime -Target $runtimeLib -ExpectedHash $backupRuntimeHash -Label "rollback runtime.lib"
    }
    Assert-CliOutputHealthy -ExePath $engine
    Assert-AtomicBuildHealthy -CompilerExe $engine
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

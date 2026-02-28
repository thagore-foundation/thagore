param(
  [string]$Tag = "",
  [string]$Channel = "stable",
  [string]$InstallRoot = "$HOME\\.thagore",
  [ValidateSet("x86_64", "aarch64")]
  [string]$Arch = "",
  [switch]$DryRun,
  [switch]$Force
)

$ErrorActionPreference = "Stop"

$RepoOwner = "thagore-foundation"
$RepoName = "thagore"

function Log([string]$Message) {
  Write-Host "[thagup.ps1] $Message"
}

function Fail([string]$Message) {
  throw "[thagup.ps1] error: $Message"
}

function Resolve-Arch([string]$ExplicitArch) {
  if ($ExplicitArch -ne "") {
    return $ExplicitArch
  }
  $raw = [Environment]::GetEnvironmentVariable("PROCESSOR_ARCHITECTURE")
  switch ($raw.ToUpperInvariant()) {
    "AMD64" { return "x86_64" }
    "ARM64" { return "aarch64" }
    default { Fail "unsupported CPU architecture: $raw" }
  }
}

function Resolve-LatestTag {
  $uri = "https://api.github.com/repos/$RepoOwner/$RepoName/releases/latest"
  $json = Invoke-RestMethod -Uri $uri
  if (-not $json.tag_name) {
    Fail "unable to resolve latest release tag"
  }
  return [string]$json.tag_name
}

function Download-FirstAvailable {
  param(
    [string[]]$Urls,
    [string]$OutFile
  )

  foreach ($url in $Urls) {
    try {
      if ($DryRun) {
        Log "[dry-run] would download: $url"
        return $url
      }
      Invoke-WebRequest -Uri $url -OutFile $OutFile -UseBasicParsing
      return $url
    } catch {
      continue
    }
  }
  Fail "no downloadable URL found"
}

function Ensure-Dir([string]$Path) {
  if ($DryRun) {
    Log "[dry-run] mkdir -p $Path"
    return
  }
  New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

function Remove-Dir([string]$Path) {
  if ($DryRun) {
    Log "[dry-run] remove dir $Path"
    return
  }
  if (Test-Path $Path) {
    Remove-Item -Recurse -Force $Path
  }
}

function Verify-Checksum([string]$ArchivePath, [string]$ChecksumPath, [string]$AssetName) {
  if ($DryRun) {
    Log "[dry-run] skip checksum verification"
    return
  }
  $line = Get-Content $ChecksumPath | Where-Object { $_ -match "\s+$([regex]::Escape($AssetName))$" } | Select-Object -First 1
  if (-not $line) {
    Fail "checksum entry not found for $AssetName"
  }
  $expected = ($line -split "\s+")[0].ToLowerInvariant()
  $actual = (Get-FileHash -Algorithm SHA256 -Path $ArchivePath).Hash.ToLowerInvariant()
  if ($expected -ne $actual) {
    Fail "checksum mismatch for $AssetName (expected=$expected actual=$actual)"
  }
}

function Ensure-UserPath([string]$BinDir) {
  $normalizedBin = [System.IO.Path]::GetFullPath($BinDir).TrimEnd("\\")
  $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
  $parts = @()
  if ($userPath) {
    $parts = $userPath -split ";" | Where-Object { $_ -ne "" }
  }

  $hasPath = $false
  foreach ($part in $parts) {
    if ($part.TrimEnd("\\").ToLowerInvariant() -eq $normalizedBin.ToLowerInvariant()) {
      $hasPath = $true
      break
    }
  }

  if (-not $hasPath) {
    if ($DryRun) {
      Log "[dry-run] add user PATH entry: $normalizedBin"
    } else {
      $newUserPath = if ([string]::IsNullOrEmpty($userPath)) { $normalizedBin } else { "$userPath;$normalizedBin" }
      [Environment]::SetEnvironmentVariable("Path", $newUserPath, "User")
      Log "added PATH entry to current user environment"
    }
  } else {
    Log "PATH already contains $normalizedBin"
  }

  if ($DryRun) {
    return
  }
  if (-not ($env:Path -split ";" | Where-Object { $_.TrimEnd("\\").ToLowerInvariant() -eq $normalizedBin.ToLowerInvariant() })) {
    $env:Path = "$normalizedBin;$env:Path"
  }
}

$ResolvedArch = Resolve-Arch -ExplicitArch $Arch
if ($Tag -eq "") {
  $Tag = Resolve-LatestTag
}

$baseUrl = "https://github.com/$RepoOwner/$RepoName/releases/download/$Tag"
$assetCandidates = @(
  "$baseUrl/thagc-core-windows-$ResolvedArch.tar.gz",
  "$baseUrl/thagc-core-windows.tar.gz"
)
$checksumCandidates = @(
  "$baseUrl/SHA256SUMS-thagc.txt",
  "$baseUrl/SHA256SUMS-thagc-windows.txt"
)

$workDir = Join-Path $env:TEMP ("thagup-" + [Guid]::NewGuid().ToString("N"))
$archivePath = Join-Path $workDir "thagc-core-windows.tar.gz"
$checksumPath = Join-Path $workDir "SHA256SUMS-thagc.txt"

$channelDir = Join-Path (Join-Path $InstallRoot "toolchains") $Channel
$targetBinPath = Join-Path $channelDir "bin\\thagc.exe"
$linkDir = Join-Path $InstallRoot "bin"
$linkPath = Join-Path $linkDir "thagc.exe"

Log "release tag: $Tag"
Log "arch: $ResolvedArch"
Log "channel: $Channel"
Log "install root: $InstallRoot"

Ensure-Dir $workDir
$archiveUrl = Download-FirstAvailable -Urls $assetCandidates -OutFile $archivePath
$checksumUrl = Download-FirstAvailable -Urls $checksumCandidates -OutFile $checksumPath

$assetName = [System.IO.Path]::GetFileName($archiveUrl)
Verify-Checksum -ArchivePath $archivePath -ChecksumPath $checksumPath -AssetName $assetName

if ((Test-Path $channelDir) -and (-not $Force)) {
  Fail "target channel exists: $channelDir (use -Force to overwrite)"
}

if (Test-Path $channelDir) {
  Remove-Dir $channelDir
}

Ensure-Dir $channelDir
if ($DryRun) {
  Log "[dry-run] tar -xzf $archivePath -C $channelDir"
} else {
  tar -xzf $archivePath -C $channelDir
}
Ensure-Dir $linkDir
if ($DryRun) {
  Log "[dry-run] copy $targetBinPath -> $linkPath"
} else {
  Copy-Item -Path $targetBinPath -Destination $linkPath -Force
}
Ensure-UserPath $linkDir

if ($DryRun) {
  Log "[dry-run] keep temp dir: $workDir"
} else {
  Remove-Dir $workDir
}

Log "install completed"
Log "binary: $targetBinPath"
Log "launcher: $linkPath"
Log "open a new PowerShell window to use thagc from PATH"

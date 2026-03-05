param(
  [string]$Tag = "",
  [string]$DragoTag = "",
  [string]$Channel = "stable",
  [string]$InstallRoot = "$HOME\\.thagore",
  [ValidateSet("x86_64", "aarch64")]
  [string]$Arch = "",
  [switch]$DryRun,
  [switch]$Force,
  [switch]$WithoutDrago
)

$ErrorActionPreference = "Stop"

$RepoOwner = "thagore-foundation"
$RepoName = "thagore"
$DragoRepoOwner = "thagore-foundation"
$DragoRepoName = "drago"

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

function Resolve-LatestTagForRepo([string]$Owner, [string]$Name) {
  $uri = "https://api.github.com/repos/$Owner/$Name/releases/latest"
  $json = Invoke-RestMethod -Uri $uri
  if (-not $json.tag_name) {
    Fail "unable to resolve latest release tag for $Owner/$Name"
  }
  return [string]$json.tag_name
}

function Resolve-LatestTag {
  return Resolve-LatestTagForRepo -Owner $RepoOwner -Name $RepoName
}

function Download-FirstAvailable {
  param(
    [string[]]$Urls,
    [string]$OutFile,
    [switch]$AllowMissing
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

  if ($AllowMissing) {
    return ""
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

function Extract-Archive([string]$ArchivePath, [string]$DestDir) {
  if ($ArchivePath.ToLowerInvariant().EndsWith(".zip")) {
    if ($DryRun) {
      Log "[dry-run] Expand-Archive $ArchivePath -> $DestDir"
      return
    }
    Expand-Archive -Path $ArchivePath -DestinationPath $DestDir -Force
    return
  }

  if ($ArchivePath.ToLowerInvariant().EndsWith(".tar.gz") -or $ArchivePath.ToLowerInvariant().EndsWith(".tgz")) {
    if ($DryRun) {
      Log "[dry-run] tar -xzf $ArchivePath -C $DestDir"
      return
    }
    tar -xzf $ArchivePath -C $DestDir
    return
  }

  Fail "unsupported archive format: $ArchivePath"
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

function Find-DragoBinary([string]$Root) {
  $candidates = Get-ChildItem -Path $Root -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object {
      $_.Name -eq "drago.exe" -or $_.Name -eq "drago.bin" -or $_.Name -eq "drago"
    } |
    Select-Object -First 1
  if ($null -eq $candidates) {
    return ""
  }
  return $candidates.FullName
}

function Install-Drago([string]$ThagcBinPath, [string]$ChannelDir, [string]$LinkDir, [string]$ResolvedArch, [string]$WorkDir) {
  if ($WithoutDrago) {
    Log "skip drago installation (-WithoutDrago)"
    return
  }

  if ([string]::IsNullOrWhiteSpace($DragoTag)) {
    $script:DragoTag = Resolve-LatestTagForRepo -Owner $DragoRepoOwner -Name $DragoRepoName
  }

  Log "drago tag: $DragoTag"
  $dragoTargetPath = Join-Path $ChannelDir "bin\\drago.exe"
  $dragoLinkPath = Join-Path $LinkDir "drago.exe"
  Ensure-Dir (Split-Path -Parent $dragoTargetPath)

  $archLabels = @()
  switch ($ResolvedArch) {
    "x86_64" { $archLabels = @("x86_64", "X64", "amd64") }
    "aarch64" { $archLabels = @("aarch64", "ARM64", "arm64") }
    default { $archLabels = @($ResolvedArch) }
  }

  $dragoBaseUrl = "https://github.com/$DragoRepoOwner/$DragoRepoName/releases/download/$DragoTag"
  $dragoAssetUrls = @()
  foreach ($label in $archLabels) {
    $dragoAssetUrls += "$dragoBaseUrl/drago-$DragoTag-windows-$label.zip"
    $dragoAssetUrls += "$dragoBaseUrl/drago-$DragoTag-windows-$label.tar.gz"
  }
  $dragoAssetUrls += "$dragoBaseUrl/drago-$DragoTag-windows.zip"
  $dragoAssetUrls += "$dragoBaseUrl/drago-$DragoTag-windows.tar.gz"

  $dragoWork = Join-Path $WorkDir "drago"
  Ensure-Dir $dragoWork
  $dragoDownloadTmp = Join-Path $dragoWork "release.download"
  $dragoReleaseUrl = Download-FirstAvailable -Urls $dragoAssetUrls -OutFile $dragoDownloadTmp -AllowMissing

  if ($DryRun) {
    if ($dragoReleaseUrl -ne "") {
      Log "[dry-run] install drago from release asset: $dragoReleaseUrl"
    } else {
      Log "[dry-run] drago release asset not found; fallback build from source"
    }
    Log "[dry-run] copy $dragoTargetPath -> $dragoLinkPath"
    return
  }

  if ($dragoReleaseUrl -ne "") {
    $dragoReleaseAsset = [System.IO.Path]::GetFileName($dragoReleaseUrl)
    $dragoReleaseArchive = Join-Path $dragoWork $dragoReleaseAsset
    Move-Item -Force $dragoDownloadTmp $dragoReleaseArchive
    $dragoReleaseExtract = Join-Path $dragoWork "release"
    Ensure-Dir $dragoReleaseExtract
    Extract-Archive -ArchivePath $dragoReleaseArchive -DestDir $dragoReleaseExtract

    $releaseBin = Find-DragoBinary -Root $dragoReleaseExtract
    if ($releaseBin -ne "") {
      Copy-Item -Path $releaseBin -Destination $dragoTargetPath -Force
      Copy-Item -Path $dragoTargetPath -Destination $dragoLinkPath -Force
      Log "installed drago from release asset: $dragoReleaseAsset"
      return
    }

    Log "drago release asset did not contain executable; fallback to source build"
  } else {
    Log "drago release asset unavailable for windows/$ResolvedArch; fallback to source build"
  }

  $dragoSourceBase = "https://github.com/$DragoRepoOwner/$DragoRepoName/archive/refs"
  $dragoSourceUrls = @(
    "$dragoSourceBase/tags/$DragoTag.tar.gz",
    "$dragoSourceBase/heads/main.tar.gz"
  )
  $dragoSourceTmp = Join-Path $dragoWork "source.download"
  $dragoSourceUrl = Download-FirstAvailable -Urls $dragoSourceUrls -OutFile $dragoSourceTmp
  $dragoSourceArchive = Join-Path $dragoWork ([System.IO.Path]::GetFileName($dragoSourceUrl))
  Move-Item -Force $dragoSourceTmp $dragoSourceArchive
  $dragoSourceExtract = Join-Path $dragoWork "source"
  Ensure-Dir $dragoSourceExtract
  Extract-Archive -ArchivePath $dragoSourceArchive -DestDir $dragoSourceExtract

  $mainFile = Get-ChildItem -Path $dragoSourceExtract -Recurse -File -Filter "main.tg" |
    Where-Object { $_.FullName -like "*\src\main.tg" } |
    Select-Object -First 1
  if ($null -eq $mainFile) {
    Fail "unable to locate drago src/main.tg from source archive"
  }

  $dragoBuildOut = Join-Path $dragoWork "drago-build.exe"
  & $ThagcBinPath build $mainFile.FullName -o $dragoBuildOut
  if ($LASTEXITCODE -ne 0) {
    Fail "failed to build drago from source archive"
  }

  Copy-Item -Path $dragoBuildOut -Destination $dragoTargetPath -Force
  Copy-Item -Path $dragoTargetPath -Destination $dragoLinkPath -Force
  Log "installed drago from source archive"
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
Ensure-Dir (Join-Path $channelDir "bin")
Extract-Archive -ArchivePath $archivePath -DestDir $channelDir
Ensure-Dir $linkDir
if ($DryRun) {
  Log "[dry-run] copy $targetBinPath -> $linkPath"
} else {
  Copy-Item -Path $targetBinPath -Destination $linkPath -Force
}

Install-Drago -ThagcBinPath $targetBinPath -ChannelDir $channelDir -LinkDir $linkDir -ResolvedArch $ResolvedArch -WorkDir $workDir
Ensure-UserPath $linkDir

if ($DryRun) {
  Log "[dry-run] keep temp dir: $workDir"
} else {
  Remove-Dir $workDir
}

Log "install completed"
Log "thagc binary: $targetBinPath"
Log "thagc launcher: $linkPath"
if (-not $WithoutDrago) {
  Log "drago launcher: $(Join-Path $linkDir 'drago.exe')"
}
Log "open a new PowerShell window to use thagc and drago from PATH"

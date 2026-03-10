param(
  [ValidateSet("indev", "extended", "nightly")]
  [string]$Channel = "indev",
  [string]$Target = "",
  [string]$Arch = "",
  [string]$Prefix = "",
  [string]$Tag = "",
  [string]$DragoTag = "",
  [switch]$Force,
  [switch]$WithDrago,
  [switch]$WithoutDrago,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$repo = "thagore-foundation/thagore"

function Log([string]$Message) {
  Write-Host "[thagup.ps1] $Message"
}

function Resolve-DefaultPrefix {
  $localAppData = [Environment]::GetFolderPath("LocalApplicationData")
  if ([string]::IsNullOrWhiteSpace($localAppData)) {
    $localAppData = Join-Path (Join-Path $HOME "AppData") "Local"
  }
  Join-Path (Join-Path $localAppData "Programs") "Thagore"
}

function Resolve-Target {
  param([string]$ArchOverride)
  $arch = if ($ArchOverride) { $ArchOverride.ToLowerInvariant() } else { $env:PROCESSOR_ARCHITECTURE.ToLowerInvariant() }
  switch ($arch) {
    "amd64" { return "x86_64-pc-windows-msvc" }
    "x86_64" { return "x86_64-pc-windows-msvc" }
    "arm64" { return "aarch64-pc-windows-msvc" }
    "x86" { return "i686-pc-windows-msvc" }
    "i686" { return "i686-pc-windows-msvc" }
    default { return "" }
  }
}

function Resolve-ReleaseTag {
  param([string]$Repo, [string]$ReleaseChannel)
  if ($ReleaseChannel -eq "nightly") {
    $releases = Invoke-RestMethod "https://api.github.com/repos/$Repo/releases"
    $nightly = $releases | Where-Object { $_.prerelease -and $_.tag_name -like "nightly-*" } | Select-Object -First 1
    return [string]$nightly.tag_name
  }
  $release = Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest"
  return [string]$release.tag_name
}

function Format-Bytes([Int64]$Bytes) {
  if ($Bytes -lt 1KB) { return "$Bytes B" }
  if ($Bytes -lt 1MB) { return "{0:N1} KiB" -f ($Bytes / 1KB) }
  if ($Bytes -lt 1GB) { return "{0:N1} MiB" -f ($Bytes / 1MB) }
  return "{0:N2} GiB" -f ($Bytes / 1GB)
}

function Read-Answer([string]$Prompt) {
  Write-Host -NoNewline $Prompt
  Read-Host
}

function Read-YesNo([string]$Prompt, [bool]$DefaultYes = $false) {
  $suffix = if ($DefaultYes) { " [Y/n]: " } else { " [y/N]: " }
  $answer = (Read-Answer ($Prompt + $suffix)).Trim().ToLowerInvariant()
  if ([string]::IsNullOrWhiteSpace($answer)) {
    return $DefaultYes
  }
  return $answer -in @("y", "yes")
}

function Resolve-RequestedPrefix([string]$CurrentPrefix, [bool]$Interactive) {
  if (-not $Interactive) {
    return $CurrentPrefix
  }
  $answer = Read-Answer "Installation path [$CurrentPrefix]: "
  if ([string]::IsNullOrWhiteSpace($answer)) {
    return $CurrentPrefix
  }
  return $answer.Trim()
}

function Get-ArchiveInstallBytes([string]$ArchivePath, [Int64]$FallbackBytes) {
  if (-not (Test-Path $ArchivePath)) {
    return $FallbackBytes
  }
  if ($ArchivePath.ToLowerInvariant().EndsWith(".zip")) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
      $total = [Int64]0
      foreach ($entry in $zip.Entries) {
        $total += [Int64]$entry.Length
      }
      if ($total -gt 0) {
        return $total
      }
    } finally {
      $zip.Dispose()
    }
  }
  return $FallbackBytes
}

function Get-FreeBytes([string]$Path) {
  $fullPath = [System.IO.Path]::GetFullPath($Path)
  $root = [System.IO.Path]::GetPathRoot($fullPath)
  if ([string]::IsNullOrWhiteSpace($root)) {
    return [Int64]0
  }
  $drive = [System.IO.DriveInfo]::new($root)
  return [Int64]$drive.AvailableFreeSpace
}

function Get-UserPathEntries {
  $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
  if ([string]::IsNullOrWhiteSpace($userPath)) {
    return @()
  }
  return @($userPath -split ";" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Set-UserPathEntries([string[]]$Entries) {
  $unique = [System.Collections.Generic.List[string]]::new()
  foreach ($entry in $Entries) {
    $trimmed = $entry.Trim()
    if ([string]::IsNullOrWhiteSpace($trimmed)) {
      continue
    }
    $normalized = $trimmed.TrimEnd('\\')
    $seen = $false
    foreach ($existing in $unique) {
      if ($existing.TrimEnd('\\').ToLowerInvariant() -eq $normalized.ToLowerInvariant()) {
        $seen = $true
        break
      }
    }
    if (-not $seen) {
      $unique.Add($trimmed)
    }
  }
  $joined = ($unique.ToArray()) -join ";"
  [Environment]::SetEnvironmentVariable("Path", $joined, "User")
  $env:Path = if ([string]::IsNullOrWhiteSpace($joined)) {
    [Environment]::GetEnvironmentVariable("Path", "Machine")
  } else {
    "$joined;" + [Environment]::GetEnvironmentVariable("Path", "Machine")
  }
}

function Remove-UserPathEntry([string]$Entry) {
  $normalized = [System.IO.Path]::GetFullPath($Entry).TrimEnd('\\').ToLowerInvariant()
  $remaining = @(Get-UserPathEntries | Where-Object {
    $_.TrimEnd('\\').ToLowerInvariant() -ne $normalized
  })
  Set-UserPathEntries $remaining
}

function Add-UserPathEntry([string]$Entry) {
  $entries = [System.Collections.Generic.List[string]]::new()
  foreach ($existing in Get-UserPathEntries) {
    $entries.Add($existing)
  }
  $entries.Add([System.IO.Path]::GetFullPath($Entry))
  Set-UserPathEntries $entries.ToArray()
}

function Get-ThagcCandidates {
  $results = [System.Collections.Generic.List[string]]::new()
  $commands = Get-Command thagc -All -ErrorAction SilentlyContinue
  foreach ($command in $commands) {
    if ($command.Source -and (Test-Path $command.Source)) {
      $results.Add([System.IO.Path]::GetFullPath($command.Source))
    }
  }
  foreach ($entry in Get-UserPathEntries) {
    $candidate = Join-Path $entry "thagc.exe"
    if (Test-Path $candidate) {
      $results.Add([System.IO.Path]::GetFullPath($candidate))
    }
  }
  return $results | Select-Object -Unique
}

function Test-ManagedInstallRoot([string]$Root) {
  if ([string]::IsNullOrWhiteSpace($Root)) {
    return $false
  }
  $bin = Join-Path $Root "bin\thagc.exe"
  $share = Join-Path $Root "share\thagore\install\thagup.ps1"
  return (Test-Path $bin) -and (Test-Path $share)
}

function Remove-ExistingInstallations([string]$NewPrefix) {
  $newPrefixFull = [System.IO.Path]::GetFullPath($NewPrefix).TrimEnd('\\')
  foreach ($thagcPath in Get-ThagcCandidates) {
    $binDir = Split-Path -Parent $thagcPath
    $installRoot = Split-Path -Parent $binDir
    $binFull = [System.IO.Path]::GetFullPath($binDir).TrimEnd('\\')
    $rootFull = [System.IO.Path]::GetFullPath($installRoot).TrimEnd('\\')
    if ($rootFull.ToLowerInvariant() -eq $newPrefixFull.ToLowerInvariant()) {
      continue
    }
    Remove-UserPathEntry $binFull
    if (Test-ManagedInstallRoot $rootFull) {
      Log "Removing previous Thagore install at $rootFull"
      if (-not $DryRun -and (Test-Path $rootFull)) {
        Remove-Item $rootFull -Recurse -Force
      }
    } else {
      Log "Removed PATH entry for legacy thagc at $binFull"
    }
  }
}

function Get-VswherePath {
  $programFilesX86 = ${env:ProgramFiles(x86)}
  if ([string]::IsNullOrWhiteSpace($programFilesX86)) {
    return ""
  }
  $candidate = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
  if (Test-Path $candidate) {
    return $candidate
  }
  return ""
}

function Find-MsvcBuildTools {
  $vswhere = Get-VswherePath
  if ([string]::IsNullOrWhiteSpace($vswhere)) {
    return ""
  }
  $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if ($LASTEXITCODE -ne 0) {
    return ""
  }
  return [string]$path
}

function Test-MsvcReady {
  if (Get-Command link.exe -ErrorAction SilentlyContinue) {
    return $true
  }
  return -not [string]::IsNullOrWhiteSpace((Find-MsvcBuildTools))
}

function Install-MsvcBuildTools {
  if (-not (Get-Command winget.exe -ErrorAction SilentlyContinue)) {
    Log "MSVC build tools are missing and winget is not available. Install Visual Studio Build Tools with the C++ workload manually."
    return
  }
  $approved = Read-YesNo "MSVC C++ build tools were not detected. Install them with winget now? This can download several GB." $false
  if (-not $approved) {
    Log "Continuing without MSVC build tools. thagc build/run may fail until they are installed."
    return
  }
  $override = '--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
  & winget.exe install --id Microsoft.VisualStudio.2022.BuildTools --exact --accept-package-agreements --accept-source-agreements --override $override
  if ($LASTEXITCODE -ne 0) {
    Log "winget install did not finish cleanly. Verify Visual Studio Build Tools manually if thagc build/run fails."
  }
}

if ([string]::IsNullOrWhiteSpace($Target)) {
  $Target = Resolve-Target -ArchOverride $Arch
}

if ([string]::IsNullOrWhiteSpace($Target)) {
  throw "Could not determine a supported Windows target triple. Pass -Target explicitly."
}

if ([string]::IsNullOrWhiteSpace($Tag)) {
  $Tag = Resolve-ReleaseTag -Repo $repo -ReleaseChannel $Channel
}

if ([string]::IsNullOrWhiteSpace($Tag)) {
  throw "Failed to resolve a release tag for release track '$Channel'."
}

$defaultPrefix = if ([string]::IsNullOrWhiteSpace($Prefix)) { Resolve-DefaultPrefix } else { $Prefix }
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("thagup-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tempRoot | Out-Null
try {
  $manifestPath = Join-Path $tempRoot "manifest.json"
  $manifestUrl = "https://github.com/$repo/releases/download/$Tag/release-manifest-$Tag.json"
  Invoke-WebRequest -Uri $manifestUrl -OutFile $manifestPath | Out-Null
  $manifest = Get-Content $manifestPath | ConvertFrom-Json

  $allowedTiers = switch ($Channel) {
    "indev" { @("indev") }
    "extended" { @("indev", "extended") }
    "nightly" { @("nightly") }
  }

  $artifact = $manifest.artifacts | Where-Object {
    $_.target -eq $Target -and $_.available -eq $true -and $allowedTiers -contains $_.tier
  } | Select-Object -First 1

  if (-not $artifact) {
    throw "No release artifact for target $Target on release track $Channel."
  }

  $archivePath = Join-Path $tempRoot $artifact.archive
  if (-not $DryRun) {
    Invoke-WebRequest -Uri $artifact.url -OutFile $archivePath | Out-Null
  }

  $estimatedInstalledBytes = Get-ArchiveInstallBytes -ArchivePath $archivePath -FallbackBytes ([Math]::Max([Int64]$artifact.size, 1MB) * 2)
  $interactive = [string]::IsNullOrWhiteSpace($Prefix) -and -not $DryRun
  $Prefix = Resolve-RequestedPrefix -CurrentPrefix $defaultPrefix -Interactive $interactive
  $Prefix = [System.IO.Path]::GetFullPath($Prefix)
  $freeBytes = Get-FreeBytes $Prefix

  Write-Host "Resolved release:"
  Write-Host "  repo:            $repo"
  Write-Host "  release track:   $Channel"
  if ($Channel -eq "indev") {
    Write-Host "  note:            in development; do not treat this toolchain as complete or frozen"
  }
  Write-Host "  tag:             $Tag"
  Write-Host "  target:          $Target"
  Write-Host "  prefix:          $Prefix"
  Write-Host "  archive:         $($artifact.archive)"
  Write-Host "  archive size:    $(Format-Bytes([Int64]$artifact.size))"
  Write-Host "  estimated usage: $(Format-Bytes($estimatedInstalledBytes))"
  Write-Host "  free on drive:   $(Format-Bytes($freeBytes))"
  Write-Host "  drago:           bundled"

  if (-not (Test-MsvcReady)) {
    Log "MSVC C++ build tools were not detected. thagc build/run on Windows needs them."
    if (-not $DryRun) {
      Install-MsvcBuildTools
    }
  } else {
    Log "MSVC C++ build tools detected."
  }

  if ($DryRun) {
    return
  }

  if ($freeBytes -gt 0 -and $estimatedInstalledBytes -gt $freeBytes) {
    throw "Not enough free disk space for the estimated install size."
  }

  if ($interactive -and -not (Read-YesNo "Continue installation?" $true)) {
    throw "Installation cancelled by user."
  }

  $sha = [System.Security.Cryptography.SHA256]::Create()
  $stream = [System.IO.File]::OpenRead($archivePath)
  try {
    $actual = ($sha.ComputeHash($stream) | ForEach-Object { $_.ToString("x2") }) -join ""
  } finally {
    $stream.Dispose()
    $sha.Dispose()
  }
  if ($actual -ne $artifact.sha256) {
    throw "Checksum verification failed."
  }

  Remove-ExistingInstallations $Prefix

  if ((Test-Path $Prefix) -and (-not $Force)) {
    Log "Replacing existing install at $Prefix"
  }
  if (Test-Path $Prefix) {
    Remove-Item $Prefix -Recurse -Force
  }
  New-Item -ItemType Directory -Path $Prefix -Force | Out-Null

  if ($archivePath.EndsWith(".zip")) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
      foreach ($entry in $zip.Entries) {
        $parts = $entry.FullName -split "[/\\]"
        if ($parts.Count -le 1) {
          continue
        }
        $relative = ($parts[1..($parts.Count - 1)] -join [System.IO.Path]::DirectorySeparatorChar)
        if ([string]::IsNullOrWhiteSpace($relative)) {
          continue
        }
        $destination = Join-Path $Prefix $relative
        if ($entry.FullName.EndsWith("/")) {
          New-Item -ItemType Directory -Path $destination -Force | Out-Null
          continue
        }
        New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $destination, $true)
      }
    } finally {
      $zip.Dispose()
    }
  } else {
    tar -xf $archivePath -C $tempRoot
    $rootDir = Get-ChildItem $tempRoot -Directory | Where-Object { $_.Name -like "thagore-*" } | Select-Object -First 1
    Copy-Item (Join-Path $rootDir.FullName "*") -Destination $Prefix -Recurse -Force
  }

  $binPath = Join-Path $Prefix 'bin'
  Add-UserPathEntry $binPath
  Write-Host "Updated user PATH with $binPath"

  Write-Host "Installed Thagore to $Prefix"
  Write-Host "Binary path: $binPath"
  Write-Host "Verify with: thagc version"

  $drago = $manifest.companion.drago
  if ($drago) {
    $effectiveDragoTag = if ($DragoTag) { $DragoTag } else { $drago.tag }
    $sourceUrl = [string]$drago.source_archive_url
    if ($drago.tag -and $effectiveDragoTag -and $drago.tag -ne $effectiveDragoTag) {
      $needle = "/refs/tags/$($drago.tag).tar.gz"
      $replacement = "/refs/tags/$effectiveDragoTag.tar.gz"
      $sourceUrl = $sourceUrl.Replace($needle, $replacement)
    }
    $dragoArchive = Join-Path $tempRoot "drago-source.tar.gz"
    Invoke-WebRequest -Uri $sourceUrl -OutFile $dragoArchive | Out-Null
    $dragoSourceRoot = Join-Path $tempRoot "drago-src"
    New-Item -ItemType Directory -Path $dragoSourceRoot -Force | Out-Null
    tar -xf $dragoArchive -C $dragoSourceRoot
    $sourceDir = Get-ChildItem $dragoSourceRoot -Directory | Select-Object -First 1
    $thagcBin = Join-Path $Prefix "bin\thagc.exe"
    $dragoBin = Join-Path $Prefix "bin\drago.exe"
    if (-not (Test-Path $thagcBin)) {
      throw "Installed thagc binary not found at $thagcBin."
    }
    & $thagcBin build (Join-Path $sourceDir.FullName "src\main.tg") -o $dragoBin
    if ($LASTEXITCODE -ne 0) {
      throw "Failed to bootstrap drago with the freshly installed thagc."
    }
    Write-Host "Installed drago from $($drago.repository)@$effectiveDragoTag"
  } else {
    Log "Release manifest did not publish drago companion metadata."
  }
} finally {
  if (Test-Path $tempRoot) {
    Remove-Item $tempRoot -Recurse -Force
  }
}

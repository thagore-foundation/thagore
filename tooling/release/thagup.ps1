param(
  [ValidateSet("stable", "extended", "nightly")]
  [string]$Channel = "stable",
  [string]$Target = "",
  [string]$Arch = "",
  [string]$Prefix = "",
  [string]$Tag = "",
  [string]$DragoTag = "",
  [switch]$WithoutDrago,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Prefix)) {
  if ($env:LOCALAPPDATA) {
    $Prefix = Join-Path $env:LOCALAPPDATA "Thagore"
  } else {
    $Prefix = Join-Path $HOME "AppData\\Local\\Thagore"
  }
}

$repo = "thagore-foundation/thagore"

function Resolve-Target {
  param([string]$ArchOverride)
  $system = "windows"
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

if ([string]::IsNullOrWhiteSpace($Target)) {
  $Target = Resolve-Target -ArchOverride $Arch
}

if ([string]::IsNullOrWhiteSpace($Target)) {
  throw "Could not determine a supported Windows target triple. Pass -Target explicitly."
}

if ([string]::IsNullOrWhiteSpace($Tag)) {
  if ($Channel -eq "nightly") {
    $releases = Invoke-RestMethod "https://api.github.com/repos/$repo/releases"
    $nightly = $releases | Where-Object { $_.prerelease -and $_.tag_name -like "nightly-*" } | Select-Object -First 1
    $Tag = $nightly.tag_name
  } else {
    $release = Invoke-RestMethod "https://api.github.com/repos/$repo/releases/latest"
    $Tag = $release.tag_name
  }
}

if ([string]::IsNullOrWhiteSpace($Tag)) {
  throw "Failed to resolve a release tag for channel '$Channel'."
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("thagup-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tempRoot | Out-Null
try {
  $manifestPath = Join-Path $tempRoot "manifest.json"
  $manifestUrl = "https://github.com/$repo/releases/download/$Tag/release-manifest-$Tag.json"
  Invoke-WebRequest -Uri $manifestUrl -OutFile $manifestPath | Out-Null
  $manifest = Get-Content $manifestPath | ConvertFrom-Json

  $allowedTiers = switch ($Channel) {
    "stable" { @("stable") }
    "extended" { @("stable", "extended") }
    "nightly" { @("nightly") }
  }

  $artifact = $manifest.artifacts | Where-Object {
    $_.target -eq $Target -and $_.available -eq $true -and $allowedTiers -contains $_.tier
  } | Select-Object -First 1

  if (-not $artifact) {
    throw "No release artifact for target $Target on channel $Channel."
  }

  Write-Host "Resolved release:"
  Write-Host "  repo:    $repo"
  Write-Host "  channel: $Channel"
  Write-Host "  tag:     $Tag"
  Write-Host "  target:  $Target"
  Write-Host "  prefix:  $Prefix"
  Write-Host "  archive: $($artifact.archive)"

  if ($DryRun) {
    return
  }

  $archivePath = Join-Path $tempRoot $artifact.archive
  Invoke-WebRequest -Uri $artifact.url -OutFile $archivePath | Out-Null

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

  New-Item -ItemType Directory -Path $Prefix -Force | Out-Null
  if ($archivePath.EndsWith(".zip")) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
      foreach ($entry in $zip.Entries) {
        $parts = $entry.FullName -split "[/\\\\]"
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

  Write-Host "Installed Thagore to $Prefix"
  Write-Host "Add $(Join-Path $Prefix 'bin') to PATH if needed."
  if (-not $WithoutDrago) {
    Write-Host "note: thagup currently installs the Thagore toolchain only."
    if ($DragoTag) {
      Write-Host "note: drago tag $DragoTag was requested but companion drago installation is not yet automated."
    }
  }
} finally {
  if (Test-Path $tempRoot) {
    Remove-Item $tempRoot -Recurse -Force
  }
}

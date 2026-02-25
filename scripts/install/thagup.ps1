<#
.SYNOPSIS
    thagup — Thagore toolchain manager for Windows
.DESCRIPTION
    Manages thagc installation: update, install specific versions,
    add/remove cross-compilation targets, self-update.
.EXAMPLE
    thagup update
    thagup install v0.5.47
    thagup target add x86_64-unknown-linux-gnu
    thagup target list
    thagup self-update
    thagup show
#>
param(
    [Parameter(Position=0)][string]$Command = "help",
    [Parameter(Position=1)][string]$Arg1 = "",
    [Parameter(Position=2)][string]$Arg2 = ""
)

$ErrorActionPreference = "Stop"

$REPO_OWNER  = "thagore-foundation"
$REPO_NAME   = "thagore"
$API_BASE    = "https://api.github.com/repos/$REPO_OWNER/$REPO_NAME"
$GH_BASE     = "https://github.com/$REPO_OWNER/$REPO_NAME"

$THAGORE_HOME  = if ($env:THAGORE_HOME) { $env:THAGORE_HOME } else { Join-Path $env:USERPROFILE ".thagore" }
$TOOLCHAIN_DIR = Join-Path $THAGORE_HOME "toolchains\stable"
$BIN_DIR       = Join-Path $THAGORE_HOME "bin"

$SUPPORTED_TARGETS = @(
    "x86_64-unknown-linux-gnu",
    "x86_64-pc-windows-msvc",
    "aarch64-apple-darwin",
    "aarch64-unknown-linux-gnu",
    "x86_64-apple-darwin"
)

function Write-Help {
    Write-Host @"
thagup — Thagore toolchain manager

USAGE:
    thagup <COMMAND> [OPTIONS]

COMMANDS:
    update                  Update thagc to the latest stable release
    install <tag>           Install a specific release tag (e.g. v0.5.47)
    target add <triple>     Install a cross-compilation target pack
    target remove <triple>  Remove an installed target pack
    target list             List installed targets
    self-update             Update thagup itself to the latest version
    show                    Show installed version and targets
    help                    Print this help

EXAMPLES:
    thagup update
    thagup install v0.5.47
    thagup target add aarch64-unknown-linux-gnu
    thagup target list
    thagup self-update

SUPPORTED TARGETS:
$($SUPPORTED_TARGETS | ForEach-Object { "    $_" } | Out-String)
"@
}

function Get-LatestTag {
    $resp = Invoke-RestMethod "$API_BASE/releases/latest" -Headers @{ "User-Agent" = "thagup/1.0" }
    return $resp.tag_name
}

function Get-CurrentVersion {
    $thagc = Join-Path $BIN_DIR "thagc.exe"
    if (Test-Path $thagc) {
        try { return (& $thagc --version 2>$null | Select-Object -First 1) }
        catch { return "(unknown)" }
    }
    return "(not installed)"
}

function Get-SHA256 {
    param([string]$FilePath)
    $hash = Get-FileHash $FilePath -Algorithm SHA256
    return $hash.Hash.ToLower()
}

function Confirm-Checksum {
    param([string]$ArchivePath, [string]$SumFile, [string]$AssetName)
    if (-not (Test-Path $SumFile)) {
        Write-Warning "Checksum file not found — skipping verification"
        return
    }
    $lines = Get-Content $SumFile
    $expected = $null
    foreach ($line in $lines) {
        $parts = $line -split '\s+', 2
        if ($parts.Count -eq 2) {
            $name = $parts[1].TrimStart('*')
            if ($name -eq $AssetName) { $expected = $parts[0].ToLower(); break }
        }
    }
    if (-not $expected) {
        Write-Warning "No checksum entry for $AssetName — skipping"
        return
    }
    $actual = Get-SHA256 $ArchivePath
    if ($actual -ne $expected) {
        throw "Checksum mismatch for $AssetName`n  expected: $expected`n  actual:   $actual"
    }
    Write-Host "[thagup] Checksum OK: $AssetName"
}

function Install-CoreBundle {
    param([string]$Tag)
    $coreAsset = "thagc-core-windows.tar.gz"
    $sumAsset  = "SHA256SUMS-thagc-windows.txt"
    $baseUrl   = "$GH_BASE/releases/download/$Tag"

    $tmp = New-TemporaryFile | Split-Path
    $tmpDir = Join-Path $tmp "thagup-install-$([guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null

    try {
        $archivePath = Join-Path $tmpDir $coreAsset
        $sumPath     = Join-Path $tmpDir $sumAsset

        Write-Host "[thagup] Downloading core bundle: $coreAsset"
        Invoke-WebRequest "$baseUrl/$coreAsset" -OutFile $archivePath -UseBasicParsing
        Write-Host "[thagup] Downloading checksums: $sumAsset"
        Invoke-WebRequest "$baseUrl/$sumAsset"  -OutFile $sumPath     -UseBasicParsing

        Confirm-Checksum $archivePath $sumPath $coreAsset

        New-Item -ItemType Directory -Force -Path "$TOOLCHAIN_DIR\bin" | Out-Null
        tar -xzf $archivePath -C $TOOLCHAIN_DIR
        Write-Host "[thagup] Core bundle installed to $TOOLCHAIN_DIR"
    } finally {
        Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue
    }
}

function Install-TargetPack {
    param([string]$Tag, [string]$Triple)
    $targetAsset = "thagc-target-$Triple-windows.tar.gz"
    $sumAsset    = "SHA256SUMS-thagc-windows.txt"
    $baseUrl     = "$GH_BASE/releases/download/$Tag"

    $tmp = New-TemporaryFile | Split-Path
    $tmpDir = Join-Path $tmp "thagup-target-$([guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null

    try {
        $archivePath = Join-Path $tmpDir $targetAsset
        $sumPath     = Join-Path $tmpDir $sumAsset

        Write-Host "[thagup] Downloading target pack: $targetAsset"
        Invoke-WebRequest "$baseUrl/$targetAsset" -OutFile $archivePath -UseBasicParsing
        Invoke-WebRequest "$baseUrl/$sumAsset"    -OutFile $sumPath     -UseBasicParsing

        Confirm-Checksum $archivePath $sumPath $targetAsset

        $packDir = Join-Path $TOOLCHAIN_DIR "targets\$Triple"
        New-Item -ItemType Directory -Force -Path $packDir | Out-Null
        tar -xzf $archivePath -C $packDir
        Write-Host "[thagup] Target installed: $Triple -> $packDir"
    } finally {
        Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue
    }
}

function Update-BinDir {
    New-Item -ItemType Directory -Force -Path $BIN_DIR | Out-Null

    # thagc.exe
    $thagcSrc = Join-Path $TOOLCHAIN_DIR "bin\thagc.exe"
    if (Test-Path $thagcSrc) {
        Copy-Item $thagcSrc (Join-Path $BIN_DIR "thagc.exe") -Force
        Write-Host "[thagup] thagc installed to $BIN_DIR\thagc.exe"
    }

    # thagup.ps1 — self-updater
    $thagupSrc = Join-Path $TOOLCHAIN_DIR "thagup.ps1"
    if (Test-Path $thagupSrc) {
        Copy-Item $thagupSrc (Join-Path $BIN_DIR "thagup.ps1") -Force
        # Create a .cmd shim so `thagup` works from cmd.exe and PowerShell without extension
        $shimPath = Join-Path $BIN_DIR "thagup.cmd"
        Set-Content $shimPath "@powershell -NoProfile -ExecutionPolicy Bypass -File `"%~dp0thagup.ps1`" %*"
        Write-Host "[thagup] thagup installed to $BIN_DIR\thagup.ps1 + thagup.cmd"
    }

    # env.ps1 — add to PATH
    $envScript = Join-Path $THAGORE_HOME "env.ps1"
    Set-Content $envScript @"
# Thagore environment — dot-source this or add to your `$PROFILE
`$env:PATH = "$BIN_DIR;" + `$env:PATH
"@
}

function Update-UserPath {
    $userPath = [Environment]::GetEnvironmentVariable("PATH", "User")
    if ($userPath -notlike "*$BIN_DIR*") {
        [Environment]::SetEnvironmentVariable("PATH", "$BIN_DIR;$userPath", "User")
        Write-Host "[thagup] Added $BIN_DIR to user PATH (restart shell to activate)"
    }
    # Also update PowerShell profile
    $profileDir = Split-Path $PROFILE -Parent
    if (-not (Test-Path $profileDir)) { New-Item -ItemType Directory -Force -Path $profileDir | Out-Null }
    if (Test-Path $PROFILE) {
        $content = Get-Content $PROFILE -Raw -ErrorAction SilentlyContinue
        if ($content -notlike "*thagore*") {
            Add-Content $PROFILE "`n# Thagore`n`$env:PATH = `"$BIN_DIR;`$env:PATH`""
            Write-Host "[thagup] Added Thagore to PowerShell profile: $PROFILE"
        }
    } else {
        Set-Content $PROFILE "# Thagore`n`$env:PATH = `"$BIN_DIR;`$env:PATH`""
        Write-Host "[thagup] Created PowerShell profile with Thagore PATH: $PROFILE"
    }
}

# ── Commands ──────────────────────────────────────────────────────────────────

function Invoke-Update {
    $tag = Get-LatestTag
    if (-not $tag) { throw "Cannot resolve latest release tag" }
    Write-Host "[thagup] Latest release: $tag"
    Write-Host "[thagup] Currently installed: $(Get-CurrentVersion)"
    Install-CoreBundle $tag
    Update-BinDir
    Write-Host "[thagup] Update complete. Run: thagc --version"
}

function Invoke-Install {
    param([string]$Tag)
    if (-not $Tag) { throw "Specify a tag, e.g.: thagup install v0.5.47" }
    Install-CoreBundle $Tag
    Update-BinDir
    Write-Host "[thagup] Installed $Tag. Run: thagc --version"
}

function Invoke-TargetAdd {
    param([string]$Triple)
    if (-not $Triple) { throw "Specify a target triple, e.g.: thagup target add x86_64-unknown-linux-gnu" }
    if ($SUPPORTED_TARGETS -notcontains $Triple) {
        throw "Unsupported target triple: $Triple`nSupported: $($SUPPORTED_TARGETS -join ', ')"
    }
    $tag = Get-LatestTag
    if (-not $tag) { throw "Cannot resolve latest release tag" }
    Install-TargetPack $tag $Triple
}

function Invoke-TargetRemove {
    param([string]$Triple)
    if (-not $Triple) { throw "Specify a target triple" }
    $packDir = Join-Path $TOOLCHAIN_DIR "targets\$Triple"
    if (Test-Path $packDir) {
        Remove-Item -Recurse -Force $packDir
        Write-Host "[thagup] Removed target: $Triple"
    } else {
        Write-Host "[thagup] Target not installed: $Triple"
    }
}

function Invoke-TargetList {
    $targetsDir = Join-Path $TOOLCHAIN_DIR "targets"
    if (-not (Test-Path $targetsDir)) { Write-Host "(no targets installed)"; return }
    $items = Get-ChildItem $targetsDir -Directory
    if ($items.Count -eq 0) { Write-Host "(no targets installed)"; return }
    $items | ForEach-Object { Write-Host "  $($_.Name)" }
}

function Invoke-SelfUpdate {
    $tag = Get-LatestTag
    if (-not $tag) { throw "Cannot resolve latest release tag" }
    $url = "https://raw.githubusercontent.com/$REPO_OWNER/$REPO_NAME/refs/tags/$tag/scripts/install/thagup.ps1"
    Write-Host "[thagup] Downloading thagup.ps1 from $tag..."
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) "thagup-self-$([guid]::NewGuid().ToString('N')).ps1"
    Invoke-WebRequest $url -OutFile $tmp -UseBasicParsing
    $dest = Join-Path $BIN_DIR "thagup.ps1"
    Copy-Item $tmp $dest -Force
    Remove-Item $tmp -ErrorAction SilentlyContinue
    Write-Host "[thagup] Self-update complete: $dest"
}

function Invoke-Show {
    Write-Host "thagup — Thagore toolchain manager"
    Write-Host "  THAGORE_HOME: $THAGORE_HOME"
    Write-Host "  Toolchain:    $TOOLCHAIN_DIR"
    Write-Host "  thagc:        $(Get-CurrentVersion)"
    Write-Host "  Installed targets:"
    Invoke-TargetList | ForEach-Object { Write-Host "    $_" }
}

# ── Dispatch ──────────────────────────────────────────────────────────────────

switch ($Command.ToLower()) {
    "update"      { Invoke-Update }
    "install"     { Invoke-Install $Arg1 }
    "target" {
        switch ($Arg1.ToLower()) {
            "add"    { Invoke-TargetAdd $Arg2 }
            "remove" { Invoke-TargetRemove $Arg2 }
            "list"   { Invoke-TargetList }
            default  { Write-Error "Unknown target subcommand: $Arg1. Use: add | remove | list" }
        }
    }
    "self-update" { Invoke-SelfUpdate }
    "show"        { Invoke-Show }
    { $_ -in @("help", "--help", "-h", "") } { Write-Help }
    default { Write-Error "Unknown command: $Command`nRun: thagup help" }
}

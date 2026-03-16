param(
  [Parameter(Mandatory = $true)]
  [ValidateSet("amd64", "x64", "x86", "arm64")]
  [string]$Arch
)

$ErrorActionPreference = "Stop"

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
  throw "vswhere.exe was not found at $vswhere"
}

$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installationPath)) {
  throw "Could not resolve a Visual Studio installation with VC tools."
}

$devCmd = Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $devCmd)) {
  throw "VsDevCmd.bat was not found at $devCmd"
}

$targetArch = if ($Arch -eq "x64") { "amd64" } else { $Arch }
$hostArch = "amd64"
$dump = & cmd.exe /s /c """$devCmd"" -no_logo -host_arch=$hostArch -arch=$targetArch >nul && set"
if ($LASTEXITCODE -ne 0) {
  throw "VsDevCmd failed for host_arch=$hostArch arch=$targetArch"
}

$wanted = @(
  "INCLUDE",
  "LIB",
  "LIBPATH",
  "PATH",
  "UCRTVersion",
  "UniversalCRTSdkDir",
  "VCINSTALLDIR",
  "VCToolsInstallDir",
  "VCToolsVersion",
  "VSCMD_ARG_HOST_ARCH",
  "VSCMD_ARG_TGT_ARCH",
  "WindowsLibPath",
  "WindowsSdkBinPath",
  "WindowsSdkDir",
  "WindowsSDKLibVersion",
  "WindowsSDKVersion"
)

$selected = @{}
foreach ($line in $dump) {
  if ($line -notmatch "=") {
    continue
  }
  $parts = $line.Split("=", 2)
  $name = $parts[0]
  $value = $parts[1]
  if ($wanted -contains $name) {
    $selected[$name] = $value
  }
}

if (-not $selected.ContainsKey("VCToolsInstallDir")) {
  throw "VsDevCmd did not populate VCToolsInstallDir."
}

foreach ($pair in $selected.GetEnumerator()) {
  "$($pair.Key)=$($pair.Value)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
}

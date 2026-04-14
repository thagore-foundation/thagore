param(
  [string]$Repo = "thagore-foundation/thagore",
  [string]$Branch = "indev-rewrite",
  [string]$Sha = "",
  [string]$ReportOut = "bootstrap-publication-audit.txt"
)

$ErrorActionPreference = "Stop"

if (-not $Sha) {
  $Sha = (git rev-parse HEAD).Trim()
}

$token = (gh auth token).Trim()
if (-not $token) {
  throw "gh auth token returned an empty token"
}

python tooling/ci/bootstrap_publication_audit.py `
  --repo $Repo `
  --branch $Branch `
  --sha $Sha `
  --token $token `
  --report-out $ReportOut

Get-Content $ReportOut

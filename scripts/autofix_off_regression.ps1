param(
    [string]$Compiler = ".\stage1.exe",
    [string]$Entry = "examples/hello.tg",
    [int]$Runs = 3,
    [double]$MaxOverheadPct = 5.0,
    [string]$EmitFlag = "--emit-ir",
    [string]$JsonOut = ""
)

function Invoke-BuildTimed {
    param(
        [string]$CompilerExe,
        [string]$EntryFile,
        [string]$OutFile,
        [int]$UseAutofixOff
    )
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    if ($UseAutofixOff -eq 1) {
        & $CompilerExe build $EntryFile $EmitFlag -o $OutFile --autofix=off | Out-Null
    } else {
        & $CompilerExe build $EntryFile $EmitFlag -o $OutFile | Out-Null
    }
    $code = $LASTEXITCODE
    $sw.Stop()
    return @{ Code = $code; Seconds = $sw.Elapsed.TotalSeconds }
}

function Get-Median {
    param([double[]]$Values)
    if ($Values.Count -eq 0) {
        return 0.0
    }
    $sorted = $Values | Sort-Object
    $n = $sorted.Count
    if (($n % 2) -eq 1) {
        return [double]$sorted[[int]($n / 2)]
    }
    $a = [double]$sorted[([int]($n / 2) - 1)]
    $b = [double]$sorted[[int]($n / 2)]
    return ($a + $b) / 2.0
}

if ($Runs -lt 1) {
    $Runs = 1
}

$warmA = Invoke-BuildTimed -CompilerExe $Compiler -EntryFile $Entry -OutFile ".tmp_autofix_off_baseline.ll" -UseAutofixOff 0
if ($warmA.Code -ne 0) {
    Write-Error "Warmup baseline failed with exit code $($warmA.Code)."
    exit 2
}
$warmB = Invoke-BuildTimed -CompilerExe $Compiler -EntryFile $Entry -OutFile ".tmp_autofix_off_flag.ll" -UseAutofixOff 1
if ($warmB.Code -ne 0) {
    Write-Error "Warmup autofix=off failed with exit code $($warmB.Code)."
    exit 2
}

$baseline = @()
$off = @()
for ($i = 0; $i -lt $Runs; $i = $i + 1) {
    $r1 = Invoke-BuildTimed -CompilerExe $Compiler -EntryFile $Entry -OutFile ".tmp_autofix_off_baseline.ll" -UseAutofixOff 0
    if ($r1.Code -ne 0) {
        Write-Error "Baseline run failed with exit code $($r1.Code)."
        exit 2
    }
    $baseline += [double]$r1.Seconds

    $r2 = Invoke-BuildTimed -CompilerExe $Compiler -EntryFile $Entry -OutFile ".tmp_autofix_off_flag.ll" -UseAutofixOff 1
    if ($r2.Code -ne 0) {
        Write-Error "autofix=off run failed with exit code $($r2.Code)."
        exit 2
    }
    $off += [double]$r2.Seconds
}

$medianBaseline = Get-Median -Values $baseline
$medianOff = Get-Median -Values $off
$overheadPct = 0.0
if ($medianBaseline -gt 0.0) {
    $overheadPct = (($medianOff - $medianBaseline) / $medianBaseline) * 100.0
}
$pass = ($overheadPct -le $MaxOverheadPct)

$obj = [ordered]@{
    compiler = $Compiler
    entry = $Entry
    runs = $Runs
    median_baseline_s = $medianBaseline
    median_autofix_off_s = $medianOff
    overhead_pct = $overheadPct
    max_overhead_pct = $MaxOverheadPct
    pass = $pass
}
$json = $obj | ConvertTo-Json -Depth 4
Write-Output $json

if ($JsonOut -ne "") {
    $json | Out-File -FilePath $JsonOut -Encoding utf8
}

if (-not $pass) {
    exit 1
}
exit 0

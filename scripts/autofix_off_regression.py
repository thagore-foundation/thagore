#!/usr/bin/env python3
import argparse
import json
import statistics
import subprocess
import time
from pathlib import Path


def run_cmd(cmd, cwd):
    t0 = time.perf_counter()
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    dt = time.perf_counter() - t0
    return proc.returncode, dt, proc.stdout, proc.stderr


def median_runtime(samples):
    if not samples:
        return 0.0
    return statistics.median(samples)


def main():
    ap = argparse.ArgumentParser(
        description="Regression gate: --autofix=off should not add meaningful build overhead."
    )
    ap.add_argument("--compiler", default="stage1.exe", help="Compiler executable path.")
    ap.add_argument("--entry", default="examples/hello.tg", help="Entry source file.")
    ap.add_argument("--emit-flag", default="--emit-ir", help="IR emit flag accepted by the compiler.")
    ap.add_argument("--runs", type=int, default=3, help="Number of timed runs per mode.")
    ap.add_argument(
        "--max-overhead-pct",
        type=float,
        default=5.0,
        help="Fail if median(off) overhead vs baseline exceeds this percent.",
    )
    ap.add_argument("--workdir", default=".", help="Working directory.")
    ap.add_argument("--json-out", default="", help="Optional JSON report output path.")
    args = ap.parse_args()

    cwd = Path(args.workdir).resolve()
    compiler = str(Path(args.compiler))
    entry = str(Path(args.entry))
    out_base = ".tmp_autofix_off_baseline.ll"
    out_off = ".tmp_autofix_off_flag.ll"

    baseline_cmd = [compiler, "build", entry, args.emit_flag, "-o", out_base]
    off_cmd = [compiler, "build", entry, args.emit_flag, "-o", out_off, "--autofix=off"]

    # Warmup both paths once.
    for cmd in (baseline_cmd, off_cmd):
        rc, _, so, se = run_cmd(cmd, cwd)
        if rc != 0:
            print("Warmup failed:", " ".join(cmd))
            print(so)
            print(se)
            return 2

    baseline_times = []
    off_times = []
    for _ in range(max(1, args.runs)):
        rc, dt, so, se = run_cmd(baseline_cmd, cwd)
        if rc != 0:
            print("Baseline run failed.")
            print(so)
            print(se)
            return 2
        baseline_times.append(dt)

        rc, dt, so, se = run_cmd(off_cmd, cwd)
        if rc != 0:
            print("Off-mode run failed.")
            print(so)
            print(se)
            return 2
        off_times.append(dt)

    med_base = median_runtime(baseline_times)
    med_off = median_runtime(off_times)
    overhead_pct = 0.0
    if med_base > 0.0:
        overhead_pct = ((med_off - med_base) / med_base) * 100.0

    result = {
        "compiler": compiler,
        "entry": entry,
        "runs": args.runs,
        "median_baseline_s": med_base,
        "median_autofix_off_s": med_off,
        "overhead_pct": overhead_pct,
        "max_overhead_pct": args.max_overhead_pct,
        "pass": overhead_pct <= args.max_overhead_pct,
    }

    print(json.dumps(result, indent=2))
    if args.json_out:
        out_path = cwd / args.json_out
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")

    if not result["pass"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

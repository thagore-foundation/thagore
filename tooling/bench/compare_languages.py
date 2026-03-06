#!/usr/bin/env python3
import argparse
import json
import math
import shutil
import statistics
import subprocess
import tempfile
import time
from pathlib import Path


def percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    sorted_values = sorted(values)
    idx = max(0, min(len(sorted_values) - 1, math.ceil(q * len(sorted_values)) - 1))
    return sorted_values[idx]


def summarize(values: list[float]) -> dict[str, float]:
    return {
        "min_ms": min(values),
        "p50_ms": statistics.median(values),
        "p95_ms": percentile(values, 0.95),
        "max_ms": max(values),
    }


def run_checked(cmd: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        raise RuntimeError(f"command failed ({proc.returncode}): {' '.join(cmd)}\n{proc.stderr}")
    return proc


def measure_runtime(cmd: list[str], runs: int, cwd: Path | None = None) -> tuple[dict[str, float], str]:
    samples_ms: list[float] = []
    first_output = ""
    for idx in range(runs):
        start_ns = time.perf_counter_ns()
        proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, check=False)
        end_ns = time.perf_counter_ns()
        if proc.returncode != 0:
            raise RuntimeError(f"runtime failed ({proc.returncode}): {' '.join(cmd)}\n{proc.stderr}")
        out = proc.stdout.strip()
        if idx == 0:
            first_output = out
        elif out != first_output:
            raise RuntimeError(f"non-deterministic output for {' '.join(cmd)}: '{first_output}' vs '{out}'")
        samples_ms.append((end_ns - start_ns) / 1_000_000.0)
    return summarize(samples_ms), first_output


def resolve_python_cmd() -> list[str]:
    if shutil.which("py"):
        return ["py", "-3"]
    if shutil.which("python3"):
        return ["python3"]
    if shutil.which("python"):
        return ["python"]
    raise RuntimeError("python interpreter not found")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--thagc", required=True, help="Path to thagc binary")
    parser.add_argument("--runs", type=int, default=20, help="Runtime runs per language")
    parser.add_argument("--opt-level", type=int, default=3, help="thagc optimization level (0..3)")
    parser.add_argument("--json-out", required=True, help="Output JSON report path")
    args = parser.parse_args()

    if args.runs <= 0:
        raise SystemExit("runs must be > 0")
    if args.opt_level < 0 or args.opt_level > 3:
        raise SystemExit("opt-level must be in range 0..3")

    thagc = Path(args.thagc)
    if not thagc.exists():
        raise SystemExit(f"thagc not found: {thagc}")

    here = Path(__file__).resolve().parent
    fixtures = here / "fixtures"
    tg_src = fixtures / "bench_sum.tg"
    py_src = fixtures / "bench_sum.py"
    go_src = fixtures / "bench_sum.go"
    rs_src = fixtures / "bench_sum.rs"
    for path in [tg_src, py_src, go_src, rs_src]:
        if not path.exists():
            raise SystemExit(f"fixture missing: {path}")

    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        thag_bin = root / "bench_thagore.bin"
        go_bin = root / "bench_go.exe"
        rs_bin = root / "bench_rust.exe"

        run_checked([str(thagc), "build", str(tg_src), "-o", str(thag_bin), f"--opt-level={args.opt_level}"])
        run_checked(["go", "build", "-o", str(go_bin), str(go_src)])
        run_checked(["rustc", "-O", "-o", str(rs_bin), str(rs_src)])

        py_cmd = resolve_python_cmd() + [str(py_src)]
        thag_stats, thag_out = measure_runtime([str(thag_bin)], args.runs)
        go_stats, go_out = measure_runtime([str(go_bin)], args.runs)
        rs_stats, rs_out = measure_runtime([str(rs_bin)], args.runs)
        py_stats, py_out = measure_runtime(py_cmd, args.runs)

    outputs = {"thagore": thag_out, "go": go_out, "rust": rs_out, "python": py_out}
    if len(set(outputs.values())) != 1:
        raise SystemExit(f"workload output mismatch: {outputs}")

    payload = {
        "runs": args.runs,
        "workload": "sum_squares_n_200000",
        "output": thag_out,
        "results": {
            "thagore": thag_stats,
            "go": go_stats,
            "rust": rs_stats,
            "python": py_stats,
        },
    }
    out_path = Path(args.json_out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(payload, indent=2))


if __name__ == "__main__":
    main()

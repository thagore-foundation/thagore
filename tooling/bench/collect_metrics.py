#!/usr/bin/env python3
import argparse
import json
import math
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


def measure_startup(bin_path: Path, iterations: int) -> dict[str, float]:
    samples_ms: list[float] = []
    for _ in range(iterations):
        start_ns = time.perf_counter_ns()
        proc = subprocess.run([str(bin_path), "--version"], capture_output=True, text=True, check=False)
        end_ns = time.perf_counter_ns()
        if proc.returncode != 0:
            raise RuntimeError(f"startup probe failed: {proc.returncode}\n{proc.stderr}")
        samples_ms.append((end_ns - start_ns) / 1_000_000.0)
    return summarize(samples_ms)


def measure_compile_latency(bin_path: Path, source_path: Path, iterations: int, opt_level: int) -> dict[str, float]:
    samples_ms: list[float] = []
    with tempfile.TemporaryDirectory() as td:
        tmp_root = Path(td)
        for idx in range(iterations):
            out_bin = tmp_root / f"collect_{idx}.bin"
            cmd = [
                str(bin_path),
                "build",
                str(source_path),
                "-o",
                str(out_bin),
                "--emit-llvm",
                f"--opt-level={opt_level}",
            ]
            start_ns = time.perf_counter_ns()
            proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
            end_ns = time.perf_counter_ns()
            if proc.returncode != 0:
                raise RuntimeError(f"compile probe failed: {proc.returncode}\n{proc.stderr}")
            samples_ms.append((end_ns - start_ns) / 1_000_000.0)
    return summarize(samples_ms)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", required=True, help="Path to thagc binary")
    parser.add_argument("--source", required=True, help="Path to benchmark .tg source")
    parser.add_argument("--platform-key", required=True, help="Platform key, e.g. linux-x86_64")
    parser.add_argument("--commit-sha", default="", help="Commit SHA for traceability")
    parser.add_argument("--startup-iterations", type=int, default=25)
    parser.add_argument("--compile-iterations", type=int, default=10)
    parser.add_argument("--opt-level", type=int, default=3)
    parser.add_argument("--json-out", required=True, help="Output JSON path")
    args = parser.parse_args()

    bin_path = Path(args.bin)
    source_path = Path(args.source)
    out_path = Path(args.json_out)
    if not bin_path.exists():
        raise SystemExit(f"binary not found: {bin_path}")
    if not source_path.exists():
        raise SystemExit(f"benchmark source not found: {source_path}")
    if args.opt_level < 0 or args.opt_level > 3:
        raise SystemExit("opt-level must be in range 0..3")

    startup = measure_startup(bin_path, args.startup_iterations)
    compile_latency = measure_compile_latency(bin_path, source_path, args.compile_iterations, args.opt_level)
    payload = {
        "platform_key": args.platform_key,
        "commit_sha": args.commit_sha,
        "tool": "thagc",
        "startup_ms": startup,
        "compile_latency_ms": compile_latency,
        "config": {
            "startup_iterations": args.startup_iterations,
            "compile_iterations": args.compile_iterations,
            "opt_level": args.opt_level,
            "source": str(source_path),
        },
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(payload, indent=2))


if __name__ == "__main__":
    main()

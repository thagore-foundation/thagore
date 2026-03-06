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


def load_budget(path: Path, key: str) -> dict[str, float | int]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if key not in payload:
        raise SystemExit(f"compile latency budget key not found: {key}")
    entry = payload[key]
    if not isinstance(entry, dict):
        raise SystemExit(f"compile latency budget entry must be an object: {key}")
    return entry


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", required=True, help="Path to thagc binary")
    parser.add_argument("--source", required=True, help="Path to benchmark .tg source")
    parser.add_argument("--budgets", required=True, help="JSON file with compile latency budgets")
    parser.add_argument("--key", required=True, help="Budget key to read")
    parser.add_argument("--opt-level", type=int, default=None, help="Override optimization level")
    args = parser.parse_args()

    bin_path = Path(args.bin)
    source_path = Path(args.source)
    budget_file = Path(args.budgets)
    if not bin_path.exists():
        raise SystemExit(f"binary not found: {bin_path}")
    if not source_path.exists():
        raise SystemExit(f"benchmark source not found: {source_path}")
    if not budget_file.exists():
        raise SystemExit(f"budget file not found: {budget_file}")

    budget = load_budget(budget_file, args.key)
    iterations = int(budget.get("iterations", 10))
    max_p95_ms = float(budget.get("max_p95_ms", 1500.0))
    opt_level = int(budget.get("opt_level", 3) if args.opt_level is None else args.opt_level)
    if iterations <= 0:
        raise SystemExit("iterations must be > 0")
    if opt_level < 0 or opt_level > 3:
        raise SystemExit("opt_level must be in range 0..3")

    samples_ms: list[float] = []
    with tempfile.TemporaryDirectory() as td:
        tmp_root = Path(td)
        for idx in range(iterations):
            out_bin = tmp_root / f"bench_{idx}.bin"
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
                raise SystemExit(
                    "compile latency budget check failed: "
                    f"build returned {proc.returncode}\n{proc.stderr}"
                )
            samples_ms.append((end_ns - start_ns) / 1_000_000.0)

    p50_ms = statistics.median(samples_ms)
    p95_ms = percentile(samples_ms, 0.95)
    min_ms = min(samples_ms)
    max_ms = max(samples_ms)
    print(
        "compile latency budget: "
        f"key={args.key} iterations={iterations} opt_level={opt_level} "
        f"min={min_ms:.3f}ms p50={p50_ms:.3f}ms p95={p95_ms:.3f}ms max={max_ms:.3f}ms "
        f"(limit p95<={max_p95_ms:.3f}ms)"
    )
    if p95_ms > max_p95_ms:
        raise SystemExit(
            "compile latency budget exceeded: "
            f"p95={p95_ms:.3f}ms > limit={max_p95_ms:.3f}ms for key={args.key}"
        )


if __name__ == "__main__":
    main()

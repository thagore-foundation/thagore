#!/usr/bin/env python3
import argparse
import json
import math
import statistics
import subprocess
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
        raise SystemExit(f"startup budget key not found: {key}")
    entry = payload[key]
    if not isinstance(entry, dict):
        raise SystemExit(f"startup budget entry must be an object: {key}")
    return entry


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", required=True, help="Path to thagc binary")
    parser.add_argument("--budgets", required=True, help="JSON file with startup budgets")
    parser.add_argument("--key", required=True, help="Budget key to read")
    args = parser.parse_args()

    bin_path = Path(args.bin)
    budget_file = Path(args.budgets)
    if not bin_path.exists():
        raise SystemExit(f"binary not found: {bin_path}")
    if not budget_file.exists():
        raise SystemExit(f"budget file not found: {budget_file}")

    budget = load_budget(budget_file, args.key)
    iterations = int(budget.get("iterations", 20))
    max_p95_ms = float(budget.get("max_p95_ms", 10.0))
    if iterations <= 0:
        raise SystemExit("iterations must be > 0")

    samples_ms: list[float] = []
    for _ in range(iterations):
        start_ns = time.perf_counter_ns()
        proc = subprocess.run([str(bin_path), "--version"], capture_output=True, text=True, check=False)
        end_ns = time.perf_counter_ns()
        if proc.returncode != 0:
            raise SystemExit(f"startup budget check failed: `--version` returned {proc.returncode}\n{proc.stderr}")
        samples_ms.append((end_ns - start_ns) / 1_000_000.0)

    p50_ms = statistics.median(samples_ms)
    p95_ms = percentile(samples_ms, 0.95)
    min_ms = min(samples_ms)
    max_ms = max(samples_ms)
    print(
        "startup budget: "
        f"key={args.key} iterations={iterations} "
        f"min={min_ms:.3f}ms p50={p50_ms:.3f}ms p95={p95_ms:.3f}ms max={max_ms:.3f}ms "
        f"(limit p95<={max_p95_ms:.3f}ms)"
    )
    if p95_ms > max_p95_ms:
        raise SystemExit(
            "startup budget exceeded: "
            f"p95={p95_ms:.3f}ms > limit={max_p95_ms:.3f}ms for key={args.key}"
        )


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--metrics", required=True, help="Benchmark metrics JSON from tooling/bench/run_benchmarks.py")
    parser.add_argument("--budgets", required=True, help="Latency budget contract JSON")
    parser.add_argument("--key", required=True, help="Budget key, for example linux-x86_64")
    args = parser.parse_args()

    metrics_path = Path(args.metrics)
    budgets_path = Path(args.budgets)
    if not metrics_path.exists():
        raise SystemExit(f"metrics file not found: {metrics_path}")
    if not budgets_path.exists():
        raise SystemExit(f"budget file not found: {budgets_path}")

    metrics = json.loads(metrics_path.read_text(encoding="utf-8"))
    budgets = json.loads(budgets_path.read_text(encoding="utf-8"))
    if args.key not in budgets:
        raise SystemExit(f"latency budget key not found: {args.key}")
    budget = budgets[args.key]
    if not isinstance(budget, dict):
        raise SystemExit(f"invalid budget entry for key={args.key}")

    startup_p95 = float(metrics["thagc_startup_ms"]["p95_ms"])
    build_p95 = float(metrics["thagc_build_ms"]["p95_ms"])
    thagore_runtime = metrics.get("comparisons", {}).get("thagore_native", {})
    if not thagore_runtime or not thagore_runtime.get("available", False):
        raise SystemExit("thagore runtime benchmark is required but unavailable")
    runtime_p95 = float(thagore_runtime["metrics"]["p95_ms"])

    max_startup = float(budget.get("max_startup_p95_ms", 0.0))
    max_build = float(budget.get("max_build_p95_ms", 0.0))
    max_runtime = float(budget.get("max_runtime_p95_ms", 0.0))
    if max_startup <= 0 or max_build <= 0 or max_runtime <= 0:
        raise SystemExit(f"invalid latency budget values for key={args.key}")

    print(
        "latency budget: "
        f"key={args.key} startup_p95={startup_p95:.3f}ms(limit={max_startup:.3f}) "
        f"build_p95={build_p95:.3f}ms(limit={max_build:.3f}) "
        f"runtime_p95={runtime_p95:.3f}ms(limit={max_runtime:.3f})"
    )

    violations: list[str] = []
    if startup_p95 > max_startup:
        violations.append(f"startup p95 {startup_p95:.3f}ms > {max_startup:.3f}ms")
    if build_p95 > max_build:
        violations.append(f"build p95 {build_p95:.3f}ms > {max_build:.3f}ms")
    if runtime_p95 > max_runtime:
        violations.append(f"runtime p95 {runtime_p95:.3f}ms > {max_runtime:.3f}ms")

    if violations:
        raise SystemExit("latency budget exceeded: " + "; ".join(violations))


if __name__ == "__main__":
    main()

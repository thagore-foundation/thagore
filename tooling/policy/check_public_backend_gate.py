#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--metrics", required=True, help="Path to benchmark metrics JSON from run_benchmarks.py")
    parser.add_argument("--contract", required=True, help="Path to v1.8 backend competition gate contract JSON")
    args = parser.parse_args()

    metrics = json.loads(Path(args.metrics).read_text(encoding="utf-8"))
    contract = json.loads(Path(args.contract).read_text(encoding="utf-8"))

    comparisons = metrics.get("comparisons", {})
    thagore = comparisons.get("thagore_native", {})
    go = comparisons.get("go", {})
    python = comparisons.get("python", {})

    if not thagore.get("available", False):
        print("v1.8 backend gate failed: thagore_native comparison is unavailable", file=sys.stderr)
        return 1
    if not go.get("available", False):
        print("v1.8 backend gate failed: go comparison is unavailable", file=sys.stderr)
        return 1
    if not python.get("available", False):
        print("v1.8 backend gate failed: python comparison is unavailable", file=sys.stderr)
        return 1

    thagore_p50 = float(thagore["metrics"]["p50_ms"])
    go_p50 = float(go["metrics"]["p50_ms"])
    python_p50 = float(python["metrics"]["p50_ms"])

    ratio_vs_go = thagore_p50 / go_p50 if go_p50 > 0.0 else float("inf")
    speedup_vs_python = python_p50 / thagore_p50 if thagore_p50 > 0.0 else 0.0

    max_ratio = float(contract["max_p50_ratio_vs_go"])
    min_speedup = float(contract["min_speedup_vs_python_x"])

    if ratio_vs_go > max_ratio:
        print(
            f"v1.8 backend gate failed: thagore/go p50 ratio {ratio_vs_go:.3f} > allowed {max_ratio:.3f}",
            file=sys.stderr,
        )
        return 1
    if speedup_vs_python < min_speedup:
        print(
            f"v1.8 backend gate failed: speedup vs python {speedup_vs_python:.3f}x < required {min_speedup:.3f}x",
            file=sys.stderr,
        )
        return 1

    print(
        "v1.8 backend gate passed: "
        f"thagore/go p50 ratio={ratio_vs_go:.3f}, speedup_vs_python={speedup_vs_python:.3f}x"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

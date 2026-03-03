#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--metrics", required=True, help="Path to benchmark metrics JSON")
    parser.add_argument("--contract", required=True, help="Path to v1.7 gate contract JSON")
    args = parser.parse_args()

    metrics = json.loads(Path(args.metrics).read_text(encoding="utf-8"))
    contract = json.loads(Path(args.contract).read_text(encoding="utf-8"))
    required_speedup = float(contract["min_speedup_vs_flask_x"])
    observed_speedup = float(metrics["speedup_vs_flask_x"])
    if observed_speedup < required_speedup:
        print(
            f"v1.7 model-serving gate failed: observed speedup {observed_speedup:.3f}x < required {required_speedup:.3f}x",
            file=sys.stderr,
        )
        return 1
    print(
        f"v1.7 model-serving gate passed: observed speedup {observed_speedup:.3f}x >= required {required_speedup:.3f}x"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

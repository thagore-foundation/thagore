#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def fmt_mib(size_bytes: int) -> str:
    return f"{size_bytes / (1024 * 1024):.2f} MiB"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", required=True, help="Path to thagc binary")
    parser.add_argument("--budgets", required=True, help="JSON file with binary size budgets")
    parser.add_argument("--key", required=True, help="Budget key to read")
    args = parser.parse_args()

    bin_path = Path(args.bin)
    budgets_path = Path(args.budgets)
    if not bin_path.exists():
        raise SystemExit(f"binary not found: {bin_path}")
    if not budgets_path.exists():
        raise SystemExit(f"budget file not found: {budgets_path}")

    payload = json.loads(budgets_path.read_text(encoding="utf-8"))
    if args.key not in payload:
        raise SystemExit(f"binary size budget key not found: {args.key}")
    entry = payload[args.key]
    if not isinstance(entry, dict) or "max_bytes" not in entry:
        raise SystemExit(f"invalid binary size budget entry for key={args.key}; expected object with max_bytes")
    max_bytes = int(entry["max_bytes"])

    current_size = bin_path.stat().st_size
    print(
        "binary size budget: "
        f"key={args.key} current={current_size} bytes ({fmt_mib(current_size)}) "
        f"limit={max_bytes} bytes ({fmt_mib(max_bytes)})"
    )
    if current_size > max_bytes:
        raise SystemExit(
            "binary size budget exceeded: "
            f"current={current_size} bytes > limit={max_bytes} bytes for key={args.key}"
        )


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def load(path: str):
    return json.loads(Path(path).read_text())


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--expected", required=True)
    parser.add_argument("--actual", required=True)
    args = parser.parse_args()

    expected = load(args.expected)
    actual = load(args.actual)
    if expected != actual:
        raise SystemExit("diagnostics parity mismatch")
    print("Diagnostics parity: OK")


if __name__ == "__main__":
    main()


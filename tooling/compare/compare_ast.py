#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def load(path: str):
    return json.loads(Path(path).read_text())


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--left", required=True)
    parser.add_argument("--right", required=True)
    args = parser.parse_args()

    left = load(args.left)
    right = load(args.right)
    if left != right:
        raise SystemExit("AST parity mismatch")
    print("AST parity: OK")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
import argparse
from pathlib import Path


def normalize(text: str) -> str:
    return "\n".join(line.rstrip() for line in text.replace("\r\n", "\n").split("\n")).strip()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--left", required=True)
    parser.add_argument("--right", required=True)
    args = parser.parse_args()

    left = normalize(Path(args.left).read_text())
    right = normalize(Path(args.right).read_text())
    if left != right:
        raise SystemExit("IR parity mismatch")
    print("IR parity: OK")


if __name__ == "__main__":
    main()


#!/usr/bin/env python3
import argparse
import hashlib
from pathlib import Path


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--left", required=True)
    parser.add_argument("--right", required=True)
    args = parser.parse_args()

    left = sha256(args.left)
    right = sha256(args.right)
    print("left:", left)
    print("right:", right)
    if left != right:
        raise SystemExit("binary hash mismatch")
    print("Binary hash parity: OK")


if __name__ == "__main__":
    main()


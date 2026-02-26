#!/usr/bin/env python3
import argparse
import tarfile
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target-dir", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(out, "w:gz") as tf:
        tf.add(args.target_dir, arcname=".")
    print(f"created {out}")


if __name__ == "__main__":
    main()


#!/usr/bin/env python3
import argparse
import tarfile
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", required=True)
    parser.add_argument("--runtime", required=False, default="")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(out, "w:gz") as tf:
        tf.add(args.bin, arcname="bin/thagc")
        if args.runtime:
            tf.add(args.runtime, arcname="lib/" + Path(args.runtime).name)
    print(f"created {out}")


if __name__ == "__main__":
    main()

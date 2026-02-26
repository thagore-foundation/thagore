#!/usr/bin/env python3
import argparse
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--branch", required=True)
    args = parser.parse_args()

    out = subprocess.check_output(["git", "branch", "--list", args.branch], text=True).strip()
    if not out:
        raise SystemExit(f"missing baseline branch: {args.branch}")
    print(f"baseline branch exists: {args.branch}")


if __name__ == "__main__":
    main()


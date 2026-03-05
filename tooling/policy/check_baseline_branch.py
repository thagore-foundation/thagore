#!/usr/bin/env python3
import argparse
import subprocess


def ref_exists(ref: str) -> bool:
    return subprocess.run(["git", "rev-parse", "--verify", "--quiet", ref], check=False).returncode == 0


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--branch", required=True)
    args = parser.parse_args()

    candidates = [
        args.branch,
        f"origin/{args.branch}",
        f"refs/remotes/origin/{args.branch}",
        f"refs/heads/{args.branch}",
    ]
    for ref in candidates:
        if ref_exists(ref):
            print(f"baseline branch exists: {ref}")
            return
    remote = subprocess.check_output(["git", "ls-remote", "--heads", "origin", args.branch], text=True).strip()
    if remote:
        print(f"baseline branch exists on origin: {args.branch}")
        return
    raise SystemExit(f"missing baseline branch: {args.branch}")


if __name__ == "__main__":
    main()

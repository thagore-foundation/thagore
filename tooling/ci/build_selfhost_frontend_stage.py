#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys


def build(thagc: pathlib.Path, repo_root: pathlib.Path, source: str, output: pathlib.Path) -> None:
    completed = subprocess.run(
        [str(thagc), "build", str(repo_root / source), "-o", str(output)],
        cwd=repo_root,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if completed.returncode != 0:
        raise SystemExit(
            f"failed to build {source}\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--thagc-bin", required=True)
    parser.add_argument("--out-dir", required=True)
    args = parser.parse_args()

    repo_root = pathlib.Path(args.repo_root).resolve()
    thagc = pathlib.Path(args.thagc_bin).resolve()
    out_dir = pathlib.Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    exe_suffix = ".exe" if sys.platform.startswith("win") else ""
    build(thagc, repo_root, "bootstrap/selfhost/frontend/scan.tg", out_dir / f"scan{exe_suffix}")
    build(thagc, repo_root, "bootstrap/selfhost/frontend/parse.tg", out_dir / f"parse{exe_suffix}")
    build(thagc, repo_root, "bootstrap/selfhost/frontend/check.tg", out_dir / f"check{exe_suffix}")
    print(f"built selfhost frontend stages into {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

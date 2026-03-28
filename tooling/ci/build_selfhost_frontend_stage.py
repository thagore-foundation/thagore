#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys


def load_manifest(repo_root: pathlib.Path, manifest_path: pathlib.Path) -> list[tuple[str, str]]:
    entries: list[tuple[str, str]] = []
    for raw_line in manifest_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        source, output_name = [part.strip() for part in line.split("|", 1)]
        entries.append((source, output_name))
    if not entries:
        raise SystemExit(f"manifest {manifest_path} has no stage entries")
    for source, _output_name in entries:
        if not (repo_root / source).is_file():
            raise SystemExit(f"manifest source does not exist: {source}")
    return entries


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
    parser.add_argument(
        "--manifest",
        default="bootstrap/selfhost/tools/frontend-stage-manifest.txt",
    )
    args = parser.parse_args()

    repo_root = pathlib.Path(args.repo_root).resolve()
    thagc = pathlib.Path(args.thagc_bin).resolve()
    out_dir = pathlib.Path(args.out_dir).resolve()
    manifest_path = (repo_root / args.manifest).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    exe_suffix = ".exe" if sys.platform.startswith("win") else ""
    entries = load_manifest(repo_root, manifest_path)
    for source, output_name in entries:
        build(thagc, repo_root, source, out_dir / f"{output_name}{exe_suffix}")
    print(f"built {len(entries)} selfhost frontend stages from {manifest_path} into {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

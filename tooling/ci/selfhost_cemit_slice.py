#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import subprocess


def load_manifest(path: pathlib.Path) -> list[tuple[str, str]]:
    rows: list[tuple[str, str]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        fixture, expected = [part.strip() for part in line.split("|", 1)]
        rows.append((fixture, expected))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--cemit-bin", required=True)
    parser.add_argument("--manifest", default="bootstrap/selfhost/corpus/cemit-validate.txt")
    parser.add_argument("--report-out", default="")
    args = parser.parse_args()

    repo_root = pathlib.Path(args.repo_root).resolve()
    cemit_bin = pathlib.Path(args.cemit_bin).resolve()
    report_lines: list[str] = []

    for fixture, expected_path in load_manifest(repo_root / args.manifest):
        completed = subprocess.run(
            [str(cemit_bin), str((repo_root / fixture).resolve().as_posix())],
            cwd=repo_root,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        if completed.returncode != 0:
            raise SystemExit(
                f"cemit slice failed for {fixture}\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
            )
        expected = (repo_root / expected_path).read_text(encoding="utf-8").replace("\r\n", "\n").strip()
        actual = completed.stdout.replace("\r\n", "\n").strip()
        if actual != expected:
            raise SystemExit(
                f"cemit slice output drift for {fixture}\nexpected:\n{expected}\nactual:\n{actual}"
            )
        report_lines.append(f"{fixture}|ok")

    if args.report_out:
        pathlib.Path(args.report_out).write_text("\n".join(report_lines) + "\n", encoding="utf-8")

    print("selfhost cemit slice ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

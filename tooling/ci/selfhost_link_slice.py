#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import pathlib
import subprocess
import tempfile


def load_manifest(path: pathlib.Path) -> list[tuple[str, int]]:
    rows: list[tuple[str, int]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        fixture, exit_code_str = [part.strip() for part in line.split("|", 1)]
        rows.append((fixture, int(exit_code_str)))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--link-bin", required=True)
    parser.add_argument("--manifest", default="bootstrap/selfhost/corpus/link-validate.txt")
    parser.add_argument("--report-out", default="")
    args = parser.parse_args()

    repo_root = pathlib.Path(args.repo_root).resolve()
    link_bin = pathlib.Path(args.link_bin).resolve()
    report_lines: list[str] = []

    with tempfile.TemporaryDirectory() as tmpdir:
        for fixture, expected_exit in load_manifest(repo_root / args.manifest):
            out_name = pathlib.Path(fixture).stem + ".exe"
            out_path = pathlib.Path(tmpdir) / out_name

            link_result = subprocess.run(
                [str(link_bin), str((repo_root / fixture).resolve().as_posix()), str(out_path)],
                cwd=repo_root,
                check=False,
                capture_output=True,
                text=True,
                encoding="utf-8",
            )
            if link_result.returncode != 0:
                raise SystemExit(
                    f"link slice failed for {fixture}\nstdout:\n{link_result.stdout}\nstderr:\n{link_result.stderr}"
                )
            if not out_path.exists():
                raise SystemExit(f"link slice produced no output binary for {fixture}")

            run_result = subprocess.run(
                [str(out_path)],
                check=False,
                capture_output=True,
            )
            actual_exit = run_result.returncode
            if actual_exit != expected_exit:
                raise SystemExit(
                    f"link slice exit code mismatch for {fixture}\n"
                    f"expected exit={expected_exit}, got exit={actual_exit}"
                )
            report_lines.append(f"{fixture}|ok|exit={actual_exit}")

    if args.report_out:
        pathlib.Path(args.report_out).write_text("\n".join(report_lines) + "\n", encoding="utf-8")

    print("selfhost link slice ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

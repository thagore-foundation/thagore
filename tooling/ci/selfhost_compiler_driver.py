#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys


def load_manifest(path: pathlib.Path, columns: int) -> list[list[str]]:
    rows: list[list[str]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = [part.strip() for part in line.split("|")]
        if len(parts) != columns:
            raise SystemExit(f"invalid manifest row in {path}: {line}")
        rows.append(parts)
    return rows


def resolve_arg(repo_root: pathlib.Path, raw: str) -> str:
    if not raw:
        return ""
    if raw.startswith("@abs:"):
        return (repo_root / raw[len("@abs:"):]).resolve().as_posix()
    return raw


def canonicalize_golden(text: str) -> str:
    normalized = text.replace("\r\n", "\n").strip()
    if sys.platform.startswith("win"):
        return re.sub(r"@\d+:\d+", "@_:@_", normalized)
    return normalized


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--compiler-bin", required=True)
    parser.add_argument("--manifest", default="bootstrap/selfhost/corpus/compiler-driver-contract.txt")
    parser.add_argument("--report-out", default="")
    args = parser.parse_args()

    repo_root = pathlib.Path(args.repo_root).resolve()
    compiler_bin = pathlib.Path(args.compiler_bin).resolve()
    report_lines: list[str] = []

    for label, cwd_raw, command, path_raw, kind, expected_exit, expected_path in load_manifest(repo_root / args.manifest, 7):
        cwd = repo_root if not cwd_raw or cwd_raw == "." else (repo_root / cwd_raw)
        cmd = [str(compiler_bin), command]
        path_arg = resolve_arg(repo_root, path_raw)
        if path_arg:
            cmd.append(path_arg)
        if kind:
            cmd.append(kind)
        completed = subprocess.run(
            cmd,
            cwd=cwd,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        expected_code = int(expected_exit)
        if completed.returncode != expected_code:
            raise SystemExit(
                f"compiler driver exit drift for {label}: expected {expected_code}, got {completed.returncode}"
                f"\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
            )
        expected = canonicalize_golden((repo_root / expected_path).read_text(encoding="utf-8"))
        actual = canonicalize_golden(completed.stdout)
        if actual != expected:
            raise SystemExit(
                f"compiler driver output drift for {label}"
                f"\nexpected:\n{expected}\nactual:\n{actual}"
            )
        report_lines.append(f"{label}|exit={expected_code}|ok")

    if args.report_out:
        pathlib.Path(args.report_out).write_text("\n".join(report_lines) + "\n", encoding="utf-8")

    print("selfhost compiler driver ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

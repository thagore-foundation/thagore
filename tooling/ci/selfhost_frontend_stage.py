#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys


def load_manifest(path: pathlib.Path, columns: int) -> list[list[str]]:
    rows: list[list[str]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            continue
        parts = [part.strip() for part in line.split("|")]
        if len(parts) != columns:
            raise SystemExit(f"invalid manifest row in {path}: {line}")
        rows.append(parts)
    return rows


def run(binary: pathlib.Path, repo_root: pathlib.Path, fixture: str, kind: str = "") -> str:
    args = [str(binary), str(repo_root / fixture)]
    if kind:
        args.append(kind)
    completed = subprocess.run(
        args,
        cwd=repo_root,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if completed.returncode != 0:
        raise SystemExit(
            f"{binary.name} failed for {fixture}\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return completed.stdout.replace("\r\n", "\n").strip()


def canonicalize_selfhost(stdout: str) -> str:
    diagnostics = ""
    marker = " || diagnostics="
    if marker in stdout:
        diagnostics = stdout.split(marker, 1)[1].strip()
    if diagnostics == "ok":
        return "ok"
    if diagnostics in {"unknown return ident", "unknown callee", "assignment to unknown local"}:
        return "unknown identifier"
    if diagnostics == "call arity mismatch":
        return "call arity mismatch"
    if diagnostics in {"assignment type mismatch", "assignment call result type mismatch"}:
        return "type mismatch"
    if diagnostics == "condition type mismatch":
        return "condition type mismatch"
    if diagnostics == "return type mismatch":
        return "return type mismatch"
    return diagnostics


def report_prefix(stdout: str) -> str:
    return stdout.split(" || diagnostics=", 1)[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--scan-bin", required=True)
    parser.add_argument("--parse-bin", required=True)
    parser.add_argument("--check-bin", required=True)
    args = parser.parse_args()

    repo_root = pathlib.Path(args.repo_root).resolve()
    scan_bin = pathlib.Path(args.scan_bin).resolve()
    parse_bin = pathlib.Path(args.parse_bin).resolve()
    check_bin = pathlib.Path(args.check_bin).resolve()

    diff_manifest = load_manifest(repo_root / "tests/selfhost_frontend/differential_corpus.txt", 2)
    for fixture, expected in diff_manifest:
        actual = canonicalize_selfhost(run(check_bin, repo_root, fixture))
        if actual != expected:
            raise SystemExit(
                f"selfhost differential drift for {fixture}: expected {expected!r}, got {actual!r}"
            )

    stage_manifest = load_manifest(repo_root / "tests/selfhost_frontend/stage_chain_corpus.txt", 3)
    for fixture, kind, expected_diag in stage_manifest:
        scan_out = run(scan_bin, repo_root, fixture, kind)
        parse_out = run(parse_bin, repo_root, fixture, kind)
        check_out = run(check_bin, repo_root, fixture, kind)

        if not scan_out.startswith("tokens="):
            raise SystemExit(f"scan output malformed for {fixture}: {scan_out}")
        if not report_prefix(parse_out).startswith(scan_out):
            raise SystemExit(
                f"parse output does not extend scan output for {fixture}\nscan:\n{scan_out}\nparse:\n{parse_out}"
            )
        if " || diagnostics=ok" not in parse_out:
            raise SystemExit(f"parse diagnostics drift for {fixture}: {parse_out}")
        if not report_prefix(check_out).startswith(report_prefix(parse_out)):
            raise SystemExit(
                f"check output does not extend parse output for {fixture}\nparse:\n{parse_out}\ncheck:\n{check_out}"
            )
        expected_fragment = f" || diagnostics={expected_diag}"
        if expected_fragment not in check_out:
            raise SystemExit(
                f"check diagnostics drift for {fixture}: expected {expected_fragment!r}\n{check_out}"
            )

    print("selfhost frontend stage lane ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

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


def run(binary: pathlib.Path, repo_root: pathlib.Path, fixture: str, kind: str, mode: str = "") -> str:
    args = [str(binary), (repo_root / fixture).resolve().as_posix()]
    if kind:
        args.append(kind)
    if mode:
        args.append(mode)
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
            f"{binary.name} failed for {fixture} kind={kind} mode={mode or 'analyze'}"
            f"\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return completed.stdout.replace("\r\n", "\n").strip()


def canonicalize_golden(text: str) -> str:
    normalized = text.replace("\r\n", "\n").strip()
    if sys.platform.startswith("win"):
        return re.sub(r"@\d+:\d+", "@_:@_", normalized)
    return normalized


def resolve_input_arg(repo_root: pathlib.Path, raw: str) -> str:
    if not raw:
        return ""
    if raw.startswith("@abs:"):
        return (repo_root / raw[len("@abs:"):]).resolve().as_posix()
    return raw


def verify_orchestration_manifest(
    repo_root: pathlib.Path,
    binary: pathlib.Path,
    manifest_path: pathlib.Path,
    report_lines: list[str],
) -> None:
    for label, cwd_raw, path_raw, kind, mode, expected_exit, expected_path in load_manifest(manifest_path, 7):
        cwd = repo_root if not cwd_raw or cwd_raw == "." else (repo_root / cwd_raw)
        args = [str(binary)]
        path_arg = resolve_input_arg(repo_root, path_raw)
        if path_arg:
            args.append(path_arg)
        if kind:
            args.append(kind)
        if mode:
            args.append(mode)
        completed = subprocess.run(
            args,
            cwd=cwd,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        expected_code = int(expected_exit)
        if completed.returncode != expected_code:
            raise SystemExit(
                f"{binary.name} orchestration drift for {label}: expected exit {expected_code}, got {completed.returncode}"
                f"\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
            )
        expected = (repo_root / expected_path).read_text(encoding="utf-8")
        actual = completed.stdout.replace("\r\n", "\n").strip()
        if canonicalize_golden(actual) != canonicalize_golden(expected):
            raise SystemExit(
                f"{binary.name} orchestration drift for {label}"
                f"\nexpected:\n{expected.strip()}\nactual:\n{actual}"
            )
        report_lines.append(f"orchestration|{label}|exit={expected_code}|ok")


def verify_manifest(
    repo_root: pathlib.Path,
    binary: pathlib.Path,
    manifest_path: pathlib.Path,
    mode: str,
    report_lines: list[str],
) -> None:
    for fixture, kind, expected_path in load_manifest(manifest_path, 3):
        actual = run(binary, repo_root, fixture, kind, mode=mode)
        expected = (repo_root / expected_path).read_text(encoding="utf-8")
        if canonicalize_golden(actual) != canonicalize_golden(expected):
            raise SystemExit(
                f"{binary.name} drift for {fixture} kind={kind} mode={mode or 'analyze'}"
                f"\nexpected:\n{expected.strip()}\nactual:\n{actual}"
            )
        mode_name = mode if mode else "analyze"
        report_lines.append(f"{mode_name}|{fixture}|kind={kind}|ok")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--main-bin", required=True)
    parser.add_argument("--report-out", default="")
    args = parser.parse_args()

    repo_root = pathlib.Path(args.repo_root).resolve()
    main_bin = pathlib.Path(args.main_bin).resolve()
    report_lines: list[str] = []

    verify_manifest(
        repo_root,
        main_bin,
        repo_root / "bootstrap/selfhost/corpus/bootstrap-analyze.txt",
        "",
        report_lines,
    )
    verify_manifest(
        repo_root,
        main_bin,
        repo_root / "bootstrap/selfhost/corpus/frontend-analyze.txt",
        "",
        report_lines,
    )
    verify_manifest(
        repo_root,
        main_bin,
        repo_root / "bootstrap/selfhost/corpus/bootstrap-desugar.txt",
        "dump-desugared",
        report_lines,
    )
    verify_manifest(
        repo_root,
        main_bin,
        repo_root / "bootstrap/selfhost/corpus/bootstrap-report.txt",
        "dump-report",
        report_lines,
    )
    verify_manifest(
        repo_root,
        main_bin,
        repo_root / "bootstrap/selfhost/corpus/frontend-report.txt",
        "dump-report",
        report_lines,
    )
    verify_orchestration_manifest(
        repo_root,
        main_bin,
        repo_root / "bootstrap/selfhost/corpus/frontend-driver-orchestration.txt",
        report_lines,
    )

    if args.report_out:
        pathlib.Path(args.report_out).write_text("\n".join(report_lines) + "\n", encoding="utf-8")

    print("selfhost frontend driver target ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

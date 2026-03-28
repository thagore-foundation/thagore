#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import subprocess


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


def verify_manifest(
    repo_root: pathlib.Path,
    binary: pathlib.Path,
    manifest_path: pathlib.Path,
    mode: str,
    report_lines: list[str],
) -> None:
    for fixture, kind, expected_path in load_manifest(manifest_path, 3):
        actual = run(binary, repo_root, fixture, kind, mode=mode)
        expected = (repo_root / expected_path).read_text(encoding="utf-8").replace("\r\n", "\n").strip()
        if actual != expected:
            raise SystemExit(
                f"{binary.name} drift for {fixture} kind={kind} mode={mode or 'analyze'}"
                f"\nexpected:\n{expected}\nactual:\n{actual}"
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
        repo_root / "tests/bootstrap_seed/analyze_corpus.txt",
        "",
        report_lines,
    )
    verify_manifest(
        repo_root,
        main_bin,
        repo_root / "tests/bootstrap_seed/desugar_corpus.txt",
        "dump-desugared",
        report_lines,
    )
    verify_manifest(
        repo_root,
        main_bin,
        repo_root / "tests/bootstrap_seed/report_corpus.txt",
        "dump-report",
        report_lines,
    )
    verify_manifest(
        repo_root,
        main_bin,
        repo_root / "tests/selfhost_frontend/report_corpus.txt",
        "dump-report",
        report_lines,
    )

    if args.report_out:
        pathlib.Path(args.report_out).write_text("\n".join(report_lines) + "\n", encoding="utf-8")

    print("selfhost frontend driver target ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

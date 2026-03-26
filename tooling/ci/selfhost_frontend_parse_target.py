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


def run(binary: pathlib.Path, repo_root: pathlib.Path, fixture: str, kind: str) -> str:
    fixture_path = (repo_root / fixture).resolve().as_posix()
    completed = subprocess.run(
        [str(binary), fixture_path, kind],
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


def report_prefix(stdout: str) -> str:
    return stdout.split(" || diagnostics=", 1)[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--scan-bin", required=True)
    parser.add_argument("--parse-bin", required=True)
    parser.add_argument("--report-out", default="")
    args = parser.parse_args()

    repo_root = pathlib.Path(args.repo_root).resolve()
    scan_bin = pathlib.Path(args.scan_bin).resolve()
    parse_bin = pathlib.Path(args.parse_bin).resolve()
    report_lines: list[str] = []

    parse_manifest = load_manifest(repo_root / "tests/selfhost_frontend/parse_corpus.txt", 3)
    for fixture, kind, expected_path in parse_manifest:
        actual = run(parse_bin, repo_root, fixture, kind)
        expected = (repo_root / expected_path).read_text(encoding="utf-8").replace("\r\n", "\n").strip()
        if actual != expected:
            raise SystemExit(
                f"parse target golden drift for {fixture}\nexpected:\n{expected}\nactual:\n{actual}"
            )
        report_lines.append(f"parse-golden|{fixture}|kind={kind}|ok")

    stage_manifest = load_manifest(repo_root / "tests/selfhost_frontend/stage_chain_corpus.txt", 3)
    for fixture, kind, expected_diag in stage_manifest:
        scan_out = run(scan_bin, repo_root, fixture, kind)
        parse_out = run(parse_bin, repo_root, fixture, kind)
        if not scan_out.startswith("tokens="):
            raise SystemExit(f"scan output malformed for {fixture}: {scan_out}")
        if not report_prefix(parse_out).startswith(scan_out):
            raise SystemExit(
                f"parse output does not extend scan output for {fixture}\nscan:\n{scan_out}\nparse:\n{parse_out}"
            )
        diag = " || diagnostics=ok"
        if diag not in parse_out:
            raise SystemExit(
                f"parse diagnostics drift for {fixture}: expected {diag!r}\n{parse_out}"
            )
        report_lines.append(
            f"stage-chain|{fixture}|kind={kind}|expected_check_diag={expected_diag}|parse=ok"
        )

    payload = "\n".join(report_lines) + "\n"
    if args.report_out:
        pathlib.Path(args.report_out).write_text(payload, encoding="utf-8")
    sys.stdout.write(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

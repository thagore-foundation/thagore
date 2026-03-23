#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys


def load_manifest(path: pathlib.Path) -> list[tuple[str, str]]:
    rows: list[tuple[str, str]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            continue
        fixture, expected = [part.strip() for part in line.split("|", 1)]
        rows.append((fixture, expected))
    return rows


def run_selfhost(binary: pathlib.Path, repo_root: pathlib.Path, fixture: str) -> tuple[int, str, str]:
    completed = subprocess.run(
        [str(binary), str(repo_root / fixture)],
        cwd=repo_root,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    return completed.returncode, completed.stdout.replace("\r\n", "\n"), completed.stderr.replace("\r\n", "\n")


def run_host(
    thagc: pathlib.Path,
    repo_root: pathlib.Path,
    fixture: str,
    selfhost_bin: pathlib.Path,
    manifest: pathlib.Path,
) -> tuple[int, str]:
    completed = subprocess.run(
        [
            str(thagc),
            "check",
            str(repo_root / fixture),
            "--selfhost-replacement-bin",
            str(selfhost_bin.resolve()),
            "--selfhost-replacement-manifest",
            str(manifest.resolve()),
            "--selfhost-replacement-strict",
        ],
        cwd=repo_root,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    return completed.returncode, completed.stderr.replace("\r\n", "\n")


def canonicalize_selfhost(stdout: str) -> str:
    marker = " || diagnostics="
    diagnostics = stdout.split(marker, 1)[1].strip() if marker in stdout else ""
    if diagnostics == "ok":
        return "ok"
    if diagnostics in {"unknown return ident", "unknown callee", "assignment to unknown local"}:
        return "unknown identifier"
    if diagnostics == "call arity mismatch":
        return "call arity mismatch"
    if diagnostics in {"assignment type mismatch", "assignment call result type mismatch", "local type mismatch"}:
        return "type mismatch"
    if diagnostics == "condition type mismatch":
        return "condition type mismatch"
    if diagnostics == "return type mismatch":
        return "return type mismatch"
    return diagnostics


def canonicalize_host(stderr: str, code: int) -> str:
    if code == 0:
        return "ok"
    if "argument count mismatch" in stderr:
        return "call arity mismatch"
    if "condition must be bool" in stderr:
        return "condition type mismatch"
    if "return type mismatch" in stderr:
        return "return type mismatch"
    if "type mismatch" in stderr:
        return "type mismatch"
    if "unknown identifier" in stderr:
        return "unknown identifier"
    return stderr.strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--host-thagc", required=True)
    parser.add_argument("--selfhost-check", required=True)
    parser.add_argument("--manifest", default="tests/selfhost_frontend/differential_corpus.txt")
    parser.add_argument("--summary-out", default="")
    args = parser.parse_args()

    repo_root = pathlib.Path(args.repo_root).resolve()
    host_thagc = pathlib.Path(args.host_thagc).resolve()
    selfhost_check = pathlib.Path(args.selfhost_check).resolve()
    manifest = (repo_root / args.manifest).resolve()
    rows = load_manifest(manifest)
    report_lines: list[str] = []

    for fixture, expected in rows:
        selfhost_code, selfhost_stdout, selfhost_stderr = run_selfhost(selfhost_check, repo_root, fixture)
        if selfhost_code != 0:
            raise SystemExit(
                f"selfhost check failed for {fixture}\nstdout:\n{selfhost_stdout}\nstderr:\n{selfhost_stderr}"
            )
        host_code, host_stderr = run_host(host_thagc, repo_root, fixture, selfhost_check, manifest)
        selfhost_label = canonicalize_selfhost(selfhost_stdout)
        host_label = canonicalize_host(host_stderr, host_code)
        if selfhost_label != expected:
            raise SystemExit(
                f"selfhost replacement drift for {fixture}: expected {expected!r}, got {selfhost_label!r}"
            )
        if host_label != expected:
            raise SystemExit(
                f"host replacement drift for {fixture}: expected {expected!r}, got {host_label!r}"
            )
        if host_label != selfhost_label:
            raise SystemExit(
                f"host/selfhost replacement mismatch for {fixture}: host={host_label!r} selfhost={selfhost_label!r}"
            )
        report_lines.append(f"{fixture}|expected={expected}|host={host_label}|selfhost={selfhost_label}")

    payload = "\n".join(report_lines) + "\n"
    if args.summary_out:
        pathlib.Path(args.summary_out).write_text(payload, encoding="utf-8")
    sys.stdout.write(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

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
    fixture_path = (repo_root / fixture).resolve().as_posix()
    args = [str(binary), fixture_path]
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
    if diagnostics in {"assignment type mismatch", "assignment call result type mismatch", "local type mismatch", "local call result type mismatch"}:
        return "type mismatch"
    if diagnostics == "condition type mismatch":
        return "condition type mismatch"
    if diagnostics in {"return type mismatch", "return call result type mismatch"}:
        return "return type mismatch"
    return diagnostics


def canonicalize_host_check(stderr: str, status_code: int) -> str:
    if status_code == 0:
        return "ok"
    if "argument count mismatch" in stderr:
        return "call arity mismatch"
    if "module resolution failed" in stderr:
        return "missing import"
    if "unresolved imported symbol" in stderr:
        return "unknown imported symbol"
    if "condition must be bool" in stderr:
        return "condition type mismatch"
    if "return type mismatch" in stderr:
        return "return type mismatch"
    if "type mismatch" in stderr:
        return "type mismatch"
    if "unknown identifier" in stderr:
        return "unknown identifier"
    return stderr.strip()


def report_prefix(stdout: str) -> str:
    return stdout.split(" || diagnostics=", 1)[0]


def verify_golden_manifest(
    repo_root: pathlib.Path,
    binary: pathlib.Path,
    manifest_path: pathlib.Path,
    report_lines: list[str],
) -> None:
    rows = load_manifest(manifest_path, 3)
    for fixture, kind, expected_path in rows:
        actual = run(binary, repo_root, fixture, kind)
        expected = (repo_root / expected_path).read_text(encoding="utf-8").replace("\r\n", "\n").strip()
        if actual != expected:
            raise SystemExit(
                f"{binary.name} golden drift for {fixture}\nexpected:\n{expected}\nactual:\n{actual}"
            )
        report_lines.append(f"golden|{binary.name}|{fixture}|kind={kind}|ok")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--scan-bin", required=True)
    parser.add_argument("--parse-bin", required=True)
    parser.add_argument("--check-bin", required=True)
    parser.add_argument("--report-out", default="")
    parser.add_argument("--host-thagc", default="")
    args = parser.parse_args()

    repo_root = pathlib.Path(args.repo_root).resolve()
    scan_bin = pathlib.Path(args.scan_bin).resolve()
    parse_bin = pathlib.Path(args.parse_bin).resolve()
    check_bin = pathlib.Path(args.check_bin).resolve()
    host_thagc = pathlib.Path(args.host_thagc).resolve() if args.host_thagc else None
    report_lines: list[str] = []

    diff_manifest = load_manifest(repo_root / "tests/selfhost_frontend/differential_corpus.txt", 3)
    for fixture, kind, expected in diff_manifest:
        selfhost_actual = canonicalize_selfhost(run(check_bin, repo_root, fixture, kind))
        if selfhost_actual != expected:
            raise SystemExit(
                f"selfhost differential drift for {fixture}: expected {expected!r}, got {selfhost_actual!r}"
            )
        if host_thagc is not None:
            completed = subprocess.run(
                [
                    str(host_thagc),
                    "check",
                    (repo_root / fixture).resolve().as_posix(),
                    "--selfhost-replacement-kind",
                    kind,
                ],
                cwd=repo_root,
                check=False,
                capture_output=True,
                text=True,
                encoding="utf-8",
            )
            host_actual = canonicalize_host_check(
                completed.stderr.replace("\r\n", "\n"),
                completed.returncode,
            )
            if host_actual != expected:
                raise SystemExit(
                    f"host check drift for {fixture}: expected {expected!r}, got {host_actual!r}"
                )
            if host_actual != selfhost_actual:
                raise SystemExit(
                    f"host/selfhost drift for {fixture}: host={host_actual!r} selfhost={selfhost_actual!r}"
                )
            report_lines.append(f"diff|{fixture}|kind={kind}|selfhost={selfhost_actual}|host={host_actual}")
        else:
            report_lines.append(f"diff|{fixture}|kind={kind}|selfhost={selfhost_actual}")

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
        report_lines.append(f"scan|{fixture}|{scan_out}")
        report_lines.append(f"parse|{fixture}|{parse_out}")
        report_lines.append(f"check|{fixture}|{check_out}")

    verify_golden_manifest(
        repo_root,
        check_bin,
        repo_root / "tests/selfhost_frontend/report_corpus.txt",
        report_lines,
    )
    verify_golden_manifest(
        repo_root,
        parse_bin,
        repo_root / "tests/selfhost_frontend/parse_corpus.txt",
        report_lines,
    )
    verify_golden_manifest(
        repo_root,
        scan_bin,
        repo_root / "tests/selfhost_frontend/scan_corpus.txt",
        report_lines,
    )

    if args.report_out:
        pathlib.Path(args.report_out).write_text(
            "\n".join(report_lines) + "\n",
            encoding="utf-8",
        )

    print("selfhost frontend stage lane ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

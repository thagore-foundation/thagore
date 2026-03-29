#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import pathlib
import re
import subprocess
import sys
import tempfile


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


def canonicalize(text: str) -> str:
    normalized = text.replace("\r\n", "\n").strip()
    if sys.platform.startswith("win"):
        return re.sub(r"@\d+:\d+", "@_:@_", normalized)
    return normalized


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--compiler-bin", required=True)
    parser.add_argument("--host-thagc", default="")
    parser.add_argument("--manifest", default="bootstrap/selfhost/corpus/backend-adapter-artifacts.txt")
    parser.add_argument("--report-out", default="")
    args = parser.parse_args()

    repo_root = pathlib.Path(args.repo_root).resolve()
    compiler_bin = pathlib.Path(args.compiler_bin).resolve()
    host_thagc = pathlib.Path(args.host_thagc).resolve() if args.host_thagc else None
    report_lines: list[str] = []

    with tempfile.TemporaryDirectory(prefix="selfhost-backend-adapter-artifacts-") as scratch_root:
        scratch_dir = pathlib.Path(scratch_root)
        for (
            label,
            cwd_raw,
            command,
            path_raw,
            kind,
            artifact_name,
            expected_exit,
            stdout_expected,
            plan_expected,
            adapter_expected,
            lowered_expected,
            host_expected,
            artifact_stdout_expected,
        ) in load_manifest(repo_root / args.manifest, 13):
            cwd = repo_root if not cwd_raw or cwd_raw == "." else (repo_root / cwd_raw)
            env = dict(os.environ)
            env["THAGORE_SELFHOST_TMP"] = str(scratch_dir)
            if host_thagc is not None:
                env["THAGORE_STAGE0"] = host_thagc.name
                env["PATH"] = f"{host_thagc.parent}{os.pathsep}{env.get('PATH', '')}"
            artifact_path = scratch_dir / artifact_name
            plan_path = scratch_dir / f"{artifact_name}.plan.txt"
            adapter_path = scratch_dir / f"{artifact_name}.adapter.txt"
            lowered_path = scratch_dir / f"{artifact_name}.lowered.txt"
            host_path = scratch_dir / f"{artifact_name}.host.txt"
            for path in (artifact_path, plan_path, adapter_path, lowered_path, host_path):
                if path.exists():
                    path.unlink()
            cmd = [str(compiler_bin), command, resolve_arg(repo_root, path_raw)]
            if kind:
                cmd.append(kind)
            cmd.append(artifact_name)
            completed = subprocess.run(
                cmd,
                cwd=cwd,
                env=env,
                check=False,
                capture_output=True,
                text=True,
                encoding="utf-8",
            )
            expected_code = int(expected_exit)
            if completed.returncode != expected_code:
                raise SystemExit(
                    f"backend adapter exit drift for {label}: expected {expected_code}, got {completed.returncode}"
                    f"\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
                )
            actual_stdout = canonicalize(completed.stdout)
            expected_stdout = canonicalize((repo_root / stdout_expected).read_text(encoding="utf-8"))
            if actual_stdout != expected_stdout:
                raise SystemExit(
                    f"backend adapter stdout drift for {label}"
                    f"\nexpected:\n{expected_stdout}\nactual:\n{actual_stdout}"
                )
            actual_plan = canonicalize(plan_path.read_text(encoding="utf-8"))
            expected_plan_text = canonicalize((repo_root / plan_expected).read_text(encoding="utf-8"))
            if actual_plan != expected_plan_text:
                raise SystemExit(
                    f"backend adapter plan drift for {label}"
                    f"\nexpected:\n{expected_plan_text}\nactual:\n{actual_plan}"
                )
            actual_adapter = canonicalize(adapter_path.read_text(encoding="utf-8"))
            expected_adapter_text = canonicalize((repo_root / adapter_expected).read_text(encoding="utf-8"))
            if actual_adapter != expected_adapter_text:
                raise SystemExit(
                    f"backend adapter request drift for {label}"
                    f"\nexpected:\n{expected_adapter_text}\nactual:\n{actual_adapter}"
                )
            actual_lowered = canonicalize(lowered_path.read_text(encoding="utf-8"))
            expected_lowered_text = canonicalize((repo_root / lowered_expected).read_text(encoding="utf-8"))
            if actual_lowered != expected_lowered_text:
                raise SystemExit(
                    f"backend adapter lowered artifact drift for {label}"
                    f"\nexpected:\n{expected_lowered_text}\nactual:\n{actual_lowered}"
                )
            actual_host = canonicalize(host_path.read_text(encoding="utf-8"))
            expected_host_text = canonicalize((repo_root / host_expected).read_text(encoding="utf-8"))
            if actual_host != expected_host_text:
                raise SystemExit(
                    f"backend adapter host command drift for {label}"
                    f"\nexpected:\n{expected_host_text}\nactual:\n{actual_host}"
                )
            if command == "build" and expected_code == 0 and not artifact_path.exists():
                raise SystemExit(f"backend adapter build artifact missing for {label}")
            if command == "build" and expected_code == 0 and artifact_stdout_expected:
                built = subprocess.run(
                    [str(artifact_path)],
                    cwd=cwd,
                    check=False,
                    capture_output=True,
                    text=True,
                    encoding="utf-8",
                )
                if built.returncode != 0:
                    raise SystemExit(
                        f"backend adapter built artifact failed for {label}: {built.returncode}"
                        f"\nstdout:\n{built.stdout}\nstderr:\n{built.stderr}"
                    )
                actual_artifact_stdout = canonicalize(built.stdout)
                expected_artifact_stdout = canonicalize((repo_root / artifact_stdout_expected).read_text(encoding="utf-8"))
                if actual_artifact_stdout != expected_artifact_stdout:
                    raise SystemExit(
                        f"backend adapter built artifact stdout drift for {label}"
                        f"\nexpected:\n{expected_artifact_stdout}\nactual:\n{actual_artifact_stdout}"
                    )
            report_lines.append(f"{label}|exit={expected_code}|ok")

    if args.report_out:
        pathlib.Path(args.report_out).write_text("\n".join(report_lines) + "\n", encoding="utf-8")
    print("selfhost backend adapter artifacts ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

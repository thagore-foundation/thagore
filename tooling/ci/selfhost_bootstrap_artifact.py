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


def canonicalize_golden(text: str) -> str:
    normalized = text.replace("\r\n", "\n").strip()
    if sys.platform.startswith("win"):
        return re.sub(r"@\d+:\d+", "@_:@_", normalized)
    return normalized


def resolve_arg(repo_root: pathlib.Path, raw: str) -> str:
    if not raw:
        return ""
    if raw.startswith("@abs:"):
        return (repo_root / raw[len("@abs:"):]).resolve().as_posix()
    return raw


def run_artifact(
    artifact: pathlib.Path,
    scratch_dir: pathlib.Path,
    env: dict[str, str],
    cwd: pathlib.Path,
    invoke: str,
    path_arg: str,
    kind: str,
    mode: str,
) -> subprocess.CompletedProcess[str]:
    cmd = [str(artifact)]
    nested_args = [part for part in mode.split(";;") if part] if mode else []
    if invoke == "version":
        cmd.append("version")
    elif invoke == "exec":
        if path_arg:
            cmd.append(path_arg)
        if kind:
            cmd.append(kind)
        if mode:
            cmd.append(mode)
    elif invoke == "build-version":
        nested_artifact = scratch_dir / kind
        if nested_artifact.exists():
            nested_artifact.unlink()
        build_cmd = [str(artifact), "build"]
        if path_arg:
            build_cmd.append(path_arg)
        build_cmd.append(kind)
        built = subprocess.run(
            build_cmd,
            cwd=cwd,
            env=env,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        if built.returncode != 0:
            return built
        if not nested_artifact.exists():
            return subprocess.CompletedProcess(build_cmd, 1, "", f"missing nested artifact: {nested_artifact}")
        return subprocess.run(
            [str(nested_artifact), "version"],
            cwd=cwd,
            env=env,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
    elif invoke == "build-exec":
        nested_artifact = scratch_dir / kind
        if nested_artifact.exists():
            nested_artifact.unlink()
        build_cmd = [str(artifact), "build"]
        if path_arg:
            build_cmd.append(path_arg)
        build_cmd.append(kind)
        built = subprocess.run(
            build_cmd,
            cwd=cwd,
            env=env,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        if built.returncode != 0:
            return built
        if not nested_artifact.exists():
            return subprocess.CompletedProcess(build_cmd, 1, "", f"missing nested artifact: {nested_artifact}")
        nested_cmd = [str(nested_artifact), *nested_args]
        return subprocess.run(
            nested_cmd,
            cwd=cwd,
            env=env,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
    elif invoke == "build-build-version" or invoke == "build-build-exec":
        nested_artifact = scratch_dir / kind
        if nested_artifact.exists():
            nested_artifact.unlink()
        build_cmd = [str(artifact), "build"]
        if path_arg:
            build_cmd.append(path_arg)
        build_cmd.append(kind)
        built = subprocess.run(
            build_cmd,
            cwd=cwd,
            env=env,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        if built.returncode != 0:
            return built
        if not nested_artifact.exists():
            return subprocess.CompletedProcess(build_cmd, 1, "", f"missing nested artifact: {nested_artifact}")
        if len(nested_args) < 2:
            return subprocess.CompletedProcess(build_cmd, 1, "", f"missing second-stage source/artifact args for {invoke}")
        second_source = nested_args[0]
        second_artifact_name = nested_args[1]
        second_artifact = scratch_dir / second_artifact_name
        if second_artifact.exists():
            second_artifact.unlink()
        second_build_cmd = [str(nested_artifact), "build", second_source, second_artifact_name]
        second_built = subprocess.run(
            second_build_cmd,
            cwd=cwd,
            env=env,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        if second_built.returncode != 0:
            return second_built
        if not second_artifact.exists():
            return subprocess.CompletedProcess(second_build_cmd, 1, "", f"missing second nested artifact: {second_artifact}")
        nested_cmd = [str(second_artifact)]
        if invoke == "build-build-version":
            nested_cmd.append("version")
        else:
            nested_cmd.extend(nested_args[2:])
        return subprocess.run(
            nested_cmd,
            cwd=cwd,
            env=env,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
    else:
        raise SystemExit(f"unsupported bootstrap artifact invoke mode: {invoke}")
    return subprocess.run(
        cmd,
        cwd=cwd,
        env=env,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--compiler-bin", required=True)
    parser.add_argument("--host-thagc", default="")
    parser.add_argument("--manifest", default="bootstrap/selfhost/corpus/bootstrap-artifact-contract.txt")
    parser.add_argument("--report-out", default="")
    args = parser.parse_args()

    repo_root = pathlib.Path(args.repo_root).resolve()
    compiler_bin = pathlib.Path(args.compiler_bin).resolve()
    host_thagc = pathlib.Path(args.host_thagc).resolve() if args.host_thagc else None
    report_lines: list[str] = []

    with tempfile.TemporaryDirectory(prefix="selfhost-bootstrap-artifact-") as scratch_root:
        scratch_dir = pathlib.Path(scratch_root)
        env = dict(os.environ)
        env["THAGORE_SELFHOST_TMP"] = str(scratch_dir)
        if host_thagc is not None:
            env["THAGORE_STAGE0"] = host_thagc.name
            env["PATH"] = f"{host_thagc.parent}{os.pathsep}{env.get('PATH', '')}"

        for label, source_raw, artifact_name, cwd_raw, invoke, path_raw, kind, mode, expected_exit, expected_path in load_manifest(
            repo_root / args.manifest, 10
        ):
            source = (repo_root / source_raw).resolve()
            artifact = scratch_dir / artifact_name
            cwd = repo_root if not cwd_raw or cwd_raw == "." else (repo_root / cwd_raw)
            if artifact.exists():
                artifact.unlink()

            build_completed = subprocess.run(
                [str(compiler_bin), "build", str(source), artifact_name],
                cwd=repo_root,
                env=env,
                check=False,
                capture_output=True,
                text=True,
                encoding="utf-8",
            )
            if build_completed.returncode != 0:
                raise SystemExit(
                    f"bootstrap artifact build failed for {label}"
                    f"\nstdout:\n{build_completed.stdout}\nstderr:\n{build_completed.stderr}"
                )
            if not artifact.exists():
                raise SystemExit(f"bootstrap artifact missing for {label}: {artifact}")

            completed = run_artifact(
                artifact,
                scratch_dir,
                env,
                cwd,
                invoke,
                resolve_arg(repo_root, path_raw),
                kind,
                mode,
            )
            expected_code = int(expected_exit)
            if completed.returncode != expected_code:
                raise SystemExit(
                    f"bootstrap artifact exit drift for {label}: expected {expected_code}, got {completed.returncode}"
                    f"\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
                )
            expected = canonicalize_golden((repo_root / expected_path).read_text(encoding="utf-8"))
            actual = canonicalize_golden(completed.stdout)
            if actual != expected:
                raise SystemExit(
                    f"bootstrap artifact output drift for {label}"
                    f"\nexpected:\n{expected}\nactual:\n{actual}"
                )
            artifact.unlink()
            report_lines.append(f"{label}|exit={expected_code}|ok")

    if args.report_out:
        pathlib.Path(args.report_out).write_text("\n".join(report_lines) + "\n", encoding="utf-8")
    print("selfhost bootstrap artifact ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

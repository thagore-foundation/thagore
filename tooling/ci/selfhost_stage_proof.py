#!/usr/bin/env python3
"""Self-hosting bootstrap proof — full Trusting-Trust gate.

Three independent checks must pass for the proof to succeed:

  (A) Behavioural equivalence: stage2 and stage3 produce identical output
      on every fixture in the proof manifest. This is the original
      Phase 5 check.

  (B) Byte-identical artifact equivalence: SHA256(stage2.exe) ==
      SHA256(stage3.exe) after PE normalization (zeroing the COFF
      TimeDateStamp and other non-deterministic header fields). This is
      the strict Trusting-Trust criterion: if stage2 and stage3 are
      bit-identical, they really are the same compiler.

  (C) Same-machine determinism: rebuilding the entire chain a second
      time on the same machine produces an identical (normalized)
      stage2 hash. Catches any hidden non-determinism not covered by
      the PE normalizer.

The standard byte-level hash comparison without normalization is
unreliable on Windows/MinGW because the GNU linker embeds a
non-deterministic PE TimeDateStamp on every link. `-Wl,/Brepro` is an
MSVC-linker option that MinGW ld silently ignores. The PE normalizer
in `tooling/ci/pe_normalize.py` is the post-link step that makes hash
equality possible.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import subprocess
import sys
import tempfile

import pe_normalize


def load_manifest(path: pathlib.Path) -> list[tuple[str, str, str]]:
    """Parse manifest. Format: fixture | command [ | artifact ].
    Artifact (3rd column) is required for build/run, empty for analyze etc."""
    rows: list[tuple[str, str, str]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) == 2:
            rows.append((parts[0], parts[1], ""))
        elif len(parts) == 3:
            rows.append((parts[0], parts[1], parts[2]))
    return rows


def build_binary(
    builder: pathlib.Path,
    compiler_source: str,
    artifact_name: str,
    scratch_dir: pathlib.Path,
    cwd: pathlib.Path,
    env: dict[str, str],
) -> pathlib.Path:
    cmd = [str(builder), "build", compiler_source, artifact_name]
    result = subprocess.run(
        cmd, cwd=cwd, env=env, check=False,
        capture_output=True, text=True, encoding="utf-8",
    )
    if result.returncode != 0:
        raise SystemExit(
            f"build failed: {builder.name} build {compiler_source}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    artifact = scratch_dir / artifact_name
    if not artifact.exists():
        raise SystemExit(f"artifact not produced: {artifact}")
    return artifact


def run_fixture(
    binary: pathlib.Path,
    command: str,
    fixture_abs: str,
    artifact: str,
    cwd: pathlib.Path,
    env: dict[str, str],
) -> str:
    cmd = [str(binary), command, fixture_abs]
    if artifact:
        cmd.append(artifact)
    result = subprocess.run(
        cmd, cwd=cwd, env=env, check=False,
        capture_output=True, text=True, encoding="utf-8",
    )
    return result.stdout.replace("\r\n", "\n").strip()


def sha256_normalized(binary: pathlib.Path) -> str:
    """Normalize the PE in place, then return its SHA256."""
    pe_normalize.normalize_pe(binary)
    return hashlib.sha256(binary.read_bytes()).hexdigest()


def build_full_chain(
    stage0: pathlib.Path,
    compiler_source: str,
    scratch_root: pathlib.Path,
    cwd: pathlib.Path,
    make_env,
    label: str,
) -> tuple[pathlib.Path, pathlib.Path, pathlib.Path]:
    """Build stage1 → stage2 → stage3 under scratch_root/label."""
    base = scratch_root / label
    base.mkdir()
    s1_dir = base / "s1"
    s1_dir.mkdir()
    s2_dir = base / "s2"
    s2_dir.mkdir()
    s3_dir = base / "s3"
    s3_dir.mkdir()

    print(f"[{label}] building stage1 (stage0 -> compiler.tg)...")
    stage1 = build_binary(stage0, compiler_source, "stage1.exe", s1_dir, cwd, make_env(s1_dir))
    print(f"[{label}]   ok: {stage1.name}")

    print(f"[{label}] building stage2 (stage1 -> compiler.tg)...")
    stage2 = build_binary(stage1, compiler_source, "stage2.exe", s2_dir, cwd, make_env(s2_dir))
    print(f"[{label}]   ok: {stage2.name}")

    print(f"[{label}] building stage3 (stage2 -> compiler.tg)...")
    stage3 = build_binary(stage2, compiler_source, "stage3.exe", s3_dir, cwd, make_env(s3_dir))
    print(f"[{label}]   ok: {stage3.name}")

    return stage1, stage2, stage3


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--stage0-bin", required=True,
                        help="selfhost compiler binary (built by host thagc)")
    parser.add_argument("--host-thagc", default="",
                        help="host LLVM compiler for fallback inside staged builds")
    parser.add_argument("--compiler-source",
                        default="bootstrap/selfhost/frontend/compiler.tg")
    parser.add_argument("--manifest",
                        default="bootstrap/selfhost/corpus/stage-proof-fixtures.txt",
                        help="fixture|command manifest for behavioural equivalence checks")
    parser.add_argument("--report-out", default="")
    parser.add_argument("--skip-rerun", action="store_true",
                        help="skip the determinism rerun check (faster, less strict)")
    args = parser.parse_args()

    repo_root = pathlib.Path(args.repo_root).resolve()
    stage0 = pathlib.Path(args.stage0_bin).resolve()
    compiler_source = (repo_root / args.compiler_source).resolve().as_posix()
    host_thagc = pathlib.Path(args.host_thagc).resolve() if args.host_thagc else None
    manifest_path = repo_root / args.manifest

    with tempfile.TemporaryDirectory(prefix="selfhost-stage-proof-") as scratch_root_str:
        scratch_root = pathlib.Path(scratch_root_str)

        def make_env(tmp_dir: pathlib.Path) -> dict[str, str]:
            env = dict(os.environ)
            env["THAGORE_SELFHOST_TMP"] = str(tmp_dir)
            if host_thagc is not None:
                env["THAGORE_STAGE0"] = host_thagc.name
                env["PATH"] = f"{host_thagc.parent}{os.pathsep}{env.get('PATH', '')}"
            return env

        cwd = repo_root

        print(f"stage0 : {stage0.name}")
        print(f"source : {compiler_source}")
        print()

        # ── Build run #1: stage1 → stage2 → stage3 ──
        stage1_a, stage2_a, stage3_a = build_full_chain(
            stage0, compiler_source, scratch_root, cwd, make_env, "run1"
        )

        # ── (B) Byte-identical hash equivalence: stage2 == stage3 ──
        print()
        print("normalizing stage binaries (PE TimeDateStamp + checksums)...")
        stage1_a_hash = sha256_normalized(stage1_a)
        stage2_a_hash = sha256_normalized(stage2_a)
        stage3_a_hash = sha256_normalized(stage3_a)
        print(f"  stage1 sha256: {stage1_a_hash}")
        print(f"  stage2 sha256: {stage2_a_hash}")
        print(f"  stage3 sha256: {stage3_a_hash}")
        if stage2_a_hash != stage3_a_hash:
            raise SystemExit(
                f"FAIL (B): stage2 != stage3 at byte level\n"
                f"  stage2 sha256: {stage2_a_hash}\n"
                f"  stage3 sha256: {stage3_a_hash}\n"
                f"  This means stage2 and stage3 are NOT bit-identical even after\n"
                f"  PE normalization. There is non-determinism beyond the COFF\n"
                f"  TimeDateStamp; extend pe_normalize.py to cover it."
            )
        print("  (B) ok: stage2 == stage3 at byte level (Trusting-Trust gate)")

        # ── (A) Behavioural equivalence: stage2 == stage3 on every fixture ──
        rows = load_manifest(manifest_path)
        if not rows:
            raise SystemExit(f"empty proof manifest: {manifest_path}")

        print()
        print(f"checking behavioural equivalence on {len(rows)} fixtures...")
        report_lines: list[str] = []
        for fixture, command, artifact in rows:
            fixture_abs = (repo_root / fixture).resolve().as_posix()
            out2 = run_fixture(stage2_a, command, fixture_abs, artifact, cwd, make_env(scratch_root / "run1" / "s2"))
            out3 = run_fixture(stage3_a, command, fixture_abs, artifact, cwd, make_env(scratch_root / "run1" / "s3"))
            if out2 != out3:
                raise SystemExit(
                    f"FAIL (A): stage2 != stage3 for {fixture} (command={command})\n"
                    f"--- stage2 ---\n{out2}\n--- stage3 ---\n{out3}"
                )
            report_lines.append(f"{fixture}|{command}|{artifact}|ok")
        print(f"  (A) ok: all {len(rows)} fixtures match stage2 vs stage3")

        # ── (C) Same-machine determinism: rebuild and compare hashes ──
        if not args.skip_rerun:
            print()
            print("rebuilding chain to verify same-machine determinism...")
            stage1_b, stage2_b, stage3_b = build_full_chain(
                stage0, compiler_source, scratch_root, cwd, make_env, "run2"
            )
            stage1_b_hash = sha256_normalized(stage1_b)
            stage2_b_hash = sha256_normalized(stage2_b)
            stage3_b_hash = sha256_normalized(stage3_b)
            print(f"  stage1 sha256 (run2): {stage1_b_hash}")
            print(f"  stage2 sha256 (run2): {stage2_b_hash}")
            print(f"  stage3 sha256 (run2): {stage3_b_hash}")
            mismatches = []
            if stage1_a_hash != stage1_b_hash:
                mismatches.append(f"stage1: {stage1_a_hash} vs {stage1_b_hash}")
            if stage2_a_hash != stage2_b_hash:
                mismatches.append(f"stage2: {stage2_a_hash} vs {stage2_b_hash}")
            if stage3_a_hash != stage3_b_hash:
                mismatches.append(f"stage3: {stage3_a_hash} vs {stage3_b_hash}")
            if mismatches:
                raise SystemExit(
                    "FAIL (C): same-machine determinism failed\n  "
                    + "\n  ".join(mismatches)
                )
            print("  (C) ok: rerun produced identical stage hashes")

        payload = "\n".join(report_lines) + "\n"
        if args.report_out:
            pathlib.Path(args.report_out).write_text(payload, encoding="utf-8")

        print()
        print("=" * 60)
        print("selfhost bootstrap proof OK")
        print("  (A) behavioural equivalence  ok")
        print("  (B) byte-identical stage2 == stage3  ok")
        if not args.skip_rerun:
            print("  (C) same-machine determinism  ok")
        else:
            print("  (C) same-machine determinism  SKIPPED")
        print("=" * 60)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

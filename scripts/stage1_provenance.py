#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
from typing import Dict, List, Tuple


SCHEMA = "thagore.stage1.provenance.v1"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalize_path(path: str) -> Path:
    return Path(path).resolve()


def artifact_record(path: Path) -> Dict[str, object]:
    if not path.is_file():
        raise FileNotFoundError(f"missing file: {path}")
    return {
        "name": path.name,
        "path": str(path),
        "size_bytes": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def parse_manifest(path: Path) -> Dict[str, str]:
    if not path.is_file():
        raise FileNotFoundError(f"missing manifest: {path}")
    current_artifact = ""
    hashes: Dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("artifact="):
            current_artifact = line.split("=", 1)[1].strip()
            continue
        if line.startswith("sha256="):
            if not current_artifact:
                continue
            sha = line.split("=", 1)[1].strip().lower()
            hashes[Path(current_artifact).name] = sha
            current_artifact = ""
    return hashes


def compare_hashes(
    source: Dict[str, str], observed: Dict[str, str], source_name: str
) -> List[str]:
    errors: List[str] = []
    for name, expected_hash in source.items():
        got = observed.get(name, "")
        if got != expected_hash:
            errors.append(
                f"{source_name} mismatch for {name}: expected={expected_hash} got={got or '<missing>'}"
            )
    return errors


def cmd_capture(args: argparse.Namespace) -> int:
    asset_paths = [normalize_path(p) for p in args.asset]
    assets = [artifact_record(p) for p in asset_paths]
    observed = {a["name"]: str(a["sha256"]) for a in assets}

    manifest_path = normalize_path(args.manifest) if args.manifest else None
    manifest_entries: Dict[str, str] = {}
    manifest_record: Dict[str, object] | None = None
    if manifest_path:
        manifest_entries = parse_manifest(manifest_path)
        manifest_record = artifact_record(manifest_path)
        manifest_record["entry_count"] = len(manifest_entries)

    stage_trace_record: Dict[str, object] | None = None
    if args.stage_trace:
        stage_trace_path = normalize_path(args.stage_trace)
        stage_trace_record = artifact_record(stage_trace_path)

    errors: List[str] = []
    if manifest_entries:
        errors.extend(compare_hashes(manifest_entries, observed, "manifest"))
    if errors and not args.allow_manifest_mismatch:
        for err in errors:
            print(f"CRITICAL: {err}")
        return 1

    payload: Dict[str, object] = {
        "schema": SCHEMA,
        "generated_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "metadata": {
            "seed_tag": args.seed_tag,
            "base_seed_tag": args.base_seed_tag,
            "runner_os": args.runner_os,
            "asset_tag": args.asset_tag,
            "arch": args.arch,
            "run_id": args.run_id,
            "run_attempt": args.run_attempt,
            "commit_sha": args.commit_sha,
            "repository": args.repository,
        },
        "artifacts": assets,
        "validation": {
            "manifest_mismatches": errors,
        },
    }
    if manifest_record is not None:
        payload["manifest"] = {
            **manifest_record,
            "entries": manifest_entries,
        }
    if stage_trace_record is not None:
        payload["stage_trace"] = stage_trace_record

    output_path = normalize_path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")
    print(f"Wrote provenance: {output_path}")
    return 0


def load_provenance(path: Path) -> Dict[str, object]:
    if not path.is_file():
        raise FileNotFoundError(f"missing provenance file: {path}")
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema") != SCHEMA:
        raise ValueError(f"unsupported schema: {data.get('schema')}")
    return data


def cmd_verify(args: argparse.Namespace) -> int:
    provenance_path = normalize_path(args.provenance)
    provenance = load_provenance(provenance_path)

    artifacts = provenance.get("artifacts", [])
    if not isinstance(artifacts, list):
        raise ValueError("invalid provenance artifacts")
    expected_from_provenance: Dict[str, str] = {}
    for entry in artifacts:
        if not isinstance(entry, dict):
            continue
        name = str(entry.get("name", ""))
        sha = str(entry.get("sha256", "")).lower()
        if name and sha:
            expected_from_provenance[name] = sha

    if args.asset:
        asset_paths = [normalize_path(p) for p in args.asset]
    else:
        asset_paths = []
        for entry in artifacts:
            if not isinstance(entry, dict):
                continue
            path = str(entry.get("path", ""))
            if path:
                asset_paths.append(Path(path))

    observed: Dict[str, str] = {}
    for path in asset_paths:
        if not path.is_file():
            print(f"CRITICAL: missing file: {path}")
            return 1
        observed[path.name] = sha256_file(path)

    errors: List[str] = []
    if args.asset:
        expected_subset: Dict[str, str] = {}
        for name in observed:
            if name not in expected_from_provenance:
                errors.append(f"provenance missing artifact entry for {name}")
            else:
                expected_subset[name] = expected_from_provenance[name]
        errors.extend(compare_hashes(expected_subset, observed, "provenance"))
    else:
        errors.extend(compare_hashes(expected_from_provenance, observed, "provenance"))

    if args.manifest:
        manifest_entries = parse_manifest(normalize_path(args.manifest))
        errors.extend(compare_hashes(manifest_entries, observed, "manifest"))

    if errors:
        for err in errors:
            print(f"CRITICAL: {err}")
        return 1

    print("Stage1 provenance verification passed.")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Capture/verify Stage1 seed provenance")
    sub = parser.add_subparsers(dest="command", required=True)

    capture = sub.add_parser("capture", help="Capture provenance metadata and hashes")
    capture.add_argument("--output", required=True, help="Output provenance JSON path")
    capture.add_argument("--manifest", help="Seed promotion manifest path")
    capture.add_argument("--stage-trace", help="Stage trace file path")
    capture.add_argument("--asset", action="append", required=True, help="Asset file path")
    capture.add_argument("--seed-tag", default="")
    capture.add_argument("--base-seed-tag", default="")
    capture.add_argument("--runner-os", default="")
    capture.add_argument("--asset-tag", default="")
    capture.add_argument("--arch", default="")
    capture.add_argument("--run-id", default="")
    capture.add_argument("--run-attempt", default="")
    capture.add_argument("--commit-sha", default="")
    capture.add_argument("--repository", default="")
    capture.add_argument(
        "--allow-manifest-mismatch",
        action="store_true",
        help="Allow writing provenance even when manifest and assets mismatch",
    )
    capture.set_defaults(func=cmd_capture)

    verify = sub.add_parser("verify", help="Verify assets against provenance and manifest")
    verify.add_argument("--provenance", required=True, help="Provenance JSON path")
    verify.add_argument("--manifest", help="Seed promotion manifest path")
    verify.add_argument("--asset", action="append", help="Asset file path")
    verify.set_defaults(func=cmd_verify)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())

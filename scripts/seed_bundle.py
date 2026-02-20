#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
from typing import Dict, List


SCHEMA = "thagore.seed.bundle.v1"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalize_path(path: str) -> Path:
    return Path(path).resolve()


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
            hashes[Path(current_artifact).name] = line.split("=", 1)[1].strip().lower()
            current_artifact = ""
    return hashes


def _artifact_kind(name: str) -> str:
    lowered = name.lower()
    if lowered.startswith("thagore-stage1"):
        return "stage1"
    if lowered.startswith("thagore-runtime"):
        return "runtime"
    if lowered.startswith("seed-promotion-manifest-"):
        return "manifest"
    if lowered.startswith("seed-stage1-provenance-"):
        return "provenance"
    if lowered.startswith("seed-stage-trace-") or lowered.startswith("release-stage-trace-"):
        return "trace"
    if lowered.startswith("seed-bundle-"):
        return "bundle"
    return "other"


def _canonical_hash_payload(rows: List[Dict[str, str]]) -> str:
    parts = [f"{r['kind']}|{r['name']}|{r['sha256']}|{r['size_bytes']}" for r in rows]
    parts.sort()
    return "\n".join(parts)


def compute_bundle_id(rows: List[Dict[str, str]]) -> str:
    payload = _canonical_hash_payload(rows).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def cmd_capture(args: argparse.Namespace) -> int:
    artifact_paths = [normalize_path(p) for p in args.asset]
    rows: List[Dict[str, str]] = []
    for path in artifact_paths:
        if not path.is_file():
            raise FileNotFoundError(f"missing file: {path}")
        rows.append(
            {
                "kind": _artifact_kind(path.name),
                "name": path.name,
                "path": str(path),
                "sha256": sha256_file(path),
                "size_bytes": str(path.stat().st_size),
            }
        )

    bundle_id = compute_bundle_id(rows)
    payload = {
        "schema": SCHEMA,
        "generated_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "metadata": {
            "seed_tag": args.seed_tag,
            "asset_tag": args.asset_tag,
            "runner_os": args.runner_os,
            "arch": args.arch,
            "commit_sha": args.commit_sha,
            "run_id": args.run_id,
            "run_attempt": args.run_attempt,
            "repository": args.repository,
            "bundle_id": bundle_id,
        },
        "artifacts": rows,
    }
    out = normalize_path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")
    print(f"Wrote seed bundle: {out}")
    print(f"bundle_id={bundle_id}")
    return 0


def _load_json(path: Path) -> Dict[str, object]:
    if not path.is_file():
        raise FileNotFoundError(f"missing file: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def _validate_required_kinds(rows: List[Dict[str, str]]) -> List[str]:
    kinds = {str(r.get("kind", "")) for r in rows}
    required = {"stage1", "runtime", "manifest", "provenance", "trace"}
    missing = [k for k in sorted(required) if k not in kinds]
    return [f"missing required artifact kind: {k}" for k in missing]


def cmd_verify(args: argparse.Namespace) -> int:
    bundle_path = normalize_path(args.bundle)
    bundle = _load_json(bundle_path)
    if bundle.get("schema") != SCHEMA:
        raise ValueError(f"unsupported bundle schema: {bundle.get('schema')}")

    errors: List[str] = []
    metadata = bundle.get("metadata", {})
    if not isinstance(metadata, dict):
        raise ValueError("invalid bundle metadata")
    bundle_commit = str(metadata.get("commit_sha", "")).strip()
    bundle_id = str(metadata.get("bundle_id", "")).strip().lower()
    if len(bundle_commit) < 7:
        errors.append("bundle metadata.commit_sha is missing")
    if len(bundle_id) != 64:
        errors.append("bundle metadata.bundle_id is invalid")

    artifacts = bundle.get("artifacts", [])
    if not isinstance(artifacts, list):
        raise ValueError("invalid bundle artifacts")

    normalized_rows: List[Dict[str, str]] = []
    for entry in artifacts:
        if not isinstance(entry, dict):
            continue
        name = str(entry.get("name", "")).strip()
        kind = str(entry.get("kind", "")).strip()
        sha = str(entry.get("sha256", "")).strip().lower()
        path_text = str(entry.get("path", "")).strip()
        size_text = str(entry.get("size_bytes", "")).strip()
        if not name or not kind or not sha or not path_text:
            errors.append(f"incomplete artifact entry in bundle: {entry}")
            continue
        path = normalize_path(path_text)
        if not path.is_file():
            errors.append(f"bundle artifact missing on disk: {path}")
            continue
        observed_sha = sha256_file(path)
        if observed_sha != sha:
            errors.append(f"bundle hash mismatch for {name}: expected={sha} got={observed_sha}")
        observed_size = str(path.stat().st_size)
        if size_text and observed_size != size_text:
            errors.append(f"bundle size mismatch for {name}: expected={size_text} got={observed_size}")
        normalized_rows.append(
            {
                "kind": kind,
                "name": name,
                "sha256": sha,
                "size_bytes": observed_size,
            }
        )

    errors.extend(_validate_required_kinds(normalized_rows))
    recomputed_bundle_id = compute_bundle_id(normalized_rows)
    if len(bundle_id) == 64 and bundle_id != recomputed_bundle_id:
        errors.append(
            f"bundle_id mismatch: expected={bundle_id} got={recomputed_bundle_id}"
        )

    if args.manifest:
        manifest_entries = parse_manifest(normalize_path(args.manifest))
        by_name = {row["name"]: row["sha256"] for row in normalized_rows}
        for name, expected in manifest_entries.items():
            got = by_name.get(name, "")
            if got != expected:
                errors.append(
                    f"manifest mismatch for {name}: expected={expected} got={got or '<missing>'}"
                )

    if args.provenance:
        prov = _load_json(normalize_path(args.provenance))
        prov_meta = prov.get("metadata", {})
        if isinstance(prov_meta, dict):
            prov_commit = str(prov_meta.get("commit_sha", "")).strip()
            prov_bundle = str(prov_meta.get("bundle_id", "")).strip().lower()
            if bundle_commit and prov_commit != bundle_commit:
                errors.append(
                    f"commit_sha mismatch between bundle and provenance: {bundle_commit} vs {prov_commit}"
                )
            if len(bundle_id) == 64 and prov_bundle != bundle_id:
                errors.append(
                    f"bundle_id mismatch between bundle and provenance: {bundle_id} vs {prov_bundle}"
                )
        else:
            errors.append("invalid provenance metadata")

    if errors:
        for err in errors:
            print(f"CRITICAL: {err}")
        return 1

    print("Seed bundle verification passed.")
    print(f"bundle_id={bundle_id or recomputed_bundle_id}")
    print(f"commit_sha={bundle_commit}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Capture/verify atomic seed bundle")
    sub = parser.add_subparsers(dest="command", required=True)

    capture = sub.add_parser("capture", help="Capture seed bundle metadata")
    capture.add_argument("--output", required=True)
    capture.add_argument("--asset", action="append", required=True)
    capture.add_argument("--seed-tag", required=True)
    capture.add_argument("--asset-tag", required=True)
    capture.add_argument("--runner-os", required=True)
    capture.add_argument("--arch", required=True)
    capture.add_argument("--commit-sha", required=True)
    capture.add_argument("--run-id", required=True)
    capture.add_argument("--run-attempt", required=True)
    capture.add_argument("--repository", required=True)
    capture.set_defaults(func=cmd_capture)

    verify = sub.add_parser("verify", help="Verify seed bundle metadata")
    verify.add_argument("--bundle", required=True)
    verify.add_argument("--manifest")
    verify.add_argument("--provenance")
    verify.set_defaults(func=cmd_verify)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Generate release manifest and checksums from packaged artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--dist-dir", type=Path, required=True)
    parser.add_argument("--targets-file", type=Path, required=True)
    parser.add_argument("--tiers", default="stable,extended")
    parser.add_argument("--require-complete", action="store_true")
    parser.add_argument("--drago-version", default="")
    parser.add_argument("--drago-tag", default="")
    return parser.parse_args()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    args = parse_args()
    targets = json.loads(args.targets_file.read_text(encoding="utf-8"))
    selected_tiers = {tier.strip() for tier in args.tiers.split(",") if tier.strip()}
    base_url = f"https://github.com/{args.repo}/releases/download/{args.tag}"
    artifacts = []
    checksum_lines = []
    missing = []

    for target in targets:
        if target["tier"] not in selected_tiers:
            continue
        extension = target["archive"]
        filename = f"thagore-{args.version}-{target['target']}.{extension}"
        path = args.dist_dir / filename
        available = path.is_file()
        if not available:
            missing.append(target["target"])
        checksum = file_sha256(path) if available else ""
        if available:
            checksum_lines.append(f"{checksum}  {filename}")
        artifacts.append(
            {
                "target": target["target"],
                "tier": target["tier"],
                "os": target["os"],
                "arch": target["arch"],
                "abi": target["abi"],
                "archive": filename,
                "format": extension,
                "automated": target["automated"],
                "available": available,
                "sha256": checksum,
                "size": path.stat().st_size if available else 0,
                "url": f"{base_url}/{filename}" if available else "",
            }
        )

    checksums_path = args.dist_dir / f"SHA256SUMS-{args.tag}.txt"
    checksums_path.write_text("\n".join(checksum_lines) + ("\n" if checksum_lines else ""), encoding="utf-8")

    if args.require_complete and missing:
        raise SystemExit(f"missing release artifacts for: {', '.join(missing)}")

    manifest = {
        "schema_version": 1,
        "repository": args.repo,
        "version": args.version,
        "tag": args.tag,
        "published_at": datetime.now(timezone.utc).isoformat(),
        "tiers": sorted(selected_tiers),
        "artifacts": artifacts,
        "checksums": {
            "file": checksums_path.name,
            "url": f"{base_url}/{checksums_path.name}",
        },
    }
    if args.drago_version:
        manifest["companion"] = {
            "drago": {
                "version": args.drago_version,
                "tag": args.drago_tag or args.drago_version,
                "repository": "thagore-foundation/drago",
                "source_archive_url": f"https://github.com/thagore-foundation/drago/archive/refs/tags/{args.drago_tag or args.drago_version}.tar.gz",
            }
        }

    manifest_path = args.dist_dir / f"release-manifest-{args.tag}.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(manifest_path)
    print(checksums_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

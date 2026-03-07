#!/usr/bin/env python3
"""Package a Thagore toolchain archive for one target triple."""

from __future__ import annotations

import argparse
import json
import shutil
import tarfile
import tempfile
import zipfile
from pathlib import Path


WINDOWS_TARGET_MARKER = "windows"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--archive-format", choices=["tar.gz", "zip"], required=True)
    parser.add_argument("--support-tier", required=True)
    parser.add_argument("--bin-dir", required=True, type=Path)
    parser.add_argument("--stdlib-dir", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--readme", type=Path, default=Path("README.md"))
    parser.add_argument("--license", type=Path, default=Path("LICENSE"))
    return parser.parse_args()


def binary_names(target: str) -> list[str]:
    if WINDOWS_TARGET_MARKER in target:
        return ["thagc.exe", "thagore.exe", "thagore-fmt.exe", "thagore-lsp.exe"]
    return ["thagc", "thagore", "thagore-fmt", "thagore-lsp"]


def copy_tree(source: Path, destination: Path) -> None:
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(source, destination)


def make_archive(source_root: Path, output: Path, archive_format: str) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    if archive_format == "tar.gz":
        with tarfile.open(output, "w:gz") as archive:
            archive.add(source_root, arcname=source_root.name)
        return

    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(source_root.rglob("*")):
            archive.write(path, arcname=path.relative_to(source_root.parent))


def main() -> int:
    args = parse_args()

    root_name = f"thagore-{args.version}-{args.target}"
    archive_name = f"{root_name}.{args.archive_format}"
    with tempfile.TemporaryDirectory(prefix="thagore-release-") as temp_dir:
        staging_root = Path(temp_dir) / root_name
        bin_root = staging_root / "bin"
        share_root = staging_root / "share" / "thagore"
        stdlib_root = share_root / "stdlib"
        bin_root.mkdir(parents=True, exist_ok=True)
        stdlib_root.mkdir(parents=True, exist_ok=True)

        for binary in binary_names(args.target):
            shutil.copy2(args.bin_dir / binary, bin_root / binary)

        copy_tree(args.stdlib_dir, stdlib_root)

        metadata = {
            "version": args.version,
            "tag": args.tag,
            "target": args.target,
            "tier": args.support_tier,
            "layout": {
                "bin": "bin",
                "stdlib": "share/thagore/stdlib",
            },
        }
        (share_root / "version.json").write_text(
            json.dumps(metadata, indent=2) + "\n",
            encoding="utf-8",
        )

        if args.readme.exists():
            shutil.copy2(args.readme, staging_root / args.readme.name)
        if args.license.exists():
            shutil.copy2(args.license, staging_root / args.license.name)

        output_path = args.out_dir / archive_name
        make_archive(staging_root, output_path, args.archive_format)
        print(output_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

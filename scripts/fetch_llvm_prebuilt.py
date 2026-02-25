#!/usr/bin/env python3
"""
fetch_llvm_prebuilt.py — Download LLVM prebuilt binaries for a target triple.

Downloads clang, lld, and libLLVM from the official LLVM GitHub releases
(github.com/llvm/llvm-project/releases) into <out_dir>/llvm/{bin,lib}/.

Usage:
    python scripts/fetch_llvm_prebuilt.py \
        --triple x86_64-unknown-linux-gnu \
        --llvm-version 21.1.8 \
        --out-dir targets/packs/x86_64-unknown-linux-gnu/llvm
"""

import argparse
import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

LLVM_GITHUB_BASE = "https://github.com/llvm/llvm-project/releases/download"

# LLVM prebuilt asset names per target triple
# Pattern: llvmorg-{version}/{asset_name}
TRIPLE_TO_ASSET = {
    "x86_64-unknown-linux-gnu":  "LLVM-{version}-Linux-X64.tar.xz",
    "aarch64-unknown-linux-gnu": "LLVM-{version}-Linux-ARM64.tar.xz",
    "x86_64-apple-darwin":       "LLVM-{version}-macOS-X64.tar.xz",
    "aarch64-apple-darwin":      "LLVM-{version}-macOS-ARM64.tar.xz",
    "x86_64-pc-windows-msvc":    "LLVM-{version}-Windows-X64.tar.xz",
}

# Compact binary set — only what thagc needs to compile and link (~90 MB target pack).
# clang / clang-{major}: C/IR compiler + driver
# ld.lld / ld64.lld:     linker (ELF / MachO)
# lld-link:              PE/COFF linker (Windows)
# llc:                   LLVM IR → object (useful for IR debugging)
# llvm-ar:               archive utility (static lib creation)
BINS_TO_EXTRACT = {
    "linux":   ["clang", "clang-{major}", "ld.lld", "llc", "llvm-ar"],
    "macos":   ["clang", "clang-{major}", "ld.lld", "ld64.lld", "llc", "llvm-ar"],
    "windows": ["clang.exe", "clang-{major}.exe", "lld-link.exe", "llc.exe", "llvm-ar.exe"],
}

# Shared libraries to extract per platform
LIBS_TO_EXTRACT = {
    "linux":   ["libLLVM-{major}.so", "libLLVM.so.{major}", "libclang.so.{major}"],
    "macos":   ["libLLVM.dylib", "libclang.dylib"],
    "windows": ["LLVM-C.dll"],
}


def detect_platform_from_triple(triple: str) -> str:
    if "linux" in triple:
        return "linux"
    if "darwin" in triple or "macos" in triple:
        return "macos"
    if "windows" in triple:
        return "windows"
    raise ValueError(f"Cannot detect platform from triple: {triple}")


def asset_url(triple: str, version: str) -> str:
    pattern = TRIPLE_TO_ASSET.get(triple)
    if not pattern:
        raise ValueError(
            f"No LLVM prebuilt asset known for triple: {triple}\n"
            f"Supported triples: {', '.join(TRIPLE_TO_ASSET)}"
        )
    asset = pattern.format(version=version)
    tag = f"llvmorg-{version}"
    return f"{LLVM_GITHUB_BASE}/{tag}/{asset}", asset


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def download_with_progress(url: str, dest: Path) -> None:
    print(f"[fetch_llvm] Downloading: {url}")
    print(f"[fetch_llvm]          -> {dest}")
    req = urllib.request.Request(url, headers={"User-Agent": "thagore-fetch-llvm/1.0"})
    with urllib.request.urlopen(req) as resp, open(dest, "wb") as out:
        total = int(resp.headers.get("Content-Length", 0))
        downloaded = 0
        bar_width = 40
        while chunk := resp.read(65536):
            out.write(chunk)
            downloaded += len(chunk)
            if total:
                pct = downloaded / total
                filled = int(bar_width * pct)
                bar = "#" * filled + "-" * (bar_width - filled)
                print(f"\r  [{bar}] {pct*100:.1f}% ({downloaded//1024//1024}MB/{total//1024//1024}MB)",
                      end="", flush=True)
    if total:
        print()  # newline after progress bar
    print(f"[fetch_llvm] Download complete: {dest.stat().st_size // 1024 // 1024}MB")


def extract_selected_files(archive: Path, out_dir: Path, platform: str, major: str) -> None:
    """Extract only the needed bins/libs from the LLVM tarball."""
    bins = [b.format(major=major) for b in BINS_TO_EXTRACT.get(platform, [])]
    libs = [l.format(major=major) for l in LIBS_TO_EXTRACT.get(platform, [])]
    wanted_names = set(bins + libs)

    bin_dir = out_dir / "bin"
    lib_dir = out_dir / "lib"
    bin_dir.mkdir(parents=True, exist_ok=True)
    lib_dir.mkdir(parents=True, exist_ok=True)

    print(f"[fetch_llvm] Extracting selected files from {archive.name} ...")
    extracted = []

    with tarfile.open(archive, "r:xz") as tf:
        for member in tf.getmembers():
            name = Path(member.name).name
            if not member.isfile():
                continue
            # Match bin or lib files by name
            if name in wanted_names or any(name.startswith(b.split("{")[0]) for b in bins + libs if "{" not in b):
                # Determine destination
                path_parts = member.name.split("/")
                if "bin" in path_parts:
                    dest_dir = bin_dir
                elif "lib" in path_parts:
                    dest_dir = lib_dir
                else:
                    continue
                f = tf.extractfile(member)
                if f is None:
                    continue
                dest_file = dest_dir / name
                with open(dest_file, "wb") as out:
                    out.write(f.read())
                # Preserve executable bit for bins
                if "bin" in path_parts:
                    dest_file.chmod(0o755)
                extracted.append(str(dest_file))
                print(f"  extracted: {dest_dir.name}/{name}")

    if not extracted:
        print("[fetch_llvm] WARNING: no files matched — falling back to full bin/ and lib/ extraction")
        _extract_full_bin_lib(archive, out_dir)
    else:
        print(f"[fetch_llvm] Extracted {len(extracted)} file(s)")


def _extract_full_bin_lib(archive: Path, out_dir: Path) -> None:
    """Fallback: extract entire bin/ and lib/ trees from the tarball."""
    bin_dir = out_dir / "bin"
    lib_dir = out_dir / "lib"
    bin_dir.mkdir(parents=True, exist_ok=True)
    lib_dir.mkdir(parents=True, exist_ok=True)

    with tarfile.open(archive, "r:xz") as tf:
        for member in tf.getmembers():
            if not member.isfile():
                continue
            parts = member.name.split("/")
            if "bin" in parts:
                idx = parts.index("bin")
                if idx == len(parts) - 2:
                    dest = bin_dir / parts[-1]
                    f = tf.extractfile(member)
                    if f:
                        dest.write_bytes(f.read())
                        dest.chmod(0o755)
            elif "lib" in parts:
                idx = parts.index("lib")
                if idx == len(parts) - 2:
                    dest = lib_dir / parts[-1]
                    f = tf.extractfile(member)
                    if f:
                        dest.write_bytes(f.read())


def create_symlinks(out_dir: Path, major: str, platform: str) -> None:
    """Create version-neutral symlinks (clang-21 -> clang, etc.)."""
    if platform == "windows":
        return  # no symlinks on Windows
    bin_dir = out_dir / "bin"
    pairs = [
        (f"clang-{major}", "clang"),
        (f"ld.lld", "lld"),
    ]
    for src_name, link_name in pairs:
        src = bin_dir / src_name
        link = bin_dir / link_name
        if src.exists() and not link.exists():
            link.symlink_to(src_name)
            print(f"[fetch_llvm] symlink: {link_name} -> {src_name}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Download LLVM prebuilt binaries for a target triple"
    )
    parser.add_argument("--triple", required=True,
                        help="Target triple (e.g. x86_64-unknown-linux-gnu)")
    parser.add_argument("--llvm-version", required=True,
                        help="LLVM version (e.g. 21.1.8)")
    parser.add_argument("--out-dir", required=True,
                        help="Output directory for llvm/bin and llvm/lib")
    parser.add_argument("--skip-verify", action="store_true",
                        help="Skip SHA256 checksum verification")
    parser.add_argument("--keep-archive", action="store_true",
                        help="Keep downloaded .tar.xz archive after extraction")
    args = parser.parse_args()

    triple = args.triple
    version = args.llvm_version
    out_dir = Path(args.out_dir)
    major = version.split(".")[0]

    plat = detect_platform_from_triple(triple)

    try:
        url, asset_name = asset_url(triple, version)
    except ValueError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1

    # Create temp dir for download
    with tempfile.TemporaryDirectory(prefix="thagore-llvm-") as tmp:
        archive_path = Path(tmp) / asset_name
        try:
            download_with_progress(url, archive_path)
        except Exception as e:
            print(f"ERROR: Download failed: {e}", file=sys.stderr)
            print(f"  URL: {url}", file=sys.stderr)
            print("  Tip: ensure network access or pre-provision LLVM manually.", file=sys.stderr)
            return 1

        print(f"[fetch_llvm] SHA256: {sha256_file(archive_path)}")

        out_dir.mkdir(parents=True, exist_ok=True)
        extract_selected_files(archive_path, out_dir, plat, major)

        if args.keep_archive:
            dest = out_dir / asset_name
            shutil.copy2(archive_path, dest)
            print(f"[fetch_llvm] Kept archive: {dest}")

    create_symlinks(out_dir, major, plat)

    # Verify key binaries exist
    bin_dir = out_dir / "bin"
    expected_bins = {
        "linux":   ["clang", "ld.lld"],
        "macos":   ["clang", "ld.lld"],
        "windows": ["clang.exe", "lld-link.exe"],
    }
    missing = []
    for b in expected_bins.get(plat, []):
        p = bin_dir / b
        if not p.exists():
            missing.append(str(p))
    if missing:
        print(f"[fetch_llvm] WARNING: expected binaries not found: {missing}")
        print("[fetch_llvm] The LLVM package layout may differ from expected. Check bin_dir manually.")
    else:
        print(f"[fetch_llvm] OK: all expected binaries present in {bin_dir}")

    print(f"[fetch_llvm] LLVM {version} for {triple} installed to {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

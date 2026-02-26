#!/usr/bin/env python3
"""
patch_glibc_compat.py — Downgrade GLIBC/GLIBCXX version requirements in ELF binaries.

When GCC 13+ compiles code with C23 features, it emits symbol references like
__isoc23_strtol@GLIBC_2.38, making the binary fail on Ubuntu 22.04 (GLIBC 2.35).

This script binary-patches:
  - GLIBC_2.38 -> GLIBC_2.35  (in .gnu.version_r string table + Vernaux hash)
  - GLIBCXX_3.4.31 -> GLIBCXX_3.4.30  (same)

Constraint: replacement strings must be same length as originals (they are).

Usage:
    python3 patch_glibc_compat.py <input_binary> [output_binary]
    If output_binary is omitted, patches in-place.
"""

import struct
import os
import sys
import shutil


def elf_hash(name: str) -> int:
    """Standard ELF symbol hash (used in .gnu.version_r Vernaux entries)."""
    h = 0
    for c in name.encode():
        h = (h << 4) + c
        g = h & 0xF0000000
        if g:
            h ^= g >> 24
        h &= ~g
    return h & 0xFFFFFFFF


PATCHES = [
    # (old_version_string, new_version_string)
    # Must be same byte length!
    ("GLIBC_2.38",     "GLIBC_2.35"),    # 10 bytes each
    ("GLIBCXX_3.4.31", "GLIBCXX_3.4.30"), # 14 bytes each
]


def patch_binary(data: bytearray, verbose: bool = True) -> int:
    """Apply all patches. Returns number of patches applied."""
    applied = 0
    for old_name, new_name in PATCHES:
        ob = old_name.encode()
        nb = new_name.encode()
        assert len(ob) == len(nb), f"Length mismatch: {old_name!r} vs {new_name!r}"

        # 1. Find and replace version string in .dynstr
        pos = data.find(ob)
        if pos == -1:
            if verbose:
                print(f"  SKIP (not found): {old_name}")
            continue

        data[pos:pos + len(ob)] = nb
        if verbose:
            print(f"  String: {old_name} -> {new_name} at 0x{pos:x}")

        # 2. Find and replace ELF hash in .gnu.version_r Vernaux entry
        old_hash = elf_hash(old_name)
        new_hash = elf_hash(new_name)
        old_hash_bytes = struct.pack("<I", old_hash)
        new_hash_bytes = struct.pack("<I", new_hash)

        hash_pos = data.find(old_hash_bytes)
        if hash_pos != -1:
            data[hash_pos:hash_pos + 4] = new_hash_bytes
            if verbose:
                print(f"  Hash:   0x{old_hash:08x} -> 0x{new_hash:08x} at 0x{hash_pos:x}")
        else:
            if verbose:
                print(f"  Hash:   0x{old_hash:08x} NOT FOUND (may still work)")

        applied += 1
    return applied


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else input_path

    if not os.path.isfile(input_path):
        print(f"Error: {input_path!r} not found", file=sys.stderr)
        sys.exit(1)

    with open(input_path, "rb") as f:
        data = bytearray(f.read())

    # Sanity: must be ELF
    if data[:4] != b"\x7fELF":
        print(f"Error: {input_path!r} is not an ELF binary", file=sys.stderr)
        sys.exit(1)

    print(f"Patching: {input_path}")
    n = patch_binary(data)
    print(f"Applied {n}/{len(PATCHES)} patches.")

    if output_path != input_path:
        shutil.copy2(input_path, output_path)

    with open(output_path, "wb") as f:
        f.write(data)

    # Preserve executable bit
    st = os.stat(input_path)
    os.chmod(output_path, st.st_mode)

    print(f"Written: {output_path}")


if __name__ == "__main__":
    main()

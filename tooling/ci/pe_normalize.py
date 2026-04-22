#!/usr/bin/env python3
"""Zero out non-deterministic PE fields so two builds of the same source
produce byte-identical files.

Fields normalized (zeroed):
  - COFF File Header TimeDateStamp (4 bytes)        — always non-deterministic
  - PE Optional Header CheckSum (4 bytes)           — defensive; depends on file
  - IMAGE_DEBUG_DIRECTORY entries TimeDateStamp     — defensive
  - IMAGE_EXPORT_DIRECTORY TimeDateStamp            — defensive
  - IMAGE_RESOURCE_DIRECTORY TimeDateStamp          — defensive

The MinGW linker (`ld`) embeds a real Unix timestamp in the COFF
TimeDateStamp on every link. `-Wl,/Brepro` is silently ignored (it is an
MSVC linker option). This normalizer is the post-link step that makes
self-hosting hash equality possible on Windows builds.

Safe to run multiple times (idempotent — zeroing already-zero fields is
a no-op).
"""
from __future__ import annotations

import argparse
import hashlib
import pathlib
import struct
import sys


def _u16(buf: bytes, off: int) -> int:
    return struct.unpack_from("<H", buf, off)[0]


def _u32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def _zero_u32(buf: bytearray, off: int) -> None:
    struct.pack_into("<I", buf, off, 0)


def _rva_to_file_offset(
    buf: bytes,
    rva: int,
    section_table_off: int,
    num_sections: int,
) -> int | None:
    for i in range(num_sections):
        sec = section_table_off + i * 40
        virtual_size = _u32(buf, sec + 8)
        virtual_addr = _u32(buf, sec + 12)
        raw_size = _u32(buf, sec + 16)
        raw_offset = _u32(buf, sec + 20)
        size = max(virtual_size, raw_size)
        if virtual_addr <= rva < virtual_addr + size:
            return raw_offset + (rva - virtual_addr)
    return None


def normalize_pe(path: pathlib.Path) -> tuple[bool, list[str]]:
    """Zero out non-deterministic PE fields. Returns (changed, notes)."""
    raw = path.read_bytes()
    buf = bytearray(raw)
    notes: list[str] = []

    if len(buf) < 0x40 or buf[:2] != b"MZ":
        raise ValueError(f"{path}: not a PE/MZ file")
    pe_off = _u32(buf, 0x3C)
    if pe_off + 24 > len(buf) or buf[pe_off:pe_off + 4] != b"PE\x00\x00":
        raise ValueError(f"{path}: PE signature not found at 0x{pe_off:x}")

    coff_off = pe_off + 4
    num_sections = _u16(buf, coff_off + 2)
    size_optional = _u16(buf, coff_off + 16)
    optional_off = coff_off + 20
    section_table_off = optional_off + size_optional

    # 1) COFF TimeDateStamp (offset coff_off + 4, 4 bytes)
    #    COFF header layout: Machine(2) NumberOfSections(2) TimeDateStamp(4) ...
    ts_off = coff_off + 4
    if _u32(buf, ts_off) != 0:
        notes.append(f"COFF TimeDateStamp 0x{_u32(buf, ts_off):08x} -> 0")
        _zero_u32(buf, ts_off)

    # 2) Optional Header CheckSum
    #    Magic at optional_off (0x10b PE32, 0x20b PE32+).
    #    CheckSum is at offset 64 within the optional header in both formats.
    if size_optional >= 68:
        magic = _u16(buf, optional_off)
        checksum_off = optional_off + 64
        if _u32(buf, checksum_off) != 0:
            notes.append(f"Optional CheckSum 0x{_u32(buf, checksum_off):08x} -> 0")
            _zero_u32(buf, checksum_off)
        # Data directories start after the standard optional header fields.
        data_dirs_off = optional_off + (96 if magic == 0x10B else 112)
    else:
        magic = 0
        data_dirs_off = 0

    def _walk_directory(idx: int, ts_offset_in_struct: int, name: str) -> None:
        if data_dirs_off == 0:
            return
        dir_off = data_dirs_off + idx * 8
        if dir_off + 8 > len(buf):
            return
        rva = _u32(buf, dir_off)
        size = _u32(buf, dir_off + 4)
        if rva == 0 or size == 0:
            return
        file_off = _rva_to_file_offset(buf, rva, section_table_off, num_sections)
        if file_off is None:
            return
        ts_at = file_off + ts_offset_in_struct
        if ts_at + 4 > len(buf):
            return
        if _u32(buf, ts_at) != 0:
            notes.append(f"{name} TimeDateStamp 0x{_u32(buf, ts_at):08x} -> 0")
            _zero_u32(buf, ts_at)

    # 3) Export directory: IMAGE_EXPORT_DIRECTORY.TimeDateStamp at offset 4
    _walk_directory(0, 4, "Export")
    # 4) Resource directory: IMAGE_RESOURCE_DIRECTORY.TimeDateStamp at offset 4
    _walk_directory(2, 4, "Resource")
    # 5) Debug directory: array of IMAGE_DEBUG_DIRECTORY (28 bytes each).
    #    Each entry's TimeDateStamp is at offset 4. Walk them all.
    if data_dirs_off and 6 * 8 + 8 <= (data_dirs_off + 7 * 8 - data_dirs_off):
        debug_dir_off = data_dirs_off + 6 * 8
        if debug_dir_off + 8 <= len(buf):
            debug_rva = _u32(buf, debug_dir_off)
            debug_size = _u32(buf, debug_dir_off + 4)
            if debug_rva and debug_size:
                debug_file = _rva_to_file_offset(
                    buf, debug_rva, section_table_off, num_sections
                )
                if debug_file is not None:
                    n = debug_size // 28
                    for j in range(n):
                        entry = debug_file + j * 28
                        if entry + 8 > len(buf):
                            break
                        ts_at = entry + 4
                        if _u32(buf, ts_at) != 0:
                            notes.append(
                                f"Debug[{j}] TimeDateStamp 0x{_u32(buf, ts_at):08x} -> 0"
                            )
                            _zero_u32(buf, ts_at)

    if not notes:
        return False, []
    path.write_bytes(bytes(buf))
    return True, notes


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=pathlib.Path,
                        help="PE files to normalize in place")
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--print-hash", action="store_true",
                        help="print sha256 of each file after normalization")
    args = parser.parse_args()

    rc = 0
    for p in args.paths:
        try:
            changed, notes = normalize_pe(p)
        except Exception as exc:
            print(f"FAIL {p}: {exc}", file=sys.stderr)
            rc = 1
            continue
        if not args.quiet:
            tag = "norm" if changed else "ok  "
            print(f"{tag} {p}")
            for n in notes:
                print(f"     {n}")
        if args.print_hash:
            digest = hashlib.sha256(p.read_bytes()).hexdigest()
            print(f"     sha256={digest}")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())

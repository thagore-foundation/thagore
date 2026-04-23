#!/usr/bin/env python3
"""Zero out non-deterministic fields in PE and ELF binaries so two builds
of the same source on different machines produce byte-identical files.

PE (Windows) — fields zeroed:
  - COFF File Header TimeDateStamp (4 bytes)        — always non-deterministic
  - PE Optional Header CheckSum (4 bytes)           — defensive; depends on file
  - IMAGE_DEBUG_DIRECTORY entries TimeDateStamp     — defensive
  - IMAGE_EXPORT_DIRECTORY TimeDateStamp            — defensive
  - IMAGE_RESOURCE_DIRECTORY TimeDateStamp          — defensive

The MinGW linker (`ld`) embeds a real Unix timestamp in the COFF
TimeDateStamp on every link. `-Wl,/Brepro` is silently ignored (it is an
MSVC linker option). This normalizer is the post-link step that makes
self-hosting hash equality possible on Windows builds.

ELF (Linux) — section contents zeroed:
  - .comment              — gcc/clang version string (e.g. "GCC: (Ubuntu
                            13.2.0-23ubuntu4) 13.2.0"). Differs across
                            ephemeral runners that received slightly
                            different toolchain patches.
  - .note.gnu.build-id    — sha1 of the linker's input. Even a tiny diff
                            in any input object propagates here.
  - .note.GNU-stack       — defensive; usually empty.

GNU ld on Linux does NOT embed a timestamp the way MinGW PE does, but
the .comment / build-id sections are version-sensitive and cause cross-
machine SHA256 mismatches even when the program text is bit-identical.
Mach-O is not currently handled — extend when a macOS lane lands.

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


def _normalize_elf(buf: bytearray) -> list[str]:
    """Zero contents of version-sensitive sections in an ELF in place.

    Targets `.comment`, `.note.gnu.build-id`, `.note.GNU-stack`. These
    are the standard culprits for cross-machine hash drift on Linux when
    two ephemeral runners have slightly different toolchain patches but
    otherwise produce identical program text.

    Section header offsets/sizes are left intact; only the bytes inside
    the section are zeroed. This keeps the ELF structurally valid and
    runnable; the dynamic loader does not require build-id contents.
    """
    notes: list[str] = []
    if len(buf) < 64:
        return notes
    ei_class = buf[4]      # 1=ELF32, 2=ELF64
    ei_data = buf[5]       # 1=little, 2=big
    endian = "<" if ei_data == 1 else ">"

    if ei_class == 1:
        e_shoff = struct.unpack_from(f"{endian}I", buf, 0x20)[0]
        e_shentsize = struct.unpack_from(f"{endian}H", buf, 0x2E)[0]
        e_shnum = struct.unpack_from(f"{endian}H", buf, 0x30)[0]
        e_shstrndx = struct.unpack_from(f"{endian}H", buf, 0x32)[0]
        sh_offset_rel = 16
        sh_size_rel = 20
        word_fmt = "I"
    elif ei_class == 2:
        e_shoff = struct.unpack_from(f"{endian}Q", buf, 0x28)[0]
        e_shentsize = struct.unpack_from(f"{endian}H", buf, 0x3A)[0]
        e_shnum = struct.unpack_from(f"{endian}H", buf, 0x3C)[0]
        e_shstrndx = struct.unpack_from(f"{endian}H", buf, 0x3E)[0]
        sh_offset_rel = 24
        sh_size_rel = 32
        word_fmt = "Q"
    else:
        return notes

    if e_shoff == 0 or e_shnum == 0 or e_shstrndx >= e_shnum:
        return notes

    shstr_hdr = e_shoff + e_shstrndx * e_shentsize
    if shstr_hdr + sh_size_rel + struct.calcsize(word_fmt) > len(buf):
        return notes
    shstr_off = struct.unpack_from(f"{endian}{word_fmt}", buf, shstr_hdr + sh_offset_rel)[0]
    shstr_size = struct.unpack_from(f"{endian}{word_fmt}", buf, shstr_hdr + sh_size_rel)[0]
    if shstr_off + shstr_size > len(buf):
        return notes

    targets = {b".comment", b".note.gnu.build-id", b".note.GNU-stack"}
    for i in range(e_shnum):
        hdr = e_shoff + i * e_shentsize
        if hdr + sh_size_rel + struct.calcsize(word_fmt) > len(buf):
            break
        sh_name = struct.unpack_from(f"{endian}I", buf, hdr)[0]
        if sh_name >= shstr_size:
            continue
        try:
            end_idx = buf.index(0, shstr_off + sh_name, shstr_off + shstr_size)
        except ValueError:
            continue
        name = bytes(buf[shstr_off + sh_name:end_idx])
        if name not in targets:
            continue
        offset = struct.unpack_from(f"{endian}{word_fmt}", buf, hdr + sh_offset_rel)[0]
        size = struct.unpack_from(f"{endian}{word_fmt}", buf, hdr + sh_size_rel)[0]
        if size == 0 or offset + size > len(buf):
            continue
        if any(buf[offset:offset + size]):
            notes.append(f"{name.decode('ascii')} ({size} bytes) -> 0")
            for j in range(offset, offset + size):
                buf[j] = 0
    return notes


def normalize_pe(path: pathlib.Path) -> tuple[bool, list[str]]:
    """Zero out non-deterministic fields in a PE or ELF binary in place.

    Returns (changed, notes). Despite the historical name, this also
    handles ELF (.comment, .note.gnu.build-id, .note.GNU-stack). Mach-O
    is a no-op until a macOS lane needs it.
    """
    raw = path.read_bytes()
    buf = bytearray(raw)
    notes: list[str] = []

    if len(buf) >= 4 and buf[:4] == b"\x7fELF":
        notes = _normalize_elf(buf)
        if not notes:
            return False, []
        path.write_bytes(bytes(buf))
        return True, notes

    if len(buf) < 0x40 or buf[:2] != b"MZ":
        return False, []
    pe_off = _u32(buf, 0x3C)
    if pe_off + 24 > len(buf) or buf[pe_off:pe_off + 4] != b"PE\x00\x00":
        return False, []

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

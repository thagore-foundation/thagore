"""Self-tests for pe_normalize.

Run with:
    python tooling/ci/test_pe_normalize.py

Covers Mach-O normalization against a synthetic binary, plus a smoke
test that the dispatcher leaves unrecognized files alone.
"""
from __future__ import annotations

import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import pe_normalize  # noqa: E402


def _build_synthetic_macho() -> bytes:
    """Mach-O 64 (little-endian) with three load commands.

    Layout:
      header (32 B)
      LC_UUID         cmd=0x1B  cmdsize=24  payload = 16 × 0xAA
      LC_BUILD_VERSION cmd=0x32 cmdsize=24  platform=1 minos=0x000B0500 sdk=0x000B0500 ntools=0
      LC_VERSION_MIN_MACOSX cmd=0x24 cmdsize=16 version=0x000A0F00 sdk=0x000B0000
    """
    LC_UUID = 0x1B
    LC_BUILD_VERSION = 0x32
    LC_VERSION_MIN_MACOSX = 0x24

    cmd_uuid = struct.pack("<II", LC_UUID, 24) + b"\xAA" * 16
    cmd_build = struct.pack("<IIIIII",
                            LC_BUILD_VERSION, 24,
                            1,            # platform = MACOS
                            0x000B0500,   # minos
                            0x000B0500,   # sdk
                            0)            # ntools
    cmd_vermin = struct.pack("<IIII",
                             LC_VERSION_MIN_MACOSX, 16,
                             0x000A0F00,  # version
                             0x000B0000)  # sdk

    cmds = cmd_uuid + cmd_build + cmd_vermin
    sizeofcmds = len(cmds)
    ncmds = 3

    header = struct.pack("<IIIIIIII",
                         0xFEEDFACF,   # magic
                         0x01000007,   # cputype = X86_64
                         0x00000003,   # cpusubtype
                         0x00000002,   # filetype = MH_EXECUTE
                         ncmds,
                         sizeofcmds,
                         0x00200085,   # flags
                         0x00000000)   # reserved
    body = b"\x42" * 64  # opaque tail to confirm it's preserved
    return header + cmds + body


class MachOTests(unittest.TestCase):
    def test_macho_zeros_uuid_and_versions(self) -> None:
        original = _build_synthetic_macho()
        with tempfile.NamedTemporaryFile(delete=False) as f:
            f.write(original)
            path = pathlib.Path(f.name)
        try:
            changed, notes = pe_normalize.normalize_pe(path)
            self.assertTrue(changed, "expected normalize_pe to report changes")
            after = path.read_bytes()
        finally:
            path.unlink(missing_ok=True)

        self.assertEqual(len(after), len(original),
                         "normalizer must not change file length")

        # Header and trailing body must be untouched.
        self.assertEqual(after[:32], original[:32], "Mach-O header changed")
        self.assertEqual(after[-64:], original[-64:], "trailing body changed")

        # LC_UUID payload (header + 8-byte cmd prefix .. +24).
        self.assertEqual(after[32 + 8:32 + 24], b"\x00" * 16, "UUID not zeroed")

        # LC_BUILD_VERSION sits right after LC_UUID. Offsets within it:
        #   +0 cmd   +4 cmdsize  +8 platform  +12 minos  +16 sdk  +20 ntools
        bv_off = 32 + 24
        self.assertEqual(after[bv_off:bv_off + 4], original[bv_off:bv_off + 4],
                         "LC_BUILD_VERSION cmd changed")
        self.assertEqual(after[bv_off + 4:bv_off + 8], original[bv_off + 4:bv_off + 8],
                         "LC_BUILD_VERSION cmdsize changed")
        self.assertEqual(after[bv_off + 8:bv_off + 12], original[bv_off + 8:bv_off + 12],
                         "platform field must be preserved")
        self.assertEqual(after[bv_off + 12:bv_off + 16], b"\x00" * 4, "minos not zeroed")
        self.assertEqual(after[bv_off + 16:bv_off + 20], b"\x00" * 4, "sdk not zeroed")
        self.assertEqual(after[bv_off + 20:bv_off + 24], original[bv_off + 20:bv_off + 24],
                         "ntools field must be preserved")

        # LC_VERSION_MIN_MACOSX sits right after LC_BUILD_VERSION.
        vm_off = bv_off + 24
        self.assertEqual(after[vm_off:vm_off + 8], original[vm_off:vm_off + 8],
                         "LC_VERSION_MIN cmd/cmdsize changed")
        self.assertEqual(after[vm_off + 8:vm_off + 12], b"\x00" * 4,
                         "LC_VERSION_MIN version not zeroed")
        self.assertEqual(after[vm_off + 12:vm_off + 16], b"\x00" * 4,
                         "LC_VERSION_MIN sdk not zeroed")

        # Notes should mention all three commands.
        joined = " | ".join(notes)
        self.assertIn("LC_UUID", joined)
        self.assertIn("LC_BUILD_VERSION minos", joined)
        self.assertIn("LC_BUILD_VERSION sdk", joined)
        self.assertIn("LC_VERSION_MIN", joined)

    def test_macho_idempotent(self) -> None:
        with tempfile.NamedTemporaryFile(delete=False) as f:
            f.write(_build_synthetic_macho())
            path = pathlib.Path(f.name)
        try:
            pe_normalize.normalize_pe(path)
            first = path.read_bytes()
            changed, _ = pe_normalize.normalize_pe(path)
            second = path.read_bytes()
        finally:
            path.unlink(missing_ok=True)

        self.assertFalse(changed, "second pass should be a no-op")
        self.assertEqual(first, second, "second pass changed bytes")

    def test_unknown_format_is_noop(self) -> None:
        with tempfile.NamedTemporaryFile(delete=False) as f:
            f.write(b"not a binary, just bytes\n" * 8)
            path = pathlib.Path(f.name)
        try:
            original = path.read_bytes()
            changed, notes = pe_normalize.normalize_pe(path)
            after = path.read_bytes()
        finally:
            path.unlink(missing_ok=True)

        self.assertFalse(changed)
        self.assertEqual(notes, [])
        self.assertEqual(after, original)


if __name__ == "__main__":
    unittest.main()

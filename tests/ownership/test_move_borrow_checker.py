import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


class MoveBorrowCheckerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.bin = resolve_thagc_bin()
        if self.bin is None:
            self.skipTest("thagc binary not found; set THAGC_BIN or build compiler first")

    def _build(self, source: str) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / "main.tg"
            out = Path(td) / "main.bin"
            src.write_text(source)
            return subprocess.run(
                [str(self.bin), "build", str(src), "-o", str(out)],
                capture_output=True,
                text=True,
                check=False,
            )

    def test_reject_use_after_move(self) -> None:
        result = self._build(
            "func main():\n"
            "  let a: own i32 = 1\n"
            "  let b = a\n"
            "  return a\n"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("E_MOVE_USE_AFTER_MOVE", result.stderr)

    def test_accept_move_then_use_new_owner(self) -> None:
        result = self._build(
            "func main():\n"
            "  let a: own i32 = 1\n"
            "  let b = a\n"
            "  return b\n"
        )
        self.assertEqual(result.returncode, 0, msg=result.stderr)

    def test_reject_double_mut_borrow(self) -> None:
        result = self._build(
            "func main():\n"
            "  let x: own i32 = 1\n"
            "  let r1: mut i32 = x\n"
            "  let r2: mut i32 = x\n"
            "  return 0\n"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("E_BORROW_CONFLICT", result.stderr)

    def test_reject_mutate_while_borrowed(self) -> None:
        result = self._build(
            "func main():\n"
            "  let x: own i32 = 1\n"
            "  let r: ref i32 = x\n"
            "  x = 2\n"
            "  return 0\n"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("E_BORROW_MUTATE_CONFLICT", result.stderr)

    def test_reject_move_while_borrowed(self) -> None:
        result = self._build(
            "func main():\n"
            "  let x: own i32 = 1\n"
            "  let r: ref i32 = x\n"
            "  let y = x\n"
            "  return 0\n"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("E_BORROW_MOVE_CONFLICT", result.stderr)


if __name__ == "__main__":
    unittest.main()

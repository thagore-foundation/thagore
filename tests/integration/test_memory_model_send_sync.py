import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


class MemoryModelSendSyncTests(unittest.TestCase):
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

    def test_reject_rc_to_send(self) -> None:
        result = self._build(
            "func main():\n"
            "  let local: Rc = 1\n"
            "  let outbound: Send = local\n"
            "  return 0\n"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("E_SEND_SYNC_004", result.stderr)

    def test_allow_arc_to_send(self) -> None:
        result = self._build(
            "func main():\n"
            "  let shared: Arc = 1\n"
            "  let outbound: Send = shared\n"
            "  return outbound\n"
        )
        self.assertEqual(result.returncode, 0, msg=result.stderr)


if __name__ == "__main__":
    unittest.main()

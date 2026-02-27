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
            "  let local: Rc = Rc(1)\n"
            "  spawn(local)\n"
            "  return 0\n"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("E_SEND_SYNC_004", result.stderr)
        self.assertIn("replace Rc<T> with Arc<T>", result.stderr)

    def test_allow_arc_to_send(self) -> None:
        result = self._build(
            "func main():\n"
            "  let shared: Arc = Arc(1)\n"
            "  spawn(shared)\n"
            "  return 0\n"
        )
        self.assertEqual(result.returncode, 0, msg=result.stderr)

    def test_reject_closure_capturing_rc_across_spawn(self) -> None:
        result = self._build(
            "func main():\n"
            "  let conn: Rc = Rc(1)\n"
            "  let work = |x| conn\n"
            "  spawn(work)\n"
            "  return 0\n"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("E_SEND_SYNC_004", result.stderr)
        self.assertIn("capture", result.stderr)

    def test_reject_nested_rc_in_struct_across_spawn(self) -> None:
        result = self._build(
            "struct Connection:\n"
            "  id: i32\n"
            "\n"
            "struct Job:\n"
            "  conn: Rc<Connection>\n"
            "\n"
            "func main():\n"
            "  let conn: Rc<Connection> = Rc(1)\n"
            "  let job: Job = Job(conn)\n"
            "  spawn(job)\n"
            "  return 0\n"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("E_SEND_SYNC_004", result.stderr)
        self.assertIn("job.conn", result.stderr)

    def test_allow_arc_nested_in_struct_across_spawn(self) -> None:
        result = self._build(
            "struct Job:\n"
            "  conn: Arc<i32>\n"
            "\n"
            "func main():\n"
            "  let conn: Arc<i32> = Arc(1)\n"
            "  let job: Job = Job(conn)\n"
            "  spawn(job)\n"
            "  return 0\n"
        )
        self.assertEqual(result.returncode, 0, msg=result.stderr)


if __name__ == "__main__":
    unittest.main()

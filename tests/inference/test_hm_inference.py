import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


class HmInferenceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.bin = resolve_thagc_bin()
        if self.bin is None:
            self.skipTest("thagc binary not found; set THAGC_BIN or build compiler first")

    def _build(self, source: str) -> tuple[subprocess.CompletedProcess[str], Path]:
        td = tempfile.TemporaryDirectory()
        root = Path(td.name)
        src = root / "main.tg"
        out = root / "main.bin"
        src.write_text(source)
        build = subprocess.run(
            [str(self.bin), "build", str(src), "-o", str(out)],
            capture_output=True,
            text=True,
            check=False,
        )
        self.addCleanup(td.cleanup)
        return build, out

    def _build_and_run(self, source: str) -> tuple[subprocess.CompletedProcess[str], subprocess.CompletedProcess[str]]:
        build, out = self._build(source)
        self.assertEqual(build.returncode, 0, msg=build.stderr)
        run = subprocess.run([str(out)], capture_output=True, text=True, check=False)
        return build, run

    def test_infers_let_without_annotation(self) -> None:
        _, run = self._build_and_run(
            "func main():\n"
            "  let base = 40\n"
            "  let result = base + 2\n"
            "  return result\n"
        )
        self.assertEqual(run.returncode, 42, msg=run.stderr)

    def test_infers_option_payload_from_some(self) -> None:
        _, run = self._build_and_run(
            "func main():\n"
            "  let value = Some(11)\n"
            "  if (is_some(value)):\n"
            "    return unwrap(value)\n"
            "  return 0\n"
        )
        self.assertEqual(run.returncode, 11, msg=run.stderr)


if __name__ == "__main__":
    unittest.main()

import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


class GenericTypecheckTests(unittest.TestCase):
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

    def test_accepts_matching_option_payload_type(self) -> None:
        build, _ = self._build(
            "func main():\n"
            "  let ok: Option<i32> = Some(5)\n"
            "  return unwrap(ok)\n"
        )
        self.assertEqual(build.returncode, 0, msg=build.stderr)

    def test_rejects_option_payload_mismatch(self) -> None:
        build, _ = self._build(
            "func main():\n"
            "  let bad: Option<i32> = Some(\"x\")\n"
            "  return 0\n"
        )
        self.assertNotEqual(build.returncode, 0)
        self.assertIn("annotation 'Option<i32>' is not assignable", build.stderr)

    def test_rejects_result_payload_mismatch_on_return(self) -> None:
        build, _ = self._build(
            "func wrong() -> Result<i32, i32>:\n"
            "  return Ok(\"x\")\n"
            "\n"
            "func main():\n"
            "  return 0\n"
        )
        self.assertNotEqual(build.returncode, 0)
        self.assertIn("return type mismatch", build.stderr)


if __name__ == "__main__":
    unittest.main()

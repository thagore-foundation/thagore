import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


class BuildAndRunE2ETests(unittest.TestCase):
    def setUp(self) -> None:
        self.bin = resolve_thagc_bin()
        if self.bin is None:
            self.skipTest("thagc binary not found; set THAGC_BIN or build compiler first")

    def test_build_and_run_main(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "hello.tg"
            out = root / "hello.bin"
            src.write_text(
                "func main():\n"
                "  let x = 7\n"
                "  print(x)\n"
                "  return x\n"
            )

            build = subprocess.run(
                [str(self.bin), "build", str(src), "-o", str(out)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(build.returncode, 0, msg=build.stderr)
            run = subprocess.run([str(out)], capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 7, msg=run.stderr)
            self.assertIn("7", run.stdout)

    def test_build_script_entrypoint(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "script.tg"
            out = root / "script.bin"
            src.write_text("let x = 3\nx + 4\n")
            build = subprocess.run(
                [str(self.bin), "build", str(src), "-o", str(out)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(build.returncode, 0, msg=build.stderr)
            run = subprocess.run([str(out)], capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 7, msg=run.stderr)


if __name__ == "__main__":
    unittest.main()

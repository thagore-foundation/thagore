import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


class CompilerSoakTests(unittest.TestCase):
    def setUp(self) -> None:
        self.bin = resolve_thagc_bin()
        if self.bin is None:
            self.skipTest("thagc binary not found; set THAGC_BIN or build compiler first")

    def test_repeat_build_run_lane(self) -> None:
        loops = 20
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for i in range(loops):
                src = root / f"case_{i}.tg"
                out = root / f"case_{i}.bin"
                src.write_text(
                    "func main():\n"
                    f"  let x = {i}\n"
                    "  return x\n"
                )
                build = subprocess.run(
                    [str(self.bin), "build", str(src), "-o", str(out)],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(build.returncode, 0, msg=f"build loop={i}: {build.stderr}")
                run = subprocess.run([str(out)], capture_output=True, text=True, check=False)
                self.assertEqual(run.returncode, i, msg=f"run loop={i}: {run.stderr}")


if __name__ == "__main__":
    unittest.main()

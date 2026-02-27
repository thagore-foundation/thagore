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

    def test_build_and_run_multi_function_program(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "multi.tg"
            out = root / "multi.bin"
            src.write_text(
                "func add(a, b):\n"
                "  return a + b\n"
                "\n"
                "func twice(v):\n"
                "  return add(v, v)\n"
                "\n"
                "func main():\n"
                "  return twice(6)\n"
            )
            build = subprocess.run(
                [str(self.bin), "build", str(src), "-o", str(out)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(build.returncode, 0, msg=build.stderr)
            run = subprocess.run([str(out)], capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 12, msg=run.stderr)

    def test_build_and_run_float_arithmetic(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "float.tg"
            out = root / "float.bin"
            src.write_text(
                "func main():\n"
                "  let x: f32 = 1.5\n"
                "  let y: f32 = 2.0\n"
                "  let z: f32 = x + y\n"
                "  print(z)\n"
                "  return 0\n"
            )
            build = subprocess.run(
                [str(self.bin), "build", str(src), "-o", str(out)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(build.returncode, 0, msg=build.stderr)
            run = subprocess.run([str(out)], capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 0, msg=run.stderr)
            self.assertIn("3.500000", run.stdout)

    def test_build_and_run_bool_values(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "bool.tg"
            out = root / "bool.bin"
            src.write_text(
                "func main():\n"
                "  let ok: bool = true\n"
                "  if (ok):\n"
                "    return 1\n"
                "  return 0\n"
            )
            build = subprocess.run(
                [str(self.bin), "build", str(src), "-o", str(out)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(build.returncode, 0, msg=build.stderr)
            run = subprocess.run([str(out)], capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 1, msg=run.stderr)


if __name__ == "__main__":
    unittest.main()

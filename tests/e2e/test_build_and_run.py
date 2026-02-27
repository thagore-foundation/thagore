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

    def test_build_and_run_struct_method_and_enum_payload(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "struct_enum.tg"
            out = root / "struct_enum.bin"
            src.write_text(
                "struct Point:\n"
                "  x: i32\n"
                "  y: i32\n"
                "\n"
                "impl Point:\n"
                "  func sum(self):\n"
                "    return self.x + self.y\n"
                "\n"
                "enum Reply:\n"
                "  Ok(i32)\n"
                "  Err(i32)\n"
                "\n"
                "func main():\n"
                "  let p = Point(10, 2)\n"
                "  let r = Ok(p.sum())\n"
                "  match (r):\n"
                "    Ok(v):\n"
                "      return v\n"
                "    Err(e):\n"
                "      return e\n"
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

    def test_build_multi_file_with_file_and_package_import(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "a" / "b").mkdir(parents=True)
            (root / "deps" / "mathpkg").mkdir(parents=True)
            (root / "a" / "b" / "c.tg").write_text(
                "pub func inc(x):\n"
                "  return x + 1\n"
            )
            (root / "deps" / "mathpkg" / "thagore.toml").write_text(
                "[package]\n"
                "name = \"mathpkg\"\n"
                "version = \"0.1.0\"\n"
            )
            (root / "deps" / "mathpkg" / "main.tg").write_text(
                "pub func pkg_double(v):\n"
                "  return v * 2\n"
            )
            (root / "thagore.toml").write_text(
                "[package]\n"
                "name = \"demo\"\n"
                "version = \"0.1.0\"\n"
                "\n"
                "[dependencies]\n"
                "mathpkg = \"path:./deps/mathpkg\"\n"
            )
            src = root / "main.tg"
            out = root / "main.bin"
            src.write_text(
                "import a.b.c\n"
                "import mathpkg\n"
                "\n"
                "func main():\n"
                "  return c.inc(mathpkg.pkg_double(4))\n"
            )
            install = subprocess.run(
                [str(self.bin), "install"],
                cwd=root,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(install.returncode, 0, msg=install.stderr)
            build = subprocess.run(
                [str(self.bin), "build", str(src), "-o", str(out)],
                cwd=root,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(build.returncode, 0, msg=build.stderr)
            run = subprocess.run([str(out)], capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 9, msg=run.stderr)

    def test_build_and_run_language_feature_complete_cli_gate(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "cli_gate.tg"
            out = root / "cli_gate.bin"
            src.write_text(
                "func parse_pos(v) -> Result<i32, i32>:\n"
                "  if (v > 0):\n"
                "    return Ok(v)\n"
                "  return Err(7)\n"
                "\n"
                "func main():\n"
                "  defer print(99)\n"
                "  let data = [3, 1, 2]\n"
                "  let tupled = (data[0], data[1], data[2])\n"
                "  let (a, b, c) = tupled\n"
                "  let transform = |x| x + c\n"
                "  let parsed: Result<i32, i32> = parse_pos(transform(a + b))\n"
                "  let value = parsed?\n"
                "  if (is_some(Some(value))):\n"
                "    print(len(data))\n"
                "  print(value)\n"
                "  return value\n"
            )
            build = subprocess.run(
                [str(self.bin), "build", str(src), "-o", str(out)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(build.returncode, 0, msg=build.stderr)
            run = subprocess.run([str(out)], capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 6, msg=run.stderr)
            lines = [line.strip() for line in run.stdout.splitlines() if line.strip()]
            self.assertIn("3", lines)
            self.assertIn("6", lines)
            self.assertEqual(lines[-1], "99")


if __name__ == "__main__":
    unittest.main()

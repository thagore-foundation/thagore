import os
import shutil
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

    def test_build_script_last_expression_is_evaluated_once(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "script_once.tg"
            out = root / "script_once.bin"
            src.write_text(
                "func hello():\n"
                "  print(\"Hello\")\n"
                "\n"
                "hello()\n"
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
            lines = [line.strip() for line in run.stdout.splitlines() if line.strip()]
            self.assertEqual(lines, ["Hello"])

    def test_build_script_top_level_print_outputs_text(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "script_print.tg"
            out = root / "script_print.bin"
            src.write_text('print("ok")\n')
            build = subprocess.run(
                [str(self.bin), "build", str(src), "-o", str(out)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(build.returncode, 0, msg=build.stderr)
            run = subprocess.run([str(out)], capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 0, msg=run.stderr)
            lines = [line.strip() for line in run.stdout.splitlines() if line.strip()]
            self.assertEqual(lines, ["ok"])

    def test_build_rejects_top_level_statements_when_main_exists(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "main_and_top_level.tg"
            out = root / "main_and_top_level.bin"
            src.write_text(
                "func main() -> i32:\n"
                "  print(\"Hello\")\n"
                "  return 0\n"
                "\n"
                "print(\"ok\")\n"
            )
            build = subprocess.run(
                [str(self.bin), "build", str(src), "-o", str(out)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertNotEqual(build.returncode, 0)
            self.assertIn("E0029", build.stderr)

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

    def test_std_core_print_helpers_work(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "core_print.tg"
            out = root / "core_print.bin"
            src.write_text(
                "from std.core import print_int, print_float, print_bool\n"
                "\n"
                "func main():\n"
                "  print_int(7)\n"
                "  print_float(2.5)\n"
                "  print_bool(true)\n"
                "  print_bool(false)\n"
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
            lines = [line.strip() for line in run.stdout.splitlines() if line.strip()]
            self.assertGreaterEqual(len(lines), 4)
            self.assertIn("7", lines)
            self.assertIn("2.500000", lines)
            self.assertIn("1", lines)
            self.assertIn("0", lines)

    def test_build_and_run_ffi_with_explicit_c_library_link(self) -> None:
        if shutil.which("clang") is None:
            self.skipTest("clang not found")
        if shutil.which("ld") is None:
            self.skipTest("linker not found")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "ffi_math.tg"
            out = root / "ffi_math.bin"
            src.write_text(
                "extern func cos(x: f64) -> f64\n"
                "\n"
                "func main():\n"
                "  let v: f64 = cos(0.0)\n"
                "  if (v > 0.5):\n"
                "    return 0\n"
                "  return 1\n"
            )
            cmd = [str(self.bin), "build", str(src), "-o", str(out)]
            if os.name != "nt":
                cmd.extend(["--link-lib", "m"])
            build = subprocess.run(cmd, capture_output=True, text=True, check=False)
            self.assertEqual(build.returncode, 0, msg=build.stderr)
            run = subprocess.run([str(out)], capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 0, msg=run.stderr)

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
            (root / "deps" / "mathpkg" / "drago.toml").write_text(
                "[package]\n"
                "name = \"mathpkg\"\n"
                "version = \"0.1.0\"\n"
            )
            (root / "deps" / "mathpkg" / "main.tg").write_text(
                "pub func pkg_double(v):\n"
                "  return v * 2\n"
            )
            (root / "drago.toml").write_text(
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

    def test_build_and_run_stdlib_io_alpha_gate(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)

            src = root / "main.tg"
            out = root / "main.bin"
            src.write_text(
                "import std.string as s\n"
                "import lib.fs as fs\n"
                "import lib.process as proc\n"
                "import lib.toml as toml\n"
                "\n"
                "func main():\n"
                "  fs.mkdir(\"tmp_gate\")\n"
                "  let cfg = s.concat(\"port=\", \"42\")\n"
                "  let path = fs.path_join(\"tmp_gate\", \"drago.toml\")\n"
                "  fs.write_file(path, cfg)\n"
                "  let raw = fs.read_file(path)\n"
                "  let h = toml.parse(raw)\n"
                "  let port = toml.get_int(h, \"port\")\n"
                "  toml.free(h)\n"
                "  let out = proc.capture(\"printf ok\")\n"
                "  s.contains(out, \"ok\")\n"
                "  fs.remove(path)\n"
                "  fs.remove(\"tmp_gate\")\n"
                "  return port\n"
            )

            build = subprocess.run(
                [str(self.bin), "build", str(src), "-o", str(out)],
                cwd=root,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(build.returncode, 0, msg=build.stderr)
            run = subprocess.run([str(out)], cwd=root, capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 42, msg=run.stderr)

    def test_build_and_run_tensor_runtime_preview(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "tensor_preview.tg"
            out = root / "tensor_preview.bin"
            src.write_text(
                "import lib.tensor as t\n"
                "\n"
                "func main() -> i32:\n"
                "  let x = t.new_i64(4)\n"
                "  let y = t.new_i64(4)\n"
                "  let z = t.new_i64(4)\n"
                "  t.fill_i64(x, 1)\n"
                "  t.fill_i64(y, 2)\n"
                "  t.axpy_i64(z, x, y, 3)\n"
                "  t.pytorch_axpy_i64(z, x, y, 3)\n"
                "  print(t.sum_i64(z))\n"
                "  t.free(x)\n"
                "  t.free(y)\n"
                "  t.free(z)\n"
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
            self.assertIn("20", run.stdout)


if __name__ == "__main__":
    unittest.main()

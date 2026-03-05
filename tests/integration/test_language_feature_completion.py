import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


class LanguageFeatureCompletionTests(unittest.TestCase):
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

    def test_defer_runs_lifo(self) -> None:
        _, run = self._build_and_run(
            "func main():\n"
            "  defer print(1)\n"
            "  defer print(2)\n"
            "  print(3)\n"
            "  return 0\n"
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)
        lines = [line.strip() for line in run.stdout.splitlines() if line.strip()]
        self.assertEqual(lines[-3:], ["3", "2", "1"])

    def test_multi_function_calls_compile_and_run(self) -> None:
        _, run = self._build_and_run(
            "func sum2(a, b):\n"
            "  return a + b\n"
            "\n"
            "func sum4(v):\n"
            "  return sum2(sum2(v, v), v)\n"
            "\n"
            "func main():\n"
            "  return sum4(3)\n"
        )
        self.assertEqual(run.returncode, 9, msg=run.stderr)

    def test_float_and_bool_variables_compile_and_run(self) -> None:
        _, run = self._build_and_run(
            "func main():\n"
            "  let x: f32 = 1.5\n"
            "  let y: f32 = 2.0\n"
            "  let z: f32 = x + y\n"
            "  let ok: bool = z > 3.0\n"
            "  if (ok):\n"
            "    print(z)\n"
            "    return 1\n"
            "  return 0\n"
        )
        self.assertEqual(run.returncode, 1, msg=run.stderr)
        self.assertIn("3.500000", run.stdout)

    def test_extern_i64_return_compiles_and_runs(self) -> None:
        _, run = self._build_and_run(
            "extern func thag_now_ms() -> i64\n"
            "\n"
            "func main():\n"
            "  let now: i64 = thag_now_ms()\n"
            "  print(now)\n"
            "  if (now > 0):\n"
            "    return 0\n"
            "  return 1\n"
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)
        self.assertRegex(run.stdout.strip(), r"^-?\d+$")

    def test_typechecker_rejects_wrong_argument_count(self) -> None:
        build, _ = self._build(
            "func add(a, b):\n"
            "  return a + b\n"
            "\n"
            "func main():\n"
            "  return add(1)\n"
        )
        self.assertNotEqual(build.returncode, 0)
        self.assertIn("expects 2 arguments but got 1", build.stderr)

    def test_typechecker_rejects_undefined_variable_and_assignment_mismatch(self) -> None:
        build, _ = self._build(
            "func main():\n"
            "  let x = 1\n"
            "  x = true\n"
            "  return y\n"
        )
        self.assertNotEqual(build.returncode, 0)
        self.assertTrue("cannot assign bool to variable 'x'" in build.stderr or "unknown identifier 'y'" in build.stderr)

    def test_closure_capture_and_call(self) -> None:
        _, run = self._build_and_run(
            "func main():\n"
            "  let base = 4\n"
            "  let add = |x| x + base\n"
            "  print(add(3))\n"
            "  return add(3)\n"
        )
        self.assertEqual(run.returncode, 7, msg=run.stderr)
        self.assertIn("7", run.stdout)

    def test_enum_payload_match_binding(self) -> None:
        _, run = self._build_and_run(
            "enum Reply:\n"
            "  Ok(i32)\n"
            "  Err(i32)\n"
            "\n"
            "func main():\n"
            "  let r = Ok(12)\n"
            "  match (r):\n"
            "    Ok(v):\n"
            "      return v\n"
            "    Err(e):\n"
            "      return e\n"
        )
        self.assertEqual(run.returncode, 12, msg=run.stderr)

    def test_struct_method_and_field_assignment(self) -> None:
        _, run = self._build_and_run(
            "struct Counter:\n"
            "  value: i32\n"
            "\n"
            "impl Counter:\n"
            "  func inc(self, delta):\n"
            "    self.value = self.value + delta\n"
            "    return self.value\n"
            "\n"
            "func main():\n"
            "  let c = Counter(1)\n"
            "  let x = c.inc(4)\n"
            "  return x\n"
        )
        self.assertEqual(run.returncode, 5, msg=run.stderr)

    def test_typechecker_rejects_undefined_method(self) -> None:
        build, _ = self._build(
            "struct Point:\n"
            "  x: i32\n"
            "\n"
            "impl Point:\n"
            "  func ok(self):\n"
            "    return self.x\n"
            "\n"
            "func main():\n"
            "  let p = Point(1)\n"
            "  return p.nope()\n"
        )
        self.assertNotEqual(build.returncode, 0)
        self.assertIn("unknown method 'nope' on struct 'Point'", build.stderr)

    def test_typechecker_rejects_undefined_struct_field(self) -> None:
        build, _ = self._build(
            "struct Point:\n"
            "  x: i32\n"
            "\n"
            "func main():\n"
            "  let p = Point(1)\n"
            "  return p.y\n"
        )
        self.assertNotEqual(build.returncode, 0)
        self.assertIn("unknown field 'y' on struct 'Point'", build.stderr)

    def test_typechecker_rejects_unknown_match_variant(self) -> None:
        build, _ = self._build(
            "enum Reply:\n"
            "  Ok(i32)\n"
            "\n"
            "func main():\n"
            "  let r = Ok(1)\n"
            "  match (r):\n"
            "    Nope(v):\n"
            "      return v\n"
            "    _:\n"
            "      return 0\n"
        )
        self.assertNotEqual(build.returncode, 0)
        self.assertIn("unknown enum variant 'Nope'", build.stderr)

    def test_interpolated_string_print(self) -> None:
        _, run = self._build_and_run(
            "func main():\n"
            "  let x = 5\n"
            "  print(v\"value={x}\")\n"
            "  return x\n"
        )
        self.assertEqual(run.returncode, 5, msg=run.stderr)
        self.assertIn("value=5", run.stdout)

    def test_comptime_bindings_resolve_before_codegen(self) -> None:
        _, run = self._build_and_run(
            "comptime:\n"
            "  let base = 40\n"
            "  let answer = base + 2\n"
            "\n"
            "func main():\n"
            "  return answer\n"
        )
        self.assertEqual(run.returncode, 42, msg=run.stderr)

    def test_macro_expansion_in_expression(self) -> None:
        _, run = self._build_and_run(
            "macro addmul(a, b, c) = (a + b) * c\n"
            "\n"
            "func main():\n"
            "  let v = addmul!(2, 3, 4)\n"
            "  return v\n"
        )
        self.assertEqual(run.returncode, 20, msg=run.stderr)

    def test_macro_argument_count_mismatch_is_rejected(self) -> None:
        build, _ = self._build(
            "macro add2(a, b) = a + b\n"
            "\n"
            "func main():\n"
            "  return add2!(1)\n"
        )
        self.assertNotEqual(build.returncode, 0)
        self.assertIn("expects 2 arguments but got 1", build.stderr)

    def test_nested_generic_annotations_compile(self) -> None:
        _, run = self._build_and_run(
            "func main():\n"
            "  let nested: Arc<Rc<i32>> = Arc(1)\n"
            "  print(1)\n"
            "  return 0\n"
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)

    def test_generic_arity_validation_rejects_invalid_result(self) -> None:
        build, _ = self._build(
            "func main():\n"
            "  let x: Result<i32> = Ok(1)\n"
            "  return 0\n"
        )
        self.assertNotEqual(build.returncode, 0)
        self.assertIn("expects 2 argument(s) but got 1", build.stderr)

    def test_option_result_builtins(self) -> None:
        _, run = self._build_and_run(
            "func main():\n"
            "  let a: Option<i32> = Some(9)\n"
            "  let b: Result<i32, i32> = Ok(4)\n"
            "  if (is_some(a)):\n"
            "    print(unwrap(a))\n"
            "  if (is_ok(b)):\n"
            "    return unwrap_or(b, 0)\n"
            "  return 0\n"
        )
        self.assertEqual(run.returncode, 4, msg=run.stderr)
        self.assertIn("9", run.stdout)

    def test_typechecker_rejects_option_payload_mismatch(self) -> None:
        build, _ = self._build(
            "func main():\n"
            "  let bad: Option<i32> = Some(\"x\")\n"
            "  return 0\n"
        )
        self.assertNotEqual(build.returncode, 0)
        self.assertIn("annotation 'Option<i32>' is not assignable", build.stderr)

    def test_typechecker_rejects_return_generic_payload_mismatch(self) -> None:
        build, _ = self._build(
            "func wrong() -> Option<i32>:\n"
            "  return Some(\"x\")\n"
            "\n"
            "func main():\n"
            "  return 0\n"
        )
        self.assertNotEqual(build.returncode, 0)
        self.assertIn("return type mismatch", build.stderr)

    def test_try_operator_early_return_on_err(self) -> None:
        _, run = self._build_and_run(
            "func may_fail(v) -> Result<i32, i32>:\n"
            "  if (v == 0):\n"
            "    return Err(11)\n"
            "  return Ok(v)\n"
            "\n"
            "func main():\n"
            "  let x = may_fail(0)?\n"
            "  return x\n"
        )
        self.assertEqual(run.returncode, 11, msg=run.stderr)

    def test_tuple_destructure_and_field_access(self) -> None:
        _, run = self._build_and_run(
            "func main():\n"
            "  let t = (1, 2, 3)\n"
            "  let (a, b, c) = t\n"
            "  return a + b + c + t.1\n"
        )
        self.assertEqual(run.returncode, 8, msg=run.stderr)

    def test_array_literal_index_and_len(self) -> None:
        _, run = self._build_and_run(
            "func main():\n"
            "  let arr = [4, 5, 6]\n"
            "  print(len(arr))\n"
            "  return arr[1]\n"
        )
        self.assertEqual(run.returncode, 5, msg=run.stderr)
        self.assertIn("3", run.stdout)

    def test_labeled_break_continue(self) -> None:
        _, run = self._build_and_run(
            "func main():\n"
            "  let i = 0\n"
            "  'outer: while (i < 5):\n"
            "    i = i + 1\n"
            "    if (i < 3):\n"
            "      continue 'outer\n"
            "    break 'outer\n"
            "  return i\n"
        )
        self.assertEqual(run.returncode, 3, msg=run.stderr)

    def test_async_await_surface_compiles_and_runs(self) -> None:
        _, run = self._build_and_run(
            "async func add1(v):\n"
            "  return v + 1\n"
            "\n"
            "func main():\n"
            "  let n = await add1(4)\n"
            "  return n\n"
        )
        self.assertEqual(run.returncode, 5, msg=run.stderr)

    def test_typestate_rejects_read_before_open(self) -> None:
        build, _ = self._build(
            "func main():\n"
            "  let conn = 1\n"
            "  read(conn)\n"
            "  return 0\n"
        )
        self.assertNotEqual(build.returncode, 0)
        self.assertIn("E_TYPESTATE_002", build.stderr)

    def test_typestate_accepts_open_read_close(self) -> None:
        _, run = self._build_and_run(
            "func main():\n"
            "  let conn = 1\n"
            "  open(conn)\n"
            "  read(conn)\n"
            "  close(conn)\n"
            "  return 0\n"
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)

    def test_typestate_state_set_rejects_wrong_argument_state(self) -> None:
        build, _ = self._build(
            "state Session: Init | Ready | Closed\n"
            "type Session = i32\n"
            "\n"
            "func boot() -> Session[Init]:\n"
            "  let s: Session[Init] = 1\n"
            "  return s\n"
            "\n"
            "func use_ready(s: Session[Ready]) -> i32:\n"
            "  return 0\n"
            "\n"
            "func main():\n"
            "  let s = boot()\n"
            "  return use_ready(s)\n"
        )
        self.assertNotEqual(build.returncode, 0)
        self.assertIn("E_STATE_MISMATCH_ARG", build.stderr)

    def test_typestate_state_set_accepts_valid_transition(self) -> None:
        _, run = self._build_and_run(
            "state Session: Init | Ready | Closed\n"
            "type Session = i32\n"
            "\n"
            "func use_ready(s: Session[Ready]) -> i32:\n"
            "  return s\n"
            "\n"
            "func consume_ready(s: Session[Ready]) -> i32:\n"
            "  return use_ready(s)\n"
            "\n"
            "func main():\n"
            "  return 0\n"
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)

    def test_typestate_reports_unknown_state_set(self) -> None:
        build, _ = self._build(
            "func use_unknown(s: UnknownSession[Init]) -> i32:\n"
            "  return 0\n"
            "\n"
            "func main():\n"
            "  return 0\n"
        )
        self.assertNotEqual(build.returncode, 0)
        self.assertIn("E_STATE_UNKNOWN_SET", build.stderr)

    def test_typestate_reports_ambiguous_when_state_missing(self) -> None:
        build, _ = self._build(
            "state Session: Init | Ready | Closed\n"
            "type Session = i32\n"
            "\n"
            "func use_ready(s: Session[Ready]) -> i32:\n"
            "  return 0\n"
            "\n"
            "func main():\n"
            "  let s: Session = 1\n"
            "  return use_ready(s)\n"
        )
        self.assertNotEqual(build.returncode, 0)
        self.assertIn("W_STATE_AMBIGUOUS", build.stderr)
        self.assertIn("E_STATE_AMBIGUOUS", build.stderr)

    def test_typestate_ignores_qualified_runtime_calls(self) -> None:
        build, _ = self._build(
            "extern func thag_fs_write(path: ptr, content: ptr) -> i32\n"
            "extern func thag_fs_read(path: ptr) -> ptr\n"
            "\n"
            "func main():\n"
            "  let p = \"tmp_v09_typestate.txt\"\n"
            "  thag_fs_write(p, \"ok\")\n"
            "  thag_fs_read(p)\n"
            "  return 0\n"
        )
        self.assertEqual(build.returncode, 0, msg=build.stderr)


if __name__ == "__main__":
    unittest.main()

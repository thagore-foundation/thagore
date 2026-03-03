import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


class V17AiScaleIntegrationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.bin = resolve_thagc_bin()
        if self.bin is None:
            self.skipTest("thagc binary not found; set THAGC_BIN or build compiler first")

    def _build_and_run(self, source: str) -> tuple[subprocess.CompletedProcess[str], subprocess.CompletedProcess[str]]:
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
        if build.returncode != 0:
            return build, subprocess.CompletedProcess(args=[str(out)], returncode=1, stdout="", stderr="")
        run = subprocess.run([str(out)], cwd=root, capture_output=True, text=True, check=False)
        return build, run

    def test_flow_construct_executes_with_rollback(self) -> None:
        source = (
            "func reserve() -> i32:\n"
            "  print(11)\n"
            "  return 1\n"
            "\n"
            "func charge() -> i32:\n"
            "  print(22)\n"
            "  return 0\n"
            "\n"
            "func rollback_reserve() -> i32:\n"
            "  print(99)\n"
            "  return 1\n"
            "\n"
            "flow checkout:\n"
            "  step reserve_step = reserve()\n"
            "    undo rollback_reserve()\n"
            "    retry 1\n"
            "    idempotent\n"
            "    timeout 200ms\n"
            "  step charge_step = charge()\n"
            "    retry 1\n"
            "    idempotent\n"
            "    irreversible\n"
            "\n"
            "func main() -> i32:\n"
            "  return checkout()\n"
        )
        build, run = self._build_and_run(source)
        self.assertEqual(build.returncode, 0, msg=build.stderr)
        self.assertEqual(run.returncode, 0, msg=run.stderr)
        lines = [line.strip() for line in run.stdout.splitlines() if line.strip()]
        self.assertIn("99", lines, msg=f"expected rollback output in {lines}")

    def test_hot_reload_watch_mode_single_iteration(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "watch.tg"
            src.write_text("func main() -> i32:\n  print(7)\n  return 0\n")
            proc = subprocess.run(
                [str(self.bin), "run", str(src), "--watch", "--watch-iterations=1"],
                cwd=root,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(proc.returncode, 0, msg=proc.stderr)
            self.assertIn("watch: hot reload enabled", proc.stdout)
            self.assertIn("7", proc.stdout)

    def test_tensor_ops_surface_runs_end_to_end(self) -> None:
        source = (
            "import lib.tensor as t\n"
            "\n"
            "func main() -> i32:\n"
            "  let a = t.new_i64(3)\n"
            "  let b = t.new_i64(3)\n"
            "  let out = t.new_i64(3)\n"
            "  t.set_i64(a, 0, 1)\n"
            "  t.set_i64(a, 1, 2)\n"
            "  t.set_i64(a, 2, 3)\n"
            "  t.set_i64(b, 0, 4)\n"
            "  t.set_i64(b, 1, 5)\n"
            "  t.set_i64(b, 2, 6)\n"
            "  t.add_i64(out, a, b)\n"
            "  t.cuda_axpy_i64(out, a, b, 2)\n"
            "  let dot = t.dot_i64(a, b)\n"
            "  print(dot)\n"
            "  print(t.argmax_i64(out))\n"
            "  t.free(a)\n"
            "  t.free(b)\n"
            "  t.free(out)\n"
            "  return 0\n"
        )
        build, run = self._build_and_run(source)
        self.assertEqual(build.returncode, 0, msg=build.stderr)
        self.assertEqual(run.returncode, 0, msg=run.stderr)
        lines = [line.strip() for line in run.stdout.splitlines() if line.strip()]
        self.assertIn("32", lines)
        self.assertIn("2", lines)

    def test_sql_builder_generates_query(self) -> None:
        source = (
            "import lib.sql as sql\n"
            "\n"
            "func main() -> i32:\n"
            "  let b = sql.builder()\n"
            "  sql.select(b, \"id,name\")\n"
            "  sql.from(b, \"users\")\n"
            "  sql.where(b, \"id > 10\")\n"
            "  sql.order_by(b, \"id DESC\")\n"
            "  sql.limit(b, 5)\n"
            "  print(sql.build(b))\n"
            "  sql.builder_free(b)\n"
            "  return 0\n"
        )
        build, run = self._build_and_run(source)
        self.assertEqual(build.returncode, 0, msg=build.stderr)
        self.assertEqual(run.returncode, 0, msg=run.stderr)
        self.assertIn("SELECT id,name FROM users WHERE id > 10 ORDER BY id DESC LIMIT 5", run.stdout)

    def test_model_serving_example_builds_and_runs(self) -> None:
        example = Path("examples/v1_7_model_serving.tg")
        self.assertTrue(example.exists(), msg="missing v1.7 model serving example")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            out = root / "model.bin"
            build = subprocess.run(
                [str(self.bin), "build", str(example), "-o", str(out)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(build.returncode, 0, msg=build.stderr)
            run = subprocess.run([str(out)], capture_output=True, check=False)
            self.assertEqual(run.returncode, 0, msg=run.stderr)
            stdout_text = run.stdout.decode("utf-8", errors="ignore")
            self.assertIn("SELECT id,value FROM feature_store WHERE id = 7 LIMIT 1", stdout_text)


if __name__ == "__main__":
    unittest.main()

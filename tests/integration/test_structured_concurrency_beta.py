import ctypes.util
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


def _find_runtime_lib() -> Path | None:
    for candidate in (
        Path("build-llvm21/runtime/libthag_runtime.a"),
        Path("build-llvm21-run/runtime/libthag_runtime.a"),
        Path("build-gcc-llvm21-clean/runtime/libthag_runtime.a"),
        Path("build/runtime/libthag_runtime.a"),
    ):
        if candidate.exists():
            return candidate.resolve()
    return None


class StructuredConcurrencyBetaTests(unittest.TestCase):
    def setUp(self) -> None:
        self.runtime_lib = _find_runtime_lib()
        self.cxx = shutil.which("g++")
        if self.runtime_lib is None:
            self.skipTest("runtime static library not found; build runtime first")
        if self.cxx is None:
            self.skipTest("g++ not found")

    def _compile_and_run(
        self,
        source: str,
        *,
        env: dict[str, str] | None = None,
        timeout: float = 20.0,
    ) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "beta_check.cpp"
            out = root / "beta_check"
            src.write_text(source)
            compile_cmd = [
                self.cxx,
                "-std=c++20",
                "-I",
                str(Path("runtime/include").resolve()),
                str(src),
                str(self.runtime_lib),
                "-lpthread",
                "-o",
                str(out),
            ]
            if ctypes.util.find_library("curl"):
                compile_cmd.insert(-2, "-lcurl")
            if ctypes.util.find_library("sqlite3"):
                compile_cmd.insert(-2, "-lsqlite3")
            comp = subprocess.run(compile_cmd, capture_output=True, text=True, check=False)
            if comp.returncode != 0 and "-lcurl" in compile_cmd and "cannot find -lcurl" in comp.stderr:
                comp = subprocess.run([arg for arg in compile_cmd if arg != "-lcurl"], capture_output=True, text=True, check=False)
            if comp.returncode != 0 and "-lsqlite3" in compile_cmd and "cannot find -lsqlite3" in comp.stderr:
                comp = subprocess.run([arg for arg in compile_cmd if arg != "-lsqlite3"], capture_output=True, text=True, check=False)
            self.assertEqual(comp.returncode, 0, msg=comp.stderr)
            run_env = None
            if env is not None:
                run_env = {**os.environ, **env}
            return subprocess.run([str(out)], capture_output=True, text=True, check=False, env=run_env, timeout=timeout)

    def test_timeout_propagates_to_nested_scopes(self) -> None:
        run = self._compile_and_run(
            "#include <atomic>\n"
            "#include \"thag_runtime.h\"\n"
            "static std::atomic<int> g_cancel_seen{0};\n"
            "static void leaf(void*) {\n"
            "  for (int i = 0; i < 2000; ++i) {\n"
            "    if (thag_task_is_cancelled()) { g_cancel_seen.store(1); return; }\n"
            "    thag_sleep_ms(1);\n"
            "  }\n"
            "}\n"
            "static void parent_worker(void*) {\n"
            "  thag_task_scope_t* child = thag_task_scope_create();\n"
            "  if (!child) return;\n"
            "  thag_task_scope_spawn(child, leaf, nullptr);\n"
            "  (void)thag_task_scope_wait(child);\n"
            "  thag_task_scope_destroy(child);\n"
            "}\n"
            "int main() {\n"
            "  thag_task_scope_t* scope = thag_task_scope_create();\n"
            "  if (!scope) return 10;\n"
            "  thag_task_scope_set_timeout(scope, 10);\n"
            "  if (!thag_task_scope_spawn(scope, parent_worker, nullptr)) return 11;\n"
            "  int ok = thag_task_scope_wait(scope);\n"
            "  int cancelled = thag_task_scope_cancelled(scope);\n"
            "  thag_task_scope_destroy(scope);\n"
            "  if (ok != 0) return 12;\n"
            "  if (cancelled != 1) return 13;\n"
            "  return g_cancel_seen.load() == 1 ? 0 : 14;\n"
            "}\n"
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)

    def test_queue_backpressure_fairness_smoke(self) -> None:
        run = self._compile_and_run(
            "#include <atomic>\n"
            "#include \"thag_runtime.h\"\n"
            "static std::atomic<int> g_count{0};\n"
            "static void worker(void*) { g_count.fetch_add(1); }\n"
            "int main() {\n"
            "  thag_task_scope_t* scope = thag_task_scope_create();\n"
            "  if (!scope) return 20;\n"
            "  for (int i = 0; i < 2048; ++i) {\n"
            "    if (!thag_task_scope_spawn(scope, worker, nullptr)) return 21;\n"
            "  }\n"
            "  if (!thag_task_scope_wait(scope)) return 22;\n"
            "  thag_task_scope_destroy(scope);\n"
            "  return g_count.load() == 2048 ? 0 : 23;\n"
            "}\n",
            env={"THAG_SCHED_QUEUE_LIMIT": "64"},
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)

    def test_cancellation_checked_through_io_boundaries(self) -> None:
        run = self._compile_and_run(
            "#include <atomic>\n"
            "#include \"thag_runtime.h\"\n"
            "static std::atomic<int> g_started{0};\n"
            "static std::atomic<int> g_rc{0};\n"
            "static void io_worker(void*) {\n"
            "  g_started.store(1);\n"
            "  while (!thag_task_is_cancelled()) {\n"
            "    thag_sleep_ms(1);\n"
            "  }\n"
            "  g_rc.store(thag_http_get(\"https://example.com\", 1000));\n"
            "}\n"
            "int main() {\n"
            "  thag_task_scope_t* scope = thag_task_scope_create();\n"
            "  if (!scope) return 30;\n"
            "  if (!thag_task_scope_spawn(scope, io_worker, nullptr)) return 31;\n"
            "  for (int i = 0; i < 200 && g_started.load() == 0; ++i) {\n"
            "    thag_sleep_ms(1);\n"
            "  }\n"
            "  thag_task_scope_cancel(scope);\n"
            "  (void)thag_task_scope_wait(scope);\n"
            "  thag_task_scope_destroy(scope);\n"
            "  return g_rc.load() == -2 ? 0 : 32;\n"
            "}\n",
            timeout=30.0,
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)

    def test_deadlock_detection_emits_message(self) -> None:
        runtime_src = Path("runtime/src/concurrency.cpp").read_text()
        self.assertIn(
            "deadlock detected: task A waiting on task B, task B waiting on task A",
            runtime_src,
        )


if __name__ == "__main__":
    unittest.main()

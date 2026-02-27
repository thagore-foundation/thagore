import ctypes.util
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


def _find_runtime_lib() -> Path | None:
    for candidate in (
        Path("build-llvm21-run/runtime/libthag_runtime.a"),
        Path("build-gcc-llvm21-clean/runtime/libthag_runtime.a"),
        Path("build/runtime/libthag_runtime.a"),
    ):
        if candidate.exists():
            return candidate.resolve()
    return None


class ConcurrencyRegressionNativeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.runtime_lib = _find_runtime_lib()
        self.cxx = shutil.which("g++")
        if self.runtime_lib is None:
            self.skipTest("runtime static library not found; build runtime first")
        if self.cxx is None:
            self.skipTest("g++ not found")

    def _compile_and_run(self, source: str) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "concurrency_check.cpp"
            out = root / "concurrency_check"
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
            return subprocess.run([str(out)], capture_output=True, text=True, check=False)

    def test_task_scope_race_regression(self) -> None:
        run = self._compile_and_run(
            "#include <atomic>\n"
            "#include \"thag_runtime.h\"\n"
            "static std::atomic<int> g_count{0};\n"
            "static void worker(void*) { g_count.fetch_add(1); }\n"
            "int main() {\n"
            "  thag_task_scope_t* scope = thag_task_scope_create();\n"
            "  if (!scope) return 10;\n"
            "  for (int i = 0; i < 512; ++i) {\n"
            "    if (!thag_task_scope_spawn(scope, worker, nullptr)) return 11;\n"
            "  }\n"
            "  if (!thag_task_scope_wait(scope)) return 12;\n"
            "  thag_task_scope_destroy(scope);\n"
            "  return g_count.load() == 512 ? 0 : 13;\n"
            "}\n"
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)

    def test_task_scope_timeout_cancel_regression(self) -> None:
        run = self._compile_and_run(
            "#include \"thag_runtime.h\"\n"
            "static void sleepy(void*) { thag_sleep_ms(50); }\n"
            "int main() {\n"
            "  thag_task_scope_t* scope = thag_task_scope_create();\n"
            "  if (!scope) return 20;\n"
            "  thag_task_scope_set_timeout_ms(scope, 5);\n"
            "  if (!thag_task_scope_spawn(scope, sleepy, nullptr)) return 21;\n"
            "  if (!thag_task_scope_spawn(scope, sleepy, nullptr)) return 22;\n"
            "  int ok = thag_task_scope_wait(scope);\n"
            "  int cancelled = thag_task_scope_cancelled(scope);\n"
            "  thag_task_scope_destroy(scope);\n"
            "  if (ok != 0) return 23;\n"
            "  return cancelled == 1 ? 0 : 24;\n"
            "}\n"
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)

    def test_async_runtime_event_loop_regression(self) -> None:
        run = self._compile_and_run(
            "#include <atomic>\n"
            "#include \"thag_runtime.h\"\n"
            "static std::atomic<int> g_count{0};\n"
            "static void worker(void*) { g_count.fetch_add(1); }\n"
            "int main() {\n"
            "  thag_async_runtime_t* rt = thag_async_runtime_create();\n"
            "  if (!rt) return 30;\n"
            "  if (!thag_async_spawn(rt, worker, nullptr)) return 31;\n"
            "  if (!thag_async_sleep(rt, 1, worker, nullptr)) return 32;\n"
            "  if (!thag_async_wait_idle(rt, 1000)) return 33;\n"
            "  thag_async_runtime_destroy(rt);\n"
            "  return g_count.load() == 2 ? 0 : 34;\n"
            "}\n"
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)


if __name__ == "__main__":
    unittest.main()

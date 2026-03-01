import ctypes.util
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


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


class ConcurrencyRegressionNativeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.runtime_lib = _find_runtime_lib()
        self.cxx = shutil.which("g++")
        self.thagc = resolve_thagc_bin()
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

    def test_child_scope_cannot_leak_outside_parent(self) -> None:
        run = self._compile_and_run(
            "#include <atomic>\n"
            "#include \"thag_runtime.h\"\n"
            "static std::atomic<int> g_done{0};\n"
            "static void child_worker(void*) { g_done.fetch_add(1); }\n"
            "static void parent_worker(void*) {\n"
            "  thag_task_scope_t* child = thag_task_scope_create();\n"
            "  thag_task_scope_spawn(child, child_worker, nullptr);\n"
            "}\n"
            "int main() {\n"
            "  thag_task_scope_t* parent = thag_task_scope_create();\n"
            "  if (!parent) return 40;\n"
            "  if (!thag_task_scope_spawn(parent, parent_worker, nullptr)) return 41;\n"
            "  int ok = thag_task_scope_wait(parent);\n"
            "  thag_task_scope_destroy(parent);\n"
            "  if (!ok) return 42;\n"
            "  return g_done.load() == 1 ? 0 : 43;\n"
            "}\n"
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)

    def test_cancel_propagates_to_nested_children(self) -> None:
        run = self._compile_and_run(
            "#include <atomic>\n"
            "#include \"thag_runtime.h\"\n"
            "static std::atomic<int> g_cancel_seen{0};\n"
            "static void sleepy(void*) {\n"
            "  for (int i = 0; i < 200; ++i) {\n"
            "    if (thag_task_is_cancelled()) { g_cancel_seen.store(1); return; }\n"
            "    thag_sleep_ms(1);\n"
            "  }\n"
            "}\n"
            "static void spawn_child(void*) {\n"
            "  thag_task_scope_t* child = thag_task_scope_create();\n"
            "  thag_task_scope_spawn(child, sleepy, nullptr);\n"
            "}\n"
            "int main() {\n"
            "  thag_task_scope_t* parent = thag_task_scope_create();\n"
            "  if (!parent) return 50;\n"
            "  if (!thag_task_scope_spawn(parent, spawn_child, nullptr)) return 51;\n"
            "  thag_sleep_ms(5);\n"
            "  thag_task_scope_cancel(parent);\n"
            "  (void)thag_task_scope_wait(parent);\n"
            "  thag_task_scope_destroy(parent);\n"
            "  return g_cancel_seen.load() == 1 ? 0 : 52;\n"
            "}\n"
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)

    def test_nursery_scope_enforces_child_lifetime(self) -> None:
        run = self._compile_and_run(
            "#include <atomic>\n"
            "#include \"thag_runtime.h\"\n"
            "static std::atomic<int> g_done{0};\n"
            "static void worker(void*) {\n"
            "  thag_sleep_ms(1);\n"
            "  g_done.fetch_add(1);\n"
            "}\n"
            "int main() {\n"
            "  thag_task_scope_t* nursery = thag_nursery_create();\n"
            "  if (!nursery) return 60;\n"
            "  for (int i = 0; i < 64; ++i) {\n"
            "    if (!thag_task_scope_spawn(nursery, worker, nullptr)) return 61;\n"
            "  }\n"
            "  int ok = thag_task_scope_wait(nursery);\n"
            "  thag_task_scope_destroy(nursery);\n"
            "  if (!ok) return 62;\n"
            "  return g_done.load() == 64 ? 0 : 63;\n"
            "}\n"
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)

    def test_rc_rejected_across_task_boundary(self) -> None:
        if self.thagc is None:
            self.skipTest("thagc binary not found; set THAGC_BIN or build compiler first")
        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / "main.tg"
            out = Path(td) / "main.bin"
            src.write_text(
                "func main():\n"
                "  let conn: Rc = 1\n"
                "  spawn(conn)\n"
                "  return 0\n"
            )
            build = subprocess.run(
                [str(self.thagc), "build", str(src), "-o", str(out)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertNotEqual(build.returncode, 0)
            self.assertIn("E_SEND_SYNC_004", build.stderr)


if __name__ == "__main__":
    unittest.main()

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


class RuntimeNativeBehaviorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.runtime_lib = _find_runtime_lib()
        self.cxx = shutil.which("g++")
        if self.runtime_lib is None:
            self.skipTest("runtime static library not found; build runtime first")
        if self.cxx is None:
            self.skipTest("g++ not found")

    def test_runtime_api_behavior(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "runtime_check.cpp"
            out = root / "runtime_check"
            src.write_text(
                "#include <atomic>\n"
                "#include <cstdint>\n"
                "#include <cstring>\n"
                "#include \"thag_runtime.h\"\n"
                "\n"
                "static std::atomic<int> g_count{0};\n"
                "static void worker(void*) { g_count.fetch_add(1); }\n"
                "\n"
                "int main() {\n"
                "  int64_t t0 = thag_now_ms();\n"
                "  thag_sleep_ms(1);\n"
                "  int64_t t1 = thag_now_ms();\n"
                "  if (t1 < t0) return 11;\n"
                "\n"
                "  void* m = thag_map_new();\n"
                "  if (!m) return 12;\n"
                "  if (!thag_map_put(m, \"k\", \"v\")) return 13;\n"
                "  const char* got = thag_map_get(m, \"k\");\n"
                "  if (!got || std::strcmp(got, \"v\") != 0) return 14;\n"
                "  thag_map_free(m);\n"
                "\n"
                "  if (thag_http_get(\"https://example.com\", 10) != 200) return 15;\n"
                "  if (thag_http_post(\"https://example.com\", \"x\", 10) <= 0) return 16;\n"
                "\n"
                "  int ws = thag_ws_connect(\"ws://local\", 10);\n"
                "  if (ws <= 0) return 17;\n"
                "  if (thag_ws_send(ws, \"hello\") <= 0) return 18;\n"
                "  if (!thag_ws_close(ws)) return 19;\n"
                "\n"
                "  int db = thag_db_connect(\"memory://\");\n"
                "  if (db <= 0) return 20;\n"
                "  if (thag_db_query(db, \"SELECT 1\") != 1) return 21;\n"
                "  if (!thag_db_close(db)) return 22;\n"
                "\n"
                "  thag_task_scope_t* scope = thag_task_scope_create();\n"
                "  if (!scope) return 23;\n"
                "  if (!thag_task_scope_spawn(scope, worker, nullptr)) return 24;\n"
                "  if (!thag_task_scope_spawn(scope, worker, nullptr)) return 25;\n"
                "  if (!thag_task_scope_wait(scope)) return 26;\n"
                "  thag_task_scope_destroy(scope);\n"
                "  if (g_count.load() != 2) return 27;\n"
                "  return 0;\n"
                "}\n"
            )
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
            comp = subprocess.run(compile_cmd, capture_output=True, text=True, check=False)
            self.assertEqual(comp.returncode, 0, msg=comp.stderr)
            run = subprocess.run([str(out)], capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 0, msg=run.stderr)


if __name__ == "__main__":
    unittest.main()

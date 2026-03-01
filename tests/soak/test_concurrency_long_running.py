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


class ConcurrencyLongRunningSoakTests(unittest.TestCase):
    def setUp(self) -> None:
        self.runtime_lib = _find_runtime_lib()
        self.cxx = shutil.which("g++")
        if self.runtime_lib is None:
            self.skipTest("runtime static library not found; build runtime first")
        if self.cxx is None:
            self.skipTest("g++ not found")

    def test_long_running_scoped_workload(self) -> None:
        seconds = int(os.environ.get("THAG_SOAK_LONG_SECONDS", "0"))
        if seconds < 3600:
            self.skipTest("set THAG_SOAK_LONG_SECONDS>=3600 to run the GA long-running soak lane")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "scope_long_soak.cpp"
            out = root / "scope_long_soak"
            src.write_text(
                "#include <atomic>\n"
                "#include \"thag_runtime.h\"\n"
                "static std::atomic<int> g_counter{0};\n"
                "static void worker(void*) {\n"
                "  if (thag_task_is_cancelled()) {\n"
                "    return;\n"
                "  }\n"
                "  g_counter.fetch_add(1);\n"
                "}\n"
                "int main() {\n"
                f"  const int duration_seconds = {seconds};\n"
                "  const int64_t deadline = thag_now_ms() + static_cast<int64_t>(duration_seconds) * 1000;\n"
                "  while (thag_now_ms() < deadline) {\n"
                "    thag_task_scope_t* scope = thag_task_scope_create();\n"
                "    if (!scope) return 10;\n"
                "    for (int i = 0; i < 128; ++i) {\n"
                "      if (!thag_task_scope_spawn(scope, worker, nullptr)) return 11;\n"
                "    }\n"
                "    if (!thag_task_scope_wait(scope)) return 12;\n"
                "    thag_task_scope_destroy(scope);\n"
                "  }\n"
                "  return g_counter.load() > 0 ? 0 : 13;\n"
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
            if ctypes.util.find_library("curl"):
                compile_cmd.insert(-2, "-lcurl")
            if ctypes.util.find_library("sqlite3"):
                compile_cmd.insert(-2, "-lsqlite3")
            if ctypes.util.find_library("ssl"):
                compile_cmd.insert(-2, "-lssl")
            if ctypes.util.find_library("crypto"):
                compile_cmd.insert(-2, "-lcrypto")
            comp = subprocess.run(compile_cmd, capture_output=True, text=True, check=False)
            if comp.returncode != 0 and "-lcurl" in compile_cmd and "cannot find -lcurl" in comp.stderr:
                comp = subprocess.run([arg for arg in compile_cmd if arg != "-lcurl"], capture_output=True, text=True, check=False)
            if comp.returncode != 0 and "-lsqlite3" in compile_cmd and "cannot find -lsqlite3" in comp.stderr:
                comp = subprocess.run([arg for arg in compile_cmd if arg != "-lsqlite3"], capture_output=True, text=True, check=False)
            if comp.returncode != 0 and "-lssl" in compile_cmd and "cannot find -lssl" in comp.stderr:
                comp = subprocess.run([arg for arg in compile_cmd if arg != "-lssl"], capture_output=True, text=True, check=False)
            if comp.returncode != 0 and "-lcrypto" in compile_cmd and "cannot find -lcrypto" in comp.stderr:
                comp = subprocess.run([arg for arg in compile_cmd if arg != "-lcrypto"], capture_output=True, text=True, check=False)
            self.assertEqual(comp.returncode, 0, msg=comp.stderr)

            run = subprocess.run(
                [str(out)],
                capture_output=True,
                text=True,
                check=False,
                timeout=float(seconds + 120),
            )
            self.assertEqual(run.returncode, 0, msg=run.stderr)


if __name__ == "__main__":
    unittest.main()

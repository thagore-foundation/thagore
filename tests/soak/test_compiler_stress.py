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


class CompilerSoakTests(unittest.TestCase):
    def setUp(self) -> None:
        self.bin = resolve_thagc_bin()
        self.runtime_lib = _find_runtime_lib()
        self.cxx = shutil.which("g++")
        if self.bin is None:
            self.skipTest("thagc binary not found; set THAGC_BIN or build compiler first")

    def test_repeat_build_run_lane(self) -> None:
        loops = 20
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for i in range(loops):
                src = root / f"case_{i}.tg"
                out = root / f"case_{i}.bin"
                src.write_text(
                    "func main():\n"
                    f"  let x = {i}\n"
                    "  return x\n"
                )
                build = subprocess.run(
                    [str(self.bin), "build", str(src), "-o", str(out)],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(build.returncode, 0, msg=f"build loop={i}: {build.stderr}")
                run = subprocess.run([str(out)], capture_output=True, text=True, check=False)
                self.assertEqual(run.returncode, i, msg=f"run loop={i}: {run.stderr}")

    def test_repeat_native_concurrency_scope_deterministic(self) -> None:
        if self.runtime_lib is None:
            self.skipTest("runtime static library not found; build runtime first")
        if self.cxx is None:
            self.skipTest("g++ not found")
        loops = 20
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "scope_stress.cpp"
            out = root / "scope_stress"
            src.write_text(
                "#include <atomic>\n"
                "#include \"thag_runtime.h\"\n"
                "static std::atomic<int> g_count{0};\n"
                "static void worker(void*) { g_count.fetch_add(1); }\n"
                "int main() {\n"
                "  g_count.store(0);\n"
                "  thag_task_scope_t* scope = thag_task_scope_create();\n"
                "  if (!scope) return 10;\n"
                "  for (int i = 0; i < 128; ++i) {\n"
                "    if (!thag_task_scope_spawn(scope, worker, nullptr)) return 11;\n"
                "  }\n"
                "  int ok = thag_task_scope_wait(scope);\n"
                "  thag_task_scope_destroy(scope);\n"
                "  if (!ok) return 12;\n"
                "  return g_count.load() == 128 ? 0 : 13;\n"
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
            if comp.returncode != 0:
                self.fail(comp.stderr)
            for i in range(loops):
                run = subprocess.run([str(out)], capture_output=True, text=True, check=False)
                self.assertEqual(run.returncode, 0, msg=f"native loop={i}: {run.stderr}")


if __name__ == "__main__":
    unittest.main()

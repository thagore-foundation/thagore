import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


class PerformanceLockdownTests(unittest.TestCase):
    def setUp(self) -> None:
        self.bin = resolve_thagc_bin()

    def test_perf_budget_contract_files_exist(self) -> None:
        required = [
            "contracts/perf/startup_budget.json",
            "contracts/perf/binary_size_budget.json",
            "contracts/perf/compile_latency_budget.json",
            "tooling/policy/check_startup_budget.py",
            "tooling/policy/check_binary_size_budget.py",
            "tooling/policy/check_compile_latency_budget.py",
            "tooling/bench/collect_metrics.py",
            "tooling/bench/compare_languages.py",
        ]
        for path in required:
            self.assertTrue(Path(path).exists(), msg=f"missing {path}")

    def test_build_rejects_invalid_opt_level(self) -> None:
        if self.bin is None:
            self.skipTest("thagc binary not found; set THAGC_BIN or build compiler first")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "invalid_opt.tg"
            out = root / "invalid_opt.bin"
            src.write_text(
                "func main():\n"
                "  return 0\n"
            )
            build = subprocess.run(
                [str(self.bin), "build", str(src), "-o", str(out), "--opt-level=9"],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertNotEqual(build.returncode, 0)
            self.assertIn("invalid --opt-level value", build.stderr)

    def test_o3_inlines_small_function_call_in_llvm_ir(self) -> None:
        if self.bin is None:
            self.skipTest("thagc binary not found; set THAGC_BIN or build compiler first")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "inline_test.tg"
            src.write_text(
                "func inc(v):\n"
                "  return v + 1\n"
                "\n"
                "func main():\n"
                "  let i = 0\n"
                "  let sum = 0\n"
                "  while (i < 128):\n"
                "    sum = sum + inc(i)\n"
                "    i = i + 1\n"
                "  return sum\n"
            )
            out_o0 = root / "inline_o0.bin"
            out_o3 = root / "inline_o3.bin"
            build_o0 = subprocess.run(
                [str(self.bin), "build", str(src), "-o", str(out_o0), "--emit-llvm", "--opt-level=0"],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(build_o0.returncode, 0, msg=build_o0.stderr)
            build_o3 = subprocess.run(
                [str(self.bin), "build", str(src), "-o", str(out_o3), "--emit-llvm", "-O3"],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(build_o3.returncode, 0, msg=build_o3.stderr)

            ll_o0 = (Path(str(out_o0) + ".ll")).read_text(encoding="utf-8")
            ll_o3 = (Path(str(out_o3) + ".ll")).read_text(encoding="utf-8")
            self.assertRegex(ll_o0, re.compile(r"call\s+.*@inc\("))
            self.assertNotRegex(ll_o3, re.compile(r"call\s+.*@inc\("))


if __name__ == "__main__":
    unittest.main()

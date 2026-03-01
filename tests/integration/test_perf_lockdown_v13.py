import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


class PerfLockdownV13Tests(unittest.TestCase):
    def setUp(self) -> None:
        self.bin = resolve_thagc_bin()
        if self.bin is None:
            self.skipTest("thagc binary not found; set THAGC_BIN or build compiler first")

    def test_benchmark_automation_outputs_metrics_and_report(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            out_json = root / "metrics.json"
            out_md = root / "report.md"
            proc = subprocess.run(
                [
                    "python3",
                    "tooling/bench/run_benchmarks.py",
                    "--thagc",
                    str(self.bin),
                    "--out-json",
                    str(out_json),
                    "--out-markdown",
                    str(out_md),
                    "--startup-iterations",
                    "2",
                    "--build-iterations",
                    "2",
                    "--run-iterations",
                    "2",
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(proc.returncode, 0, msg=proc.stderr)
            payload = json.loads(out_json.read_text())
            self.assertIn("thagc_startup_ms", payload)
            self.assertIn("thagc_build_ms", payload)
            self.assertIn("comparisons", payload)
            self.assertIn("thagore_native", payload["comparisons"])
            self.assertTrue(out_md.exists())

    def test_latency_budget_policy_accepts_reasonable_metrics(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            metrics = root / "metrics.json"
            budgets = root / "budget.json"
            metrics.write_text(
                json.dumps(
                    {
                        "thagc_startup_ms": {"p95_ms": 5.0},
                        "thagc_build_ms": {"p95_ms": 120.0},
                        "comparisons": {"thagore_native": {"available": True, "metrics": {"p95_ms": 4.0}}},
                    },
                    ensure_ascii=True,
                    indent=2,
                )
                + "\n"
            )
            budgets.write_text(
                json.dumps(
                    {
                        "linux-x86_64": {
                            "max_startup_p95_ms": 10.0,
                            "max_build_p95_ms": 200.0,
                            "max_runtime_p95_ms": 10.0,
                        }
                    },
                    ensure_ascii=True,
                    indent=2,
                )
                + "\n"
            )
            proc = subprocess.run(
                [
                    "python3",
                    "tooling/policy/check_latency_budget.py",
                    "--metrics",
                    str(metrics),
                    "--budgets",
                    str(budgets),
                    "--key",
                    "linux-x86_64",
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(proc.returncode, 0, msg=proc.stderr)


if __name__ == "__main__":
    unittest.main()

import json
import re
import unittest
from pathlib import Path


class V17AiScaleContractTests(unittest.TestCase):
    def test_roadmap_marks_v17_items_completed(self) -> None:
        roadmap = Path("ROADMAP.md").read_text()
        self.assertIn("- [x] Package ecosystem: 100+ packages trên Drago Registry", roadmap)
        self.assertIn("- [x] Có thể serve AI model inference bằng Thagore, nhanh hơn Python Flask 10x", roadmap)
        self.assertIn("| v1.7 | AI & scale | ✅ Released |", roadmap)

    def test_registry_catalog_has_100_plus_packages(self) -> None:
        text = Path("docs/community/registry-package-catalog-v1.7.md").read_text()
        items = re.findall(r"^\d+\.\s+`[^`]+`", text, flags=re.MULTILINE)
        self.assertGreaterEqual(len(items), 100)

    def test_model_serving_gate_contract_is_met(self) -> None:
        contract = json.loads(Path("contracts/perf/model_serving_gate_v1_7.json").read_text())
        metrics = json.loads(Path("docs/perf/benchmark-v1.7-model-serving.json").read_text())
        self.assertGreaterEqual(
            float(metrics["speedup_vs_flask_x"]),
            float(contract["min_speedup_vs_flask_x"]),
        )

    def test_model_serving_report_exists(self) -> None:
        report = Path("docs/perf/benchmark-v1.7-model-serving.md")
        self.assertTrue(report.exists())
        text = report.read_text()
        self.assertIn("Speedup vs Flask", text)


if __name__ == "__main__":
    unittest.main()

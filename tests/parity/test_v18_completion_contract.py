import unittest
from pathlib import Path


class V18CompletionContractTests(unittest.TestCase):
    def test_roadmap_v18_engineering_items_marked_done(self) -> None:
        roadmap = Path("ROADMAP.md").read_text(encoding="utf-8")
        self.assertIn(
            "- [x] Generic types đầy đủ (built-in generic families `Option/Result/List/Rc/Arc` with nested + arity validation)",
            roadmap,
        )
        self.assertIn(
            "- [x] Benchmark public: cạnh tranh với Go trên backend benchmarks (`docs/perf/benchmark-v1.8-public.md`, CI gate `check_public_backend_gate.py`)",
            roadmap,
        )
        self.assertIn(
            "- [x] Windows/macOS/Linux desktop app được build bằng Thagore (workflow `.github/workflows/desktop-app-matrix.yml`)",
            roadmap,
        )
        self.assertIn("v1.8 note: engineering gates are in-repo/CI verifiable.", roadmap)

    def test_ci_enforces_v18_public_backend_gate(self) -> None:
        workflow = Path(".github/workflows/ci.yml").read_text(encoding="utf-8")
        self.assertIn("check_public_backend_gate.py", workflow)
        self.assertIn("contracts/perf/backend_compete_v1_8.json", workflow)

    def test_desktop_app_matrix_workflow_exists_with_three_os(self) -> None:
        workflow = Path(".github/workflows/desktop-app-matrix.yml").read_text(encoding="utf-8")
        self.assertIn("os: [ubuntu-latest, macos-latest, windows-latest]", workflow)
        self.assertIn("examples/v1_6_drawing_app.tg", workflow)

    def test_v18_public_benchmark_artifacts_present(self) -> None:
        report = Path("docs/perf/benchmark-v1.8-public.md").read_text(encoding="utf-8")
        metrics = Path("docs/perf/benchmark-v1.8-public.json").read_text(encoding="utf-8")
        self.assertIn("| go | yes |", report)
        self.assertIn("\"thagore_native\"", metrics)
        self.assertIn("\"go\"", metrics)
        self.assertIn("\"python\"", metrics)


if __name__ == "__main__":
    unittest.main()

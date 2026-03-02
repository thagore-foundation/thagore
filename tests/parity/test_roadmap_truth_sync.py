import unittest
from pathlib import Path


class RoadmapTruthSyncTests(unittest.TestCase):
    def test_flow_runtime_execution_is_not_marked_done(self) -> None:
        roadmap = Path("ROADMAP.md").read_text()
        self.assertIn(
            "- [ ] Flow runtime/codegen execution path (flow block emits executable runtime behavior)",
            roadmap,
        )

    def test_selfhost_milestone_is_not_marked_done(self) -> None:
        roadmap = Path("ROADMAP.md").read_text()
        self.assertIn(
            "- [ ] Selfhost milestone — compiler tự compile một phần code của mình",
            roadmap,
        )
        self.assertIn(
            "Selfhost note: workflow `selfhost-readiness.yml` currently validates deterministic/soak readiness only;",
            roadmap,
        )

    def test_v14_descriptions_match_current_lsp_and_fix_scope(self) -> None:
        roadmap = Path("ROADMAP.md").read_text()
        self.assertIn(
            "- [x] LSP server MVP — protocol wiring (`--stdio`), basic completion keywords, text-search definition lookup",
            roadmap,
        )
        self.assertIn(
            "- [x] `thagc fix` — safe syntax autofix lane (normalize indent + append missing block `:`)",
            roadmap,
        )

    def test_selfhost_workflow_no_fake_hash_smoke(self) -> None:
        workflow = Path(".github/workflows/selfhost-readiness.yml").read_text()
        self.assertNotIn("echo thagc > /tmp/a.bin", workflow)
        self.assertNotIn("compare_binary_hash.py", workflow)
        self.assertIn("build and soak readiness", workflow)


if __name__ == "__main__":
    unittest.main()

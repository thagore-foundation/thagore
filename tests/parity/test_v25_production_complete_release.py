import unittest
from pathlib import Path


class V25ProductionCompleteReleaseParityTests(unittest.TestCase):
    def test_version_is_bumped_to_v25(self) -> None:
        version = Path("VERSION").read_text(encoding="utf-8").strip()
        header = Path("compiler/include/thagc/shared/version.hpp").read_text(encoding="utf-8")
        self.assertEqual(version, "2.5.0")
        self.assertIn('kCompilerVersion = "2.5.0"', header)
        self.assertIn('kCompilerVersionTag = "v2.5.0"', header)

    def test_roadmap_and_rewrite_status_mark_v25_release_cut(self) -> None:
        roadmap = Path("ROADMAP.md").read_text(encoding="utf-8")
        rewrite_status = Path("docs/architecture/rewrite-status.md").read_text(encoding="utf-8")
        self.assertIn("Current effective milestone by code audit (March 6, 2026): `v2.5`", roadmap)
        self.assertIn("| v2.5 | **Production complete** | ✅ Released |", roadmap)
        self.assertIn("## v2.5 — Production Complete ✅ Released", roadmap)
        self.assertIn("Current release cut: `v2.5.0`", rewrite_status)

    def test_release_docs_and_readme_references_exist(self) -> None:
        readme = Path("README.md").read_text(encoding="utf-8")
        self.assertTrue(Path("docs/release/v2.5.0-checklist.md").is_file())
        self.assertTrue(Path("docs/release/v2.5.0-release-notes.md").is_file())
        self.assertIn("docs/release/v2.5.0-checklist.md", readme)
        self.assertIn("docs/release/v2.5.0-release-notes.md", readme)

    def test_driver_exposes_explain_and_lsp_v25_features(self) -> None:
        parser = Path("compiler/src/driver/parser.cpp").read_text(encoding="utf-8")
        core = Path("compiler/src/driver/core.cpp").read_text(encoding="utf-8")
        lsp = Path("compiler/src/driver/lsp.cpp").read_text(encoding="utf-8")
        self.assertIn('cmd == "--explain"', parser)
        self.assertIn("CommandKind::Explain", core)
        self.assertIn('hoverProvider\\":true', lsp)
        self.assertIn('method == "textDocument/diagnostic"', lsp)


if __name__ == "__main__":
    unittest.main()

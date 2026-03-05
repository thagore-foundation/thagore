import unittest
from pathlib import Path


class V19SpanReleaseContractTests(unittest.TestCase):
    def test_roadmap_marks_v19_released(self) -> None:
        roadmap = Path("ROADMAP.md").read_text(encoding="utf-8")
        self.assertIn("| v1.9 | Span system + token pipeline | ✅ Released |", roadmap)
        self.assertIn("## v1.9 — Span System + Token Pipeline ✅ Released", roadmap)

    def test_version_semver_stays_at_or_above_v19_contract(self) -> None:
        version = Path("VERSION").read_text(encoding="utf-8").strip()
        header = Path("compiler/include/thagc/shared/version.hpp").read_text(encoding="utf-8")
        parts = version.split(".")
        self.assertEqual(len(parts), 3)
        major, minor, patch = (int(parts[0]), int(parts[1]), int(parts[2]))
        self.assertGreaterEqual((major, minor, patch), (1, 9, 0))
        self.assertIn(f'kCompilerVersion = "{version}"', header)
        self.assertIn(f'kCompilerVersionTag = "v{version}"', header)

    def test_changelog_mentions_span_aware_diagnostics(self) -> None:
        changelog = Path("CHANGELOG.md").read_text(encoding="utf-8")
        self.assertIn("span-aware diagnostics", changelog)


if __name__ == "__main__":
    unittest.main()

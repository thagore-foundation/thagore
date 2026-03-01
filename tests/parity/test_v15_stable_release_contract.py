import json
import re
import unittest
from pathlib import Path


class V15StableReleaseContractTests(unittest.TestCase):
    def test_cli_command_groups_reach_ten(self) -> None:
        manifest = json.loads(Path("contracts/manifest.json").read_text())
        groups = manifest.get("required_cli_groups", [])
        self.assertEqual(len(groups), 10)
        expected = {"build", "run", "check", "fmt", "fix", "repl", "lsp", "target", "state", "migrate"}
        self.assertEqual(set(groups), expected)

    def test_no_open_p0_p1_in_stability_registry(self) -> None:
        data = json.loads(Path("contracts/stability/p0_p1_registry.json").read_text())
        issues = data.get("issues", [])
        open_critical = []
        for issue in issues:
            severity = str(issue.get("severity", "")).upper()
            status = str(issue.get("status", "")).lower()
            if severity in {"P0", "P1"} and status not in {"closed", "resolved", "done"}:
                open_critical.append(issue)
        self.assertEqual(open_critical, [], msg=f"open critical issues: {open_critical}")

    def test_v15_docs_exist(self) -> None:
        required = [
            "docs/reference/language-reference-v1.5.md",
            "docs/reference/stdlib-reference-v1.5.md",
            "docs/reference/cli-command-groups-v1.5.md",
            "docs/tutorials/beginner.md",
            "docs/tutorials/intermediate.md",
            "docs/tutorials/advanced.md",
            "docs/tutorials/build-a-bot.md",
            "docs/vi/huong-dan-v1.5.md",
            "docs/release/v1.5-stable-checklist.md",
            "docs/runbooks/production-troubleshooting-v1.5.md",
        ]
        for path in required:
            self.assertTrue(Path(path).exists(), msg=f"missing doc: {path}")

    def test_registry_catalog_has_twenty_plus_packages(self) -> None:
        text = Path("docs/community/registry-package-catalog-v1.5.md").read_text()
        items = re.findall(r"^\d+\.\s+`[^`]+`", text, flags=re.MULTILINE)
        self.assertGreaterEqual(len(items), 20)

    def test_example_project_set_exists(self) -> None:
        required = [
            "examples/cli-tool/main.tg",
            "examples/rest-api/main.tg",
            "examples/bot/main.tg",
            "examples/algorithm-visualizer/main.tg",
        ]
        for path in required:
            self.assertTrue(Path(path).exists(), msg=f"missing example: {path}")

    def test_policy_workflow_enforces_stability_registry_gate(self) -> None:
        workflow = Path(".github/workflows/policy.yml").read_text()
        self.assertIn("check_p0_p1_registry.py --registry contracts/stability/p0_p1_registry.json", workflow)


if __name__ == "__main__":
    unittest.main()

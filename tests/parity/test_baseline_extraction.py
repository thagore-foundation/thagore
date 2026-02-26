import json
import subprocess
import tempfile
import unittest
from pathlib import Path


BRANCH = "backup/main-archive-20260226-211152"


class BaselineExtractionTests(unittest.TestCase):
    def test_extract_cli_spec(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            out = Path(td) / "cli.json"
            subprocess.check_call(
                [
                    "python3",
                    "tooling/baseline/extract_cli_spec.py",
                    "--branch",
                    BRANCH,
                    "--out",
                    str(out),
                ]
            )
            data = json.loads(out.read_text())
            self.assertIn("build", data["commands_expected"])
            self.assertIn("target", data["commands_expected"])

    def test_extract_grammar_and_semantics(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            out = Path(td)
            subprocess.check_call(
                [
                    "python3",
                    "tooling/baseline/extract_grammar_spec.py",
                    "--branch",
                    BRANCH,
                    "--out-dir",
                    str(out / "grammar"),
                ]
            )
            subprocess.check_call(
                [
                    "python3",
                    "tooling/baseline/extract_semantic_spec.py",
                    "--branch",
                    BRANCH,
                    "--out-dir",
                    str(out / "semantics"),
                ]
            )
            self.assertTrue((out / "grammar" / "tokens.yaml").exists())
            self.assertTrue((out / "semantics" / "type_rules_snapshot.yaml").exists())


if __name__ == "__main__":
    unittest.main()

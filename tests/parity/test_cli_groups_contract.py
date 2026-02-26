import json
import unittest
from pathlib import Path


class CliGroupsContractTests(unittest.TestCase):
    def test_all_required_groups_are_present_in_dispatch(self) -> None:
        required = json.loads(Path("contracts/manifest.json").read_text())["required_cli_groups"]
        core = Path("compiler/src/driver/core.cpp").read_text()
        for cmd in required:
            enum_name = cmd.capitalize()
            self.assertIn(f"CommandKind::{enum_name}", core)

    def test_no_legacy_not_implemented_handler(self) -> None:
        for path in Path("compiler/src/driver").glob("*.cpp"):
            text = path.read_text()
            self.assertNotIn("handle_not_implemented", text)


if __name__ == "__main__":
    unittest.main()


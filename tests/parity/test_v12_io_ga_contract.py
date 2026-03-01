import unittest
from pathlib import Path


class V12IoGaContractTests(unittest.TestCase):
    def test_roadmap_v12_io_items_are_checked(self) -> None:
        roadmap = Path("ROADMAP.md").read_text()
        self.assertIn("- [x] Multi-OS IO parity — Linux/macOS/Windows", roadmap)
        self.assertIn("- [x] HTTP/WebSocket/DB lanes pass trên Linux/macOS/Windows trong CI", roadmap)

    def test_io_parity_workflow_is_three_os_matrix(self) -> None:
        workflow = Path(".github/workflows/io-parity.yml").read_text()
        self.assertIn("name: IO Parity", workflow)
        self.assertIn("os: [ubuntu-latest, macos-latest, windows-latest]", workflow)
        self.assertIn("python -m unittest tests.integration.test_io_stack_ga", workflow)


if __name__ == "__main__":
    unittest.main()

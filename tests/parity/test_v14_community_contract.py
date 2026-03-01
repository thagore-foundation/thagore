import unittest
from pathlib import Path


class V14CommunityContractTests(unittest.TestCase):
    def test_roadmap_v14_community_items_are_checked(self) -> None:
        roadmap = Path("ROADMAP.md").read_text()
        self.assertIn("- [x] Drago Registry public launch", roadmap)
        self.assertIn("- [x] Contributor guide hoàn chỉnh", roadmap)
        self.assertIn("- [x] Discord/GitHub Discussions active", roadmap)
        self.assertIn("- [x] First external contributor PR merged", roadmap)

    def test_community_ops_workflow_exists(self) -> None:
        workflow = Path(".github/workflows/community-ops.yml").read_text()
        self.assertIn("release:", workflow)
        self.assertIn("Announce to Discord", workflow)
        self.assertIn("Open GitHub Discussion Announcement", workflow)

    def test_community_docs_exist(self) -> None:
        self.assertTrue(Path("docs/community/registry-public-launch.md").exists())
        self.assertTrue(Path("docs/community/external-contributor-pr-log.md").exists())
        self.assertTrue(Path("docs/contributor-guide/complete.md").exists())


if __name__ == "__main__":
    unittest.main()

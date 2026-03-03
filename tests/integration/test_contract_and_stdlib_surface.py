import unittest
from pathlib import Path


class ContractAndStdlibSurfaceTests(unittest.TestCase):
    def test_v15_contract_files_exist(self) -> None:
        required = [
            "contracts/concurrency/structured_concurrency.yaml",
            "contracts/memory/model.yaml",
            "contracts/io/client_surface.yaml",
            "contracts/deploy/story.yaml",
        ]
        for path in required:
            self.assertTrue(Path(path).exists(), msg=f"missing {path}")

    def test_stdlib_modules_exist(self) -> None:
        required = [
            "stdlib/lib/http.tg",
            "stdlib/lib/grpc.tg",
            "stdlib/lib/ws.tg",
            "stdlib/lib/db.tg",
            "stdlib/lib/sql.tg",
            "stdlib/lib/trace.tg",
            "stdlib/lib/time.tg",
            "stdlib/lib/map.tg",
            "stdlib/lib/tensor.tg",
            "stdlib/lib/gui.tg",
            "stdlib/lib/fs.tg",
            "stdlib/lib/process.tg",
            "stdlib/lib/toml.tg",
            "stdlib/std/core.tg",
            "stdlib/std/string.tg",
            "stdlib/std/list.tg",
        ]
        for path in required:
            self.assertTrue(Path(path).exists(), msg=f"missing {path}")

    def test_core_stdlib_documents_rc_arc(self) -> None:
        core = Path("stdlib/std/core.tg").read_text()
        self.assertIn("Rc<T>", core)
        self.assertIn("Arc<T>", core)
        self.assertIn("pub func rc_new", core)
        self.assertIn("pub func arc_new", core)


if __name__ == "__main__":
    unittest.main()

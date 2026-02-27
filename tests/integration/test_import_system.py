import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


class ImportSystemIntegrationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.bin = resolve_thagc_bin()
        if self.bin is None:
            self.skipTest("thagc binary not found; set THAGC_BIN or build compiler first")

    def _build(self, root: Path, entry: Path) -> subprocess.CompletedProcess[str]:
        out = root / "main.bin"
        return subprocess.run(
            [str(self.bin), "build", str(entry), "-o", str(out)],
            cwd=root,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_import_prefix_usage(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "a" / "b").mkdir(parents=True)
            (root / "a" / "b" / "c.tg").write_text(
                "func add1(v):\n"
                "  return v + 1\n"
            )
            entry = root / "main.tg"
            entry.write_text(
                "import a.b.c\n"
                "\n"
                "func main():\n"
                "  return c.add1(4)\n"
            )
            build = self._build(root, entry)
            self.assertEqual(build.returncode, 0, msg=build.stderr)
            run = subprocess.run([str(root / "main.bin")], capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 5, msg=run.stderr)

    def test_from_import_direct_usage(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "util").mkdir(parents=True)
            (root / "util" / "math.tg").write_text(
                "func triple(v):\n"
                "  return v * 3\n"
            )
            entry = root / "main.tg"
            entry.write_text(
                "from util.math import triple\n"
                "\n"
                "func main():\n"
                "  return triple(3)\n"
            )
            build = self._build(root, entry)
            self.assertEqual(build.returncode, 0, msg=build.stderr)
            run = subprocess.run([str(root / "main.bin")], capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 9, msg=run.stderr)

    def test_import_alias_conflict_resolution(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "a").mkdir(parents=True)
            (root / "b").mkdir(parents=True)
            (root / "a" / "x.tg").write_text(
                "func one():\n"
                "  return 1\n"
            )
            (root / "b" / "x.tg").write_text(
                "func two():\n"
                "  return 2\n"
            )
            entry = root / "main.tg"
            entry.write_text(
                "import a.x\n"
                "import b.x as bx\n"
                "\n"
                "func main():\n"
                "  return x.one() + bx.two()\n"
            )
            build = self._build(root, entry)
            self.assertEqual(build.returncode, 0, msg=build.stderr)
            run = subprocess.run([str(root / "main.bin")], capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 3, msg=run.stderr)

    def test_import_conflict_requires_alias(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "a").mkdir(parents=True)
            (root / "b").mkdir(parents=True)
            (root / "a" / "x.tg").write_text("func one():\n  return 1\n")
            (root / "b" / "x.tg").write_text("func two():\n  return 2\n")
            entry = root / "main.tg"
            entry.write_text(
                "import a.x\n"
                "import b.x\n"
                "\n"
                "func main():\n"
                "  return 0\n"
            )
            build = self._build(root, entry)
            self.assertNotEqual(build.returncode, 0)
            self.assertIn("import prefix conflict", build.stderr)

    def test_missing_module_error_message(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            entry = root / "main.tg"
            entry.write_text(
                "import a.b.c\n"
                "\n"
                "func main():\n"
                "  return c.run()\n"
            )
            build = self._build(root, entry)
            self.assertNotEqual(build.returncode, 0)
            self.assertIn("module `a/b/c.tg` not found", build.stderr)

    def test_circular_import_error_message(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "a").mkdir(parents=True)
            (root / "b").mkdir(parents=True)
            (root / "a" / "mod.tg").write_text(
                "import b.mod\n"
                "\n"
                "func f():\n"
                "  return mod.g()\n"
            )
            (root / "b" / "mod.tg").write_text(
                "import a.mod\n"
                "\n"
                "func g():\n"
                "  return 1\n"
            )
            entry = root / "main.tg"
            entry.write_text(
                "import a.mod\n"
                "\n"
                "func main():\n"
                "  return mod.f()\n"
            )
            build = self._build(root, entry)
            self.assertNotEqual(build.returncode, 0)
            self.assertIn("circular import detected", build.stderr)
            self.assertIn("a/mod.tg", build.stderr)
            self.assertIn("b/mod.tg", build.stderr)

    def test_package_not_in_manifest_error_message(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "thagore.toml").write_text(
                "[package]\n"
                "name = \"demo\"\n"
                "version = \"0.1.0\"\n"
                "\n"
                "[dependencies]\n"
            )
            entry = root / "main.tg"
            entry.write_text(
                "import ghostpkg\n"
                "\n"
                "func main():\n"
                "  return ghostpkg.run()\n"
            )
            build = self._build(root, entry)
            self.assertNotEqual(build.returncode, 0)
            self.assertIn("package `ghostpkg` not found in dependencies", build.stderr)


if __name__ == "__main__":
    unittest.main()

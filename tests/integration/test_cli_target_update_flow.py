import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


class CliSurfaceV12bTests(unittest.TestCase):
    def setUp(self) -> None:
        self.bin = resolve_thagc_bin()
        if self.bin is None:
            self.skipTest("thagc binary not found; set THAGC_BIN or build compiler first")

    def _run(self, cwd: Path, *args: str, extra_env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
        env = dict(os.environ)
        env["THAGORE_HOME"] = str(cwd / ".thagore-home")
        if extra_env:
            env.update(extra_env)
        return subprocess.run([str(self.bin), *args], cwd=cwd, env=env, capture_output=True, text=True, check=False)

    def test_legacy_install_update_commands_are_removed(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            install = self._run(root, "install")
            update = self._run(root, "update", "check")
            self.assertNotEqual(install.returncode, 0)
            self.assertNotEqual(update.returncode, 0)
            self.assertIn("unknown command", install.stderr)
            self.assertIn("unknown command", update.stderr)

    def test_check_command_passes_valid_program(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "ok.tg"
            src.write_text("func main():\n  return 0\n")
            check = self._run(root, "check", str(src))
            self.assertEqual(check.returncode, 0, msg=check.stderr)
            self.assertIn("check: OK", check.stdout)

    def test_fmt_command_normalizes_tabs_and_trailing_spaces(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "fmt_me.tg"
            src.write_text("func main():\n\treturn 0   \n")
            fmt = self._run(root, "fmt", str(src))
            self.assertEqual(fmt.returncode, 0, msg=fmt.stderr)
            self.assertEqual(src.read_text(), "func main():\n  return 0\n")

    def test_migrate_converts_legacy_manifest_and_lock(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "thagore.toml").write_text(
                "[package]\n"
                "name = \"demo\"\n"
                "version = \"0.1.0\"\n"
                "\n"
                "[dependencies]\n"
                "json = \"path:./deps/json\"\n"
            )
            (root / "thagore.lock").write_text(
                "[package]\n"
                "name = \"demo\"\n"
                "version = \"0.1.0\"\n"
            )
            migrate = self._run(root, "migrate")
            self.assertEqual(migrate.returncode, 0, msg=migrate.stderr)
            self.assertTrue((root / "drago.toml").exists())
            self.assertTrue((root / "drago.lock").exists())

    def test_build_delegates_to_drago_when_drago_manifest_exists(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "drago.toml").write_text(
                "[package]\n"
                "name = \"demo\"\n"
                "version = \"0.1.0\"\n"
            )
            marker = root / "drago-build.log"
            if os.name == "nt":
                drago = root / "drago.bat"
                drago.write_text("@echo off\necho %* > " + str(marker) + "\nexit /b 0\n")
            else:
                drago = root / "drago"
                drago.write_text(
                    "#!/usr/bin/env bash\n"
                    "set -euo pipefail\n"
                    "printf '%s\\n' \"$*\" > '" + str(marker) + "'\n"
                )
                drago.chmod(0o755)
            env = {"PATH": str(root) + os.pathsep + os.environ.get("PATH", "")}
            build = self._run(root, "build", extra_env=env)
            self.assertEqual(build.returncode, 0, msg=build.stderr)
            self.assertTrue(marker.exists())
            self.assertIn("build", marker.read_text())


if __name__ == "__main__":
    unittest.main()


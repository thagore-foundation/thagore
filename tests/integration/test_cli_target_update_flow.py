import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


class CliTargetUpdateFlowTests(unittest.TestCase):
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

    def _host_triple(self) -> str | None:
        for tool in ("clang", "gcc"):
            p = subprocess.run([tool, "-dumpmachine"], capture_output=True, text=True, check=False)
            if p.returncode == 0 and p.stdout.strip():
                return p.stdout.strip()
        return None

    def test_target_and_flow_journal(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "ok.tg"
            src.write_text("func main():\n  let x = 1\n  return x\n")

            install = self._run(root, "install", "toolchain", "--yes")
            self.assertEqual(install.returncode, 0, msg=install.stderr)

            add = self._run(root, "target", "add", "x86_64-unknown-linux-gnu", "--cc=clang")
            self.assertEqual(add.returncode, 0, msg=add.stderr)

            doctor = self._run(root, "target", "doctor")
            self.assertEqual(doctor.returncode, 0, msg=doctor.stderr)
            self.assertIn("OK", doctor.stdout)

            flow = self._run(root, "flow", "simulate", str(src))
            self.assertEqual(flow.returncode, 0, msg=flow.stderr)

            build_target = self._run(root, "build", str(src), "--target=x86_64-unknown-linux-gnu")
            self.assertEqual(build_target.returncode, 0, msg=build_target.stderr)

            recover = self._run(root, "flow", "recover")
            self.assertEqual(recover.returncode, 0, msg=recover.stderr)
            self.assertIn('"status": "ok"', recover.stdout)

    def test_update_dry_run(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            check = self._run(root, "update", "check")
            self.assertEqual(check.returncode, 0, msg=check.stderr)

            dry = self._run(root, "update", "apply", "--dry-run")
            self.assertEqual(dry.returncode, 0, msg=dry.stderr)
            self.assertIn("[dry-run]", dry.stdout)

    def test_update_apply_changes_reported_version(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            apply = self._run(root, "update", "apply", "v9.9.9", "--yes")
            self.assertEqual(apply.returncode, 0, msg=apply.stderr)
            self.assertIn("v9.9.9", apply.stdout)

            version = self._run(root, "--version")
            self.assertEqual(version.returncode, 0, msg=version.stderr)
            self.assertIn("thagore v9.9.9", version.stdout)

    def test_update_apply_runs_installer_when_managed_toolchain_present(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            home = root / ".thagore-home"
            bin_name = "thagc.exe" if os.name == "nt" else "thagc"
            managed_bin = home / "toolchains" / "stable" / "bin" / bin_name
            managed_bin.parent.mkdir(parents=True, exist_ok=True)
            managed_bin.write_text("placeholder\n")

            marker = root / "installer-args.txt"
            if os.name == "nt":
                script = root / "fake_thagup.ps1"
                script.write_text(
                    "$args | Out-File -FilePath "
                    + "'" + str(marker).replace("\\", "\\\\") + "'"
                    + " -Encoding utf8\n"
                )
            else:
                script = root / "fake_thagup.sh"
                script.write_text(
                    "#!/usr/bin/env bash\n"
                    "set -euo pipefail\n"
                    "printf '%s\\n' \"$@\" > " + "'" + str(marker) + "'\n"
                )
                script.chmod(0o755)

            apply = self._run(
                root,
                "update",
                "apply",
                "v1.2.3",
                "--yes",
                extra_env={"THAGC_UPDATE_SCRIPT_URL": script.resolve().as_uri()},
            )
            self.assertEqual(apply.returncode, 0, msg=apply.stderr)
            self.assertTrue(marker.exists(), msg="installer script did not run")
            args_text = marker.read_text()
            if os.name == "nt":
                self.assertIn("-Tag", args_text)
                self.assertIn("v1.2.3", args_text)
            else:
                self.assertIn("--tag", args_text)
                self.assertIn("v1.2.3", args_text)
            current = (home / "current-version.txt").read_text().strip()
            self.assertEqual(current, "v1.2.3")

    def test_build_target_one_command_auto_init(self) -> None:
        triple = self._host_triple()
        if not triple:
            self.skipTest("no compiler triple available")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "main.tg"
            out = root / "main.bin"
            src.write_text("func main():\n  return 0\n")
            build = self._run(root, "build", str(src), "-o", str(out), f"--target={triple}")
            self.assertEqual(build.returncode, 0, msg=build.stderr)
            self.assertTrue(out.exists())
            manifest = root / ".thagc" / "targets" / triple / "manifest.json"
            self.assertTrue(manifest.exists(), msg=f"missing manifest {manifest}")


if __name__ == "__main__":
    unittest.main()

import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


class V16JoyReleaseIntegrationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.bin = resolve_thagc_bin()
        if self.bin is None:
            self.skipTest("thagc binary not found; set THAGC_BIN or build compiler first")

    def _build_and_run(self, source: str) -> tuple[subprocess.CompletedProcess[str], subprocess.CompletedProcess[str], Path]:
        td = tempfile.TemporaryDirectory()
        root = Path(td.name)
        src = root / "main.tg"
        out = root / "main.bin"
        src.write_text(source)
        build = subprocess.run(
            [str(self.bin), "build", str(src), "-o", str(out)],
            capture_output=True,
            text=True,
            check=False,
        )
        self.addCleanup(td.cleanup)
        if build.returncode != 0:
            return build, subprocess.CompletedProcess(args=[str(out)], returncode=1, stdout="", stderr=""), root
        run = subprocess.run([str(out)], cwd=root, capture_output=True, text=True, check=False)
        return build, run, root

    def test_gui_canvas_drawing_app_renders_ppm_frame(self) -> None:
        build, run, _ = self._build_and_run(
            "from lib.gui import canvas\n"
            "from lib.gui import clear\n"
            "from lib.gui import draw_line\n"
            "from lib.gui import draw_point\n"
            "from lib.gui import present\n"
            "from lib.gui import last_frame_path\n"
            "from lib.gui import destroy\n"
            "\n"
            "func main():\n"
            "  let c = canvas(32, 32, \"joy_test\")\n"
            "  clear(c, 16777215)\n"
            "  draw_line(c, 0, 0, 31, 31, 255)\n"
            "  draw_point(c, 8, 16, 16711680)\n"
            "  present(c)\n"
            "  print(last_frame_path(c))\n"
            "  destroy(c)\n"
            "  return 0\n"
        )
        self.assertEqual(build.returncode, 0, msg=build.stderr)
        self.assertEqual(run.returncode, 0, msg=run.stderr)
        lines = [line.strip() for line in run.stdout.splitlines() if line.strip()]
        self.assertGreater(len(lines), 0, msg="expected printed frame path")
        frame_path = Path(lines[-1])
        self.assertTrue(frame_path.exists(), msg=f"frame path not found: {frame_path}")
        header = frame_path.read_text().splitlines()[:1]
        self.assertEqual(header, ["P3"])

    def test_gui_fixed_timestep_returns_requested_steps(self) -> None:
        build, run, _ = self._build_and_run(
            "from lib.gui import canvas\n"
            "from lib.gui import run_fixed_timestep\n"
            "from lib.gui import destroy\n"
            "\n"
            "func main():\n"
            "  let c = canvas(8, 8, \"loop_test\")\n"
            "  let n = run_fixed_timestep(c, 120, 4)\n"
            "  destroy(c)\n"
            "  return n\n"
        )
        self.assertEqual(build.returncode, 0, msg=build.stderr)
        self.assertEqual(run.returncode, 4, msg=run.stderr)


if __name__ == "__main__":
    unittest.main()

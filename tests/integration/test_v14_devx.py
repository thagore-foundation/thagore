import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


def _frame(payload: str) -> bytes:
    encoded = payload.encode("utf-8")
    return f"Content-Length: {len(encoded)}\r\n\r\n".encode("utf-8") + encoded


class V14DevxIntegrationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.bin = resolve_thagc_bin()
        if self.bin is None:
            self.skipTest("thagc binary not found; set THAGC_BIN or build compiler first")

    def _run(self, cwd: Path, *args: str, input_text: str | None = None) -> subprocess.CompletedProcess[str]:
        env = dict(os.environ)
        env["THAGORE_HOME"] = str(cwd / ".thagore-home")
        return subprocess.run(
            [str(self.bin), *args],
            cwd=cwd,
            env=env,
            input=input_text,
            text=True,
            capture_output=True,
            check=False,
        )

    def test_fix_autofixes_missing_colon_and_tabs(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "fix_me.tg"
            src.write_text("func main()\n\treturn 0   \n")
            fixed = self._run(root, "fix", str(src))
            self.assertEqual(fixed.returncode, 0, msg=fixed.stderr)
            self.assertEqual(src.read_text(), "func main():\n  return 0\n")

    def test_repl_can_run_buffered_program(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            repl_input = "let x = 3\nprint(x + 4)\n:run\n:quit\n"
            repl = self._run(root, "repl", input_text=repl_input)
            self.assertEqual(repl.returncode, 0, msg=repl.stderr)
            self.assertIn("7", repl.stdout)
            self.assertIn("exit code: 0", repl.stdout)

    def test_lsp_stdio_initialize_completion_definition(self) -> None:
        initialize = (
            '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":null,'
            '"capabilities":{}}}'
        )
        did_open = (
            '{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///main.tg",'
            '"languageId":"thagore","version":1,"text":"func greet():\\n  return 0\\nfunc main():\\n  greet()\\n"}}}'
        )
        completion = (
            '{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///main.tg"},'
            '"position":{"line":3,"character":2}}}'
        )
        definition = (
            '{"jsonrpc":"2.0","id":3,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///main.tg"},'
            '"position":{"line":3,"character":3}}}'
        )
        shutdown = '{"jsonrpc":"2.0","id":4,"method":"shutdown","params":{}}'
        exit_msg = '{"jsonrpc":"2.0","method":"exit","params":{}}'
        payload = b"".join(
            [_frame(initialize), _frame(did_open), _frame(completion), _frame(definition), _frame(shutdown), _frame(exit_msg)]
        )

        proc = subprocess.Popen(
            [str(self.bin), "lsp", "--stdio"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = proc.communicate(payload, timeout=20)
        self.assertEqual(proc.returncode, 0, msg=err.decode("utf-8", errors="ignore"))
        text = out.decode("utf-8", errors="ignore")
        self.assertIn('"definitionProvider":true', text)
        self.assertIn('"label":"func"', text)
        self.assertIn('"uri":"file:///main.tg"', text)


if __name__ == "__main__":
    unittest.main()

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


def _frame(payload: str) -> bytes:
    encoded = payload.encode("utf-8")
    return f"Content-Length: {len(encoded)}\r\n\r\n".encode("utf-8") + encoded


class V25ProductionCompleteIntegrationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.bin = resolve_thagc_bin()
        if self.bin is None:
            self.skipTest("thagc binary not found; set THAGC_BIN or build compiler first")

    def _run(self, cwd: Path, *args: str) -> subprocess.CompletedProcess[str]:
        env = dict(os.environ)
        env["THAGORE_HOME"] = str(cwd / ".thagore-home")
        return subprocess.run(
            [str(self.bin), *args],
            cwd=cwd,
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_explain_emits_error_documentation(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            result = self._run(root, "--explain", "E_MOD_002")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertIn("E_MOD_002", result.stdout)
            self.assertIn("Import resolution failed", result.stdout)
            self.assertIn("help:", result.stdout)

    def test_lsp_hover_and_diagnostic_requests(self) -> None:
        initialize = (
            '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":null,'
            '"capabilities":{}}}'
        )
        did_open = (
            '{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///main.tg",'
            '"languageId":"thagore","version":1,"text":"func main()\\n\\tlet answer = 42\\n  return answer\\n"}}}'
        )
        hover = (
            '{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///main.tg"},'
            '"position":{"line":2,"character":9}}}'
        )
        diagnostic = (
            '{"jsonrpc":"2.0","id":3,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":"file:///main.tg"}}}'
        )
        shutdown = '{"jsonrpc":"2.0","id":4,"method":"shutdown","params":{}}'
        exit_msg = '{"jsonrpc":"2.0","method":"exit","params":{}}'

        payload = b"".join(
            [_frame(initialize), _frame(did_open), _frame(hover), _frame(diagnostic), _frame(shutdown), _frame(exit_msg)]
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
        self.assertIn('"hoverProvider":true', text)
        self.assertIn('"method":"textDocument/publishDiagnostics"', text)
        self.assertIn("E_PARSE_001", text)
        self.assertIn("W_STYLE_TAB", text)
        self.assertIn("answer: i64", text)


if __name__ == "__main__":
    unittest.main()

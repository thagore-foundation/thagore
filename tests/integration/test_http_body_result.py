import http.server
import socketserver
import threading
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


class _QuietHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        return


class _EchoHandler(_QuietHandler):
    def do_GET(self):
        if self.path == "/ping":
            body = b"pong"
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        self.send_response(404)
        self.end_headers()

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        payload = self.rfile.read(length) if length > 0 else b""
        body = b"echo:" + payload
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def start_server(handler_cls):
    httpd = socketserver.TCPServer(("127.0.0.1", 0), handler_cls)
    port = httpd.server_address[1]
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    return httpd, port


class HttpBodyResultTests(unittest.TestCase):
    def setUp(self) -> None:
        self.bin = resolve_thagc_bin()
        if self.bin is None:
            self.skipTest("thagc binary not found; set THAGC_BIN or build compiler first")

    def _build_and_run(self, source: str) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "main.tg"
            out = root / "main.bin"
            src.write_text(source)
            build = subprocess.run(
                [str(self.bin), "build", str(src), "-o", str(out)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(build.returncode, 0, msg=build.stderr)
            return subprocess.run([str(out)], capture_output=True, text=True, check=False, cwd=root)

    def test_http_get_result_captures_body(self) -> None:
        server, port = start_server(_EchoHandler)
        try:
            run = self._build_and_run(
                "extern func thag_http_get_result(url: ptr, timeout_ms: i32) -> ptr\n"
                "extern func thag_http_result_status(res: ptr) -> i32\n"
                "extern func thag_http_result_body(res: ptr) -> ptr\n"
                "extern func thag_http_result_free(res: ptr) -> void\n"
                "extern func thag_http_result_is_null(res: ptr) -> i32\n"
                "extern func thag_str_equals(a: ptr, b: ptr) -> i32\n"
                "extern func thag_map_is_null_ptr(ptr: ptr) -> i32\n"
                "\n"
                "func main():\n"
                "  let url = \"http://127.0.0.1:" + str(port) + "/ping\"\n"
                "  let res: ptr = thag_http_get_result(url, 5000)\n"
                "  if (thag_http_result_is_null(res) == 1): return 71\n"
                "  let status: i32 = thag_http_result_status(res)\n"
                "  if (status != 200): thag_http_result_free(res); return 72\n"
                "  let body: ptr = thag_http_result_body(res)\n"
                "  if (thag_str_equals(body, \"pong\") != 1): thag_http_result_free(res); return 73\n"
                "  thag_http_result_free(res)\n"
                "  return 0\n"
            )
            self.assertEqual(run.returncode, 0, msg=run.stderr)
        finally:
            server.shutdown()
            server.server_close()

    def test_http_post_result_captures_body(self) -> None:
        server, port = start_server(_EchoHandler)
        try:
            run = self._build_and_run(
                "extern func thag_http_post_result(url: ptr, payload: ptr, timeout_ms: i32) -> ptr\n"
                "extern func thag_http_result_status(res: ptr) -> i32\n"
                "extern func thag_http_result_body(res: ptr) -> ptr\n"
                "extern func thag_http_result_free(res: ptr) -> void\n"
                "extern func thag_http_result_is_null(res: ptr) -> i32\n"
                "extern func thag_str_contains(a: ptr, sub: ptr) -> i32\n"
                "extern func thag_map_is_null_ptr(ptr: ptr) -> i32\n"
                "\n"
                "func main():\n"
                "  let url = \"http://127.0.0.1:" + str(port) + "/echo\"\n"
                "  let res: ptr = thag_http_post_result(url, \"data\", 5000)\n"
                "  if (thag_http_result_is_null(res) == 1): return 81\n"
                "  let status: i32 = thag_http_result_status(res)\n"
                "  if (status != 200): thag_http_result_free(res); return 82\n"
                "  let body: ptr = thag_http_result_body(res)\n"
                "  if (thag_str_contains(body, \"data\") != 1): thag_http_result_free(res); return 83\n"
                "  thag_http_result_free(res)\n"
                "  return 0\n"
            )
            self.assertEqual(run.returncode, 0, msg=run.stderr)
        finally:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    unittest.main()

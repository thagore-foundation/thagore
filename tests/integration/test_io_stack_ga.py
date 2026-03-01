import subprocess
import tempfile
import unittest
from pathlib import Path

from tests._support import resolve_thagc_bin


class IoStackGATests(unittest.TestCase):
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

    def test_http_retry_backoff_contract(self) -> None:
        run = self._build_and_run(
            "extern func thag_now_ms() -> i64\n"
            "extern func thag_http_get_retry(url: ptr, timeout_ms: i32, retries: i32, backoff_ms: i32) -> i32\n"
            "extern func thag_str_concat(a: ptr, b: ptr) -> ptr\n"
            "extern func thag_str_free(s: ptr) -> void\n"
            "\n"
            "func main():\n"
            "  let t0: i64 = thag_now_ms()\n"
            "  let prefix = thag_str_concat(\"http:\", \"/\")\n"
            "  let url = thag_str_concat(prefix, \"/127.0.0.1:1\")\n"
            "  let rc: i32 = thag_http_get_retry(url, 5, 2, 20)\n"
            "  thag_str_free(prefix)\n"
            "  thag_str_free(url)\n"
            "  let dt: i64 = thag_now_ms() - t0\n"
            "  if (rc == -2):\n"
            "    return 0\n"
            "  if (rc == 599):\n"
            "    if (dt < 50):\n"
            "      return 11\n"
            "    return 0\n"
            "  return 12\n"
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)

    def test_ws_retry_backoff_contract(self) -> None:
        run = self._build_and_run(
            "extern func thag_now_ms() -> i64\n"
            "extern func thag_ws_connect_retry(endpoint: ptr, timeout_ms: i32, retries: i32, backoff_ms: i32) -> i32\n"
            "extern func thag_ws_close(handle: i32) -> i32\n"
            "extern func thag_str_concat(a: ptr, b: ptr) -> ptr\n"
            "extern func thag_str_free(s: ptr) -> void\n"
            "\n"
            "func main():\n"
            "  let t0: i64 = thag_now_ms()\n"
            "  let prefix = thag_str_concat(\"ws:\", \"/\")\n"
            "  let endpoint = thag_str_concat(prefix, \"/127.0.0.1:1\")\n"
            "  let h: i32 = thag_ws_connect_retry(endpoint, 5, 2, 20)\n"
            "  thag_str_free(prefix)\n"
            "  thag_str_free(endpoint)\n"
            "  let dt: i64 = thag_now_ms() - t0\n"
            "  if (h == -2):\n"
            "    return 0\n"
            "  if (h > 0):\n"
            "    thag_ws_close(h)\n"
            "    return 0\n"
            "  if (h == 0):\n"
            "    if (dt < 50):\n"
            "      return 21\n"
            "    return 0\n"
            "  return 22\n"
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)

    def test_db_retry_and_query_contract(self) -> None:
        run = self._build_and_run(
            "extern func thag_now_ms() -> i64\n"
            "extern func thag_db_connect(dsn: ptr) -> i32\n"
            "extern func thag_db_connect_retry(dsn: ptr, retries: i32, backoff_ms: i32) -> i32\n"
            "extern func thag_db_query_retry(handle: i32, query: ptr, retries: i32, backoff_ms: i32) -> i32\n"
            "extern func thag_db_close(handle: i32) -> i32\n"
            "extern func thag_str_concat(a: ptr, b: ptr) -> ptr\n"
            "extern func thag_str_free(s: ptr) -> void\n"
            "\n"
            "func main():\n"
            "  let db: i32 = thag_db_connect(\"tmp_v12_io.db\")\n"
            "  if (db <= 0):\n"
            "    return 31\n"
            "  if (thag_db_query_retry(db, \"SELECT 1\", 2, 5) != 1):\n"
            "    return 32\n"
            "  if (thag_db_close(db) != 1):\n"
            "    return 33\n"
            "  let t0: i64 = thag_now_ms()\n"
            "  let prefix = thag_str_concat(\"bad:\", \"/\")\n"
            "  let bad_dsn = thag_str_concat(prefix, \"/dsn\")\n"
            "  let bad: i32 = thag_db_connect_retry(bad_dsn, 2, 20)\n"
            "  thag_str_free(prefix)\n"
            "  thag_str_free(bad_dsn)\n"
            "  let dt: i64 = thag_now_ms() - t0\n"
            "  if (bad == -2):\n"
            "    return 0\n"
            "  if (bad == 0):\n"
            "    if (dt < 50):\n"
            "      return 34\n"
            "    return 0\n"
            "  return 35\n"
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)

    def test_json_runtime_contract(self) -> None:
        run = self._build_and_run(
            "extern func thag_json_parse(content: ptr) -> ptr\n"
            "extern func thag_json_get_str(handle: ptr, key: ptr) -> ptr\n"
            "extern func thag_json_get_int(handle: ptr, key: ptr) -> i64\n"
            "extern func thag_json_set_int(handle: ptr, key: ptr, value: i64) -> i32\n"
            "extern func thag_json_stringify(handle: ptr) -> ptr\n"
            "extern func thag_json_free(handle: ptr) -> void\n"
            "extern func thag_str_contains(s: ptr, sub: ptr) -> i32\n"
            "extern func thag_str_equals(a: ptr, b: ptr) -> i32\n"
            "extern func thag_str_free(s: ptr) -> void\n"
            "extern func thag_map_is_null_ptr(ptr: ptr) -> i32\n"
            "\n"
            "func main():\n"
            "  let doc = thag_json_parse(\"{\\\"name\\\":\\\"drago\\\",\\\"count\\\":7}\")\n"
            "  if (thag_map_is_null_ptr(doc) == 1):\n"
            "    return 41\n"
            "  let name = thag_json_get_str(doc, \"name\")\n"
            "  if (thag_map_is_null_ptr(name) == 1):\n"
            "    return 42\n"
            "  if (thag_str_equals(name, \"drago\") != 1):\n"
            "    return 43\n"
            "  thag_str_free(name)\n"
            "  if (thag_json_get_int(doc, \"count\") != 7):\n"
            "    return 44\n"
            "  if (thag_json_set_int(doc, \"count\", 9) != 1):\n"
            "    return 45\n"
            "  let out = thag_json_stringify(doc)\n"
            "  if (thag_map_is_null_ptr(out) == 1):\n"
            "    return 46\n"
            "  if (thag_str_contains(out, \"\\\"count\\\":9\") != 1):\n"
            "    return 47\n"
            "  thag_str_free(out)\n"
            "  thag_json_free(doc)\n"
            "  return 0\n"
        )
        self.assertEqual(run.returncode, 0, msg=run.stderr)


if __name__ == "__main__":
    unittest.main()

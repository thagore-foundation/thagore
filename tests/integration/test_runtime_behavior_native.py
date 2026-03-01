import ctypes.util
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


def _find_runtime_lib() -> Path | None:
    for candidate in (
        Path("build-llvm21/runtime/libthag_runtime.a"),
        Path("build-llvm21-run/runtime/libthag_runtime.a"),
        Path("build-gcc-llvm21-clean/runtime/libthag_runtime.a"),
        Path("build/runtime/libthag_runtime.a"),
    ):
        if candidate.exists():
            return candidate.resolve()
    return None


class RuntimeNativeBehaviorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.runtime_lib = _find_runtime_lib()
        self.cxx = shutil.which("g++")
        if self.runtime_lib is None:
            self.skipTest("runtime static library not found; build runtime first")
        if self.cxx is None:
            self.skipTest("g++ not found")

    def test_runtime_api_behavior(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "runtime_check.cpp"
            out = root / "runtime_check"
            src.write_text(
                "#include <atomic>\n"
                "#include <cstdint>\n"
                "#include <cstring>\n"
                "#include \"thag_runtime.h\"\n"
                "\n"
                "static std::atomic<int> g_count{0};\n"
                "static std::atomic<int> g_async{0};\n"
                "static void worker(void*) { g_count.fetch_add(1); }\n"
                "static void async_worker(void*) { g_async.fetch_add(1); }\n"
                "\n"
                "int main() {\n"
                "  int64_t t0 = thag_now_ms();\n"
                "  thag_sleep_ms(1);\n"
                "  int64_t t1 = thag_now_ms();\n"
                "  if (t1 < t0) return 11;\n"
                "\n"
                "  void* m = thag_map_new();\n"
                "  if (!m) return 12;\n"
                "  if (!thag_map_put(m, \"k\", \"v\")) return 13;\n"
                "  const char* got = thag_map_get(m, \"k\");\n"
                "  if (!got || std::strcmp(got, \"v\") != 0) return 14;\n"
                "  thag_map_free(m);\n"
                "\n"
                "  const char* s_cat = thag_str_concat(\"ab\", \"cd\");\n"
                "  if (!s_cat || std::strcmp(s_cat, \"abcd\") != 0) return 15;\n"
                "  thag_str_free(s_cat);\n"
                "  void* split_parts = thag_str_split(\"a,b\", \",\");\n"
                "  if (!split_parts || thag_str_array_len(split_parts) != 2) return 16;\n"
                "  const char* joined = thag_str_join(split_parts, \":\");\n"
                "  if (!joined || std::strcmp(joined, \"a:b\") != 0) return 17;\n"
                "  thag_str_free(joined);\n"
                "  thag_str_array_free(split_parts);\n"
                "  const char* trimmed = thag_str_trim(\"  hello  \");\n"
                "  if (!trimmed || std::strcmp(trimmed, \"hello\") != 0) return 18;\n"
                "  thag_str_free(trimmed);\n"
                "  if (!thag_str_contains(\"thagore\", \"gore\")) return 19;\n"
                "  if (!thag_str_starts_with(\"thagore\", \"tha\")) return 20;\n"
                "  if (!thag_str_equals(\"same\", \"same\")) return 21;\n"
                "  if (thag_str_len(\"len\") != 3) return 22;\n"
                "  const char* from_int = thag_str_from_int(42);\n"
                "  if (!from_int || std::strcmp(from_int, \"42\") != 0) return 23;\n"
                "  thag_str_free(from_int);\n"
                "  if (thag_str_to_int(\"123\") != 123) return 24;\n"
                "  const char* sub = thag_str_substr(\"thagore\", 1, 3);\n"
                "  if (!sub || std::strcmp(sub, \"hag\") != 0) return 25;\n"
                "  thag_str_free(sub);\n"
                "  const char* replaced = thag_str_replace(\"a-b-a\", \"a\", \"x\");\n"
                "  if (!replaced || std::strcmp(replaced, \"x-b-x\") != 0) return 26;\n"
                "  thag_str_free(replaced);\n"
                "  const char* formatted = thag_str_format(\"v={}\", \"9\");\n"
                "  if (!formatted || std::strcmp(formatted, \"v=9\") != 0) return 27;\n"
                "  thag_str_free(formatted);\n"
                "\n"
                "  if (!thag_fs_mkdir(\"tmp_io\")) return 28;\n"
                "  const char* cfg_path = thag_fs_path_join(\"tmp_io\", \"cfg.toml\");\n"
                "  if (!cfg_path) return 29;\n"
                "  if (!thag_fs_write(cfg_path, \"[app]\\nport = 42\\nname = \\\"drago\\\"\\n\")) return 30;\n"
                "  if (!thag_fs_exists(cfg_path)) return 31;\n"
                "  if (thag_fs_is_dir(cfg_path)) return 32;\n"
                "  if (thag_fs_filesize(cfg_path) <= 0) return 33;\n"
                "  const char* cwd = thag_fs_getcwd();\n"
                "  if (!cwd || !*cwd) return 34;\n"
                "  thag_str_free(cwd);\n"
                "  const char* cfg_raw = thag_fs_read(cfg_path);\n"
                "  if (!cfg_raw) return 35;\n"
                "  void* entries = thag_fs_readdir(\"tmp_io\");\n"
                "  if (!entries || thag_str_array_len(entries) < 1) return 36;\n"
                "  thag_str_array_free(entries);\n"
                "\n"
                "  void* toml = thag_toml_parse(cfg_raw);\n"
                "  thag_str_free(cfg_raw);\n"
                "  if (!toml) return 37;\n"
                "  if (thag_toml_get_int(toml, \"app.port\") != 42) return 38;\n"
                "  void* app = thag_toml_get_section(toml, \"app\");\n"
                "  if (!app) return 39;\n"
                "  const char* app_name = thag_toml_get_str(app, \"name\");\n"
                "  if (!app_name || std::strcmp(app_name, \"drago\") != 0) return 40;\n"
                "  thag_str_free(app_name);\n"
                "  void* keys = thag_toml_get_keys(app);\n"
                "  if (!keys || thag_str_array_len(keys) < 2) return 41;\n"
                "  thag_str_array_free(keys);\n"
                "  thag_toml_free(app);\n"
                "  thag_toml_free(toml);\n"
                "\n"
                "  void* json = thag_json_parse(\"{\\\"name\\\":\\\"drago\\\",\\\"count\\\":7}\");\n"
                "  if (!json) return 66;\n"
                "  const char* jname = thag_json_get_str(json, \"name\");\n"
                "  if (!jname || std::strcmp(jname, \"drago\") != 0) return 67;\n"
                "  thag_str_free(jname);\n"
                "  if (thag_json_get_int(json, \"count\") != 7) return 68;\n"
                "  if (!thag_json_set_int(json, \"count\", 9)) return 69;\n"
                "  const char* json_text = thag_json_stringify(json);\n"
                "  if (!json_text || !thag_str_contains(json_text, \"\\\"count\\\":9\")) return 70;\n"
                "  thag_str_free(json_text);\n"
                "  thag_json_free(json);\n"
                "\n"
                "#if defined(_WIN32)\n"
                "  const char* run_cmd = \"cmd /c exit 0\";\n"
                "  const char* capture_cmd = \"cmd /c echo ok\";\n"
                "#else\n"
                "  const char* run_cmd = \"true\";\n"
                "  const char* capture_cmd = \"printf ok\";\n"
                "#endif\n"
                "  if (thag_process_run(run_cmd) != 0) return 42;\n"
                "  const char* captured = thag_process_capture(capture_cmd);\n"
                "  if (!captured || !thag_str_contains(captured, \"ok\")) return 43;\n"
                "  thag_str_free(captured);\n"
                "  if (thag_process_argc() < 0) return 44;\n"
                "  const char* env = thag_process_env(\"PATH\");\n"
                "  if (!env || !*env) return 45;\n"
                "  thag_str_free(env);\n"
                "\n"
                "  if (thag_http_get(\"https://example.com\", 10) <= 0) return 46;\n"
                "  if (thag_http_post(\"https://example.com\", \"x\", 10) <= 0) return 47;\n"
                "  int64_t h0 = thag_now_ms();\n"
                "  int h_retry = thag_http_get_retry(\"http://127.0.0.1:1\", 5, 2, 20);\n"
                "  int64_t h1 = thag_now_ms();\n"
                "  if (h_retry != 599 && h_retry != -2) return 71;\n"
                "  if (h_retry != -2 && h1 - h0 < 50) return 72;\n"
                "\n"
                "  int ws = thag_ws_connect(\"ws://local\", 10);\n"
                "  if (ws <= 0) return 48;\n"
                "  if (thag_ws_send(ws, \"hello\") <= 0) return 49;\n"
                "  if (!thag_ws_close(ws)) return 50;\n"
                "  int64_t w0 = thag_now_ms();\n"
                "  int ws_retry = thag_ws_connect_retry(\"ws://127.0.0.1:1\", 5, 2, 20);\n"
                "  int64_t w1 = thag_now_ms();\n"
                "  if (ws_retry != 0 && ws_retry != -2) return 73;\n"
                "  if (ws_retry == 0 && w1 - w0 < 50) return 74;\n"
                "  if (ws_retry > 0) thag_ws_close(ws_retry);\n"
                "\n"
                "  int db = thag_db_connect(\"memory://\");\n"
                "  if (db <= 0) return 51;\n"
                "  if (thag_db_query(db, \"SELECT 1\") != 1) return 52;\n"
                "  if (thag_db_query_retry(db, \"SELECT 1\", 2, 5) != 1) return 75;\n"
                "  if (!thag_db_close(db)) return 53;\n"
                "  int64_t d0 = thag_now_ms();\n"
                "  int db_retry = thag_db_connect_retry(\"bad://dsn\", 2, 20);\n"
                "  int64_t d1 = thag_now_ms();\n"
                "  if (db_retry != 0 && db_retry != -2) return 76;\n"
                "  if (db_retry == 0 && d1 - d0 < 50) return 77;\n"
                "\n"
                "  if (thag_crypto_available()) {\n"
                "    const char* sha = thag_crypto_sha256_hex(\"abc\");\n"
                "    if (!sha || std::strlen(sha) != 64) return 78;\n"
                "    const char* hmac = thag_crypto_hmac_sha256_hex(\"k\", \"abc\");\n"
                "    if (!hmac || std::strlen(hmac) != 64) return 79;\n"
                "    thag_str_free(sha);\n"
                "    thag_str_free(hmac);\n"
                "  }\n"
                "\n"
                "  thag_task_scope_t* scope = thag_task_scope_create();\n"
                "  if (!scope) return 54;\n"
                "  if (!thag_task_scope_spawn(scope, worker, nullptr)) return 55;\n"
                "  if (!thag_task_scope_spawn(scope, worker, nullptr)) return 56;\n"
                "  if (!thag_task_scope_wait(scope)) return 57;\n"
                "  thag_task_scope_destroy(scope);\n"
                "  if (g_count.load() != 2) return 58;\n"
                "\n"
                "  thag_async_runtime_t* rt = thag_async_runtime_create();\n"
                "  if (!rt) return 59;\n"
                "  if (!thag_async_spawn(rt, async_worker, nullptr)) return 60;\n"
                "  if (!thag_async_sleep(rt, 1, async_worker, nullptr)) return 61;\n"
                "  if (!thag_async_wait_idle(rt, 1000)) return 62;\n"
                "  thag_async_runtime_destroy(rt);\n"
                "  if (g_async.load() != 2) return 63;\n"
                "\n"
                "  if (!thag_fs_remove(cfg_path)) return 80;\n"
                "  if (!thag_fs_remove(\"tmp_io\")) return 81;\n"
                "  thag_str_free(cfg_path);\n"
                "  return 0;\n"
                "}\n"
            )
            compile_cmd = [
                self.cxx,
                "-std=c++20",
                "-I",
                str(Path("runtime/include").resolve()),
                str(src),
                str(self.runtime_lib),
                "-lpthread",
                "-o",
                str(out),
            ]
            if ctypes.util.find_library("curl"):
                compile_cmd.insert(-2, "-lcurl")
            if ctypes.util.find_library("sqlite3"):
                compile_cmd.insert(-2, "-lsqlite3")
            if ctypes.util.find_library("ssl"):
                compile_cmd.insert(-2, "-lssl")
            if ctypes.util.find_library("crypto"):
                compile_cmd.insert(-2, "-lcrypto")
            comp = subprocess.run(compile_cmd, capture_output=True, text=True, check=False)
            if comp.returncode != 0 and "-lcurl" in compile_cmd and "cannot find -lcurl" in comp.stderr:
                retry_cmd = [arg for arg in compile_cmd if arg != "-lcurl"]
                comp = subprocess.run(retry_cmd, capture_output=True, text=True, check=False)
            elif comp.returncode != 0 and "-lcurl" not in compile_cmd and "curl_" in comp.stderr:
                retry_cmd = compile_cmd[:-2] + ["-lcurl"] + compile_cmd[-2:]
                comp = subprocess.run(retry_cmd, capture_output=True, text=True, check=False)
            if comp.returncode != 0 and "-lsqlite3" in compile_cmd and "cannot find -lsqlite3" in comp.stderr:
                retry_cmd = [arg for arg in compile_cmd if arg != "-lsqlite3"]
                comp = subprocess.run(retry_cmd, capture_output=True, text=True, check=False)
            elif comp.returncode != 0 and "-lsqlite3" not in compile_cmd and "sqlite3_" in comp.stderr:
                retry_cmd = compile_cmd[:-2] + ["-lsqlite3"] + compile_cmd[-2:]
                comp = subprocess.run(retry_cmd, capture_output=True, text=True, check=False)
            if comp.returncode != 0 and "-lssl" in compile_cmd and "cannot find -lssl" in comp.stderr:
                retry_cmd = [arg for arg in compile_cmd if arg != "-lssl"]
                comp = subprocess.run(retry_cmd, capture_output=True, text=True, check=False)
            if comp.returncode != 0 and "-lcrypto" in compile_cmd and "cannot find -lcrypto" in comp.stderr:
                retry_cmd = [arg for arg in compile_cmd if arg != "-lcrypto"]
                comp = subprocess.run(retry_cmd, capture_output=True, text=True, check=False)
            self.assertEqual(comp.returncode, 0, msg=comp.stderr)
            run = subprocess.run([str(out)], capture_output=True, text=True, check=False, cwd=root)
            self.assertEqual(run.returncode, 0, msg=run.stderr)


if __name__ == "__main__":
    unittest.main()

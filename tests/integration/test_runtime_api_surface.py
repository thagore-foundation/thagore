import unittest
from pathlib import Path


class RuntimeApiSurfaceTests(unittest.TestCase):
    def test_structured_concurrency_symbols_exist(self) -> None:
        header = Path("runtime/include/thag_runtime.h").read_text()
        required = [
            "thag_task_scope_create",
            "thag_nursery_create",
            "thag_task_scope_spawn",
            "thag_task_scope_cancel",
            "thag_task_scope_set_timeout",
            "thag_task_scope_set_timeout_ms",
            "thag_task_scope_wait",
            "thag_task_scope_cancelled",
            "thag_task_trace_set_enabled",
            "thag_task_trace_enabled",
            "thag_task_trace_set_hook",
            "thag_task_scope_dump_tree",
            "thag_async_runtime_create",
            "thag_async_runtime_destroy",
            "thag_async_spawn",
            "thag_async_sleep",
            "thag_async_wait_idle",
        ]
        for symbol in required:
            self.assertIn(symbol, header)

    def test_stdlib_backend_symbols_exist(self) -> None:
        header = Path("runtime/include/thag_runtime.h").read_text()
        required = [
            "thag_str_concat",
            "thag_str_split",
            "thag_str_join",
            "thag_str_trim",
            "thag_str_contains",
            "thag_str_starts_with",
            "thag_str_equals",
            "thag_str_len",
            "thag_str_from_int",
            "thag_str_to_int",
            "thag_str_substr",
            "thag_str_replace",
            "thag_str_format",
            "thag_fs_read",
            "thag_fs_write",
            "thag_fs_exists",
            "thag_fs_mkdir",
            "thag_fs_readdir",
            "thag_fs_remove",
            "thag_fs_getcwd",
            "thag_fs_path_join",
            "thag_fs_is_dir",
            "thag_fs_filesize",
            "thag_process_run",
            "thag_process_capture",
            "thag_process_argv",
            "thag_process_argc",
            "thag_process_env",
            "thag_process_exit",
            "thag_toml_parse",
            "thag_toml_get_str",
            "thag_toml_get_int",
            "thag_toml_get_section",
            "thag_toml_get_keys",
            "thag_toml_free",
        ]
        for symbol in required:
            self.assertIn(symbol, header)

    def test_runtime_build_defines_concurrency_source(self) -> None:
        cmake = Path("runtime/CMakeLists.txt").read_text()
        self.assertIn("src/concurrency.cpp", cmake)

    def test_runtime_build_defines_stdlib_backend_sources(self) -> None:
        cmake = Path("runtime/CMakeLists.txt").read_text()
        required = [
            "src/string.cpp",
            "src/fs.cpp",
            "src/process.cpp",
            "src/toml.cpp",
            "src/http.cpp",
            "src/ws.cpp",
            "src/db.cpp",
        ]
        for source in required:
            self.assertIn(source, cmake)


if __name__ == "__main__":
    unittest.main()

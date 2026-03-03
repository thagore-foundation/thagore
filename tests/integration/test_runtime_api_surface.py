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
            "thag_trace_span_begin",
            "thag_trace_span_end",
            "thag_trace_event",
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
            "thag_gui_create_canvas",
            "thag_gui_destroy_canvas",
            "thag_gui_clear",
            "thag_gui_draw_point",
            "thag_gui_draw_line",
            "thag_gui_present",
            "thag_gui_last_frame_path",
            "thag_gui_poll_event",
            "thag_gui_should_close",
            "thag_gui_request_close",
            "thag_gui_set_target_fps",
            "thag_gui_tick",
            "thag_toml_parse",
            "thag_toml_get_str",
            "thag_toml_get_int",
            "thag_toml_get_section",
            "thag_toml_get_keys",
            "thag_toml_free",
            "thag_json_parse",
            "thag_json_get_str",
            "thag_json_get_int",
            "thag_json_set_str",
            "thag_json_set_int",
            "thag_json_stringify",
            "thag_json_free",
            "thag_cuda_available",
            "thag_opencl_available",
            "thag_tensor_new_i64",
            "thag_tensor_len",
            "thag_tensor_fill_i64",
            "thag_tensor_set_i64",
            "thag_tensor_get_i64",
            "thag_tensor_sum_i64",
            "thag_tensor_axpy_i64",
            "thag_tensor_cuda_axpy_i64",
            "thag_pytorch_axpy_i64",
            "thag_tensor_add_i64",
            "thag_tensor_mul_i64",
            "thag_tensor_dot_i64",
            "thag_tensor_scale_i64",
            "thag_tensor_relu_i64",
            "thag_tensor_argmax_i64",
            "thag_tensor_free",
            "thag_crypto_sha256_hex",
            "thag_crypto_hmac_sha256_hex",
            "thag_crypto_available",
            "thag_grpc_call",
            "thag_grpc_health",
            "thag_sql_builder_new",
            "thag_sql_builder_select",
            "thag_sql_builder_from",
            "thag_sql_builder_where",
            "thag_sql_builder_order_by",
            "thag_sql_builder_limit",
            "thag_sql_builder_build",
            "thag_sql_builder_reset",
            "thag_sql_builder_free",
            "thag_sql_migrate_apply",
            "thag_http_get_retry",
            "thag_http_post_retry",
            "thag_ws_connect_retry",
            "thag_db_connect_retry",
            "thag_db_query_retry",
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
            "src/gui.cpp",
            "src/toml.cpp",
            "src/http.cpp",
            "src/grpc.cpp",
            "src/ws.cpp",
            "src/db.cpp",
            "src/sql.cpp",
            "src/json.cpp",
            "src/tensor.cpp",
            "src/crypto.cpp",
        ]
        for source in required:
            self.assertIn(source, cmake)


if __name__ == "__main__":
    unittest.main()

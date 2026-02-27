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
            "thag_async_runtime_create",
            "thag_async_runtime_destroy",
            "thag_async_spawn",
            "thag_async_sleep",
            "thag_async_wait_idle",
        ]
        for symbol in required:
            self.assertIn(symbol, header)

    def test_runtime_build_defines_concurrency_source(self) -> None:
        cmake = Path("runtime/CMakeLists.txt").read_text()
        self.assertIn("src/concurrency.cpp", cmake)


if __name__ == "__main__":
    unittest.main()

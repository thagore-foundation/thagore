from pathlib import Path
import unittest


class V22HirPipelineParityTests(unittest.TestCase):
    def test_hir_and_ty_headers_exist(self) -> None:
        self.assertTrue(Path("compiler/include/thagc/hir/expr.hpp").is_file())
        self.assertTrue(Path("compiler/include/thagc/hir/typecheck.hpp").is_file())
        self.assertTrue(Path("compiler/include/thagc/ty/ty.hpp").is_file())

    def test_cmake_wires_hir_typecheck_source(self) -> None:
        cmake = Path("compiler/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("src/middleend/hir_typecheck.cpp", cmake)

    def test_frontend_typechecker_uses_hir_inference_path(self) -> None:
        builder = Path("compiler/src/frontend/builder.cpp").read_text(encoding="utf-8")
        self.assertIn('#include "thagc/hir/typecheck.hpp"', builder)
        self.assertIn("hir::infer_expression(", builder)
        self.assertIn("hir::lower_ast_expr(st.expression_ast)", builder)


if __name__ == "__main__":
    unittest.main()

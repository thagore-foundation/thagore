from pathlib import Path
import unittest


class V23HmGenericsPipelineParityTests(unittest.TestCase):
    def test_hir_typecheck_header_exposes_unifier_and_ty_api(self) -> None:
        header = Path("compiler/include/thagc/hir/typecheck.hpp").read_text(encoding="utf-8")
        self.assertIn("class Unifier", header)
        self.assertIn("infer_expression_ty(", header)
        self.assertIn("check_expression_ty(", header)

    def test_frontend_typechecker_wires_tree_type_checks(self) -> None:
        builder = Path("compiler/src/frontend/builder.cpp").read_text(encoding="utf-8")
        self.assertIn("resolve_declared_user_type_tree(", builder)
        self.assertIn("try_check_statement_expression_against_tree_type_hir(", builder)
        self.assertIn("hir::check_expression_ty(", builder)


if __name__ == "__main__":
    unittest.main()

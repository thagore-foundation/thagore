import unittest
from pathlib import Path


class SyntaxContractAlignmentTests(unittest.TestCase):
    def test_token_enum_covers_required_contract_keywords(self) -> None:
        token_hpp = Path("compiler/include/thagc/frontend/token.hpp").read_text()
        for enum_name in (
            "KeywordStruct",
            "KeywordImpl",
            "KeywordImport",
            "KeywordExtern",
            "Less",
            "LessEqual",
            "Greater",
            "GreaterEqual",
            "EqualEqual",
            "BangEqual",
        ):
            self.assertIn(enum_name, token_hpp)

    def test_lexer_recognizes_extended_keywords_and_operators(self) -> None:
        lexer_cpp = Path("compiler/src/frontend/core.cpp").read_text()
        for keyword in ('text == "struct"', 'text == "impl"', 'text == "import"', 'text == "extern"'):
            self.assertIn(keyword, lexer_cpp)
        for op in ("TokenKind::LessEqual", "TokenKind::GreaterEqual", "TokenKind::EqualEqual", "TokenKind::BangEqual"):
            self.assertIn(op, lexer_cpp)

    def test_parser_enforces_indentation_and_if_while_parentheses(self) -> None:
        parser_cpp = Path("compiler/src/frontend/ir.cpp").read_text()
        self.assertIn("function body must be indentation-scoped", parser_cpp)
        self.assertIn("if requires parentheses and trailing ':'", parser_cpp)
        self.assertIn("while requires parentheses and trailing ':'", parser_cpp)

    def test_parser_has_expression_precedence_pipeline(self) -> None:
        parser_cpp = Path("compiler/src/frontend/ir.cpp").read_text()
        for fn in (
            "parse_multiplicative",
            "parse_additive",
            "parse_comparison",
            "parse_expression_equality",
            "parse_statement_expression",
        ):
            self.assertIn(fn, parser_cpp)

    def test_statement_ast_tracks_parsed_expression(self) -> None:
        ast_hpp = Path("compiler/include/thagc/frontend/ast.hpp").read_text()
        for field in ("has_expression", "expression_valid", "expression_normalized", "expression_error"):
            self.assertIn(field, ast_hpp)

    def test_parser_expression_supports_string_and_unary_minus(self) -> None:
        parser_cpp = Path("compiler/src/frontend/ir.cpp").read_text()
        self.assertIn("unterminated string literal in expression", parser_cpp)
        self.assertIn("tok.kind == ExprTokenKind::Operator && tok.text == \"-\"", parser_cpp)

    def test_typechecker_expression_rules_present(self) -> None:
        checker_cpp = Path("compiler/src/frontend/builder.cpp").read_text()
        for marker in (
            "operator '",
            "condition expression must be i32",
            "return type mismatch in function",
            "unknown identifier",
            "parse_let_name",
        ):
            self.assertIn(marker, checker_cpp)


if __name__ == "__main__":
    unittest.main()

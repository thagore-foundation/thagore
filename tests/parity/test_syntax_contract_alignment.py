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


if __name__ == "__main__":
    unittest.main()

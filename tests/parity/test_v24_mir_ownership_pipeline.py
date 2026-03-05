from pathlib import Path
import unittest


class V24MirOwnershipPipelineParityTests(unittest.TestCase):
    def test_mir_headers_and_sources_exist(self) -> None:
        self.assertTrue(Path("compiler/include/thagc/mir/mir.hpp").is_file())
        self.assertTrue(Path("compiler/include/thagc/middleend/mir_lowering.hpp").is_file())
        self.assertTrue(Path("compiler/include/thagc/middleend/ownership.hpp").is_file())
        self.assertTrue(Path("compiler/src/middleend/mir_lowering.cpp").is_file())
        self.assertTrue(Path("compiler/src/middleend/ownership.cpp").is_file())

    def test_frontend_typechecker_wires_ownership_check(self) -> None:
        builder = Path("compiler/src/frontend/builder.cpp").read_text(encoding="utf-8")
        self.assertIn('#include "thagc/middleend/ownership.hpp"', builder)
        self.assertIn("middleend::check_program_ownership(program, diag)", builder)


if __name__ == "__main__":
    unittest.main()

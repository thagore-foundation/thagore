#include "thagc/frontend/typechecker.hpp"

namespace thagc::semantics {

bool TypeChecker::check(const syntax::AstProgram& program, support::DiagnosticSink& diag) const {
  if (program.source.empty()) {
    diag.error("E0001", "source is empty");
    return false;
  }
  return true;
}

}  // namespace thagc::semantics

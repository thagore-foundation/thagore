#pragma once

#include "thagc/shared/diag.hpp"
#include "thagc/frontend/ast.hpp"

namespace thagc::semantics {

class TypeChecker {
 public:
  bool check(const syntax::AstProgram& program, support::DiagnosticSink& diag) const;
};

}  // namespace thagc::semantics


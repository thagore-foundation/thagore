#pragma once

#include "thagc/support/diag.hpp"
#include "thagc/syntax/ast.hpp"

namespace thagc::semantics {

class TypeChecker {
 public:
  bool check(const syntax::AstProgram& program, support::DiagnosticSink& diag) const;
};

}  // namespace thagc::semantics


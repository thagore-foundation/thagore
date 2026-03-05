#pragma once

#include "thagc/frontend/ast.hpp"
#include "thagc/shared/diag.hpp"

namespace thagc::middleend {

bool check_program_ownership(const syntax::AstProgram& program, support::DiagnosticSink& diag);

}  // namespace thagc::middleend

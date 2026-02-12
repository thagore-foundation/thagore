#pragma once

#include "thagore/common/diagnostics.hpp"
#include "thagore/common/result.hpp"
#include "thagore/frontend/ast.hpp"

namespace thagore {

class SemanticAnalyzer {
public:
  auto analyze(std::unique_ptr<ModuleDecl> module) -> Result<TypedModule, Diagnostic>;
};

} // namespace thagore

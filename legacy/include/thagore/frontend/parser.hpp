#pragma once

#include "thagore/common/diagnostics.hpp"
#include "thagore/common/result.hpp"
#include "thagore/frontend/ast.hpp"
#include "thagore/frontend/token.hpp"

#include <span>

namespace thagore {

class Parser {
public:
  auto parseModule(std::span<const Token> tokens) -> Result<std::unique_ptr<ModuleDecl>, Diagnostic>;
};

} // namespace thagore

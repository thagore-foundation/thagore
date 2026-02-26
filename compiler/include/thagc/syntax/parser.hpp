#pragma once

#include <vector>

#include "thagc/syntax/ast.hpp"
#include "thagc/syntax/token.hpp"

namespace thagc::syntax {

class Parser {
 public:
  AstProgram parse(const std::vector<Token>& tokens, const std::string& source) const;
};

}  // namespace thagc::syntax


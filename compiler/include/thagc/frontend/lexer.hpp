#pragma once

#include <string>
#include <vector>

#include "thagc/frontend/token.hpp"

namespace thagc::syntax {

class Lexer {
 public:
  std::vector<Token> tokenize(const std::string& source) const;
};

}  // namespace thagc::syntax


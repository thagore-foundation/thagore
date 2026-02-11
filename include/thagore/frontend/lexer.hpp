#pragma once

#include "thagore/common/diagnostics.hpp"
#include "thagore/common/result.hpp"
#include "thagore/frontend/token.hpp"

#include <string_view>
#include <vector>

namespace thagore {

class Lexer {
public:
  auto tokenize(std::string_view source, std::string file) -> Result<std::vector<Token>, Diagnostic>;
};

} // namespace thagore

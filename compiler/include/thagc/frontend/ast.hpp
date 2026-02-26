#pragma once

#include <string>
#include <vector>

namespace thagc::syntax {

struct AstProgram {
  std::vector<std::string> top_level_lines;
  std::string source;
};

}  // namespace thagc::syntax


#pragma once

#include <string>

#include "thagc/frontend/ast.hpp"

namespace thagc::lowering {

struct CoreProgram {
  std::string normalized_source;
  bool has_main = false;
  int main_return_literal = 0;
};

CoreProgram lower_to_core(const syntax::AstProgram& program);

}  // namespace thagc::lowering

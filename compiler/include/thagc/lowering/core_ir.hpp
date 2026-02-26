#pragma once

#include <string>

#include "thagc/syntax/ast.hpp"

namespace thagc::lowering {

struct CoreProgram {
  std::string normalized_source;
};

CoreProgram lower_to_core(const syntax::AstProgram& program);

}  // namespace thagc::lowering


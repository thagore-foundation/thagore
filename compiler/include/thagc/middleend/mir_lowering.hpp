#pragma once

#include "thagc/frontend/ast.hpp"
#include "thagc/mir/mir.hpp"

namespace thagc::middleend {

mir::MirBody lower_function_to_mir(const syntax::AstFunction& fn);

}  // namespace thagc::middleend

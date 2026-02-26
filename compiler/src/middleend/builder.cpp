#include "thagc/middleend/core_ir.hpp"

namespace thagc::lowering {

CoreProgram lower_to_core(const syntax::AstProgram& program) {
  CoreProgram core;
  core.normalized_source = program.source;
  core.has_main = program.has_main;
  core.main_return_literal = program.main_return_literal;
  return core;
}

}  // namespace thagc::lowering

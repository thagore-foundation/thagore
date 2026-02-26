#include "thagc/lowering/core_ir.hpp"

namespace thagc::lowering {

CoreProgram lower_to_core(const syntax::AstProgram& program) {
  CoreProgram core;
  core.normalized_source = program.source;
  return core;
}

}  // namespace thagc::lowering


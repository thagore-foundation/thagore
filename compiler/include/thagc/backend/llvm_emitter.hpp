#pragma once

#include <string>

#include "thagc/middleend/core_ir.hpp"
#include "thagc/shared/diag.hpp"

namespace thagc::codegen {

class LlvmEmitter {
 public:
  bool emit_llvm_ir(const lowering::CoreProgram& core, const std::string& module_name, const std::string& llvm_ir_path,
                    support::DiagnosticSink& diag) const;
  bool emit_object(const lowering::CoreProgram& core, const std::string& module_name, const std::string& object_path,
                   support::DiagnosticSink& diag) const;
};

}  // namespace thagc::codegen


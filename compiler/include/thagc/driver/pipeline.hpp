#pragma once

#include <string>

#include "thagc/support/diag.hpp"

namespace thagc::driver {

struct BuildOptions {
  std::string input_path;
  std::string output_path;
  bool emit_llvm = false;
  std::string llvm_ir_path;
};

class CompilerPipeline {
 public:
  bool build(const BuildOptions& options, support::DiagnosticSink& diag) const;
};

}  // namespace thagc::driver


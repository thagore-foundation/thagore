#pragma once

#include <string>
#include <vector>

#include "thagc/shared/diag.hpp"

namespace thagc::driver {

struct BuildOptions {
  std::string input_path;
  std::string output_path;
  std::string target_triple;
  std::string target_linker;
  std::string target_sysroot;
  std::vector<std::string> extra_link_args;
  int opt_level = 2;
  bool emit_llvm = false;
  std::string llvm_ir_path;
};

class CompilerPipeline {
 public:
  bool build(const BuildOptions& options, support::DiagnosticSink& diag) const;
};

}  // namespace thagc::driver

#include "thagc/driver/command_handlers.hpp"

#include <iostream>

#include "thagc/driver/pipeline.hpp"

namespace thagc::driver {

int handle_build(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag) {
  if (cmd.input_path.empty()) {
    std::cerr << "ERROR: missing input path for build\n";
    return 1;
  }
  if (cmd.output_path.empty()) {
    std::cerr << "ERROR: missing -o output for build\n";
    return 1;
  }

  BuildOptions options;
  options.input_path = cmd.input_path;
  options.output_path = cmd.output_path;
  options.emit_llvm = cmd.emit_llvm;
  if (options.emit_llvm) {
    options.llvm_ir_path = options.output_path + ".ll";
  }
  if (!pipeline.build(options, diag)) {
    for (const auto& d : diag.diagnostics()) {
      std::cerr << d.code << ": " << d.message << "\n";
    }
    return 1;
  }
  return 0;
}

}  // namespace thagc::driver


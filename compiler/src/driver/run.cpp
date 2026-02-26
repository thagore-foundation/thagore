#include "thagc/driver/command_handlers.hpp"

#include <iostream>

#include "thagc/driver/pipeline.hpp"
#include "thagc/shared/process.hpp"

namespace thagc::driver {

static std::string default_output(const std::string& input) {
  return input + ".bin";
}

int handle_run(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag) {
  if (cmd.input_path.empty()) {
    std::cerr << "ERROR: missing input path for run\n";
    return 1;
  }

  BuildOptions options;
  options.input_path = cmd.input_path;
  options.output_path = cmd.output_path.empty() ? default_output(cmd.input_path) : cmd.output_path;
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

  const int rc = support::run_process({options.output_path});
  if (rc != 0) {
    std::cerr << "ERROR: run failed with exit code " << rc << "\n";
    return 1;
  }
  return 0;
}

int handle_test(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag) {
  // Rewrite milestone: test command currently executes compile+run for a single entry.
  return handle_run(cmd, pipeline, diag);
}

}  // namespace thagc::driver


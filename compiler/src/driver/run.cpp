#include "thagc/driver/command_handlers.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include "thagc/driver/common.hpp"
#include "thagc/driver/pipeline.hpp"
#include "thagc/shared/process.hpp"

namespace thagc::driver {

namespace {

static std::string default_base_name(const std::string& input) {
  if (input.size() > 3 && input.compare(input.size() - 3, 3, ".tg") == 0) {
    return input.substr(0, input.size() - 3);
  }
  return input;
}

static std::string default_output(const std::string& input) {
  return default_base_name(input) + ".bin";
}

static void print_diagnostics(const support::DiagnosticSink& diag) {
  for (const auto& d : diag.diagnostics()) {
    const char* level = d.level == support::DiagnosticLevel::Warning ? "warning" : "error";
    if (!d.file.empty()) {
      const int line = d.line > 0 ? d.line : 1;
      const int col = d.column > 0 ? d.column : 1;
      std::cerr << d.file << ":" << line << ":" << col << ": " << level << " " << d.code << ": " << d.message
                << "\n";
    } else {
      std::cerr << level << " " << d.code << ": " << d.message << "\n";
    }
    const std::string hint = support::diagnostic_fix_suggestion(d);
    if (!hint.empty()) {
      std::cerr << "  help: " << hint << "\n";
    }
  }
}

}  // namespace

int handle_run(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag) {
  if (cmd.input_path.empty()) {
    std::cerr << "ERROR: missing input path for run\n";
    return 1;
  }

  BuildOptions options;
  options.input_path = cmd.input_path;
  options.output_path = cmd.output_path.empty() ? default_output(cmd.input_path) : cmd.output_path;
  options.target_triple = cmd.target_triple;
  options.include_paths = cmd.include_paths;
  options.extra_link_args = compose_link_extra_args(cmd);
  options.emit_llvm = cmd.emit_llvm;
  if (!apply_target_config(options, cmd.target_triple, diag)) {
    print_diagnostics(diag);
    return 1;
  }
  if (options.emit_llvm) {
    options.llvm_ir_path = cmd.output_path.empty() ? (default_base_name(cmd.input_path) + ".ll")
                                                    : (options.output_path + ".ll");
  }

  if (!pipeline.build(options, diag)) {
    print_diagnostics(diag);
    return 1;
  }
  if (cmd.emit_llvm) {
    std::cout << "run: emitted LLVM IR only, skip execution\n";
    return 0;
  }

  const std::string exe = (std::filesystem::path(options.output_path).is_relative())
                              ? ("./" + options.output_path)
                              : options.output_path;
  const int rc = support::run_process({exe});
  if (rc != 0) {
    std::cerr << "ERROR: run failed with exit code " << rc << "\n";
    return 1;
  }
  return 0;
}

}  // namespace thagc::driver

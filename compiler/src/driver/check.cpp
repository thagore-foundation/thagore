#include "thagc/driver/command_handlers.hpp"

#include <filesystem>
#include <iostream>

#include "thagc/driver/common.hpp"
#include "thagc/driver/pipeline.hpp"

namespace thagc::driver {

namespace {

static std::string default_base_name(const std::string& input) {
  if (input.size() > 3 && input.compare(input.size() - 3, 3, ".tg") == 0) {
    return input.substr(0, input.size() - 3);
  }
  return input;
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

int handle_check(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag) {
  if (cmd.input_path.empty()) {
    std::cerr << "ERROR: missing input path for check\n";
    return 1;
  }

  BuildOptions options;
  options.input_path = cmd.input_path;
#if defined(_WIN32)
  options.output_path = cmd.output_path.empty() ? (default_base_name(cmd.input_path) + ".check.exe") : cmd.output_path;
#else
  options.output_path = cmd.output_path.empty() ? (default_base_name(cmd.input_path) + ".check.bin") : cmd.output_path;
#endif
  options.target_triple = cmd.target_triple;
  options.include_paths = cmd.include_paths;
  options.emit_llvm = true;
  options.llvm_ir_path = options.output_path + ".ll";
  if (!apply_target_config(options, cmd.target_triple, diag)) {
    print_diagnostics(diag);
    return 1;
  }
  if (!pipeline.build(options, diag)) {
    print_diagnostics(diag);
    return 1;
  }

  std::error_code ec;
  std::filesystem::remove(options.llvm_ir_path, ec);
  std::cout << "check: OK (" << cmd.input_path << ")\n";
  return 0;
}

}  // namespace thagc::driver

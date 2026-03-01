#include "thagc/driver/command_handlers.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <regex>

#include "thagc/driver/common.hpp"
#include "thagc/driver/pipeline.hpp"
#include "thagc/shared/process.hpp"

namespace thagc::driver {

static std::string default_base_name(const std::string& input) {
  if (input.size() > 3 && input.compare(input.size() - 3, 3, ".tg") == 0) {
    return input.substr(0, input.size() - 3);
  }
  return input;
}

static std::string default_output(const std::string& input) {
  return default_base_name(input) + ".bin";
}

static int extract_line_from_message(const std::string& message) {
  std::smatch m;
  static const std::regex kLinePattern(R"(line\s+([0-9]+))");
  if (std::regex_search(message, m, kLinePattern) && m.size() >= 2) {
    return std::stoi(m[1].str());
  }
  return 1;
}

static void print_diagnostics(const ParsedCommand& cmd, const support::DiagnosticSink& diag) {
  for (const auto& d : diag.diagnostics()) {
    const std::string file = d.file.empty() ? (cmd.input_path.empty() ? "<unknown>" : cmd.input_path) : d.file;
    const int line = d.line > 0 ? d.line : extract_line_from_message(d.message);
    const int column = d.column > 0 ? d.column : 1;
    std::cerr << file << ":" << std::max(1, line) << ":" << std::max(1, column) << ": " << d.code << ": " << d.message
              << "\n";
  }
}

int handle_build(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag) {
  if (cmd.input_path.empty()) {
    std::error_code ec;
    const bool has_drago_manifest = std::filesystem::exists("drago.toml", ec) && !ec;
    if (has_drago_manifest) {
      const int rc = support::run_process({"drago", "build"});
      if (rc != 0) {
        std::cerr << "ERROR: delegated drago build failed with exit code " << rc << "\n";
        return 1;
      }
      return 0;
    }
    std::cerr << "ERROR: missing input path for build\n";
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
    print_diagnostics(cmd, diag);
    return 1;
  }
  if (options.emit_llvm) {
    options.llvm_ir_path = cmd.output_path.empty() ? (default_base_name(cmd.input_path) + ".ll")
                                                    : (options.output_path + ".ll");
  }
  if (!pipeline.build(options, diag)) {
    print_diagnostics(cmd, diag);
    return 1;
  }
  return 0;
}

}  // namespace thagc::driver

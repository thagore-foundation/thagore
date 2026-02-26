#include "thagc/cli/command_router.hpp"

#include <iostream>
#include <string>

#include "thagc/driver/pipeline.hpp"
#include "thagc/support/diag.hpp"

namespace thagc::cli {

static void print_help() {
  std::cout << "thagc 0.1.0\n";
  std::cout << "Usage:\n";
  std::cout << "  thagc --help\n";
  std::cout << "  thagc --version\n";
  std::cout << "  thagc build <input.tg> -o <output> [--emit-llvm]\n";
}

int CommandRouter::run(int argc, char** argv) const {
  if (argc <= 1) {
    print_help();
    return 0;
  }

  const std::string cmd = argv[1];
  if (cmd == "--help" || cmd == "help") {
    print_help();
    return 0;
  }
  if (cmd == "--version" || cmd == "version") {
    std::cout << "thagc 0.1.0\n";
    return 0;
  }
  if (cmd != "build") {
    std::cerr << "ERROR: unknown command: " << cmd << "\n";
    return 1;
  }
  if (argc < 5) {
    std::cerr << "ERROR: missing arguments for build\n";
    return 1;
  }

  driver::BuildOptions options;
  options.input_path = argv[2];
  for (int i = 3; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-o" && i + 1 < argc) {
      options.output_path = argv[++i];
      continue;
    }
    if (arg == "--emit-llvm") {
      options.emit_llvm = true;
      continue;
    }
  }
  if (options.output_path.empty()) {
    std::cerr << "ERROR: missing -o output\n";
    return 1;
  }
  if (options.emit_llvm) {
    options.llvm_ir_path = options.output_path + ".ll";
  }

  support::DiagnosticSink diag;
  const bool ok = driver::CompilerPipeline().build(options, diag);
  if (!ok) {
    for (const auto& d : diag.diagnostics()) {
      std::cerr << d.code << ": " << d.message << "\n";
    }
    return 1;
  }
  return 0;
}

}  // namespace thagc::cli


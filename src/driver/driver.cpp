#include "thagore/driver/driver.hpp"

#include "thagore/backend/ir_generator.hpp"
#include "thagore/common/diagnostics.hpp"
#include "thagore/frontend/lexer.hpp"
#include "thagore/frontend/parser.hpp"
#include "thagore/frontend/semantic.hpp"

#include "llvm/MC/TargetRegistry.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#if __has_include(<print>)
#include <print>
#endif
#include <optional>
#include <sstream>

namespace thagore {
namespace {

auto parseArgs(const std::vector<std::string> &args) -> Result<DriverOptions, Diagnostic> {
  DriverOptions options {};
  for (std::size_t i = 1; i < args.size(); ++i) {
    const auto &arg = args[i];
    if (arg == "--emit-ir") {
      options.emitIR = true;
      continue;
    }
    if (arg == "--emit-obj") {
      options.emitObject = true;
      continue;
    }
    if (arg == "-o") {
      if (i + 1 >= args.size()) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::InvalidCli,
          .message = "Missing output path after -o.",
          .span = {},
        });
      }
      options.outputFile = args[++i];
      continue;
    }
    if (arg.starts_with("--opt=")) {
      const auto level = arg.substr(6);
      if (level.empty() || level.size() > 1 || level[0] < '0' || level[0] > '3') {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::InvalidCli,
          .message = std::format("Invalid optimization level '{}'.", level),
          .span = {},
        });
      }
      options.optLevel = level[0] - '0';
      continue;
    }
    if (arg.starts_with('-')) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::InvalidCli,
        .message = std::format("Unknown flag '{}'.", arg),
        .span = {},
      });
    }
    options.inputFile = arg;
  }

  if (options.inputFile.empty()) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::InvalidCli,
      .message = "Missing input file. Usage: thag <input.tg> [--emit-ir|--emit-obj] -o <output>",
      .span = {},
    });
  }

  if (!options.emitIR && !options.emitObject) {
    options.emitIR = true;
  }

  if (options.outputFile.empty()) {
    const auto stem = std::filesystem::path(options.inputFile).stem().string();
    options.outputFile = options.emitObject ? stem + ".o" : stem + ".ll";
  }
  return options;
}

auto readFile(const std::string &path) -> Result<std::string, Diagnostic> {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::IoError,
      .message = std::format("Cannot open input file '{}'.", path),
      .span = {},
    });
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

auto createTargetMachine(const DriverOptions &options) -> Result<std::unique_ptr<llvm::TargetMachine>, Diagnostic> {
  const auto triple = llvm::sys::getDefaultTargetTriple();
  std::string error;
  const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple, error);
  if (target == nullptr) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = std::format("Cannot initialize target '{}': {}", triple, error),
      .span = {},
    });
  }

  llvm::TargetOptions targetOptions;
  auto relocationModel = std::optional<llvm::Reloc::Model>();
  auto level = llvm::CodeGenOptLevel::Default;
  if (options.optLevel <= 0) {
    level = llvm::CodeGenOptLevel::None;
  } else if (options.optLevel == 1) {
    level = llvm::CodeGenOptLevel::Less;
  } else if (options.optLevel >= 3) {
    level = llvm::CodeGenOptLevel::Aggressive;
  }

  auto machine = std::unique_ptr<llvm::TargetMachine>(
    target->createTargetMachine(triple, "generic", "", targetOptions, relocationModel, std::nullopt, level)
  );
  if (!machine) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = "Failed to create LLVM target machine.",
      .span = {},
    });
  }
  return machine;
}

} // namespace

auto Driver::run(const std::vector<std::string> &args) -> int {
  auto options = parseArgs(args);
  if (!options) {
    printDiagnostic(options.error());
    return 1;
  }

  auto source = readFile(options->inputFile);
  if (!source) {
    printDiagnostic(source.error());
    return 1;
  }

  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  auto targetMachine = createTargetMachine(options.value());
  if (!targetMachine) {
    printDiagnostic(targetMachine.error());
    return 1;
  }

  Lexer lexer {};
  auto tokens = lexer.tokenize(source.value(), options->inputFile);
  if (!tokens) {
    printDiagnostic(tokens.error());
    return 1;
  }

  Parser parser {};
  auto moduleAst = parser.parseModule(tokens.value());
  if (!moduleAst) {
    printDiagnostic(moduleAst.error());
    return 1;
  }

  SemanticAnalyzer semantic {};
  auto typed = semantic.analyze(std::move(moduleAst.value()));
  if (!typed) {
    printDiagnostic(typed.error());
    return 1;
  }

  llvm::LLVMContext context;
  IRGenerator generator {context};
  auto module = generator.lower(typed.value(), options->inputFile);
  if (!module) {
    printDiagnostic(module.error());
    return 1;
  }

  module.value()->setTargetTriple(llvm::Triple(llvm::sys::getDefaultTargetTriple()));
  module.value()->setDataLayout(targetMachine.value()->createDataLayout());

  auto opt = BackendPipeline::optimizeModule(*module.value(), options->optLevel);
  if (!opt) {
    printDiagnostic(opt.error());
    return 1;
  }

  if (options->emitIR) {
    auto result = BackendPipeline::emitIR(*module.value(), options->outputFile);
    if (!result) {
      printDiagnostic(result.error());
      return 1;
    }
  }

  if (options->emitObject) {
    auto result = BackendPipeline::emitObject(*module.value(), *targetMachine.value(), options->outputFile);
    if (!result) {
      printDiagnostic(result.error());
      return 1;
    }
  }

#if defined(__cpp_lib_print) && __cpp_lib_print >= 202207L
  std::print("thag: emitted {}\n", options->outputFile);
#else
  std::cout << std::format("thag: emitted {}\n", options->outputFile);
#endif
  return 0;
}

} // namespace thagore

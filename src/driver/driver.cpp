#include "thagore/driver/driver.hpp"

#include "thagore/backend/ir_generator.hpp"
#include "thagore/common/diagnostics.hpp"
#include "thagore/frontend/lexer.hpp"
#include "thagore/frontend/parser.hpp"
#include "thagore/frontend/semantic.hpp"

#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#include <cstdlib>
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
  std::size_t i = 1;
  if (i < args.size() && args[i] == "build") {
    options.mode = DriverMode::BuildExecutable;
    ++i;
  }

  for (; i < args.size(); ++i) {
    const auto &arg = args[i];
    if (arg == "--emit-ir") {
      options.emitIR = true;
      continue;
    }
    if (arg == "--emit-obj") {
      options.emitObject = true;
      continue;
    }
    if (arg == "--release") {
      options.release = true;
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
      .message =
        "Missing input file. Usage: thag [build] <input.tg> [--emit-ir] [--emit-obj] [--release] -o <output>",
      .span = {},
    });
  }

  if (options.mode == DriverMode::CompileOnly && !options.emitIR && !options.emitObject) {
    options.emitIR = true;
  }

  if (options.outputFile.empty()) {
    const auto stem = std::filesystem::path(options.inputFile).stem().string();
    if (options.mode == DriverMode::BuildExecutable) {
      options.outputFile = stem + ".exe";
    } else {
      options.outputFile = options.emitObject ? stem + ".o" : stem + ".ll";
    }
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

auto findLLVMTool(const std::string &toolName) -> std::optional<std::filesystem::path> {
  if (const char *binEnv = std::getenv("THAG_LLVM_BIN"); binEnv != nullptr && *binEnv != '\0') {
    const auto candidate = std::filesystem::path(binEnv) / toolName;
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }

  if (const char *llvmDir = std::getenv("LLVM_DIR"); llvmDir != nullptr && *llvmDir != '\0') {
    auto base = std::filesystem::path(llvmDir);
    if (base.filename() == "llvm") {
      base = base.parent_path().parent_path().parent_path();
    }
    const auto candidate = base / "bin" / toolName;
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }

  const auto localRoot = std::filesystem::current_path() / "llvm";
  if (std::filesystem::exists(localRoot)) {
    for (const auto &entry : std::filesystem::directory_iterator(localRoot)) {
      if (!entry.is_directory()) {
        continue;
      }
      const auto candidate = entry.path() / "bin" / toolName;
      if (std::filesystem::exists(candidate)) {
        return candidate;
      }
    }
  }

  if (auto resolved = llvm::sys::findProgramByName(toolName)) {
    return std::filesystem::path(*resolved);
  }
  return std::nullopt;
}

auto runTool(const std::filesystem::path &tool, const std::vector<std::string> &arguments, std::string_view stage)
  -> Result<void, Diagnostic> {
  std::vector<std::string> argvStrings {};
  argvStrings.reserve(arguments.size() + 1);
  argvStrings.push_back(tool.string());
  argvStrings.insert(argvStrings.end(), arguments.begin(), arguments.end());

  llvm::SmallVector<llvm::StringRef, 16> argvRefs {};
  argvRefs.reserve(argvStrings.size());
  for (const auto &arg : argvStrings) {
    argvRefs.push_back(arg);
  }

  std::string errorMessage {};
  const int exitCode = llvm::sys::ExecuteAndWait(
    tool.string(),
    argvRefs,
    std::nullopt,
    {},
    0,
    0,
    &errorMessage
  );

  if (exitCode != 0) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = std::format("{} failed with exit code {}: {}", stage, exitCode, errorMessage),
      .span = {},
    });
  }
  return {};
}

auto linkExecutable(
  const DriverOptions &options,
  const std::filesystem::path &objectPath,
  const std::filesystem::path &outputPath,
  const std::filesystem::path &runtimeLibPath
) -> Result<void, Diagnostic> {
  auto lld = findLLVMTool("lld-link.exe");
  if (!lld) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = "Cannot find lld-link.exe. Set THAG_LLVM_BIN or LLVM_DIR.",
      .span = {},
    });
  }

  std::vector<std::string> args {
    "/NOLOGO",
    std::format("/OUT:{}", outputPath.string()),
    "/SUBSYSTEM:CONSOLE",
    objectPath.string(),
    runtimeLibPath.string(),
  };

  if (options.release) {
    args.push_back("/OPT:REF");
    args.push_back("/OPT:ICF");
    args.push_back("/DEBUG:NONE");
    args.push_back("/INCREMENTAL:NO");
  }

  auto linkResult = runTool(*lld, args, "lld-link");
  if (!linkResult) {
    return std::unexpected(linkResult.error());
  }

  if (options.release) {
    auto strip = findLLVMTool("llvm-strip.exe");
    if (!strip) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "Cannot find llvm-strip.exe for --release mode.",
        .span = {},
      });
    }

    auto stripResult = runTool(*strip, {"--strip-all", outputPath.string()}, "llvm-strip");
    if (!stripResult) {
      return std::unexpected(stripResult.error());
    }
  }

  return {};
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

  const auto outputPath = std::filesystem::path(options->outputFile);
  const auto objectPath = (options->mode == DriverMode::BuildExecutable)
    ? outputPath.parent_path() / (outputPath.stem().string() + ".obj")
    : outputPath;

  if (options->emitObject || options->mode == DriverMode::BuildExecutable) {
    auto result = BackendPipeline::emitObject(*module.value(), *targetMachine.value(), objectPath.string());
    if (!result) {
      printDiagnostic(result.error());
      return 1;
    }
  }

  if (options->mode == DriverMode::BuildExecutable) {
    const auto thagExe = std::filesystem::absolute(std::filesystem::path(args[0]));
    const auto runtimeLib = thagExe.parent_path() / "thag_runtime.lib";
    if (!std::filesystem::exists(runtimeLib)) {
      printDiagnostic(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = std::format("Missing runtime library '{}'.", runtimeLib.string()),
        .span = {},
      });
      return 1;
    }

    auto linkResult = linkExecutable(options.value(), objectPath, outputPath, runtimeLib);
    if (!linkResult) {
      printDiagnostic(linkResult.error());
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

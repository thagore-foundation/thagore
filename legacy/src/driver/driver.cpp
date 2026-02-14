#include "thagore/driver/driver.hpp"

#include "thagore/backend/ir_generator.hpp"
#include "thagore/common/diagnostics.hpp"
#include "thagore/frontend/lexer.hpp"
#include "thagore/frontend/parser.hpp"
#include "thagore/frontend/semantic.hpp"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Config/llvm-config.h"
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
#include <unordered_map>
#include <unordered_set>

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

struct ImportExportMap {
  std::string namespacePrefix {};
  std::unordered_map<std::string, std::string> functions {};
  std::unordered_map<std::string, std::string> structs {};
};

struct ModuleCacheEntry {
  ImportExportMap exports {};
};

auto sanitizeNamespace(const std::string &key) -> std::string {
  std::size_t hashed = std::hash<std::string> {}(key);
  return std::format("__mod_{:x}_", hashed);
}

auto parseModuleFile(const std::filesystem::path &path) -> Result<std::unique_ptr<ModuleDecl>, Diagnostic> {
  auto source = readFile(path.string());
  if (!source) {
    return std::unexpected(source.error());
  }
  Lexer lexer {};
  auto tokens = lexer.tokenize(source.value(), path.string());
  if (!tokens) {
    return std::unexpected(tokens.error());
  }
  Parser parser {};
  auto module = parser.parseModule(tokens.value());
  if (!module) {
    return std::unexpected(module.error());
  }
  return module;
}

auto resolveImportPath(const std::filesystem::path &baseDir, const std::string &rawPath)
  -> Result<std::filesystem::path, Diagnostic> {
  const std::filesystem::path raw {rawPath};
  std::vector<std::filesystem::path> candidates {};

  auto pushCandidates = [&](const std::filesystem::path &root, bool includeFolderAsFile) {
    if (raw.has_extension()) {
      candidates.push_back(root / raw);
    } else {
      if (includeFolderAsFile) {
        candidates.push_back(root / raw);
      }
      candidates.push_back(root / (raw.string() + ".tg"));
      candidates.push_back(root / raw / "mod.tg");
      candidates.push_back(root / (raw.string() + "/mod.tg"));
    }
  };

  if (raw.is_absolute()) {
    pushCandidates(std::filesystem::path {}, true);
  } else {
    const bool isBareModuleName = !raw.has_parent_path() && !raw.has_extension();
    if (isBareModuleName) {
      // Flat module resolution: local module first, then standard library folder.
      pushCandidates(baseDir, true);
      pushCandidates(std::filesystem::current_path() / "lib", false);
      pushCandidates(std::filesystem::current_path(), true);
    } else {
      pushCandidates(baseDir, true);
      pushCandidates(std::filesystem::current_path(), true);
    }
  }

  for (const auto &candidate : candidates) {
    if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate)) {
      return std::filesystem::weakly_canonical(candidate);
    }
  }

  return std::unexpected(Diagnostic {
    .code = ErrorCode::IoError,
    .message = std::format("Cannot resolve import '{}'.", rawPath),
    .span = {},
  });
}

void renameTypeNames(TypePtr &type, const std::unordered_map<std::string, std::string> &structRenames) {
  if (!type) {
    return;
  }
  if (type->base == BaseType::Struct) {
    if (auto it = structRenames.find(type->name); it != structRenames.end()) {
      type->name = it->second;
    }
  } else if (type->base == BaseType::Array && type->elementType) {
    renameTypeNames(type->elementType, structRenames);
  }
}

void renameExprNames(
  std::unique_ptr<Expr> &expr,
  const std::unordered_map<std::string, std::string> &funcRenames,
  const std::unordered_map<std::string, std::string> &structRenames
);

void renameStmtNames(
  std::unique_ptr<Stmt> &stmt,
  const std::unordered_map<std::string, std::string> &funcRenames,
  const std::unordered_map<std::string, std::string> &structRenames
) {
  if (!stmt) {
    return;
  }
  switch (stmt->kind) {
    case NodeKind::LetStmt: {
      auto &s = static_cast<LetStmt &>(*stmt);
      renameExprNames(s.init, funcRenames, structRenames);
      return;
    }
    case NodeKind::AssignStmt: {
      auto &s = static_cast<AssignStmt &>(*stmt);
      renameExprNames(s.value, funcRenames, structRenames);
      return;
    }
    case NodeKind::MemberAssignStmt: {
      auto &s = static_cast<MemberAssignStmt &>(*stmt);
      renameExprNames(s.value, funcRenames, structRenames);
      return;
    }
    case NodeKind::ArrayAssignStmt: {
      auto &s = static_cast<ArrayAssignStmt &>(*stmt);
      renameExprNames(s.index, funcRenames, structRenames);
      renameExprNames(s.value, funcRenames, structRenames);
      return;
    }
    case NodeKind::ReturnStmt: {
      auto &s = static_cast<ReturnStmt &>(*stmt);
      if (s.value) {
        renameExprNames(s.value, funcRenames, structRenames);
      }
      return;
    }
    case NodeKind::IfStmt: {
      auto &s = static_cast<IfStmt &>(*stmt);
      renameExprNames(s.condition, funcRenames, structRenames);
      for (auto &nested : s.thenBlock->statements) {
        renameStmtNames(nested, funcRenames, structRenames);
      }
      if (s.elseBlock) {
        for (auto &nested : s.elseBlock->statements) {
          renameStmtNames(nested, funcRenames, structRenames);
        }
      }
      return;
    }
    case NodeKind::LoopStmt: {
      auto &s = static_cast<LoopStmt &>(*stmt);
      if (s.condition) {
        renameExprNames(s.condition, funcRenames, structRenames);
      }
      for (auto &nested : s.body->statements) {
        renameStmtNames(nested, funcRenames, structRenames);
      }
      return;
    }
    case NodeKind::ExprStmt: {
      auto &s = static_cast<ExprStmt &>(*stmt);
      renameExprNames(s.expr, funcRenames, structRenames);
      return;
    }
    case NodeKind::BlockStmt: {
      auto &s = static_cast<BlockStmt &>(*stmt);
      for (auto &nested : s.statements) {
        renameStmtNames(nested, funcRenames, structRenames);
      }
      return;
    }
    default:
      return;
  }
}

void renameExprNames(
  std::unique_ptr<Expr> &expr,
  const std::unordered_map<std::string, std::string> &funcRenames,
  const std::unordered_map<std::string, std::string> &structRenames
) {
  if (!expr) {
    return;
  }
  switch (expr->kind) {
    case NodeKind::BinaryExpr: {
      auto &e = static_cast<BinaryExpr &>(*expr);
      renameExprNames(e.left, funcRenames, structRenames);
      renameExprNames(e.right, funcRenames, structRenames);
      return;
    }
    case NodeKind::CallExpr: {
      auto &e = static_cast<CallExpr &>(*expr);
      if (auto fn = funcRenames.find(e.callee); fn != funcRenames.end()) {
        e.callee = fn->second;
      } else if (auto st = structRenames.find(e.callee); st != structRenames.end()) {
        e.callee = st->second;
      }
      for (auto &arg : e.args) {
        renameExprNames(arg, funcRenames, structRenames);
      }
      return;
    }
    case NodeKind::MemberExpr: {
      auto &e = static_cast<MemberExpr &>(*expr);
      renameExprNames(e.object, funcRenames, structRenames);
      return;
    }
    case NodeKind::MethodCallExpr: {
      auto &e = static_cast<MethodCallExpr &>(*expr);
      renameExprNames(e.object, funcRenames, structRenames);
      for (auto &arg : e.args) {
        renameExprNames(arg, funcRenames, structRenames);
      }
      return;
    }
    case NodeKind::IndexExpr: {
      auto &e = static_cast<IndexExpr &>(*expr);
      renameExprNames(e.array, funcRenames, structRenames);
      renameExprNames(e.index, funcRenames, structRenames);
      return;
    }
    case NodeKind::ArrayLiteralExpr: {
      auto &e = static_cast<ArrayLiteralExpr &>(*expr);
      for (auto &item : e.elements) {
        renameExprNames(item, funcRenames, structRenames);
      }
      return;
    }
    default:
      return;
  }
}

void rewriteQualifiedCallsInExpr(std::unique_ptr<Expr> &expr, const std::unordered_map<std::string, ImportExportMap> &imports);

void rewriteQualifiedCallsInStmt(std::unique_ptr<Stmt> &stmt, const std::unordered_map<std::string, ImportExportMap> &imports) {
  if (!stmt) {
    return;
  }
  switch (stmt->kind) {
    case NodeKind::LetStmt: {
      auto &s = static_cast<LetStmt &>(*stmt);
      rewriteQualifiedCallsInExpr(s.init, imports);
      return;
    }
    case NodeKind::AssignStmt: {
      auto &s = static_cast<AssignStmt &>(*stmt);
      rewriteQualifiedCallsInExpr(s.value, imports);
      return;
    }
    case NodeKind::MemberAssignStmt: {
      auto &s = static_cast<MemberAssignStmt &>(*stmt);
      rewriteQualifiedCallsInExpr(s.value, imports);
      return;
    }
    case NodeKind::ArrayAssignStmt: {
      auto &s = static_cast<ArrayAssignStmt &>(*stmt);
      rewriteQualifiedCallsInExpr(s.index, imports);
      rewriteQualifiedCallsInExpr(s.value, imports);
      return;
    }
    case NodeKind::ReturnStmt: {
      auto &s = static_cast<ReturnStmt &>(*stmt);
      if (s.value) {
        rewriteQualifiedCallsInExpr(s.value, imports);
      }
      return;
    }
    case NodeKind::IfStmt: {
      auto &s = static_cast<IfStmt &>(*stmt);
      rewriteQualifiedCallsInExpr(s.condition, imports);
      for (auto &nested : s.thenBlock->statements) {
        rewriteQualifiedCallsInStmt(nested, imports);
      }
      if (s.elseBlock) {
        for (auto &nested : s.elseBlock->statements) {
          rewriteQualifiedCallsInStmt(nested, imports);
        }
      }
      return;
    }
    case NodeKind::LoopStmt: {
      auto &s = static_cast<LoopStmt &>(*stmt);
      if (s.condition) {
        rewriteQualifiedCallsInExpr(s.condition, imports);
      }
      for (auto &nested : s.body->statements) {
        rewriteQualifiedCallsInStmt(nested, imports);
      }
      return;
    }
    case NodeKind::ExprStmt: {
      auto &s = static_cast<ExprStmt &>(*stmt);
      rewriteQualifiedCallsInExpr(s.expr, imports);
      return;
    }
    case NodeKind::BlockStmt: {
      auto &s = static_cast<BlockStmt &>(*stmt);
      for (auto &nested : s.statements) {
        rewriteQualifiedCallsInStmt(nested, imports);
      }
      return;
    }
    default:
      return;
  }
}

void rewriteQualifiedCallsInExpr(std::unique_ptr<Expr> &expr, const std::unordered_map<std::string, ImportExportMap> &imports) {
  if (!expr) {
    return;
  }
  switch (expr->kind) {
    case NodeKind::BinaryExpr: {
      auto &e = static_cast<BinaryExpr &>(*expr);
      rewriteQualifiedCallsInExpr(e.left, imports);
      rewriteQualifiedCallsInExpr(e.right, imports);
      return;
    }
    case NodeKind::CallExpr: {
      auto &e = static_cast<CallExpr &>(*expr);
      for (auto &arg : e.args) {
        rewriteQualifiedCallsInExpr(arg, imports);
      }
      return;
    }
    case NodeKind::MemberExpr: {
      auto &e = static_cast<MemberExpr &>(*expr);
      rewriteQualifiedCallsInExpr(e.object, imports);
      return;
    }
    case NodeKind::MethodCallExpr: {
      auto &e = static_cast<MethodCallExpr &>(*expr);
      rewriteQualifiedCallsInExpr(e.object, imports);
      for (auto &arg : e.args) {
        rewriteQualifiedCallsInExpr(arg, imports);
      }
      if (e.object && e.object->kind == NodeKind::IdentifierExpr) {
        const auto &id = static_cast<const IdentifierExpr &>(*e.object);
        if (auto it = imports.find(id.name); it != imports.end()) {
          auto fn = it->second.functions.find(e.method);
          if (fn != it->second.functions.end()) {
            expr = std::make_unique<CallExpr>(fn->second, std::move(e.args), e.span);
            return;
          }
          auto st = it->second.structs.find(e.method);
          if (st != it->second.structs.end()) {
            expr = std::make_unique<CallExpr>(st->second, std::move(e.args), e.span);
            return;
          }
        }
      }
      return;
    }
    case NodeKind::IndexExpr: {
      auto &e = static_cast<IndexExpr &>(*expr);
      rewriteQualifiedCallsInExpr(e.array, imports);
      rewriteQualifiedCallsInExpr(e.index, imports);
      return;
    }
    case NodeKind::ArrayLiteralExpr: {
      auto &e = static_cast<ArrayLiteralExpr &>(*expr);
      for (auto &item : e.elements) {
        rewriteQualifiedCallsInExpr(item, imports);
      }
      return;
    }
    default:
      return;
  }
}

auto rewriteQualifiedCallsInModule(
  ModuleDecl &module,
  const std::unordered_map<std::string, ImportExportMap> &imports
) -> void {
  for (auto &fn : module.functions) {
    if (fn->body) {
      for (auto &stmt : fn->body->statements) {
        rewriteQualifiedCallsInStmt(stmt, imports);
      }
    }
  }
  for (auto &stmt : module.topLevelStatements) {
    rewriteQualifiedCallsInStmt(stmt, imports);
  }
}

auto renameModuleSymbols(std::unique_ptr<ModuleDecl> &module, const std::string &namespacePrefix) -> ImportExportMap {
  ImportExportMap exports {};
  exports.namespacePrefix = namespacePrefix;

  std::unordered_map<std::string, std::string> structRenames {};
  for (auto &st : module->structs) {
    const std::string original = st->name;
    const std::string renamed = namespacePrefix + original;
    exports.structs.emplace(original, renamed);
    structRenames.emplace(original, renamed);
    st->name = renamed;
  }

  std::unordered_map<std::string, std::string> functionRenames {};
  for (auto &fn : module->functions) {
    if (fn->isExtern) {
      continue;
    }
    const std::string original = fn->name;
    const std::string renamed = namespacePrefix + original;
    functionRenames.emplace(original, renamed);
    if (fn->methodOwner.empty()) {
      exports.functions.emplace(fn->sourceName, renamed);
    }
    fn->name = renamed;
  }

  for (auto &st : module->structs) {
    for (auto &field : st->fields) {
      renameTypeNames(field.type, structRenames);
    }
  }

  for (auto &fn : module->functions) {
    if (fn->methodOwner == "__module__") {
      fn->methodOwner.clear();
    }
    if (!fn->methodOwner.empty()) {
      if (auto it = structRenames.find(fn->methodOwner); it != structRenames.end()) {
        fn->methodOwner = it->second;
      }
    }
    for (auto &param : fn->params) {
      renameTypeNames(param.type, structRenames);
    }
    renameTypeNames(fn->returnType, structRenames);
    if (fn->body) {
      for (auto &stmt : fn->body->statements) {
        renameStmtNames(stmt, functionRenames, structRenames);
      }
    }
  }

  module->imports.clear();
  return exports;
}

auto loadImportedModule(
  const std::filesystem::path &modulePath,
  std::unordered_map<std::string, ModuleCacheEntry> &cache,
  std::unordered_set<std::string> &loading,
  std::vector<std::unique_ptr<StructDecl>> &importedStructs,
  std::vector<std::unique_ptr<FunctionDecl>> &importedFunctions
) -> Result<ImportExportMap, Diagnostic> {
  const auto canonical = std::filesystem::weakly_canonical(modulePath).string();
  if (auto it = cache.find(canonical); it != cache.end()) {
    return it->second.exports;
  }
  if (loading.contains(canonical)) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::SemanticError,
      .message = std::format("Import cycle detected at '{}'.", canonical),
      .span = {},
    });
  }
  loading.insert(canonical);

  auto parsed = parseModuleFile(canonical);
  if (!parsed) {
    loading.erase(canonical);
    return std::unexpected(parsed.error());
  }

  auto module = std::move(parsed.value());
  std::unordered_map<std::string, ImportExportMap> importAliases {};
  const auto moduleDir = std::filesystem::path(canonical).parent_path();
  for (const auto &imp : module->imports) {
    auto resolved = resolveImportPath(moduleDir, imp.path);
    if (!resolved) {
      loading.erase(canonical);
      return std::unexpected(resolved.error());
    }
    auto imported = loadImportedModule(resolved.value(), cache, loading, importedStructs, importedFunctions);
    if (!imported) {
      loading.erase(canonical);
      return std::unexpected(imported.error());
    }
    importAliases[imp.alias] = imported.value();
  }
  rewriteQualifiedCallsInModule(*module, importAliases);

  if (!module->topLevelStatements.empty()) {
    loading.erase(canonical);
    return std::unexpected(Diagnostic {
      .code = ErrorCode::SemanticError,
      .message = std::format("Imported module '{}' cannot contain top-level executable statements.", canonical),
      .span = module->span,
    });
  }

  auto exports = renameModuleSymbols(module, sanitizeNamespace(canonical));
  for (auto &st : module->structs) {
    importedStructs.push_back(std::move(st));
  }
  for (auto &fn : module->functions) {
    importedFunctions.push_back(std::move(fn));
  }

  cache.emplace(canonical, ModuleCacheEntry {.exports = exports});
  loading.erase(canonical);
  return exports;
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
  auto machine = std::unique_ptr<llvm::TargetMachine>(
    target->createTargetMachine(triple, "generic", "", targetOptions, relocationModel)
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
#if defined(_WIN32)
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
#else
  auto cxx = findLLVMTool("clang++");
  if (!cxx) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = "Cannot find clang++. Set THAG_LLVM_BIN or LLVM_DIR.",
      .span = {},
    });
  }

  std::vector<std::string> args {
    "-no-pie",
    objectPath.string(),
    runtimeLibPath.string(),
    "-o",
    outputPath.string(),
  };

  auto linkResult = runTool(*cxx, args, "clang++");
  if (!linkResult) {
    return std::unexpected(linkResult.error());
  }

  if (options.release) {
    auto strip = findLLVMTool("llvm-strip");
    if (!strip) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "Cannot find llvm-strip for --release mode.",
        .span = {},
      });
    }

    auto stripResult = runTool(*strip, {"--strip-all", outputPath.string()}, "llvm-strip");
    if (!stripResult) {
      return std::unexpected(stripResult.error());
    }
  }

  return {};
#endif
}

} // namespace

auto Driver::run(const std::vector<std::string> &args) -> int {
  auto options = parseArgs(args);
  if (!options) {
    printDiagnostic(options.error());
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

  auto rootPath = std::filesystem::weakly_canonical(std::filesystem::path(options->inputFile));
  auto moduleAst = parseModuleFile(rootPath);
  if (!moduleAst) {
    printDiagnostic(moduleAst.error());
    return 1;
  }

  std::unordered_map<std::string, ModuleCacheEntry> cache {};
  std::unordered_set<std::string> loading {};
  std::vector<std::unique_ptr<StructDecl>> importedStructs {};
  std::vector<std::unique_ptr<FunctionDecl>> importedFunctions {};
  std::unordered_map<std::string, ImportExportMap> rootAliases {};

  const auto baseDir = rootPath.parent_path();
  for (const auto &imp : moduleAst.value()->imports) {
    auto resolved = resolveImportPath(baseDir, imp.path);
    if (!resolved) {
      printDiagnostic(resolved.error());
      return 1;
    }
    auto imported = loadImportedModule(
      resolved.value(),
      cache,
      loading,
      importedStructs,
      importedFunctions
    );
    if (!imported) {
      printDiagnostic(imported.error());
      return 1;
    }
    rootAliases[imp.alias] = imported.value();
  }

  rewriteQualifiedCallsInModule(*moduleAst.value(), rootAliases);
  moduleAst.value()->imports.clear();

  for (auto &st : importedStructs) {
    moduleAst.value()->structs.insert(moduleAst.value()->structs.begin(), std::move(st));
  }
  for (auto &fn : importedFunctions) {
    moduleAst.value()->functions.insert(moduleAst.value()->functions.begin(), std::move(fn));
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

#if LLVM_VERSION_MAJOR >= 21
  module.value()->setTargetTriple(llvm::Triple(llvm::sys::getDefaultTargetTriple()));
#else
  module.value()->setTargetTriple(llvm::sys::getDefaultTargetTriple());
#endif
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
#if defined(_WIN32)
    ? outputPath.parent_path() / (outputPath.stem().string() + ".obj")
#else
    ? outputPath.parent_path() / (outputPath.stem().string() + ".o")
#endif
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
    const auto runtimeLib = thagExe.parent_path()
#if defined(_WIN32)
      / "thag_runtime.lib";
#else
      / "libthag_runtime.a";
#endif
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

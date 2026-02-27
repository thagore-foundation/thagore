#include "thagc/infra/adapters.hpp"
#include "thagc/infra/embedded_runtime.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace thagc::infra {

namespace {

static std::filesystem::path runtime_temp_directory() {
  namespace fs = std::filesystem;
#if defined(_WIN32)
  char raw_temp[MAX_PATH + 1] = {};
  const DWORD len = ::GetTempPathA(MAX_PATH, raw_temp);
  if (len > 0 && len <= MAX_PATH) {
    return fs::path(std::string(raw_temp, len));
  }
  std::error_code ec;
  fs::path fallback = fs::temp_directory_path(ec);
  if (!ec) {
    return fallback;
  }
  return fs::path(".");
#else
  std::error_code ec;
  fs::path fallback = fs::temp_directory_path(ec);
  if (!ec) {
    return fallback;
  }
  return fs::path("/tmp");
#endif
}

static unsigned long runtime_process_id() {
#if defined(_WIN32)
  return static_cast<unsigned long>(::GetCurrentProcessId());
#else
  return static_cast<unsigned long>(::getpid());
#endif
}

class TempRuntimeArchive {
 public:
  explicit TempRuntimeArchive(std::filesystem::path path) : path_(std::move(path)) {}
  TempRuntimeArchive(const TempRuntimeArchive&) = delete;
  TempRuntimeArchive& operator=(const TempRuntimeArchive&) = delete;

  TempRuntimeArchive(TempRuntimeArchive&& other) noexcept : path_(std::move(other.path_)) {
    other.path_.clear();
  }

  TempRuntimeArchive& operator=(TempRuntimeArchive&& other) noexcept {
    if (this != &other) {
      cleanup();
      path_ = std::move(other.path_);
      other.path_.clear();
    }
    return *this;
  }

  ~TempRuntimeArchive() {
    cleanup();
  }

  std::string path_string() const {
    return path_.string();
  }

 private:
  void cleanup() {
    if (path_.empty()) {
      return;
    }
    std::error_code ec;
    std::filesystem::remove(path_, ec);
    path_.clear();
  }

  std::filesystem::path path_;
};

static std::optional<TempRuntimeArchive> extract_runtime_archive(support::DiagnosticSink& diag) {
  if (thagore::kRuntimeLibLen == 0u) {
    diag.error("E3001", "embedded runtime archive is empty");
    return std::nullopt;
  }

  const std::filesystem::path archive_path =
      runtime_temp_directory() / ("thag_runtime_" + std::to_string(runtime_process_id()) + ".a");
  std::ofstream output(archive_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    diag.error("E3001", "failed to create temporary runtime archive: " + archive_path.string());
    return std::nullopt;
  }

  output.write(reinterpret_cast<const char*>(thagore::kRuntimeLib), static_cast<std::streamsize>(thagore::kRuntimeLibLen));
  output.flush();
  const bool write_ok = output.good();
  output.close();
  if (!write_ok) {
    std::error_code ec;
    std::filesystem::remove(archive_path, ec);
    diag.error("E3001", "failed to write temporary runtime archive: " + archive_path.string());
    return std::nullopt;
  }

  return TempRuntimeArchive(archive_path);
}

}  // namespace

std::vector<syntax::Token> LexerAdapter::tokenize(const std::string& source) {
  return syntax::Lexer().tokenize(source);
}

syntax::AstProgram ParserAdapter::parse(const std::vector<syntax::Token>& tokens, const std::string& source) {
  return syntax::Parser().parse(tokens, source);
}

bool TypeCheckerAdapter::check(const syntax::AstProgram& program, support::DiagnosticSink& diag) {
  return semantics::TypeChecker().check(program, diag);
}

lowering::CoreProgram LoweringAdapter::lower(const syntax::AstProgram& program) {
  return lowering::lower_to_core(program);
}

bool LlvmCodegenAdapter::emit_object(const lowering::CoreProgram& core, const std::string& module_name,
                                     const std::string& object_path, const std::string& target_triple,
                                     support::DiagnosticSink& diag) {
  return codegen::LlvmEmitter().emit_object(core, module_name, object_path, target_triple, diag);
}

bool LlvmCodegenAdapter::emit_llvm_ir(const lowering::CoreProgram& core, const std::string& module_name,
                                      const std::string& llvm_ir_path, const std::string& target_triple,
                                      support::DiagnosticSink& diag) {
  return codegen::LlvmEmitter().emit_llvm_ir(core, module_name, llvm_ir_path, target_triple, diag);
}

domain::LinkResult ClangLinkerAdapter::link_executable(const domain::LinkPlan& plan,
                                                       support::DiagnosticSink& diag) {
  domain::LinkResult out;
  const std::string linker = plan.linker_path.empty() ? "clang" : plan.linker_path;
  std::optional<TempRuntimeArchive> runtime_archive = extract_runtime_archive(diag);
  if (!runtime_archive.has_value()) {
    out.exit_code = 1;
    out.success = false;
    out.error = "embedded runtime extraction failed";
    out.command = linker + " " + plan.object_path + " -o " + plan.output_path;
    return out;
  }

  std::vector<std::string> clang_link = {linker, plan.object_path, "-o", plan.output_path};
  clang_link.push_back(runtime_archive->path_string());
#if defined(__linux__)
  clang_link.push_back("-lstdc++");
  clang_link.push_back("-lpthread");
#endif
#if defined(__linux__)
  clang_link.push_back("-no-pie");
#endif
  if (!plan.target_triple.empty()) {
    clang_link.push_back("--target=" + plan.target_triple);
  }
  if (!plan.sysroot.empty()) {
    clang_link.push_back("--sysroot=" + plan.sysroot);
  }
  clang_link.insert(clang_link.end(), plan.extra_args.begin(), plan.extra_args.end());
  const int rc = support::run_process(clang_link);

  out.exit_code = rc;
  out.command = linker + " " + plan.object_path + " -o " + plan.output_path;
  if (rc != 0) {
    out.success = false;
    out.error = "clang link failed";
    diag.error("E3001", out.error + " with exit code: " + std::to_string(rc));
    return out;
  }
  out.success = true;
  return out;
}

}  // namespace thagc::infra

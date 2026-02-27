#include "thagc/driver/pipeline.hpp"

#include <cctype>
#include <filesystem>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "thagc/application/build_usecase.hpp"
#include "thagc/domain/model.hpp"
#include "thagc/infra/adapters.hpp"
#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

static std::string trim(const std::string& text) {
  std::size_t left = 0;
  while (left < text.size() && std::isspace(static_cast<unsigned char>(text[left]))) {
    ++left;
  }
  std::size_t right = text.size();
  while (right > left && std::isspace(static_cast<unsigned char>(text[right - 1]))) {
    --right;
  }
  return text.substr(left, right - left);
}

static bool starts_with(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

static std::string parse_import_target(const std::string& line) {
  const std::string clean = trim(line);
  if (!starts_with(clean, "import ")) {
    return "";
  }
  std::string target = trim(clean.substr(7));
  if (target.empty()) {
    return "";
  }
  if ((target.front() == '"' && target.back() == '"') || (target.front() == '\'' && target.back() == '\'')) {
    target = target.substr(1, target.size() - 2);
  }
  return trim(target);
}

static std::vector<std::filesystem::path> import_candidates(const std::filesystem::path& importer,
                                                            const std::string& target) {
  std::vector<std::filesystem::path> out;
  const std::filesystem::path parent = importer.parent_path();
  const std::filesystem::path target_path(target);
  if (target_path.is_absolute()) {
    out.push_back(target_path);
    if (target_path.extension() != ".tg") {
      out.push_back(target_path.string() + ".tg");
    }
    return out;
  }

  out.push_back(parent / target_path);
  if (target_path.extension() != ".tg") {
    out.push_back(parent / (target + ".tg"));
  }

  const std::filesystem::path cwd = std::filesystem::current_path();
  out.push_back(cwd / "stdlib" / "lib" / target_path);
  out.push_back(cwd / "stdlib" / "std" / target_path);
  if (target_path.extension() != ".tg") {
    out.push_back(cwd / "stdlib" / "lib" / (target + ".tg"));
    out.push_back(cwd / "stdlib" / "std" / (target + ".tg"));
  }
  return out;
}

static bool resolve_import_path(const std::filesystem::path& importer, const std::string& target,
                                std::filesystem::path& resolved) {
  for (const auto& candidate : import_candidates(importer, target)) {
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec) && !ec) {
      resolved = std::filesystem::absolute(candidate, ec);
      if (!ec) {
        return true;
      }
    }
  }
  return false;
}

static bool expand_imports_recursive(const std::filesystem::path& file, std::set<std::string>& visiting,
                                     std::set<std::string>& loaded, std::string& out, support::DiagnosticSink& diag) {
  const std::string abs = support::absolute_path(file.string());
  if (loaded.find(abs) != loaded.end()) {
    return true;
  }
  if (visiting.find(abs) != visiting.end()) {
    diag.error("E_MOD_001", "cyclic import detected at '" + abs + "'");
    return false;
  }
  visiting.insert(abs);

  const std::string source = support::read_text_file(abs);
  std::istringstream in(source);
  std::string line;
  while (std::getline(in, line)) {
    const std::string target = parse_import_target(line);
    if (!target.empty()) {
      std::filesystem::path resolved;
      if (!resolve_import_path(abs, target, resolved)) {
        diag.error("E_MOD_002", "cannot resolve import '" + target + "' from '" + abs + "'");
        visiting.erase(abs);
        return false;
      }
      if (!expand_imports_recursive(resolved, visiting, loaded, out, diag)) {
        visiting.erase(abs);
        return false;
      }
      continue;
    }
    out += line;
    out.push_back('\n');
  }

  visiting.erase(abs);
  loaded.insert(abs);
  return true;
}

static std::string expand_imports(const std::string& entry, support::DiagnosticSink& diag) {
  std::set<std::string> visiting;
  std::set<std::string> loaded;
  std::string merged;
  if (!expand_imports_recursive(entry, visiting, loaded, merged, diag)) {
    return "";
  }
  return merged;
}

bool CompilerPipeline::build(const BuildOptions& options, support::DiagnosticSink& diag) const {
  try {
    infra::LexerAdapter lexer;
    infra::ParserAdapter parser;
    infra::TypeCheckerAdapter checker;
    infra::LoweringAdapter lowerer;
    infra::LlvmCodegenAdapter codegen;
    infra::ClangLinkerAdapter linker;

    application::BuildUseCase usecase(lexer, parser, checker, lowerer, codegen, linker);
    domain::BuildRequest request;
    request.input_path = options.input_path;
    request.source_text = expand_imports(options.input_path, diag);
    if (request.source_text.empty()) {
      return false;
    }
    request.output_path = options.output_path;
    request.target_triple = options.target_triple;
    request.target_linker = options.target_linker;
    request.target_sysroot = options.target_sysroot;
    request.emit_llvm = options.emit_llvm;
    request.llvm_ir_path = options.llvm_ir_path;

    return usecase.execute(request, diag).success;
  } catch (const std::exception& ex) {
    diag.error("E3999", ex.what());
    return false;
  }
}

}  // namespace thagc::driver

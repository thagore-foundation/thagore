#include "thagc/driver/pipeline.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "thagc/application/build_usecase.hpp"
#include "thagc/domain/model.hpp"
#include "thagc/driver/resolver.hpp"
#include "thagc/frontend/lexer.hpp"
#include "thagc/frontend/parser.hpp"
#include "thagc/infra/adapters.hpp"
#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

namespace {

struct ModuleImportBinding {
  syntax::AstImport import_decl;
  ResolvedImport resolved;
};

struct ModuleNode {
  std::string key;
  std::string path;
  std::string source;
  std::vector<ModuleImportBinding> imports;
  std::unordered_set<std::string> all_symbols;
  std::unordered_set<std::string> exports;
  bool has_top_level_executable = false;
  bool is_entry = false;
};

struct ModuleCacheRecord {
  std::uint64_t source_hash = 0;
  bool has_top_level_executable = false;
  std::vector<syntax::AstImport> imports;
  std::unordered_set<std::string> all_symbols;
  std::unordered_set<std::string> exports;
};

class ModuleIncrementalCache {
 public:
  explicit ModuleIncrementalCache(std::filesystem::path project_root) : cache_file_(std::move(project_root)) {
    cache_file_ /= ".thagore";
    cache_file_ /= "incremental";
    cache_file_ /= "modules.cache";
    load();
  }

  const ModuleCacheRecord* lookup(const std::string& module_path, std::uint64_t source_hash) const {
    auto it = previous_.find(module_path);
    if (it == previous_.end()) {
      return nullptr;
    }
    if (it->second.source_hash != source_hash) {
      return nullptr;
    }
    return &it->second;
  }

  void stage(const std::string& module_path, ModuleCacheRecord record) {
    next_[module_path] = std::move(record);
  }

  bool persist() const {
    std::error_code ec;
    const std::filesystem::path dir = cache_file_.parent_path();
    std::filesystem::create_directories(dir, ec);
    if (ec) {
      return false;
    }
    const std::filesystem::path temp = cache_file_.string() + ".tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      return false;
    }
    out << "VERSION 1\n";
    std::vector<std::string> paths;
    paths.reserve(next_.size());
    for (const auto& [path, _] : next_) {
      paths.push_back(path);
    }
    std::sort(paths.begin(), paths.end());
    for (const auto& path : paths) {
      auto it = next_.find(path);
      if (it == next_.end()) {
        continue;
      }
      const ModuleCacheRecord& record = it->second;
      out << "MODULE " << std::quoted(path) << ' ' << record.source_hash << ' '
          << (record.has_top_level_executable ? 1 : 0) << '\n';
      write_symbol_line(out, "EXPORTS", path, record.exports);
      write_symbol_line(out, "ALL", path, record.all_symbols);
      for (const auto& import_decl : record.imports) {
        out << "IMPORT " << std::quoted(path) << ' ' << (import_decl.is_from_import ? 1 : 0) << ' '
            << import_decl.line << ' ' << import_decl.column << ' ' << std::quoted(import_decl.alias) << ' '
            << import_decl.module_path.size();
        for (const auto& seg : import_decl.module_path) {
          out << ' ' << std::quoted(seg);
        }
        out << ' ' << import_decl.symbols.size();
        for (const auto& sym : import_decl.symbols) {
          out << ' ' << std::quoted(sym);
        }
        out << '\n';
      }
      out << "END " << std::quoted(path) << '\n';
    }
    out.close();
    if (!out.good()) {
      return false;
    }
    std::filesystem::rename(temp, cache_file_, ec);
    if (ec) {
      std::filesystem::remove(cache_file_, ec);
      ec.clear();
      std::filesystem::rename(temp, cache_file_, ec);
      if (ec) {
        std::filesystem::remove(temp, ec);
        return false;
      }
    }
    return true;
  }

 private:
  static void write_symbol_line(std::ostream& out, const char* tag, const std::string& path,
                                const std::unordered_set<std::string>& symbols) {
    std::vector<std::string> sorted(symbols.begin(), symbols.end());
    std::sort(sorted.begin(), sorted.end());
    out << tag << ' ' << std::quoted(path) << ' ' << sorted.size();
    for (const auto& sym : sorted) {
      out << ' ' << std::quoted(sym);
    }
    out << '\n';
  }

  static bool parse_symbol_line(const std::string& line, const std::string& expected_tag, std::string& path_out,
                                std::unordered_set<std::string>& symbols_out) {
    std::istringstream in(line);
    std::string tag;
    if (!(in >> tag) || tag != expected_tag) {
      return false;
    }
    std::string path;
    std::size_t count = 0;
    if (!(in >> std::quoted(path) >> count)) {
      return false;
    }
    std::unordered_set<std::string> symbols;
    for (std::size_t i = 0; i < count; ++i) {
      std::string symbol;
      if (!(in >> std::quoted(symbol))) {
        return false;
      }
      symbols.insert(symbol);
    }
    path_out = std::move(path);
    symbols_out = std::move(symbols);
    return true;
  }

  void load() {
    previous_.clear();
    std::ifstream in(cache_file_, std::ios::binary);
    if (!in.is_open()) {
      return;
    }
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty()) {
        continue;
      }
      std::istringstream parser(line);
      std::string tag;
      if (!(parser >> tag)) {
        continue;
      }
      if (tag == "VERSION") {
        continue;
      }
      if (tag == "MODULE") {
        std::string path;
        ModuleCacheRecord record;
        int has_top_level = 0;
        if (!(parser >> std::quoted(path) >> record.source_hash >> has_top_level)) {
          continue;
        }
        record.has_top_level_executable = has_top_level != 0;
        previous_[path] = std::move(record);
        continue;
      }
      if (tag == "EXPORTS" || tag == "ALL") {
        std::string path;
        std::unordered_set<std::string> symbols;
        if (!parse_symbol_line(line, tag, path, symbols)) {
          continue;
        }
        auto it = previous_.find(path);
        if (it == previous_.end()) {
          continue;
        }
        if (tag == "EXPORTS") {
          it->second.exports = std::move(symbols);
        } else {
          it->second.all_symbols = std::move(symbols);
        }
        continue;
      }
      if (tag == "IMPORT") {
        std::string path;
        int is_from = 0;
        int line_no = 0;
        int col = 1;
        std::string alias;
        std::size_t module_count = 0;
        if (!(parser >> std::quoted(path) >> is_from >> line_no >> col >> std::quoted(alias) >> module_count)) {
          continue;
        }
        syntax::AstImport import_decl;
        import_decl.is_from_import = is_from != 0;
        import_decl.line = line_no;
        import_decl.column = col;
        import_decl.alias = std::move(alias);
        for (std::size_t i = 0; i < module_count; ++i) {
          std::string seg;
          if (!(parser >> std::quoted(seg))) {
            import_decl.module_path.clear();
            break;
          }
          import_decl.module_path.push_back(std::move(seg));
        }
        if (import_decl.module_path.empty()) {
          continue;
        }
        std::size_t symbols_count = 0;
        if (!(parser >> symbols_count)) {
          continue;
        }
        for (std::size_t i = 0; i < symbols_count; ++i) {
          std::string sym;
          if (!(parser >> std::quoted(sym))) {
            import_decl.symbols.clear();
            break;
          }
          import_decl.symbols.push_back(std::move(sym));
        }
        auto it = previous_.find(path);
        if (it == previous_.end()) {
          continue;
        }
        it->second.imports.push_back(std::move(import_decl));
        continue;
      }
    }
  }

  std::filesystem::path cache_file_;
  std::unordered_map<std::string, ModuleCacheRecord> previous_;
  std::unordered_map<std::string, ModuleCacheRecord> next_;
};

static std::string trim_copy(const std::string& text) {
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

static bool is_ident_body(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

static std::string extract_struct_name(const std::string& header) {
  if (!starts_with(header, "struct ")) {
    return "";
  }
  std::string name = trim_copy(header.substr(7));
  if (!name.empty() && name.back() == ':') {
    name.pop_back();
  }
  return trim_copy(name);
}

static std::string extract_enum_name(const std::string& header) {
  if (!starts_with(header, "enum ")) {
    return "";
  }
  std::string name = trim_copy(header.substr(5));
  if (!name.empty() && name.back() == ':') {
    name.pop_back();
  }
  return trim_copy(name);
}

static std::unordered_set<std::string> collect_all_symbols(const syntax::AstProgram& ast) {
  std::unordered_set<std::string> out;
  for (const auto& fn : ast.functions) {
    if (fn.name.empty()) {
      continue;
    }
    if (fn.name.find('.') != std::string::npos) {
      continue;
    }
    if (fn.name == "main") {
      continue;
    }
    out.insert(fn.name);
  }
  for (const std::string& header : ast.structs) {
    const std::string name = extract_struct_name(header);
    if (!name.empty()) {
      out.insert(name);
    }
  }
  for (const std::string& header : ast.enums) {
    const std::string name = extract_enum_name(header);
    if (!name.empty()) {
      out.insert(name);
    }
  }
  return out;
}

static std::unordered_set<std::string> collect_prefixed_symbol_refs(const std::string& source, const std::string& prefix) {
  std::unordered_set<std::string> out;
  if (prefix.empty()) {
    return out;
  }
  const std::string needle = prefix + ".";
  std::istringstream in(source);
  std::string line;
  while (std::getline(in, line)) {
    const std::string clean = trim_copy(line);
    if (starts_with(clean, "import ") || starts_with(clean, "from ")) {
      continue;
    }
    std::size_t pos = 0;
    while (pos < line.size()) {
      const std::size_t found = line.find(needle, pos);
      if (found == std::string::npos) {
        break;
      }
      const bool left_ok = found == 0 || !is_ident_body(line[found - 1]);
      std::size_t cursor = found + needle.size();
      if (!left_ok || cursor >= line.size() || !(std::isalpha(static_cast<unsigned char>(line[cursor])) || line[cursor] == '_')) {
        pos = found + 1;
        continue;
      }
      const std::size_t symbol_start = cursor;
      while (cursor < line.size() && is_ident_body(line[cursor])) {
        ++cursor;
      }
      out.insert(line.substr(symbol_start, cursor - symbol_start));
      pos = cursor;
    }
  }
  return out;
}

static std::unordered_set<std::string> collect_exports(const syntax::AstProgram& ast) {
  std::unordered_set<std::string> out;
  for (const auto& fn : ast.functions) {
    if (fn.name.empty()) {
      continue;
    }
    if (fn.name.find('.') != std::string::npos) {
      continue;
    }
    if (fn.name == "main") {
      continue;
    }
    auto vis = ast.function_visibility.find(fn.name);
    if (vis != ast.function_visibility.end() && vis->second) {
      out.insert(fn.name);
    }
  }
  for (const std::string& header : ast.structs) {
    const std::string name = extract_struct_name(header);
    auto vis = ast.struct_visibility.find(name);
    if (!name.empty() && vis != ast.struct_visibility.end() && vis->second) {
      out.insert(name);
    }
  }
  for (const std::string& header : ast.enums) {
    const std::string name = extract_enum_name(header);
    auto vis = ast.enum_visibility.find(name);
    if (!name.empty() && vis != ast.enum_visibility.end() && vis->second) {
      out.insert(name);
    }
  }
  return out;
}

static std::string join_cycle_path(const std::vector<std::string>& stack, const std::string& back_edge) {
  std::size_t begin = 0;
  for (std::size_t i = 0; i < stack.size(); ++i) {
    if (stack[i] == back_edge) {
      begin = i;
      break;
    }
  }
  std::string out;
  for (std::size_t i = begin; i < stack.size(); ++i) {
    if (!out.empty()) {
      out += " -> ";
    }
    out += stack[i];
  }
  if (!out.empty()) {
    out += " -> ";
  }
  out += back_edge;
  return out;
}

static std::string replace_prefixed_symbol(const std::string& line, const std::string& prefix, const std::string& symbol) {
  const std::string pattern = prefix + "." + symbol;
  if (pattern.empty()) {
    return line;
  }
  std::string out = line;
  std::size_t pos = 0;
  while (pos < out.size()) {
    const std::size_t found = out.find(pattern, pos);
    if (found == std::string::npos) {
      break;
    }
    const std::size_t end = found + pattern.size();
    const bool left_ok = found == 0 || !is_ident_body(out[found - 1]);
    const bool right_ok = end >= out.size() || !is_ident_body(out[end]);
    if (!left_ok || !right_ok) {
      pos = found + 1;
      continue;
    }
    out.replace(found, pattern.size(), symbol);
    pos = found + symbol.size();
  }
  return out;
}

static std::uint64_t hash_source_text(const std::string& source) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (unsigned char ch : source) {
    hash ^= static_cast<std::uint64_t>(ch);
    hash *= 1099511628211ULL;
  }
  return hash;
}

static bool parse_module_source(const std::string& path, const std::string& source, syntax::AstProgram& ast,
                                support::DiagnosticSink& diag) {
  syntax::Lexer lexer;
  syntax::Parser parser;
  const auto tokens = lexer.tokenize(source);
  ast = parser.parse(tokens, source);
  ast.source_path = path;
  if (!ast.parse_errors.empty()) {
    for (const std::string& message : ast.parse_errors) {
      diag.error("E_MOD_200", message, path);
    }
    return false;
  }
  return true;
}

static bool load_module_recursive(const std::string& path, bool is_entry, ModuleResolver& resolver,
                                  const ProjectManifest& manifest, const std::vector<std::string>& include_paths,
                                  std::unordered_map<std::string, ModuleNode>& modules,
                                  std::unordered_set<std::string>& visiting, std::unordered_set<std::string>& loaded,
                                  std::vector<std::string>& stack, std::vector<std::string>& postorder,
                                  ModuleIncrementalCache& incremental_cache, support::DiagnosticSink& diag) {
  const std::string key = std::filesystem::weakly_canonical(path).string();
  if (loaded.find(key) != loaded.end()) {
    return true;
  }
  if (visiting.find(key) != visiting.end()) {
    diag.error("E_MOD_201", "circular import detected: " + join_cycle_path(stack, key));
    return false;
  }
  visiting.insert(key);
  stack.push_back(key);

  std::string source;
  try {
    source = support::read_text_file(key);
  } catch (const std::exception& ex) {
    diag.error("E_MOD_202", "cannot read module '" + key + "': " + std::string(ex.what()));
    stack.pop_back();
    visiting.erase(key);
    return false;
  }

  const std::uint64_t source_hash = hash_source_text(source);
  std::vector<syntax::AstImport> parsed_imports;
  ModuleNode node;
  node.key = key;
  node.path = key;
  node.source = source;
  const ModuleCacheRecord* cached = incremental_cache.lookup(key, source_hash);
  if (cached != nullptr) {
    parsed_imports = cached->imports;
    node.all_symbols = cached->all_symbols;
    node.exports = cached->exports;
    node.has_top_level_executable = cached->has_top_level_executable;
  } else {
    syntax::AstProgram ast;
    if (!parse_module_source(key, source, ast, diag)) {
      stack.pop_back();
      visiting.erase(key);
      return false;
    }
    parsed_imports = ast.imports;
    node.all_symbols = collect_all_symbols(ast);
    node.exports = collect_exports(ast);
    node.has_top_level_executable = !ast.top_level_statements.empty();

    ModuleCacheRecord record;
    record.source_hash = source_hash;
    record.has_top_level_executable = node.has_top_level_executable;
    record.imports = parsed_imports;
    record.all_symbols = node.all_symbols;
    record.exports = node.exports;
    incremental_cache.stage(key, std::move(record));
  }
  if (!is_entry && node.has_top_level_executable) {
    diag.error("E_MOD_203", "imported module cannot contain top-level executable statements", key, 1, 1);
    stack.pop_back();
    visiting.erase(key);
    return false;
  }
  if (cached != nullptr) {
    ModuleCacheRecord record;
    record.source_hash = source_hash;
    record.has_top_level_executable = node.has_top_level_executable;
    record.imports = parsed_imports;
    record.all_symbols = node.all_symbols;
    record.exports = node.exports;
    incremental_cache.stage(key, std::move(record));
  }
  node.is_entry = is_entry;

  for (const auto& import_decl : parsed_imports) {
    ResolvedImport resolved;
    if (!resolver.resolve_import(import_decl, key, manifest, include_paths, resolved, diag)) {
      stack.pop_back();
      visiting.erase(key);
      return false;
    }
    node.imports.push_back(ModuleImportBinding{import_decl, resolved});
  }

  modules[key] = node;
  for (const auto& binding : modules[key].imports) {
    if (!load_module_recursive(binding.resolved.absolute_path, false, resolver, manifest, include_paths, modules,
                               visiting, loaded, stack, postorder, incremental_cache, diag)) {
      stack.pop_back();
      visiting.erase(key);
      return false;
    }
  }

  stack.pop_back();
  visiting.erase(key);
  loaded.insert(key);
  postorder.push_back(key);
  return true;
}

static bool validate_import_bindings(const std::unordered_map<std::string, ModuleNode>& modules,
                                     support::DiagnosticSink& diag) {
  for (const auto& [module_key, module] : modules) {
    (void)module_key;
    std::unordered_map<std::string, std::string> prefix_owner;
    for (const auto& binding : module.imports) {
      auto target_it = modules.find(binding.resolved.module_key);
      if (target_it == modules.end()) {
        diag.error("E_MOD_204", "internal resolver error: unresolved module '" + binding.resolved.module_key + "'");
        return false;
      }
      if (binding.import_decl.is_from_import) {
        const std::unordered_set<std::string>& all_symbols = target_it->second.all_symbols;
        for (const std::string& symbol : binding.import_decl.symbols) {
          if (target_it->second.exports.find(symbol) == target_it->second.exports.end()) {
            if (all_symbols.find(symbol) != all_symbols.end()) {
              diag.error("E_MOD_208", "symbol `" + symbol + "` is private — add `pub` to export it", module.path,
                         binding.import_decl.line, binding.import_decl.column);
            } else {
              diag.error("E_MOD_205",
                         "symbol '" + symbol + "' is not exported by module '" + binding.resolved.display_name + "'",
                         module.path, binding.import_decl.line, binding.import_decl.column);
            }
            return false;
          }
        }
        continue;
      }
      const std::string prefix = binding.import_decl.alias.empty() ? binding.import_decl.module_path.back() : binding.import_decl.alias;
      auto conflict = prefix_owner.find(prefix);
      if (conflict != prefix_owner.end() && conflict->second != binding.resolved.module_key) {
        diag.error("E_MOD_206", "import prefix conflict for '" + prefix + "', use alias to disambiguate", module.path,
                   binding.import_decl.line, binding.import_decl.column);
        return false;
      }
      prefix_owner[prefix] = binding.resolved.module_key;

      const std::unordered_set<std::string>& all_symbols = target_it->second.all_symbols;
      const std::unordered_set<std::string> refs = collect_prefixed_symbol_refs(module.source, prefix);
      for (const std::string& symbol : refs) {
        if (all_symbols.find(symbol) == all_symbols.end()) {
          continue;
        }
        if (target_it->second.exports.find(symbol) == target_it->second.exports.end()) {
          diag.error("E_MOD_208", "symbol `" + symbol + "` is private — add `pub` to export it", module.path,
                     binding.import_decl.line, binding.import_decl.column);
          return false;
        }
      }
    }
  }
  return true;
}

static std::string rewrite_module_source(const ModuleNode& module, const std::unordered_map<std::string, ModuleNode>& modules) {
  std::unordered_map<std::string, std::vector<std::string>> prefix_symbols;
  for (const auto& binding : module.imports) {
    if (binding.import_decl.is_from_import) {
      continue;
    }
    auto target_it = modules.find(binding.resolved.module_key);
    if (target_it == modules.end()) {
      continue;
    }
    const std::string prefix = binding.import_decl.alias.empty() ? binding.import_decl.module_path.back() : binding.import_decl.alias;
    std::vector<std::string> symbols;
    symbols.reserve(target_it->second.exports.size());
    for (const std::string& symbol : target_it->second.exports) {
      symbols.push_back(symbol);
    }
    std::sort(symbols.begin(), symbols.end(), [](const std::string& lhs, const std::string& rhs) {
      if (lhs.size() == rhs.size()) {
        return lhs < rhs;
      }
      return lhs.size() > rhs.size();
    });
    prefix_symbols[prefix] = std::move(symbols);
  }

  std::string merged;
  std::istringstream in(module.source);
  std::string line;
  while (std::getline(in, line)) {
    const std::string clean = trim_copy(line);
    if (starts_with(clean, "import ") || starts_with(clean, "from ")) {
      continue;
    }
    std::string rewritten = line;
    for (const auto& [prefix, symbols] : prefix_symbols) {
      for (const std::string& symbol : symbols) {
        rewritten = replace_prefixed_symbol(rewritten, prefix, symbol);
      }
    }
    merged += rewritten;
    merged.push_back('\n');
  }
  return merged;
}

static bool build_merged_source(const std::string& entry_path, ModuleResolver& resolver, ProjectManifest& manifest,
                                const std::vector<std::string>& include_paths, std::string& out_source,
                                support::DiagnosticSink& diag) {
  if (!resolver.load_project_manifest(entry_path, manifest, diag)) {
    return false;
  }
  if (!resolver.write_lock_file(manifest, diag)) {
    return false;
  }

  const std::filesystem::path cache_root =
      manifest.root_path.empty() ? std::filesystem::current_path() : std::filesystem::path(manifest.root_path);
  ModuleIncrementalCache incremental_cache(cache_root);
  std::unordered_map<std::string, ModuleNode> modules;
  std::unordered_set<std::string> visiting;
  std::unordered_set<std::string> loaded;
  std::vector<std::string> stack;
  std::vector<std::string> postorder;
  if (!load_module_recursive(entry_path, true, resolver, manifest, include_paths, modules, visiting, loaded, stack,
                             postorder, incremental_cache, diag)) {
    return false;
  }
  if (!validate_import_bindings(modules, diag)) {
    return false;
  }

  out_source.clear();
  for (const std::string& key : postorder) {
    auto it = modules.find(key);
    if (it == modules.end()) {
      continue;
    }
    out_source += rewrite_module_source(it->second, modules);
    out_source.push_back('\n');
  }
  incremental_cache.persist();
  return true;
}

}  // namespace

bool CompilerPipeline::build(const BuildOptions& options, support::DiagnosticSink& diag) const {
  try {
    infra::LexerAdapter lexer;
    infra::ParserAdapter parser;
    infra::TypeCheckerAdapter checker;
    infra::LoweringAdapter lowerer;
    infra::LlvmCodegenAdapter codegen;
    infra::ClangLinkerAdapter linker;
    ModuleResolver resolver;

    application::BuildUseCase usecase(lexer, parser, checker, lowerer, codegen, linker);
    domain::BuildRequest request;
    request.input_path = options.input_path;
    ProjectManifest manifest;
    if (!build_merged_source(options.input_path, resolver, manifest, options.include_paths, request.source_text, diag)) {
      return false;
    }
    if (request.source_text.empty()) {
      diag.error("E_MOD_207", "empty compilation unit after import resolution", options.input_path);
      return false;
    }
    request.output_path = options.output_path;
    request.target_triple = options.target_triple;
    request.target_linker = options.target_linker;
    request.target_sysroot = options.target_sysroot;
    request.link_extra_args = options.extra_link_args;
    request.emit_llvm = options.emit_llvm;
    request.llvm_ir_path = options.llvm_ir_path;

    return usecase.execute(request, diag).success;
  } catch (const std::exception& ex) {
    diag.error("E3999", ex.what());
    return false;
  }
}

}  // namespace thagc::driver

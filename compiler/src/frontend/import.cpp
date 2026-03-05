#include "internal.hpp"

namespace thagc::syntax {

static bool parse_module_path(const std::string& text, std::vector<std::string>& out, std::string& error) {
  out.clear();
  std::size_t i = 0;
  while (i < text.size()) {
    std::size_t dot = text.find('.', i);
    if (dot == std::string::npos) {
      dot = text.size();
    }
    const std::string segment = trim(text.substr(i, dot - i));
    if (!is_identifier(segment)) {
      error = "invalid import module path segment '" + segment + "'";
      return false;
    }
    out.push_back(segment);
    i = dot + 1;
  }
  if (out.empty()) {
    error = "import module path is empty";
    return false;
  }
  return true;
}

static bool parse_import_symbols(const std::string& text, std::vector<std::string>& out, std::string& error) {
  out.clear();
  std::size_t i = 0;
  while (i < text.size()) {
    std::size_t comma = text.find(',', i);
    if (comma == std::string::npos) {
      comma = text.size();
    }
    const std::string symbol = trim(text.substr(i, comma - i));
    if (symbol == "*") {
      error = "wildcard import is not supported";
      return false;
    }
    if (!is_identifier(symbol)) {
      error = "invalid imported symbol '" + symbol + "'";
      return false;
    }
    out.push_back(symbol);
    i = comma + 1;
  }
  if (out.empty()) {
    error = "from-import requires at least one symbol";
    return false;
  }
  return true;
}

bool parse_import_decl(const std::string& line, AstImport& out, std::string& error) {
  const std::string clean = trim(line);
  if (starts_with(clean, "import ")) {
    std::string rest = trim(clean.substr(7));
    std::string module_text = rest;
    std::string alias;
    const std::size_t as_pos = rest.find(" as ");
    if (as_pos != std::string::npos) {
      module_text = trim(rest.substr(0, as_pos));
      alias = trim(rest.substr(as_pos + 4));
      if (!is_identifier(alias)) {
        error = "invalid import alias '" + alias + "'";
        return false;
      }
    }
    std::vector<std::string> module_path;
    if (!parse_module_path(module_text, module_path, error)) {
      return false;
    }
    out = AstImport{};
    out.is_from_import = false;
    out.module_path = std::move(module_path);
    out.alias = alias;
    out.raw = clean;
    return true;
  }
  if (starts_with(clean, "from ")) {
    const std::string rest = trim(clean.substr(5));
    const std::size_t import_pos = rest.find(" import ");
    if (import_pos == std::string::npos) {
      error = "from-import must use 'from <module> import <symbol>'";
      return false;
    }
    const std::string module_text = trim(rest.substr(0, import_pos));
    const std::string symbols_text = trim(rest.substr(import_pos + 8));
    std::vector<std::string> module_path;
    if (!parse_module_path(module_text, module_path, error)) {
      return false;
    }
    std::vector<std::string> symbols;
    if (!parse_import_symbols(symbols_text, symbols, error)) {
      return false;
    }
    out = AstImport{};
    out.is_from_import = true;
    out.module_path = std::move(module_path);
    out.symbols = std::move(symbols);
    out.raw = clean;
    return true;
  }
  error = "not an import declaration";
  return false;
}

}  // namespace thagc::syntax

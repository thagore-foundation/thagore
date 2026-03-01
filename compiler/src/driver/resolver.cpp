#include "thagc/driver/resolver.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "thagc/driver/embedded_stdlib.hpp"
#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

namespace {

static constexpr const char* kPrimaryManifestName = "drago.toml";

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

static std::vector<std::string> manifest_candidates() {
  return {kPrimaryManifestName};
}

static std::string strip_inline_comment(const std::string& line) {
  const std::size_t pos = line.find('#');
  if (pos == std::string::npos) {
    return line;
  }
  return line.substr(0, pos);
}

static bool parse_key_value(const std::string& line, std::string& key, std::string& value) {
  const std::size_t eq = line.find('=');
  if (eq == std::string::npos) {
    return false;
  }
  key = trim_copy(line.substr(0, eq));
  value = trim_copy(line.substr(eq + 1));
  if (key.empty()) {
    return false;
  }
  if (value.size() >= 2) {
    const bool quoted = (value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'');
    if (quoted) {
      value = value.substr(1, value.size() - 2);
    }
  }
  return true;
}

static std::filesystem::path canonical_or_absolute(const std::filesystem::path& path) {
  std::error_code ec;
  const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
  if (!ec) {
    return canonical;
  }
  return std::filesystem::absolute(path, ec);
}

static std::filesystem::path discover_project_root(const std::string& start_path) {
  std::error_code ec;
  std::filesystem::path current = start_path.empty() ? std::filesystem::current_path() : std::filesystem::path(start_path);
  current = canonical_or_absolute(current);
  const bool started_from_file = std::filesystem::is_regular_file(current, ec);
  ec.clear();
  if (std::filesystem::is_regular_file(current, ec)) {
    current = current.parent_path();
  }
  if (current.empty()) {
    current = std::filesystem::current_path();
  }
  const std::vector<std::string> manifests = manifest_candidates();
  for (;;) {
    for (const auto& manifest : manifests) {
      if (std::filesystem::exists(current / manifest, ec) && !ec) {
        return current;
      }
      ec.clear();
    }
    if (current == current.root_path()) {
      break;
    }
    const std::filesystem::path parent = current.parent_path();
    if (parent == current || parent.empty()) {
      break;
    }
    current = parent;
  }
  const std::filesystem::path fallback = canonical_or_absolute(start_path);
  if (started_from_file) {
    return fallback.parent_path();
  }
  return fallback;
}

static bool parse_manifest_file(const std::filesystem::path& manifest_path, const std::string& manifest_name,
                                ProjectManifest& out, support::DiagnosticSink& diag) {
  std::string data;
  try {
    data = support::read_text_file(manifest_path.string());
  } catch (const std::exception& ex) {
    diag.error("E_MOD_100", "cannot read " + manifest_name + ": " + std::string(ex.what()));
    return false;
  }

  enum class Section { None, Package, Dependencies };
  Section section = Section::None;
  std::size_t line_no = 0;
  std::size_t offset = 0;
  while (offset <= data.size()) {
    std::size_t nl = data.find('\n', offset);
    if (nl == std::string::npos) {
      nl = data.size();
    }
    ++line_no;
    std::string line = data.substr(offset, nl - offset);
    line = trim_copy(strip_inline_comment(line));
    offset = nl + 1;
    if (line.empty()) {
      if (nl >= data.size()) {
        break;
      }
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      const std::string header = trim_copy(line.substr(1, line.size() - 2));
      if (header == "package") {
        section = Section::Package;
      } else if (header == "dependencies") {
        section = Section::Dependencies;
      } else {
        section = Section::None;
      }
      if (nl >= data.size()) {
        break;
      }
      continue;
    }
    std::string key;
    std::string value;
    if (!parse_key_value(line, key, value)) {
      diag.error("E_MOD_101",
                 "invalid " + manifest_name + " line " + std::to_string(line_no) + ": expected key=value");
      return false;
    }
    if (section == Section::Package) {
      if (key == "name") {
        out.package_name = value;
      } else if (key == "version") {
        out.package_version = value;
      }
    } else if (section == Section::Dependencies) {
      out.dependencies[key] = value;
    }
    if (nl >= data.size()) {
      break;
    }
  }

  if (out.package_name.empty() || out.package_version.empty()) {
    diag.error("E_MOD_102", manifest_name + " requires [package] name and version");
    return false;
  }
  return true;
}

static std::string module_relpath(const syntax::AstImport& import_decl) {
  std::string rel;
  for (std::size_t i = 0; i < import_decl.module_path.size(); ++i) {
    if (i > 0) {
      rel += "/";
    }
    rel += import_decl.module_path[i];
  }
  rel += ".tg";
  return rel;
}

static std::filesystem::path home_directory_path() {
  const char* home = std::getenv("HOME");
  if (home != nullptr && *home != '\0') {
    return std::filesystem::path(home);
  }
  return std::filesystem::current_path();
}

static std::filesystem::path thagore_home_path() {
  const char* explicit_home = std::getenv("THAGORE_HOME");
  if (explicit_home != nullptr && *explicit_home != '\0') {
    return std::filesystem::path(explicit_home);
  }
  return home_directory_path() / ".thagore";
}

static bool ensure_writable_directory(const std::filesystem::path& dir) {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    return false;
  }
  const std::filesystem::path probe = dir / ".write-test";
  {
    std::ofstream out(probe, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      return false;
    }
    out << "ok";
    if (!out.good()) {
      return false;
    }
  }
  std::filesystem::remove(probe, ec);
  return true;
}

static bool ensure_parent_dir(const std::filesystem::path& file_path) {
  std::error_code ec;
  const std::filesystem::path parent = file_path.parent_path();
  if (parent.empty()) {
    return true;
  }
  std::filesystem::create_directories(parent, ec);
  return !ec;
}

static bool write_embedded_stdlib_file(const std::filesystem::path& target, const EmbeddedStdlibFile& source) {
  if (!ensure_parent_dir(target)) {
    return false;
  }
  std::ofstream out(target, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    return false;
  }
  out.write(reinterpret_cast<const char*>(source.data), static_cast<std::streamsize>(source.size));
  out.flush();
  return out.good();
}

static std::filesystem::path embedded_stdlib_root() {
  std::error_code ec;
  std::filesystem::path temp_root = std::filesystem::temp_directory_path(ec);
  if (ec) {
    temp_root = std::filesystem::current_path();
  }
  const std::vector<std::filesystem::path> candidates = {
      thagore_home_path() / "stdlib-embedded",
      std::filesystem::current_path() / ".thagore" / "stdlib-embedded",
      temp_root / "thagore" / "stdlib-embedded",
  };
  for (const auto& candidate : candidates) {
    if (ensure_writable_directory(candidate)) {
      return candidate;
    }
  }
  return {};
}

static std::filesystem::path materialize_embedded_stdlib_module(const std::string& rel) {
  const std::filesystem::path cache_root = embedded_stdlib_root();
  if (cache_root.empty()) {
    return {};
  }
  std::error_code ec;
  std::filesystem::create_directories(cache_root, ec);
  if (ec) {
    return {};
  }

  for (unsigned int i = 0; i < kEmbeddedStdlibFileCount; ++i) {
    const EmbeddedStdlibFile& item = kEmbeddedStdlibFiles[i];
    if (rel != item.relative_path) {
      continue;
    }
    const std::filesystem::path target = cache_root / item.relative_path;
    bool need_write = true;
    const bool exists = std::filesystem::exists(target, ec) && !ec;
    ec.clear();
    if (exists) {
      const std::uintmax_t size = std::filesystem::file_size(target, ec);
      if (!ec && size == static_cast<std::uintmax_t>(item.size)) {
        need_write = false;
      }
      ec.clear();
    }
    if (need_write && !write_embedded_stdlib_file(target, item)) {
      return {};
    }
    return canonical_or_absolute(target);
  }
  return {};
}

static std::filesystem::path resolve_dependency_source(const std::string& value, const std::string& project_root) {
  std::string path = trim_copy(value);
  if (starts_with(path, "path:")) {
    path = trim_copy(path.substr(5));
  } else if (starts_with(path, "file:")) {
    path = trim_copy(path.substr(5));
  }
  if (path.empty()) {
    return {};
  }
  std::filesystem::path candidate(path);
  if (candidate.is_relative()) {
    candidate = std::filesystem::path(project_root) / candidate;
  }
  std::error_code ec;
  if (std::filesystem::exists(candidate, ec) && !ec) {
    return canonical_or_absolute(candidate);
  }
  return {};
}

static std::filesystem::path resolve_stdlib_from_env(const std::string& rel) {
  const char* base = std::getenv("THAG_STDLIB_PATH");
  if (base == nullptr || *base == '\0') {
    return {};
  }
  const std::filesystem::path candidate = std::filesystem::path(base) / rel;
  std::error_code ec;
  if (std::filesystem::exists(candidate, ec) && !ec) {
    return canonical_or_absolute(candidate);
  }
  return {};
}

static std::filesystem::path resolve_stdlib_import(const std::filesystem::path& project_root, const std::string& rel) {
  std::error_code ec;
  const std::filesystem::path project_candidate = project_root / rel;
  if (std::filesystem::exists(project_candidate, ec) && !ec) {
    return canonical_or_absolute(project_candidate);
  }
  ec.clear();

  const std::filesystem::path embedded = materialize_embedded_stdlib_module(rel);
  if (!embedded.empty()) {
    return embedded;
  }
  return resolve_stdlib_from_env(rel);
}

static std::filesystem::path locate_package_entry(const std::filesystem::path& package_root, const std::string& package_name) {
  std::error_code ec;
  const std::vector<std::filesystem::path> direct_candidates = {
      package_root / "main.tg", package_root / "src" / "main.tg", package_root / (package_name + ".tg")};
  for (const auto& candidate : direct_candidates) {
    if (std::filesystem::exists(candidate, ec) && !ec) {
      return canonical_or_absolute(candidate);
    }
  }
  std::vector<std::filesystem::path> all_sources;
  for (std::filesystem::recursive_directory_iterator it(package_root, ec), end; !ec && it != end; ++it) {
    if (!it->is_regular_file()) {
      continue;
    }
    if (it->path().extension() == ".tg") {
      all_sources.push_back(canonical_or_absolute(it->path()));
    }
  }
  if (all_sources.empty()) {
    return {};
  }
  std::sort(all_sources.begin(), all_sources.end());
  return all_sources.front();
}

}  // namespace

bool ModuleResolver::load_project_manifest(const std::string& start_path, ProjectManifest& out,
                                           support::DiagnosticSink& diag) const {
  out = ProjectManifest{};
  const std::filesystem::path root = discover_project_root(start_path);
  out.root_path = root.string();
  std::error_code ec;

  std::filesystem::path selected_manifest;
  for (const auto& name : manifest_candidates()) {
    const std::filesystem::path candidate = root / name;
    if (std::filesystem::exists(candidate, ec) && !ec) {
      selected_manifest = candidate;
      out.manifest_name = name;
      break;
    }
    ec.clear();
  }

  if (selected_manifest.empty()) {
    out.found = false;
    out.manifest_name = kPrimaryManifestName;
    out.lock_name = "drago.lock";
    return true;
  }

  out.found = true;
  out.manifest_path = selected_manifest.string();
  out.lock_name = "drago.lock";
  return parse_manifest_file(selected_manifest, out.manifest_name, out, diag);
}

bool ModuleResolver::write_lock_file(const ProjectManifest& manifest, support::DiagnosticSink& diag) const {
  if (!manifest.found) {
    return true;
  }
  std::vector<std::string> deps;
  deps.reserve(manifest.dependencies.size());
  for (const auto& [name, _] : manifest.dependencies) {
    deps.push_back(name);
  }
  std::sort(deps.begin(), deps.end());

  std::string content;
  content += "# Auto-generated by thagc. Do not edit.\n";
  content += "[package]\n";
  content += "name = \"" + manifest.package_name + "\"\n";
  content += "version = \"" + manifest.package_version + "\"\n\n";
  content += "[dependencies]\n";
  for (const std::string& name : deps) {
    auto it = manifest.dependencies.find(name);
    if (it == manifest.dependencies.end()) {
      continue;
    }
    content += name + " = \"" + it->second + "\"\n";
  }
  try {
    const std::string lock_name = manifest.lock_name.empty() ? "drago.lock" : manifest.lock_name;
    support::write_text_file((std::filesystem::path(manifest.root_path) / lock_name).string(), content);
  } catch (const std::exception& ex) {
    const std::string lock_name = manifest.lock_name.empty() ? "drago.lock" : manifest.lock_name;
    diag.error("E_MOD_103", "cannot write " + lock_name + ": " + std::string(ex.what()));
    return false;
  }
  return true;
}

bool ModuleResolver::resolve_import(const syntax::AstImport& import_decl, const std::string& importer_path,
                                    const ProjectManifest& manifest, const std::vector<std::string>& include_paths,
                                    ResolvedImport& out,
                                    support::DiagnosticSink& diag) const {
  (void)importer_path;
  out = ResolvedImport{};
  if (import_decl.module_path.empty()) {
    diag.error("E_MOD_104", "empty import module path");
    return false;
  }

  const bool single_segment = import_decl.module_path.size() == 1;
  const std::string first = import_decl.module_path.front();
  const std::filesystem::path root = manifest.root_path.empty() ? std::filesystem::current_path()
                                                                 : std::filesystem::path(manifest.root_path);
  const std::string rel = module_relpath(import_decl);

  if (first == "std" || first == "lib") {
    const std::filesystem::path stdlib_path = resolve_stdlib_import(root, rel);
    if (!stdlib_path.empty()) {
      out.is_package = false;
      out.absolute_path = stdlib_path.string();
      out.module_key = out.absolute_path;
      out.display_name = rel;
      return true;
    }
  }

  if (single_segment) {
    auto dep = manifest.dependencies.find(first);
    if (dep != manifest.dependencies.end()) {
      const std::filesystem::path package_root = resolve_dependency_source(dep->second, manifest.root_path);
      if (package_root.empty()) {
        diag.error("E_MOD_105", "package `" + first + "` is declared but not resolved by drago");
        return false;
      }
      const std::filesystem::path entry = locate_package_entry(package_root, first);
      if (entry.empty()) {
        diag.error("E_MOD_106", "package `" + first + "` does not contain any .tg source file");
        return false;
      }
      out.is_package = true;
      out.absolute_path = entry.string();
      out.module_key = out.absolute_path;
      out.display_name = first;
      return true;
    }

    for (const std::string& include : include_paths) {
      if (include.empty()) {
        continue;
      }
      std::filesystem::path include_root(include);
      if (include_root.is_relative()) {
        include_root = root / include_root;
      }
      const std::filesystem::path pkg_root = canonical_or_absolute(include_root / first);
      const std::filesystem::path entry = locate_package_entry(pkg_root, first);
      if (!entry.empty()) {
        out.is_package = true;
        out.absolute_path = entry.string();
        out.module_key = out.absolute_path;
        out.display_name = first;
        return true;
      }
    }
  }

  const std::filesystem::path file_path = root / rel;
  std::error_code ec;
  if (std::filesystem::exists(file_path, ec) && !ec) {
    const std::filesystem::path abs = canonical_or_absolute(file_path);
    out.is_package = false;
    out.absolute_path = abs.string();
    out.module_key = out.absolute_path;
    out.display_name = rel;
    return true;
  }

  if (single_segment) {
    diag.error("E_MOD_107",
               "package `" + first + "` not found in dependencies, use drago add " + first + " or drago install");
    return false;
  }
  diag.error("E_MOD_108", "module `" + rel + "` not found");
  return false;
}

}  // namespace thagc::driver

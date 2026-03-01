#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "thagc/frontend/ast.hpp"
#include "thagc/shared/diag.hpp"

namespace thagc::driver {

struct ProjectManifest {
  bool found = false;
  std::string root_path;
  std::string manifest_path;
  std::string manifest_name;
  std::string lock_name;
  std::string package_name;
  std::string package_version;
  std::unordered_map<std::string, std::string> dependencies;
};

struct ResolvedImport {
  bool is_package = false;
  std::string module_key;
  std::string absolute_path;
  std::string display_name;
};

class ModuleResolver {
 public:
  bool load_project_manifest(const std::string& start_path, ProjectManifest& out, support::DiagnosticSink& diag) const;
  bool write_lock_file(const ProjectManifest& manifest, support::DiagnosticSink& diag) const;
  bool resolve_import(const syntax::AstImport& import_decl, const std::string& importer_path, const ProjectManifest& manifest,
                      ResolvedImport& out, support::DiagnosticSink& diag) const;
};

}  // namespace thagc::driver

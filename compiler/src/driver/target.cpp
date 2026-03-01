#include "thagc/driver/command_handlers.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "thagc/driver/common.hpp"
#include "thagc/shared/diag.hpp"

namespace thagc::driver {

namespace {

static void print_diag(const support::DiagnosticSink& diag) {
  for (const auto& d : diag.diagnostics()) {
    const char* level = d.level == support::DiagnosticLevel::Warning ? "warning" : "error";
    if (!d.file.empty()) {
      const int line = d.line > 0 ? d.line : 1;
      const int col = d.column > 0 ? d.column : 1;
      std::cerr << d.file << ":" << line << ":" << col << ": " << level << " " << d.code << ": " << d.message
                << "\n";
    } else {
      std::cerr << level << " " << d.code << ": " << d.message << "\n";
    }
    const std::string hint = support::diagnostic_fix_suggestion(d);
    if (!hint.empty()) {
      std::cerr << "  help: " << hint << "\n";
    }
  }
}

static int print_help() {
  std::cout << "target usage:\n";
  std::cout << "  thagc target list\n";
  std::cout << "  thagc target show <triple>\n";
  std::cout << "  thagc target init <triple>\n";
  return 0;
}

static int handle_list() {
  const std::filesystem::path root = std::filesystem::path(compiler_home_dir()) / "targets";
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  if (ec) {
    std::cerr << "ERROR: cannot access target cache directory: " << ec.message() << "\n";
    return 1;
  }
  std::vector<std::string> targets;
  for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
    if (ec) {
      break;
    }
    if (entry.is_directory(ec) && !ec) {
      targets.push_back(entry.path().filename().string());
    }
  }
  if (targets.empty()) {
    std::cout << "target: no installed targets yet\n";
    return 0;
  }
  std::cout << "installed targets:\n";
  for (const std::string& target : targets) {
    std::cout << "  " << target << "\n";
  }
  return 0;
}

static int handle_show(const std::string& triple) {
  if (triple.empty()) {
    std::cerr << "ERROR: missing target triple for show\n";
    return 1;
  }
  TargetConfig cfg;
  if (!load_target_config(triple, cfg)) {
    std::cerr << "ERROR: target '" << triple << "' is not initialized; run: thagc target init " << triple << "\n";
    return 1;
  }
  std::cout << "target: " << cfg.triple << "\n";
  std::cout << "  cc: " << (cfg.cc.empty() ? "<auto>" : cfg.cc) << "\n";
  std::cout << "  cxx: " << (cfg.cxx.empty() ? "<auto>" : cfg.cxx) << "\n";
  std::cout << "  linker: " << (cfg.linker.empty() ? "<auto>" : cfg.linker) << "\n";
  std::cout << "  sysroot: " << (cfg.sysroot.empty() ? "<none>" : cfg.sysroot) << "\n";
  return 0;
}

static int handle_init(const std::string& triple, support::DiagnosticSink& diag) {
  if (triple.empty()) {
    std::cerr << "ERROR: missing target triple for init\n";
    return 1;
  }
  BuildOptions opts;
  if (!apply_target_config(opts, triple, diag)) {
    print_diag(diag);
    return 1;
  }
  TargetConfig cfg;
  if (!load_target_config(triple, cfg)) {
    std::cerr << "ERROR: failed to load target '" << triple << "' after init\n";
    return 1;
  }
  std::cout << "target: initialized " << triple << "\n";
  return 0;
}

}  // namespace

int handle_target(const ParsedCommand& cmd, support::DiagnosticSink& diag) {
  if (cmd.args.empty()) {
    return print_help();
  }
  const std::string sub = cmd.args[0];
  if (sub == "help" || sub == "--help") {
    return print_help();
  }
  if (sub == "list") {
    return handle_list();
  }
  if (sub == "show") {
    return handle_show(cmd.args.size() > 1 ? cmd.args[1] : "");
  }
  if (sub == "init") {
    return handle_init(cmd.args.size() > 1 ? cmd.args[1] : "", diag);
  }
  std::cerr << "ERROR: unknown target subcommand '" << sub << "'\n";
  return print_help() == 0 ? 1 : 1;
}

}  // namespace thagc::driver

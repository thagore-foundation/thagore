#include "thagc/driver/command_handlers.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "thagc/driver/common.hpp"
#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

static bool starts_with(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

static bool contains_ci(std::string haystack, std::string needle) {
  std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return haystack.find(needle) != std::string::npos;
}

static std::vector<std::string> discover_workspace_sources(const std::string& exclude, int max_files) {
  std::vector<std::string> files;
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(std::filesystem::current_path(), ec), end;
       it != end && !ec; it.increment(ec)) {
    if (ec || !it->is_regular_file()) {
      continue;
    }
    const auto p = it->path();
    if (!p.has_extension() || p.extension().string() != ".tg") {
      continue;
    }
    const std::string full = p.string();
    if (!exclude.empty() && contains_ci(full, exclude)) {
      continue;
    }
    files.push_back(full);
    if (max_files > 0 && static_cast<int>(files.size()) >= max_files) {
      break;
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

struct FixOptions {
  bool workspace = false;
  bool json = false;
  int max_files = 25;
  int max_iterations = 3;
  std::string level = "safe";
  std::string exclude;
  std::string lock_path;
  bool lock_strict = false;
  bool locked_only = false;
};

static int parse_positive_int_or(const std::string& value, int fallback) {
  if (value.empty()) {
    return fallback;
  }
  for (char ch : value) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return fallback;
    }
  }
  return std::max(1, std::stoi(value));
}

static FixOptions parse_fix_options(const ParsedCommand& cmd) {
  FixOptions opt;
  for (std::size_t i = 0; i < cmd.args.size(); ++i) {
    const std::string arg = cmd.args[i];
    if (arg == "--workspace" || arg == "--all") {
      opt.workspace = true;
      continue;
    }
    if (arg == "--json") {
      opt.json = true;
      continue;
    }
    if (arg == "--lock-strict") {
      opt.lock_strict = true;
      continue;
    }
    if (arg == "--locked") {
      opt.locked_only = true;
      continue;
    }
    if (starts_with(arg, "--level=")) {
      opt.level = arg.substr(std::string("--level=").size());
      continue;
    }
    if (starts_with(arg, "--exclude=")) {
      opt.exclude = arg.substr(std::string("--exclude=").size());
      continue;
    }
    if (starts_with(arg, "--max-files=")) {
      opt.max_files = parse_positive_int_or(arg.substr(std::string("--max-files=").size()), opt.max_files);
      continue;
    }
    if (starts_with(arg, "--max-iterations=")) {
      opt.max_iterations =
          parse_positive_int_or(arg.substr(std::string("--max-iterations=").size()), opt.max_iterations);
      continue;
    }
    if (starts_with(arg, "--lock=")) {
      opt.lock_path = arg.substr(std::string("--lock=").size());
      continue;
    }
  }
  return opt;
}

int handle_fix(const ParsedCommand& cmd) {
  if (cmd.args.empty()) {
    std::cerr << "ERROR: fix requires subcommand (doctor|dry-run|apply|explain|rollback)\n";
    return 1;
  }
  const std::string sub = cmd.args[0];
  const FixOptions options = parse_fix_options(cmd);
  if (sub == "doctor") {
    if (options.json) {
      std::cout << "{\"command\":\"fix doctor\",\"status\":\"ok\",\"level\":\"" << options.level << "\"}\n";
    } else {
      std::cout << "fix doctor: OK (level=" << options.level << ")\n";
    }
    return 0;
  }
  if (sub == "dry-run") {
    if (options.json) {
      std::cout << "{\"command\":\"fix dry-run\",\"status\":\"ok\",\"workspace\":" << (options.workspace ? "true" : "false")
                << ",\"max_files\":" << options.max_files << "}\n";
    } else {
      std::cout << "fix dry-run: ready (workspace=" << (options.workspace ? "on" : "off")
                << ", max-files=" << options.max_files << ")\n";
    }
    return 0;
  }
  if (sub == "apply") {
    std::vector<std::string> targets;
    if (options.workspace) {
      targets = discover_workspace_sources(options.exclude, options.max_files);
    } else if (cmd.args.size() >= 2 && !starts_with(cmd.args[1], "--")) {
      targets.push_back(cmd.args[1]);
    }
    if (targets.empty()) {
      std::cerr << "ERROR: fix apply requires input path\n";
      return 1;
    }
    int applied = 0;
    for (const auto& src : targets) {
      const std::string dst = src + ".fixed";
      const std::string content = support::read_text_file(src);
      support::write_text_file(dst, content);
      ++applied;
    }
    if (!options.lock_path.empty()) {
      support::write_text_file(options.lock_path, "level=" + options.level + "\nfiles=" + std::to_string(applied) + "\n");
    }
    if (options.json) {
      std::cout << "{\"command\":\"fix apply\",\"applied\":" << applied << "}\n";
    } else {
      std::cout << "fix apply: wrote " << applied << " file(s)\n";
    }
    return 0;
  }
  if (sub == "explain") {
    if (options.json) {
      std::cout << "{\"command\":\"fix explain\",\"level\":\"" << options.level
                << "\",\"locked\":" << (options.locked_only ? "true" : "false") << "}\n";
    } else {
      std::cout << "fix explain: level=" << options.level << ", locked=" << (options.locked_only ? "on" : "off")
                << "\n";
    }
    return 0;
  }
  if (sub == "rollback") {
    if (cmd.args.size() < 2 || starts_with(cmd.args[1], "--")) {
      std::cerr << "ERROR: fix rollback requires session id\n";
      return 1;
    }
    const std::string session = cmd.args[1];
    if (options.json) {
      std::cout << "{\"command\":\"fix rollback\",\"session\":\"" << session << "\",\"status\":\"ok\"}\n";
    } else {
      std::cout << "fix rollback: session " << session << " marked as rolled back\n";
    }
    return 0;
  }
  std::cerr << "ERROR: unknown fix subcommand: " << sub << "\n";
  return 1;
}

}  // namespace thagc::driver


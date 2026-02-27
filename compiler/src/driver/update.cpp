#include "thagc/driver/command_handlers.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

#include "thagc/driver/common.hpp"
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

static std::string read_or_default(const std::string& path, const std::string& fallback) {
  if (!support::file_exists(path)) {
    return fallback;
  }
  return trim(support::read_text_file(path));
}

static std::vector<int> parse_semver_triplet(const std::string& version) {
  std::vector<int> out;
  std::string cur;
  for (char ch : version) {
    if (ch == '.') {
      if (cur.empty()) return {};
      out.push_back(std::stoi(cur));
      cur.clear();
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return {};
    }
    cur.push_back(ch);
  }
  if (!cur.empty()) {
    out.push_back(std::stoi(cur));
  }
  if (out.size() != 3) {
    return {};
  }
  return out;
}

static int compare_versions(const std::string& lhs, const std::string& rhs) {
  const auto a = parse_semver_triplet(lhs);
  const auto b = parse_semver_triplet(rhs);
  if (a.empty() || b.empty()) {
    return lhs.compare(rhs);
  }
  for (std::size_t i = 0; i < 3; ++i) {
    if (a[i] < b[i]) return -1;
    if (a[i] > b[i]) return 1;
  }
  return 0;
}

int handle_update(const ParsedCommand& cmd) {
  if (cmd.args.empty()) {
    std::cerr << "ERROR: update requires subcommand (check|apply|rollback)\n";
    return 1;
  }
  const std::string sub = cmd.args[0];
  if (sub != "check" && sub != "apply" && sub != "rollback") {
    std::cerr << "ERROR: unknown update subcommand: " << sub << "\n";
    return 1;
  }
  const std::string home = compiler_home_dir();
  const std::string current_file = home + "/current-version.txt";
  const std::string latest_file = home + "/latest-version.txt";
  const std::string rollback_file = home + "/rollback-version.txt";
  std::string current = read_or_default(current_file, "0.1.0");
  std::string latest = read_or_default(latest_file, current);

  if (sub == "check") {
    const int cmp = compare_versions(current, latest);
    if (cmp < 0) {
      std::cout << "update check: new version available " << latest << " (current " << current << ")\n";
    } else if (cmp == 0) {
      std::cout << "update check: up to date (" << current << ")\n";
    } else {
      std::cout << "update check: current version (" << current << ") is newer than channel (" << latest << ")\n";
    }
    return 0;
  }
  if (sub == "apply") {
    if (cmd.args.size() >= 2) {
      latest = cmd.args[1];
      support::write_text_file(latest_file, latest + "\n");
    }
    if (compare_versions(current, latest) >= 0) {
      std::cout << "update apply: nothing to apply (current " << current << ", latest " << latest << ")\n";
      return 0;
    }
    support::write_text_file(rollback_file, current + "\n");
    support::write_text_file(current_file, latest + "\n");
    support::write_text_file(home + "/update-state.txt", "applied:" + latest + "\n");
    std::cout << "update apply: updated to " << latest << "\n";
    return 0;
  }
  if (!support::file_exists(rollback_file)) {
    std::cout << "update rollback: no rollback version recorded\n";
    return 0;
  }
  const std::string rollback = read_or_default(rollback_file, current);
  support::write_text_file(current_file, rollback + "\n");
  support::write_text_file(home + "/update-state.txt", "rollback:" + rollback + "\n");
  std::cout << "update rollback: restored " << rollback << "\n";
  return 0;
}

}  // namespace thagc::driver


#include "thagc/driver/command_handlers.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include "thagc/driver/common.hpp"
#include "thagc/shared/filesystem.hpp"
#include "thagc/shared/process.hpp"
#include "thagc/shared/version.hpp"

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
  std::string clean = version;
  if (!clean.empty() && clean[0] == 'v') {
    clean = clean.substr(1);
  }
  std::string cur;
  for (char ch : clean) {
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

static std::string parse_latest_tag(const std::string& payload) {
  std::smatch m;
  std::regex re("\"tag_name\"\\s*:\\s*\"([^\"]+)\"");
  if (std::regex_search(payload, m, re) && m.size() >= 2) {
    return m[1].str();
  }
  return "";
}

static std::string fetch_latest_tag() {
  int rc = 0;
  const std::string payload = support::run_process_capture(
      {"curl", "-fsSL", "https://api.github.com/repos/thagore-foundation/thagore/releases/latest"}, &rc);
  if (rc != 0 || payload.empty()) {
    return "";
  }
  return parse_latest_tag(payload);
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
  const std::string channel_file = home + "/update-channel.txt";
  std::string current = read_or_default(current_file, std::string(support::kCompilerVersion));
  std::string latest = read_or_default(latest_file, current);

  bool dry_run = false;
  bool yes = false;
  std::string requested_version;
  for (std::size_t i = 1; i < cmd.args.size(); ++i) {
    if (cmd.args[i] == "--dry-run") {
      dry_run = true;
      continue;
    }
    if (cmd.args[i] == "--yes") {
      yes = true;
      continue;
    }
    if (!cmd.args[i].empty() && cmd.args[i][0] != '-') {
      requested_version = cmd.args[i];
    }
  }

  if (requested_version.empty()) {
    const std::string fetched = fetch_latest_tag();
    if (!fetched.empty()) {
      latest = fetched;
      support::write_text_file(latest_file, latest + "\n");
    }
  } else {
    latest = requested_version;
    support::write_text_file(latest_file, latest + "\n");
  }

  if (sub == "check") {
    const std::string channel = read_or_default(channel_file, "stable");
    const int cmp = compare_versions(current, latest);
    if (cmp < 0) {
      std::cout << "update check: new version available " << latest << " (current " << current << ", channel "
                << channel << ")\n";
    } else if (cmp == 0) {
      std::cout << "update check: up to date (" << current << ")\n";
    } else {
      std::cout << "update check: current version (" << current << ") is newer than channel (" << latest << ")\n";
    }
    return 0;
  }
  if (sub == "apply") {
    if (dry_run) {
      if (compare_versions(current, latest) >= 0) {
        std::cout << "[dry-run] update apply: nothing to apply (current " << current << ", latest " << latest << ")\n";
      } else {
        std::cout << "[dry-run] would update " << current << " -> " << latest << "\n";
        std::cout << "[dry-run] would write rollback marker: " << rollback_file << "\n";
      }
      return 0;
    }
    if (compare_versions(current, latest) >= 0) {
      std::cout << "update apply: nothing to apply (current " << current << ", latest " << latest << ")\n";
      return 0;
    }
    if (!yes) {
      std::cout << "Apply update " << current << " -> " << latest << "? [Y/n]: ";
      std::string answer;
      std::getline(std::cin, answer);
      if (!answer.empty() && answer != "y" && answer != "Y" && answer != "yes" && answer != "YES") {
        std::cout << "update apply: cancelled\n";
        return 0;
      }
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
  if (!yes && !dry_run) {
    std::cout << "Rollback to previous version? [Y/n]: ";
    std::string answer;
    std::getline(std::cin, answer);
    if (!answer.empty() && answer != "y" && answer != "Y" && answer != "yes" && answer != "YES") {
      std::cout << "update rollback: cancelled\n";
      return 0;
    }
  }
  const std::string rollback = read_or_default(rollback_file, current);
  if (dry_run) {
    std::cout << "[dry-run] would rollback " << current << " -> " << rollback << "\n";
    return 0;
  }
  support::write_text_file(current_file, rollback + "\n");
  support::write_text_file(home + "/update-state.txt", "rollback:" + rollback + "\n");
  std::cout << "update rollback: restored " << rollback << "\n";
  return 0;
}

}  // namespace thagc::driver

#include "thagc/driver/command_handlers.hpp"

#include <cctype>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

namespace {

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

static std::string normalize_indent_and_rstrip(const std::string& line) {
  std::size_t idx = 0;
  int spaces = 0;
  while (idx < line.size()) {
    if (line[idx] == ' ') {
      ++spaces;
      ++idx;
      continue;
    }
    if (line[idx] == '\t') {
      spaces += 2;
      ++idx;
      continue;
    }
    break;
  }
  std::string out = std::string(static_cast<std::size_t>(spaces), ' ') + line.substr(idx);
  while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '\r')) {
    out.pop_back();
  }
  return out;
}

static bool starts_with(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

static bool needs_block_colon(const std::string& trimmed_line) {
  if (trimmed_line.empty()) {
    return false;
  }
  if (trimmed_line.back() == ':') {
    return false;
  }
  if (trimmed_line == "else") {
    return true;
  }
  static const std::vector<std::string> kPrefixes = {
      "func ", "async func ", "if ", "while ", "for ", "match ", "struct ", "enum ", "trait ", "impl ", "flow ",
      "step "};
  for (const std::string& prefix : kPrefixes) {
    if (starts_with(trimmed_line, prefix)) {
      return true;
    }
  }
  return false;
}

static std::string apply_autofix(const std::string& source, int& line_changes) {
  std::istringstream in(source);
  std::string out;
  std::string line;
  bool first = true;
  line_changes = 0;
  while (std::getline(in, line)) {
    std::string fixed = normalize_indent_and_rstrip(line);
    const std::string trimmed = trim_copy(fixed);
    if (needs_block_colon(trimmed)) {
      fixed.push_back(':');
    }
    if (!first) {
      out.push_back('\n');
    }
    first = false;
    out += fixed;
    if (fixed != line) {
      ++line_changes;
    }
  }
  if (!out.empty() && out.back() != '\n') {
    out.push_back('\n');
  }
  return out;
}

}  // namespace

int handle_fix(const ParsedCommand& cmd) {
  if (cmd.input_path.empty()) {
    std::cerr << "ERROR: missing input path for fix\n";
    return 1;
  }
  std::string source;
  try {
    source = support::read_text_file(cmd.input_path);
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: cannot read input for fix: " << ex.what() << "\n";
    return 1;
  }

  int line_changes = 0;
  const std::string fixed = apply_autofix(source, line_changes);
  if (fixed == source) {
    std::cout << "fix: no change (" << cmd.input_path << ")\n";
    return 0;
  }

  try {
    support::write_text_file(cmd.input_path, fixed);
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: cannot write fixed file: " << ex.what() << "\n";
    return 1;
  }
  std::cout << "fix: wrote " << cmd.input_path << " (" << line_changes << " line(s) changed)\n";
  return 0;
}

}  // namespace thagc::driver

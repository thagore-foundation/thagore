#include "thagc/driver/command_handlers.hpp"

#include <cctype>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>

#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

namespace {

static std::string rstrip_copy(const std::string& line) {
  std::size_t right = line.size();
  while (right > 0 && (line[right - 1] == ' ' || line[right - 1] == '\t' || line[right - 1] == '\r')) {
    --right;
  }
  return line.substr(0, right);
}

static std::string normalize_indent(const std::string& line) {
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
  return std::string(static_cast<std::size_t>(spaces), ' ') + line.substr(idx);
}

static std::string format_source(const std::string& source) {
  std::istringstream in(source);
  std::string out;
  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    if (!first) {
      out.push_back('\n');
    }
    first = false;
    out += rstrip_copy(normalize_indent(line));
  }
  if (!out.empty() && out.back() != '\n') {
    out.push_back('\n');
  }
  return out;
}

}  // namespace

int handle_fmt(const ParsedCommand& cmd) {
  if (cmd.input_path.empty()) {
    std::cerr << "ERROR: missing input path for fmt\n";
    return 1;
  }
  std::string source;
  try {
    source = support::read_text_file(cmd.input_path);
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: cannot read input for fmt: " << ex.what() << "\n";
    return 1;
  }

  const std::string formatted = format_source(source);
  try {
    support::write_text_file(cmd.input_path, formatted);
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: cannot write formatted file: " << ex.what() << "\n";
    return 1;
  }
  std::cout << "fmt: wrote " << cmd.input_path << "\n";
  return 0;
}

}  // namespace thagc::driver

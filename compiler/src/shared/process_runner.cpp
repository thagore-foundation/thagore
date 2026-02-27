#include "thagc/shared/process.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace thagc::support {

static std::string quote_if_needed(const std::string& arg) {
  if (arg.find(' ') == std::string::npos && arg.find('"') == std::string::npos) {
    return arg;
  }
  std::string escaped;
  escaped.reserve(arg.size() + 2);
  escaped.push_back('"');
  for (char ch : arg) {
    if (ch == '"') {
      escaped += "\\\"";
    } else {
      escaped.push_back(ch);
    }
  }
  escaped.push_back('"');
  return escaped;
}

int run_process(const std::vector<std::string>& args) {
  std::stringstream cmd;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (i > 0) {
      cmd << ' ';
    }
    cmd << quote_if_needed(args[i]);
  }
  return std::system(cmd.str().c_str());
}

std::string run_process_capture(const std::vector<std::string>& args, int* exit_code) {
  std::stringstream cmd;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (i > 0) {
      cmd << ' ';
    }
    cmd << quote_if_needed(args[i]);
  }
  std::string out;
  std::array<char, 1024> buffer{};
#if defined(_WIN32)
  FILE* pipe = _popen(cmd.str().c_str(), "r");
#else
  FILE* pipe = popen(cmd.str().c_str(), "r");
#endif
  if (pipe == nullptr) {
    if (exit_code != nullptr) {
      *exit_code = -1;
    }
    return out;
  }
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    out += buffer.data();
  }
#if defined(_WIN32)
  const int rc = _pclose(pipe);
#else
  const int rc = pclose(pipe);
#endif
  if (exit_code != nullptr) {
    *exit_code = rc;
  }
  return out;
}

}  // namespace thagc::support

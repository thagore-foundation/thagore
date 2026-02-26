#include "thagc/shared/process.hpp"

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

}  // namespace thagc::support


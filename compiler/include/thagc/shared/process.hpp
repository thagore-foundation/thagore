#pragma once

#include <string>
#include <vector>

namespace thagc::support {

int run_process(const std::vector<std::string>& args);
std::string run_process_capture(const std::vector<std::string>& args, int* exit_code = nullptr);

}  // namespace thagc::support

#pragma once

#include "thagore/common/result.hpp"

#include <string>
#include <vector>

namespace thagore {

struct DriverOptions {
  std::string inputFile {};
  std::string outputFile {};
  bool emitIR {false};
  bool emitObject {false};
  int optLevel {2};
};

class Driver {
public:
  auto run(const std::vector<std::string> &args) -> int;
};

} // namespace thagore

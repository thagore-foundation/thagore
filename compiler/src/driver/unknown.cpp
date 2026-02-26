#include "thagc/driver/command_handlers.hpp"

#include <iostream>

namespace thagc::driver {

int handle_unknown() {
  std::cerr << "ERROR: unknown command\n";
  return 2;
}

}  // namespace thagc::driver


#include "thagc/driver/command_handlers.hpp"

#include <iostream>

namespace thagc::driver {

int handle_unknown() {
  std::cerr << "ERROR: unknown command\n";
  std::cerr << "Use: thagc build|run|check|fmt|migrate ... (package/update flows are handled by drago)\n";
  return 2;
}

}  // namespace thagc::driver

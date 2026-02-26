#include "thagc/runtime/abi.hpp"

namespace thagc::runtime {

std::string runtime_library_name() {
#if defined(_WIN32)
  return "thag_runtime.lib";
#else
  return "libthag_runtime.a";
#endif
}

}  // namespace thagc::runtime


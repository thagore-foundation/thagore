#pragma once

namespace thagc::cli {

class CommandRouter {
 public:
  int run(int argc, char** argv) const;
};

}  // namespace thagc::cli


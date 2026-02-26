#include "thagc/driver/command_router.hpp"

int main(int argc, char** argv) {
  return thagc::cli::CommandRouter().run(argc, argv);
}


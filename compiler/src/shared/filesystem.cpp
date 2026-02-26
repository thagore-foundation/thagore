#include "thagc/shared/filesystem.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace thagc::support {

std::string read_text_file(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot open file: " + path);
  }
  std::stringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void write_text_file(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    throw std::runtime_error("cannot write file: " + path);
  }
  out << content;
}

bool file_exists(const std::string& path) {
  return std::filesystem::exists(path);
}

std::string absolute_path(const std::string& path) {
  return std::filesystem::absolute(path).string();
}

}  // namespace thagc::support

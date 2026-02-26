#pragma once

#include <string>

namespace thagc::support {

std::string read_text_file(const std::string& path);
void write_text_file(const std::string& path, const std::string& content);
bool file_exists(const std::string& path);
std::string absolute_path(const std::string& path);

}  // namespace thagc::support


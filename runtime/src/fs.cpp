#include "thag/fs.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <string>

#include "thag/string.h"

namespace {

static char* dup_cstr(const std::string& text) {
  char* out = static_cast<char*>(std::malloc(text.size() + 1));
  if (out == nullptr) {
    return nullptr;
  }
  if (!text.empty()) {
    std::memcpy(out, text.data(), text.size());
  }
  out[text.size()] = '\0';
  return out;
}

}  // namespace

extern "C" {

const char* thag_fs_read(const char* path) {
  if (path == nullptr) {
    return nullptr;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return nullptr;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return dup_cstr(buffer.str());
}

int thag_fs_write(const char* path, const char* content) {
  if (path == nullptr || content == nullptr) {
    return 0;
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return 0;
  }
  out << content;
  return out.good() ? 1 : 0;
}

int thag_fs_exists(const char* path) {
  if (path == nullptr) {
    return 0;
  }
  std::error_code ec;
  return std::filesystem::exists(path, ec) && !ec ? 1 : 0;
}

int thag_fs_mkdir(const char* path) {
  if (path == nullptr) {
    return 0;
  }
  std::error_code ec;
  if (std::filesystem::create_directories(path, ec)) {
    return 1;
  }
  if (!ec && std::filesystem::exists(path, ec)) {
    return 1;
  }
  return 0;
}

void* thag_fs_readdir(const char* path) {
  if (path == nullptr) {
    return nullptr;
  }
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec || !std::filesystem::is_directory(path, ec)) {
    return nullptr;
  }
  void* out = thag_str_array_new();
  for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
    if (ec) {
      break;
    }
    const std::string name = entry.path().filename().string();
    if (name.empty()) {
      continue;
    }
    (void)thag_str_array_push(out, name.c_str());
  }
  return out;
}

int thag_fs_remove(const char* path) {
  if (path == nullptr) {
    return 0;
  }
  std::error_code ec;
  const std::uintmax_t removed = std::filesystem::remove_all(path, ec);
  if (ec) {
    return 0;
  }
  return removed > 0 ? 1 : 0;
}

const char* thag_fs_getcwd(void) {
  std::error_code ec;
  const std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (ec) {
    return nullptr;
  }
  return dup_cstr(cwd.string());
}

const char* thag_fs_path_join(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) {
    return nullptr;
  }
  const std::filesystem::path joined = std::filesystem::path(a) / std::filesystem::path(b);
  return dup_cstr(joined.string());
}

int thag_fs_is_dir(const char* path) {
  if (path == nullptr) {
    return 0;
  }
  std::error_code ec;
  return std::filesystem::is_directory(path, ec) && !ec ? 1 : 0;
}

int64_t thag_fs_filesize(const char* path) {
  if (path == nullptr) {
    return -1;
  }
  std::error_code ec;
  const std::uintmax_t size = std::filesystem::file_size(path, ec);
  if (ec) {
    return -1;
  }
  return static_cast<int64_t>(size);
}

}  // extern "C"

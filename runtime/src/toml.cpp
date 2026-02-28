#include "thag/toml.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "thag/string.h"

namespace {

struct TomlHandle {
  std::shared_ptr<std::unordered_map<std::string, std::string>> values;
  std::string prefix;
};

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

static std::string trim_copy(const std::string& text) {
  std::size_t left = 0;
  while (left < text.size() && std::isspace(static_cast<unsigned char>(text[left]))) {
    ++left;
  }
  std::size_t right = text.size();
  while (right > left && std::isspace(static_cast<unsigned char>(text[right - 1]))) {
    --right;
  }
  return text.substr(left, right - left);
}

static std::string strip_comment(const std::string& line) {
  bool in_quote = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (ch == '"' && (i == 0 || line[i - 1] != '\\')) {
      in_quote = !in_quote;
      continue;
    }
    if (!in_quote && ch == '#') {
      return line.substr(0, i);
    }
  }
  return line;
}

static std::string normalize_value(std::string value) {
  value = trim_copy(value);
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    return value.substr(1, value.size() - 2);
  }
  if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

static std::string resolve_lookup_key(const TomlHandle* handle, const std::string& key) {
  if (handle == nullptr) {
    return key;
  }
  if (handle->prefix.empty()) {
    return key;
  }
  if (key.find('.') != std::string::npos) {
    return key;
  }
  return handle->prefix + "." + key;
}

static const std::string* find_value(const TomlHandle* handle, const std::string& key) {
  if (handle == nullptr || handle->values == nullptr) {
    return nullptr;
  }
  const std::string full_key = resolve_lookup_key(handle, key);
  const auto it = handle->values->find(full_key);
  if (it == handle->values->end()) {
    return nullptr;
  }
  return &it->second;
}

}  // namespace

extern "C" {

void* thag_toml_parse(const char* content) {
  if (content == nullptr) {
    return nullptr;
  }

  auto* handle = new TomlHandle();
  handle->values = std::make_shared<std::unordered_map<std::string, std::string>>();
  handle->prefix.clear();

  std::string section;
  std::string text(content);
  std::size_t from = 0;
  while (from <= text.size()) {
    std::size_t end = text.find('\n', from);
    if (end == std::string::npos) {
      end = text.size();
    }
    std::string line = trim_copy(strip_comment(text.substr(from, end - from)));
    from = end + 1;
    if (line.empty()) {
      if (end >= text.size()) {
        break;
      }
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      section = trim_copy(line.substr(1, line.size() - 2));
      if (end >= text.size()) {
        break;
      }
      continue;
    }
    const std::size_t eq = line.find('=');
    if (eq == std::string::npos || eq == 0) {
      if (end >= text.size()) {
        break;
      }
      continue;
    }
    const std::string key = trim_copy(line.substr(0, eq));
    if (key.empty()) {
      if (end >= text.size()) {
        break;
      }
      continue;
    }
    const std::string value = normalize_value(line.substr(eq + 1));
    const std::string full_key = section.empty() ? key : section + "." + key;
    (*handle->values)[full_key] = value;
    if (end >= text.size()) {
      break;
    }
  }
  return handle;
}

const char* thag_toml_get_str(const void* raw_handle, const char* key) {
  if (raw_handle == nullptr || key == nullptr) {
    return nullptr;
  }
  const auto* handle = static_cast<const TomlHandle*>(raw_handle);
  const std::string* value = find_value(handle, key);
  if (value == nullptr) {
    return nullptr;
  }
  return dup_cstr(*value);
}

int64_t thag_toml_get_int(const void* raw_handle, const char* key) {
  if (raw_handle == nullptr || key == nullptr) {
    return 0;
  }
  const auto* handle = static_cast<const TomlHandle*>(raw_handle);
  const std::string* value = find_value(handle, key);
  if (value == nullptr) {
    return 0;
  }
  char* end = nullptr;
  const long long parsed = std::strtoll(value->c_str(), &end, 10);
  if (end == value->c_str()) {
    return 0;
  }
  return static_cast<int64_t>(parsed);
}

void* thag_toml_get_section(const void* raw_handle, const char* section) {
  if (raw_handle == nullptr || section == nullptr || *section == '\0') {
    return nullptr;
  }
  const auto* parent = static_cast<const TomlHandle*>(raw_handle);
  if (parent->values == nullptr) {
    return nullptr;
  }

  auto* child = new TomlHandle();
  child->values = parent->values;
  const std::string sec(section);
  if (parent->prefix.empty()) {
    child->prefix = sec;
  } else {
    child->prefix = parent->prefix + "." + sec;
  }
  return child;
}

void* thag_toml_get_keys(const void* raw_handle) {
  if (raw_handle == nullptr) {
    return nullptr;
  }
  const auto* handle = static_cast<const TomlHandle*>(raw_handle);
  if (handle->values == nullptr) {
    return nullptr;
  }

  std::set<std::string> keys;
  const std::string prefix = handle->prefix.empty() ? std::string() : handle->prefix + ".";
  for (const auto& [full_key, _] : *handle->values) {
    if (!prefix.empty()) {
      if (full_key.rfind(prefix, 0) != 0) {
        continue;
      }
    }
    std::string local = prefix.empty() ? full_key : full_key.substr(prefix.size());
    if (local.empty()) {
      continue;
    }
    const std::size_t dot = local.find('.');
    if (dot != std::string::npos) {
      local = local.substr(0, dot);
    }
    keys.insert(local);
  }

  void* out = thag_str_array_new();
  for (const auto& key : keys) {
    (void)thag_str_array_push(out, key.c_str());
  }
  return out;
}

void thag_toml_free(void* handle) {
  delete static_cast<TomlHandle*>(handle);
}

}  // extern "C"

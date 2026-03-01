#include "thag/json.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

namespace {

struct JsonEntry {
  std::string value;
  bool quoted = false;
};

struct JsonHandle {
  std::unordered_map<std::string, JsonEntry> values;
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

static void skip_ws(const std::string& text, std::size_t& i) {
  while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
    ++i;
  }
}

static bool parse_json_string(const std::string& text, std::size_t& i, std::string& out) {
  out.clear();
  if (i >= text.size() || text[i] != '"') {
    return false;
  }
  ++i;
  while (i < text.size()) {
    const char ch = text[i++];
    if (ch == '"') {
      return true;
    }
    if (ch != '\\') {
      out.push_back(ch);
      continue;
    }
    if (i >= text.size()) {
      return false;
    }
    const char esc = text[i++];
    if (esc == '"' || esc == '\\' || esc == '/') {
      out.push_back(esc);
    } else if (esc == 'n') {
      out.push_back('\n');
    } else if (esc == 'r') {
      out.push_back('\r');
    } else if (esc == 't') {
      out.push_back('\t');
    } else {
      return false;
    }
  }
  return false;
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

static bool parse_json_value(const std::string& text, std::size_t& i, JsonEntry& out) {
  out = JsonEntry{};
  skip_ws(text, i);
  if (i >= text.size()) {
    return false;
  }
  if (text[i] == '"') {
    out.quoted = true;
    return parse_json_string(text, i, out.value);
  }

  const std::size_t start = i;
  while (i < text.size() && text[i] != ',' && text[i] != '}') {
    ++i;
  }
  const std::string token = trim_copy(text.substr(start, i - start));
  if (token.empty()) {
    return false;
  }
  out.quoted = false;
  out.value = token;
  return true;
}

static std::string escape_json_string(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 4);
  for (char ch : text) {
    if (ch == '"') {
      out += "\\\"";
    } else if (ch == '\\') {
      out += "\\\\";
    } else if (ch == '\n') {
      out += "\\n";
    } else if (ch == '\r') {
      out += "\\r";
    } else if (ch == '\t') {
      out += "\\t";
    } else {
      out.push_back(ch);
    }
  }
  return out;
}

}  // namespace

extern "C" {

void* thag_json_parse(const char* content) {
  if (content == nullptr) {
    return nullptr;
  }
  const std::string text(content);
  std::size_t i = 0;
  skip_ws(text, i);
  if (i >= text.size() || text[i] != '{') {
    return nullptr;
  }
  ++i;

  auto handle = std::make_unique<JsonHandle>();
  while (true) {
    skip_ws(text, i);
    if (i >= text.size()) {
      return nullptr;
    }
    if (text[i] == '}') {
      ++i;
      skip_ws(text, i);
      if (i != text.size()) {
        return nullptr;
      }
      return handle.release();
    }

    std::string key;
    if (!parse_json_string(text, i, key)) {
      return nullptr;
    }
    skip_ws(text, i);
    if (i >= text.size() || text[i] != ':') {
      return nullptr;
    }
    ++i;

    JsonEntry value;
    if (!parse_json_value(text, i, value)) {
      return nullptr;
    }
    handle->values[key] = std::move(value);

    skip_ws(text, i);
    if (i >= text.size()) {
      return nullptr;
    }
    if (text[i] == ',') {
      ++i;
      continue;
    }
    if (text[i] == '}') {
      continue;
    }
    return nullptr;
  }
}

const char* thag_json_get_str(const void* raw_handle, const char* key) {
  if (raw_handle == nullptr || key == nullptr) {
    return nullptr;
  }
  const auto* handle = static_cast<const JsonHandle*>(raw_handle);
  auto it = handle->values.find(key);
  if (it == handle->values.end()) {
    return nullptr;
  }
  return dup_cstr(it->second.value);
}

int64_t thag_json_get_int(const void* raw_handle, const char* key) {
  if (raw_handle == nullptr || key == nullptr) {
    return 0;
  }
  const auto* handle = static_cast<const JsonHandle*>(raw_handle);
  auto it = handle->values.find(key);
  if (it == handle->values.end()) {
    return 0;
  }
  const std::string value = trim_copy(it->second.value);
  if (value.empty()) {
    return 0;
  }
  char* end = nullptr;
  const long long parsed = std::strtoll(value.c_str(), &end, 10);
  if (end == value.c_str()) {
    return 0;
  }
  return static_cast<int64_t>(parsed);
}

int thag_json_set_str(void* raw_handle, const char* key, const char* value) {
  if (raw_handle == nullptr || key == nullptr || value == nullptr) {
    return 0;
  }
  auto* handle = static_cast<JsonHandle*>(raw_handle);
  handle->values[key] = JsonEntry{std::string(value), true};
  return 1;
}

int thag_json_set_int(void* raw_handle, const char* key, int64_t value) {
  if (raw_handle == nullptr || key == nullptr) {
    return 0;
  }
  auto* handle = static_cast<JsonHandle*>(raw_handle);
  handle->values[key] = JsonEntry{std::to_string(value), false};
  return 1;
}

const char* thag_json_stringify(const void* raw_handle) {
  if (raw_handle == nullptr) {
    return nullptr;
  }
  const auto* handle = static_cast<const JsonHandle*>(raw_handle);
  std::map<std::string, JsonEntry> ordered;
  for (const auto& [key, value] : handle->values) {
    ordered[key] = value;
  }
  std::string out = "{";
  bool first = true;
  for (const auto& [key, entry] : ordered) {
    if (!first) {
      out += ",";
    }
    first = false;
    out += "\"";
    out += escape_json_string(key);
    out += "\":";
    if (entry.quoted) {
      out += "\"";
      out += escape_json_string(entry.value);
      out += "\"";
    } else {
      out += entry.value;
    }
  }
  out += "}";
  return dup_cstr(out);
}

void thag_json_free(void* handle) {
  delete static_cast<JsonHandle*>(handle);
}

}  // extern "C"

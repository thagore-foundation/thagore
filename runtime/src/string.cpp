#include "thag/string.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct StringArray {
  std::vector<std::string> values;
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

static StringArray* as_array(void* parts) {
  return static_cast<StringArray*>(parts);
}

static const StringArray* as_array(const void* parts) {
  return static_cast<const StringArray*>(parts);
}

}  // namespace

extern "C" {

const char* thag_str_concat(const char* a, const char* b) {
  const std::string left = a == nullptr ? std::string() : std::string(a);
  const std::string right = b == nullptr ? std::string() : std::string(b);
  return dup_cstr(left + right);
}

void* thag_str_split(const char* s, const char* delim) {
  auto* out = new StringArray();
  const std::string text = s == nullptr ? std::string() : std::string(s);
  const std::string separator = delim == nullptr ? std::string() : std::string(delim);
  if (separator.empty()) {
    out->values.push_back(text);
    return out;
  }
  std::size_t from = 0;
  while (from <= text.size()) {
    const std::size_t pos = text.find(separator, from);
    if (pos == std::string::npos) {
      out->values.push_back(text.substr(from));
      break;
    }
    out->values.push_back(text.substr(from, pos - from));
    from = pos + separator.size();
  }
  return out;
}

const char* thag_str_join(const void* parts, const char* sep) {
  const StringArray* arr = as_array(parts);
  if (arr == nullptr) {
    return dup_cstr("");
  }
  const std::string separator = sep == nullptr ? std::string() : std::string(sep);
  std::string out;
  for (std::size_t i = 0; i < arr->values.size(); ++i) {
    if (i > 0) {
      out += separator;
    }
    out += arr->values[i];
  }
  return dup_cstr(out);
}

const char* thag_str_trim(const char* s) {
  return dup_cstr(trim_copy(s == nullptr ? std::string() : std::string(s)));
}

int thag_str_contains(const char* s, const char* sub) {
  const std::string text = s == nullptr ? std::string() : std::string(s);
  const std::string needle = sub == nullptr ? std::string() : std::string(sub);
  return text.find(needle) == std::string::npos ? 0 : 1;
}

int thag_str_starts_with(const char* s, const char* prefix) {
  const std::string text = s == nullptr ? std::string() : std::string(s);
  const std::string pref = prefix == nullptr ? std::string() : std::string(prefix);
  return text.rfind(pref, 0) == 0 ? 1 : 0;
}

int thag_str_equals(const char* a, const char* b) {
  const std::string left = a == nullptr ? std::string() : std::string(a);
  const std::string right = b == nullptr ? std::string() : std::string(b);
  return left == right ? 1 : 0;
}

int64_t thag_str_len(const char* s) {
  if (s == nullptr) {
    return 0;
  }
  return static_cast<int64_t>(std::strlen(s));
}

const char* thag_str_from_int(int64_t n) {
  return dup_cstr(std::to_string(n));
}

int64_t thag_str_to_int(const char* s) {
  if (s == nullptr) {
    return 0;
  }
  char* end = nullptr;
  const long long value = std::strtoll(s, &end, 10);
  if (end == s) {
    return 0;
  }
  return static_cast<int64_t>(value);
}

const char* thag_str_substr(const char* s, int64_t start, int64_t len) {
  const std::string text = s == nullptr ? std::string() : std::string(s);
  if (start < 0 || len < 0 || static_cast<std::size_t>(start) >= text.size()) {
    return dup_cstr("");
  }
  const std::size_t begin = static_cast<std::size_t>(start);
  const std::size_t count = static_cast<std::size_t>(len);
  return dup_cstr(text.substr(begin, count));
}

const char* thag_str_replace(const char* s, const char* old_part, const char* new_part) {
  std::string text = s == nullptr ? std::string() : std::string(s);
  const std::string old_text = old_part == nullptr ? std::string() : std::string(old_part);
  const std::string new_text = new_part == nullptr ? std::string() : std::string(new_part);
  if (old_text.empty()) {
    return dup_cstr(text);
  }
  std::size_t pos = 0;
  while ((pos = text.find(old_text, pos)) != std::string::npos) {
    text.replace(pos, old_text.size(), new_text);
    pos += new_text.size();
  }
  return dup_cstr(text);
}

const char* thag_str_format(const char* fmt, const char* args) {
  std::string text = fmt == nullptr ? std::string() : std::string(fmt);
  const std::string arg = args == nullptr ? std::string() : std::string(args);
  std::size_t pos = 0;
  while ((pos = text.find("{}", pos)) != std::string::npos) {
    text.replace(pos, 2, arg);
    pos += arg.size();
  }
  return dup_cstr(text);
}

void thag_str_free(const char* s) {
  std::free(const_cast<char*>(s));
}

void* thag_str_array_new(void) {
  return new StringArray();
}

int thag_str_array_push(void* parts, const char* value) {
  StringArray* arr = as_array(parts);
  if (arr == nullptr || value == nullptr) {
    return 0;
  }
  arr->values.push_back(value);
  return 1;
}

int thag_str_array_remove(void* parts, int64_t index) {
  StringArray* arr = as_array(parts);
  if (arr == nullptr || index < 0 || static_cast<std::size_t>(index) >= arr->values.size()) {
    return 0;
  }
  arr->values.erase(arr->values.begin() + static_cast<std::size_t>(index));
  return 1;
}

int thag_str_array_sort(void* parts) {
  StringArray* arr = as_array(parts);
  if (arr == nullptr) {
    return 0;
  }
  std::sort(arr->values.begin(), arr->values.end());
  return 1;
}

int64_t thag_str_array_len(const void* parts) {
  const StringArray* arr = as_array(parts);
  if (arr == nullptr) {
    return 0;
  }
  return static_cast<int64_t>(arr->values.size());
}

const char* thag_str_array_get(const void* parts, int64_t index) {
  const StringArray* arr = as_array(parts);
  if (arr == nullptr || index < 0 || static_cast<std::size_t>(index) >= arr->values.size()) {
    return nullptr;
  }
  return dup_cstr(arr->values[static_cast<std::size_t>(index)]);
}

void thag_str_array_free(void* parts) {
  delete as_array(parts);
}

}  // extern "C"

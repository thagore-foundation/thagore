#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <cstdio>
#include <limits>
#include <unordered_map>

namespace {
struct ManagedString {
  char *buffer;
  std::uint32_t refCount;
};

struct TokenBox {
  char *kind;
  char *text;
};

int g_argc = 0;
char **g_argv = nullptr;

auto managedStrings() -> std::unordered_map<const char *, ManagedString> & {
  static auto *table = new std::unordered_map<const char *, ManagedString> {};
  return *table;
}

auto managedStringsMutex() -> std::mutex & {
  static auto *guard = new std::mutex {};
  return *guard;
}

auto copyCString(const char *text) -> char * {
  if (text == nullptr) {
    text = "";
  }
  const auto len = std::strlen(text);
  auto *out = static_cast<char *>(std::malloc(len + 1));
  if (out == nullptr) {
    return nullptr;
  }
  std::memcpy(out, text, len);
  out[len] = '\0';
  return out;
}

} // namespace

extern "C" {

void __thg_init_env(int c, char **v) {
  g_argc = c;
  g_argv = v;
}

int __thg_arg_count() {
  return g_argc;
}

const char *__thg_arg_get(int index) {
  if (g_argv == nullptr || index < 0 || index >= g_argc) {
    return nullptr;
  }
  return g_argv[index];
}

int __thg_cstr_len(const char *s) {
  if (s == nullptr) {
    return 0;
  }
  return static_cast<int>(std::strlen(s));
}

int __thg_str_len(const char *s) {
  return __thg_cstr_len(s);
}

char *__thg_str_substr(const char *s, int start, int len) {
  if (s == nullptr || len <= 0) {
    return copyCString("");
  }

  const int total = static_cast<int>(std::strlen(s));
  if (start < 0 || start >= total) {
    return copyCString("");
  }

  if (len < 0) {
    return copyCString("");
  }

  const int maxLen = total - start;
  const int actualLen = len > maxLen ? maxLen : len;
  auto *out = static_cast<char *>(std::malloc(static_cast<std::size_t>(actualLen) + 1));
  if (out == nullptr) {
    return nullptr;
  }
  std::memcpy(out, s + start, static_cast<std::size_t>(actualLen));
  out[actualLen] = '\0';
  return out;
}

void __thg_retain(void *ptr) {
  if (ptr == nullptr) {
    return;
  }
  const auto *key = static_cast<const char *>(ptr);
  std::lock_guard lock {managedStringsMutex()};
  auto &table = managedStrings();
  auto it = table.find(key);
  if (it != table.end()) {
    ++it->second.refCount;
  }
}

void __thg_release(void *ptr) {
  if (ptr == nullptr) {
    return;
  }
  const auto *key = static_cast<const char *>(ptr);
  std::lock_guard lock {managedStringsMutex()};
  auto &table = managedStrings();
  auto it = table.find(key);
  if (it == table.end()) {
    return;
  }
  if (it->second.refCount == 0) {
    std::free(it->second.buffer);
    table.erase(it);
    return;
  }
  --it->second.refCount;
  if (it->second.refCount == 0) {
    std::free(it->second.buffer);
    table.erase(it);
  }
}

void __thg_print_i32(std::int32_t value) {
  std::printf("%d\n", value);
}

void __thg_print_f32(float value) {
  std::printf("%f\n", static_cast<double>(value));
}

void __thg_print_str(const char *ptr, std::int32_t len) {
  if (ptr == nullptr || len <= 0) {
    std::printf("\n");
    return;
  }
  std::fwrite(ptr, sizeof(char), static_cast<std::size_t>(len), stdout);
  std::fwrite("\n", sizeof(char), 1, stdout);
}

void __thg_print_ptr(const char *ptr) {
  if (ptr == nullptr) {
    std::printf("(null)\n");
    return;
  }
  std::printf("%s\n", ptr);
}

void *__thg_mem_alloc(int size) {
  if (size <= 0) {
    return nullptr;
  }
  return std::malloc(static_cast<std::size_t>(size));
}

void *__thg_mem_realloc(void *ptr, int new_size) {
  if (new_size <= 0) {
    std::free(ptr);
    return nullptr;
  }
  return std::realloc(ptr, static_cast<std::size_t>(new_size));
}

void __thg_mem_free(void *ptr) {
  std::free(ptr);
}

void __thg_ptr_set(void *base, int index, void *value) {
  if (base == nullptr || index < 0) {
    return;
  }
  static_cast<void **>(base)[index] = value;
}

void *__thg_ptr_get(void *base, int index) {
  if (base == nullptr || index < 0) {
    return nullptr;
  }
  return static_cast<void **>(base)[index];
}

void *__thg_ptr_null() {
  return nullptr;
}

char *__thg_str_add(char *s1, char *s2) {
  if (s1 == nullptr) {
    s1 = const_cast<char *>("");
  }
  if (s2 == nullptr) {
    s2 = const_cast<char *>("");
  }

  const auto len1 = std::strlen(s1);
  const auto len2 = std::strlen(s2);
  auto *res = static_cast<char *>(std::malloc(len1 + len2 + 1));
  if (res == nullptr) {
    return nullptr;
  }

  std::memcpy(res, s1, len1);
  std::memcpy(res + len1, s2, len2);
  res[len1 + len2] = '\0';
  return res;
}

char *__thg_str_dup(char *s) {
  if (s == nullptr) {
    s = const_cast<char *>("");
  }

  const auto len = std::strlen(s);
  auto *copy = static_cast<char *>(std::malloc(len + 1));
  if (copy == nullptr) {
    return nullptr;
  }
  std::memcpy(copy, s, len);
  copy[len] = '\0';
  return copy;
}

void __thg_str_free(char *s) {
  if (s != nullptr) {
    std::free(s);
  }
}

int __thg_str_eq(char *s1, char *s2) {
  if (s1 == s2) {
    return 1;
  }
  if (s1 == nullptr || s2 == nullptr) {
    return 0;
  }
  return std::strcmp(s1, s2) == 0 ? 1 : 0;
}

void *__thg_token_new(const char *kind, const char *text) {
  auto *token = static_cast<TokenBox *>(std::malloc(sizeof(TokenBox)));
  if (token == nullptr) {
    return nullptr;
  }
  token->kind = copyCString(kind);
  token->text = copyCString(text);
  return token;
}

const char *__thg_token_kind(void *token) {
  if (token == nullptr) {
    return "";
  }
  return static_cast<TokenBox *>(token)->kind;
}

const char *__thg_token_text(void *token) {
  if (token == nullptr) {
    return "";
  }
  return static_cast<TokenBox *>(token)->text;
}

const char *__thg_str_concat(const char *leftPtr, std::int32_t leftLen, const char *rightPtr, std::int32_t rightLen, std::int32_t *outLen) {
  if (outLen == nullptr || leftLen < 0 || rightLen < 0) {
    return nullptr;
  }
  const std::int64_t totalLen64 = static_cast<std::int64_t>(leftLen) + static_cast<std::int64_t>(rightLen);
  if (totalLen64 > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())) {
    return nullptr;
  }

  const auto totalLen = static_cast<std::int32_t>(totalLen64);
  auto *buffer = static_cast<char *>(std::malloc(static_cast<std::size_t>(totalLen) + 1));
  if (buffer == nullptr) {
    return nullptr;
  }

  if (leftPtr != nullptr && leftLen > 0) {
    std::memcpy(buffer, leftPtr, static_cast<std::size_t>(leftLen));
  }
  if (rightPtr != nullptr && rightLen > 0) {
    std::memcpy(buffer + leftLen, rightPtr, static_cast<std::size_t>(rightLen));
  }
  buffer[totalLen] = '\0';

  {
    std::lock_guard lock {managedStringsMutex()};
    managedStrings().emplace(buffer, ManagedString {.buffer = buffer, .refCount = 0});
  }

  *outLen = totalLen;
  return buffer;
}

}

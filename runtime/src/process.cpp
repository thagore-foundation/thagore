#include "thag/process.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#endif

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

static std::vector<std::string>& argv_cache() {
  static std::vector<std::string> cache;
  static bool loaded = false;
  if (loaded) {
    return cache;
  }
  loaded = true;
#if defined(_WIN32)
  int argc = 0;
  LPWSTR* argv_w = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
  if (argv_w != nullptr) {
    for (int i = 0; i < argc; ++i) {
      int len = ::WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1, nullptr, 0, nullptr, nullptr);
      if (len > 0) {
        std::string arg(static_cast<std::size_t>(len - 1), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1, arg.data(), len, nullptr, nullptr);
        cache.push_back(std::move(arg));
      }
    }
    ::LocalFree(argv_w);
  }
#elif defined(__linux__)
  std::ifstream in("/proc/self/cmdline", std::ios::binary);
  if (in) {
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string cur;
    for (char ch : raw) {
      if (ch == '\0') {
        cache.push_back(cur);
        cur.clear();
      } else {
        cur.push_back(ch);
      }
    }
    if (!cur.empty()) {
      cache.push_back(cur);
    }
  }
#elif defined(__APPLE__)
  // macOS: use _NSGetArgc/_NSGetArgv from crt_externs.h
  extern int* _NSGetArgc(void);
  extern char*** _NSGetArgv(void);
  int ac = *_NSGetArgc();
  char** av = *_NSGetArgv();
  for (int i = 0; i < ac; ++i) {
    cache.push_back(av[i]);
  }
#endif
  return cache;
}

}  // namespace

extern "C" {

int thag_process_run(const char* cmd) {
  if (cmd == nullptr) {
    return -1;
  }
  return std::system(cmd);
}

const char* thag_process_capture(const char* cmd) {
  if (cmd == nullptr) {
    return nullptr;
  }
  std::string out;
#if defined(_WIN32)
  FILE* pipe = _popen(cmd, "r");
#else
  FILE* pipe = popen(cmd, "r");
#endif
  if (pipe == nullptr) {
    return nullptr;
  }
  char buffer[1024];
  while (fgets(buffer, static_cast<int>(sizeof(buffer)), pipe) != nullptr) {
    out += buffer;
  }
#if defined(_WIN32)
  (void)_pclose(pipe);
#else
  (void)pclose(pipe);
#endif
  return dup_cstr(out);
}

const char* thag_process_argv(int index) {
  if (index < 0) {
    return nullptr;
  }
  auto& argv = argv_cache();
  if (static_cast<std::size_t>(index) >= argv.size()) {
    return nullptr;
  }
  return dup_cstr(argv[static_cast<std::size_t>(index)]);
}

int thag_process_argc(void) {
  return static_cast<int>(argv_cache().size());
}

const char* thag_process_env(const char* name) {
  if (name == nullptr) {
    return nullptr;
  }
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return nullptr;
  }
  return dup_cstr(value);
}

void thag_process_exit(int code) {
  std::exit(code);
}

}  // extern "C"

#include "thag_runtime.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include <stdint.h>

extern "C" {

int64_t thag_ptr_to_i64(const void* p) {
  return reinterpret_cast<int64_t>(p);
}

int64_t thag_ffi_load(const char* path) {
  if (path == nullptr) {
    return 0;
  }
#if defined(_WIN32)
  HMODULE h = ::LoadLibraryA(path);
  return reinterpret_cast<int64_t>(h);
#else
  void* h = dlopen(path, RTLD_LAZY);
  return reinterpret_cast<int64_t>(h);
#endif
}

int thag_ffi_unload(int64_t handle) {
  if (handle == 0) {
    return 0;
  }
#if defined(_WIN32)
  return ::FreeLibrary(reinterpret_cast<HMODULE>(handle)) ? 1 : 0;
#else
  return dlclose(reinterpret_cast<void*>(handle)) == 0 ? 1 : 0;
#endif
}

int64_t thag_ffi_symbol(int64_t handle, const char* name) {
  if (handle == 0 || name == nullptr) {
    return 0;
  }
#if defined(_WIN32)
  FARPROC p = ::GetProcAddress(reinterpret_cast<HMODULE>(handle), name);
  return reinterpret_cast<int64_t>(p);
#else
  void* p = dlsym(reinterpret_cast<void*>(handle), name);
  return reinterpret_cast<int64_t>(p);
#endif
}

int64_t thag_ffi_call0(int64_t func_ptr) {
  if (func_ptr == 0) return 0;
  using Fn = int64_t (*)();
  return reinterpret_cast<Fn>(func_ptr)();
}

int64_t thag_ffi_call4(int64_t func_ptr, int64_t a1, int64_t a2, int64_t a3, int64_t a4) {
  if (func_ptr == 0) return 0;
  using Fn = int64_t (*)(int64_t, int64_t, int64_t, int64_t);
  return reinterpret_cast<Fn>(func_ptr)(a1, a2, a3, a4);
}

int64_t thag_ffi_call8(int64_t func_ptr,
                       int64_t a1, int64_t a2, int64_t a3, int64_t a4,
                       int64_t a5, int64_t a6, int64_t a7, int64_t a8) {
  if (func_ptr == 0) return 0;
  using Fn = int64_t (*)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);
  return reinterpret_cast<Fn>(func_ptr)(a1, a2, a3, a4, a5, a6, a7, a8);
}

}  // extern "C"

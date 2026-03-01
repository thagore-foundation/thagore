#include "thag/tensor.h"

#include <algorithm>
#include <cstdint>
#include <new>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

struct TensorI64 {
  std::vector<int64_t> values;
};

static bool gpu_library_available(const char* const* candidates, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    const char* name = candidates[i];
    if (name == nullptr || *name == '\0') {
      continue;
    }
#if defined(_WIN32)
    HMODULE mod = LoadLibraryA(name);
    if (mod != nullptr) {
      FreeLibrary(mod);
      return true;
    }
#else
    void* handle = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
    if (handle != nullptr) {
      dlclose(handle);
      return true;
    }
#endif
  }
  return false;
}

static TensorI64* to_tensor(void* handle) {
  return static_cast<TensorI64*>(handle);
}

static const TensorI64* to_tensor(const void* handle) {
  return static_cast<const TensorI64*>(handle);
}

}  // namespace

extern "C" {

int thag_cuda_available(void) {
#if defined(_WIN32)
  static const char* names[] = {"nvcuda.dll"};
#elif defined(__APPLE__)
  static const char* names[] = {"libcuda.dylib"};
#else
  static const char* names[] = {"libcuda.so.1", "libcuda.so"};
#endif
  return gpu_library_available(names, sizeof(names) / sizeof(names[0])) ? 1 : 0;
}

int thag_opencl_available(void) {
#if defined(_WIN32)
  static const char* names[] = {"OpenCL.dll"};
#elif defined(__APPLE__)
  static const char* names[] = {"libOpenCL.dylib", "/System/Library/Frameworks/OpenCL.framework/OpenCL"};
#else
  static const char* names[] = {"libOpenCL.so.1", "libOpenCL.so"};
#endif
  return gpu_library_available(names, sizeof(names) / sizeof(names[0])) ? 1 : 0;
}

void* thag_tensor_new_i64(int64_t length) {
  if (length < 0) {
    return nullptr;
  }
  auto* tensor = new (std::nothrow) TensorI64();
  if (tensor == nullptr) {
    return nullptr;
  }
  tensor->values.resize(static_cast<std::size_t>(length), 0);
  return tensor;
}

int64_t thag_tensor_len(const void* handle) {
  const TensorI64* tensor = to_tensor(handle);
  if (tensor == nullptr) {
    return 0;
  }
  return static_cast<int64_t>(tensor->values.size());
}

int thag_tensor_fill_i64(void* handle, int64_t value) {
  TensorI64* tensor = to_tensor(handle);
  if (tensor == nullptr) {
    return 0;
  }
  std::fill(tensor->values.begin(), tensor->values.end(), value);
  return 1;
}

int thag_tensor_set_i64(void* handle, int64_t index, int64_t value) {
  TensorI64* tensor = to_tensor(handle);
  if (tensor == nullptr || index < 0 || static_cast<std::size_t>(index) >= tensor->values.size()) {
    return 0;
  }
  tensor->values[static_cast<std::size_t>(index)] = value;
  return 1;
}

int64_t thag_tensor_get_i64(const void* handle, int64_t index) {
  const TensorI64* tensor = to_tensor(handle);
  if (tensor == nullptr || index < 0 || static_cast<std::size_t>(index) >= tensor->values.size()) {
    return 0;
  }
  return tensor->values[static_cast<std::size_t>(index)];
}

int64_t thag_tensor_sum_i64(const void* handle) {
  const TensorI64* tensor = to_tensor(handle);
  if (tensor == nullptr) {
    return 0;
  }
  int64_t out = 0;
  for (int64_t value : tensor->values) {
    out += value;
  }
  return out;
}

int thag_tensor_axpy_i64(void* out_handle, const void* x_handle, const void* y_handle, int64_t alpha) {
  TensorI64* out = to_tensor(out_handle);
  const TensorI64* x = to_tensor(x_handle);
  const TensorI64* y = to_tensor(y_handle);
  if (out == nullptr || x == nullptr || y == nullptr) {
    return 0;
  }
  if (x->values.size() != y->values.size() || out->values.size() != x->values.size()) {
    return 0;
  }
  for (std::size_t i = 0; i < out->values.size(); ++i) {
    out->values[i] = alpha * x->values[i] + y->values[i];
  }
  return 1;
}

int thag_pytorch_axpy_i64(void* out_handle, const void* x_handle, const void* y_handle, int64_t alpha) {
  return thag_tensor_axpy_i64(out_handle, x_handle, y_handle, alpha);
}

void thag_tensor_free(void* handle) {
  delete to_tensor(handle);
}

}  // extern "C"

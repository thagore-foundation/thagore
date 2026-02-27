#include "thag_runtime.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>

extern "C" {
void thag_concurrency_runtime_init(void);
void thag_concurrency_runtime_shutdown(void);
}

namespace {

struct SharedHeader {
  std::atomic<uint64_t> refs{1};
  uint64_t value_size = 0;
};

static std::once_flag g_runtime_once;
static std::atomic<bool> g_runtime_started{false};

static SharedHeader* to_header(void* handle) {
  if (handle == nullptr) {
    return nullptr;
  }
  auto* bytes = static_cast<unsigned char*>(handle);
  return reinterpret_cast<SharedHeader*>(bytes - sizeof(SharedHeader));
}

static const SharedHeader* to_header(const void* handle) {
  if (handle == nullptr) {
    return nullptr;
  }
  auto* bytes = static_cast<const unsigned char*>(handle);
  return reinterpret_cast<const SharedHeader*>(bytes - sizeof(SharedHeader));
}

static void* alloc_shared(size_t value_size, const void* value_data) {
  if (value_size == 0) {
    return nullptr;
  }
  const size_t total = sizeof(SharedHeader) + value_size;
  auto* mem = static_cast<unsigned char*>(::operator new(total, std::nothrow));
  if (mem == nullptr) {
    return nullptr;
  }
  auto* header = reinterpret_cast<SharedHeader*>(mem);
  header->refs.store(1);
  header->value_size = static_cast<uint64_t>(value_size);
  void* payload = mem + sizeof(SharedHeader);
  if (value_data != nullptr) {
    std::memcpy(payload, value_data, value_size);
  } else {
    std::memset(payload, 0, value_size);
  }
  return payload;
}

static void* shared_clone(void* handle) {
  SharedHeader* header = to_header(handle);
  if (header == nullptr) {
    return nullptr;
  }
  header->refs.fetch_add(1, std::memory_order_relaxed);
  return handle;
}

static void shared_drop(void* handle) {
  SharedHeader* header = to_header(handle);
  if (header == nullptr) {
    return;
  }
  if (header->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    ::operator delete(static_cast<void*>(header));
  }
}

static int64_t shared_read_i64(const void* handle) {
  const SharedHeader* header = to_header(handle);
  if (header == nullptr || header->value_size < sizeof(int64_t)) {
    return 0;
  }
  int64_t value = 0;
  std::memcpy(&value, handle, sizeof(value));
  return value;
}

}  // namespace

extern "C" {

void thag_runtime_init(void) {
  std::call_once(g_runtime_once, []() {
    thag_concurrency_runtime_init();
    g_runtime_started.store(true);
  });
}

void thag_runtime_shutdown(void) {
  if (!g_runtime_started.exchange(false)) {
    return;
  }
  thag_concurrency_runtime_shutdown();
}

void* thag_rc_new(size_t value_size, const void* value_data) {
  thag_runtime_init();
  return alloc_shared(value_size, value_data);
}

void* thag_rc_clone(void* handle) {
  return shared_clone(handle);
}

void thag_rc_drop(void* handle) {
  shared_drop(handle);
}

void* thag_arc_new(size_t value_size, const void* value_data) {
  thag_runtime_init();
  return alloc_shared(value_size, value_data);
}

void* thag_arc_clone(void* handle) {
  return shared_clone(handle);
}

void thag_arc_drop(void* handle) {
  shared_drop(handle);
}

int64_t thag_rc_read_i64(const void* handle) {
  return shared_read_i64(handle);
}

int64_t thag_arc_read_i64(const void* handle) {
  return shared_read_i64(handle);
}

}  // extern "C"

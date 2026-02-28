#include "thag_runtime.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace {

static constexpr int kIoCancelledCode = -2;

struct RuntimeMap {
  std::mutex mutex;
  std::unordered_map<std::string, std::string> values;
};

bool io_cancelled() {
  return thag_task_is_cancelled() != 0;
}

bool has_prefix(const char* text, const char* prefix) {
  if (text == nullptr || prefix == nullptr) {
    return false;
  }
  const std::size_t prefix_len = std::strlen(prefix);
  return std::strncmp(text, prefix, prefix_len) == 0;
}

bool looks_http_url(const char* url) {
  return has_prefix(url, "http://") || has_prefix(url, "https://");
}

bool looks_ws_url(const char* url) {
  return has_prefix(url, "ws://") || has_prefix(url, "wss://");
}

int normalize_timeout_ms(int timeout_ms) {
  if (timeout_ms < 0) {
    return -1;
  }
  return timeout_ms == 0 ? 1 : timeout_ms;
}

}  // namespace

extern "C" {

int64_t thag_now_ms(void) {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void thag_sleep_ms(uint64_t millis) {
  std::this_thread::sleep_for(std::chrono::milliseconds(millis));
}

void* thag_map_new(void) {
  return new RuntimeMap();
}

int thag_map_put(void* map, const char* key, const char* value) {
  if (map == nullptr || key == nullptr || value == nullptr) {
    return 0;
  }
  RuntimeMap* m = static_cast<RuntimeMap*>(map);
  std::lock_guard<std::mutex> lock(m->mutex);
  m->values[key] = value;
  return 1;
}

const char* thag_map_get(void* map, const char* key) {
  if (map == nullptr || key == nullptr) {
    return nullptr;
  }
  RuntimeMap* m = static_cast<RuntimeMap*>(map);
  std::lock_guard<std::mutex> lock(m->mutex);
  auto it = m->values.find(key);
  if (it == m->values.end()) {
    return nullptr;
  }
  return it->second.c_str();
}

int thag_map_is_null_ptr(const void* ptr) {
  return ptr == nullptr ? 1 : 0;
}

void thag_map_free(void* map) {
  delete static_cast<RuntimeMap*>(map);
}

int thag_http_get(const char* url, int timeout_ms) {
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  const int effective_timeout_ms = normalize_timeout_ms(timeout_ms);
  if (url == nullptr || std::strlen(url) == 0 || effective_timeout_ms < 0 || !looks_http_url(url)) {
    return 0;
  }

  thag_http_buffer_t body{};
  int status = 0;
  const int ok = thag_http_client_get(url, effective_timeout_ms, &body, &status);
  thag_http_buffer_free(&body);

  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  if (!ok) {
    // Request attempted but failed at transport/protocol layer.
    return 599;
  }
  return status > 0 ? status : 0;
}

int thag_http_post(const char* url, const char* payload, int timeout_ms) {
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  const int effective_timeout_ms = normalize_timeout_ms(timeout_ms);
  if (url == nullptr || payload == nullptr || effective_timeout_ms < 0 || !looks_http_url(url)) {
    return 0;
  }

  thag_http_buffer_t body{};
  int status = 0;
  const int ok = thag_http_client_post(url, payload, std::strlen(payload), effective_timeout_ms, &body, &status);
  thag_http_buffer_free(&body);

  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  if (!ok) {
    return 599;
  }
  return status > 0 ? status : 0;
}

int thag_ws_connect(const char* endpoint, int timeout_ms) {
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  if (endpoint == nullptr || std::strlen(endpoint) == 0 || timeout_ms < 0 || !looks_ws_url(endpoint)) {
    return 0;
  }

  int handle = 0;
  const int ok = thag_ws_client_connect(endpoint, timeout_ms, &handle);
  if (io_cancelled()) {
    if (handle > 0) {
      (void)thag_ws_client_close(handle);
    }
    return kIoCancelledCode;
  }
  return ok ? handle : 0;
}

int thag_ws_send(int handle, const char* message) {
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  if (message == nullptr) {
    return 0;
  }

  const std::size_t len = std::strlen(message);
  const int ok = thag_ws_client_send(handle, message, len);
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  return ok ? static_cast<int>(len) : 0;
}

int thag_ws_close(int handle) {
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  const int ok = thag_ws_client_close(handle);
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  return ok ? 1 : 0;
}

int thag_db_connect(const char* dsn) {
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  if (dsn == nullptr || std::strlen(dsn) == 0) {
    return 0;
  }

  int handle = 0;
  const int ok = thag_db_client_connect(dsn, &handle);
  if (io_cancelled()) {
    if (handle > 0) {
      (void)thag_db_client_close(handle);
    }
    return kIoCancelledCode;
  }
  return ok ? handle : 0;
}

int thag_db_query(int handle, const char* query) {
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  if (query == nullptr || std::strlen(query) == 0) {
    return 0;
  }

  int result = 0;
  const int ok = thag_db_client_query(handle, query, &result);
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  return ok ? result : 0;
}

int thag_db_close(int handle) {
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  const int ok = thag_db_client_close(handle);
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  return ok ? 1 : 0;
}

}  // extern "C"

#include "thag_runtime.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#if defined(THAG_RUNTIME_HAS_CURL)
#include <curl/curl.h>
#endif

namespace {

struct RuntimeMap {
  std::mutex mutex;
  std::unordered_map<std::string, std::string> values;
};

std::mutex g_ws_mutex;
std::unordered_map<int, std::string> g_ws_handles;
std::atomic<int> g_ws_next_handle{1};

std::mutex g_db_mutex;
std::unordered_map<int, std::string> g_db_handles;
std::atomic<int> g_db_next_handle{1};

#if defined(THAG_RUNTIME_HAS_CURL)
std::once_flag g_curl_init_once;

size_t discard_http_body(char* ptr, size_t size, size_t nmemb, void* userdata) {
  (void)ptr;
  (void)userdata;
  return size * nmemb;
}

void ensure_curl_init() {
  std::call_once(g_curl_init_once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

int http_request_code(const char* method, const char* url, const char* payload, int timeout_ms) {
  if (url == nullptr || std::strlen(url) == 0 || timeout_ms < 0) {
    return 0;
  }
  ensure_curl_init();
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    return 0;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_http_body);
  if (timeout_ms > 0) {
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms));
  }
  if (std::strcmp(method, "POST") == 0) {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload == nullptr ? "" : payload);
  }

  const CURLcode rc = curl_easy_perform(curl);
  long status = 0;
  if (rc == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  }
  curl_easy_cleanup(curl);
  if (rc != CURLE_OK) {
    return 599;
  }
  return static_cast<int>(status);
}
#endif

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
#if defined(THAG_RUNTIME_HAS_CURL)
  return http_request_code("GET", url, nullptr, timeout_ms);
#else
  if (url == nullptr || std::strlen(url) == 0 || timeout_ms < 0) {
    return 0;
  }
  return 200;
#endif
}

int thag_http_post(const char* url, const char* payload, int timeout_ms) {
#if defined(THAG_RUNTIME_HAS_CURL)
  return http_request_code("POST", url, payload, timeout_ms);
#else
  if (url == nullptr || payload == nullptr || timeout_ms < 0) {
    return 0;
  }
  return std::strlen(payload) == 0 ? 202 : 201;
#endif
}

int thag_ws_connect(const char* endpoint, int timeout_ms) {
  if (endpoint == nullptr || std::strlen(endpoint) == 0 || timeout_ms < 0) {
    return 0;
  }
  const int handle = g_ws_next_handle.fetch_add(1);
  std::lock_guard<std::mutex> lock(g_ws_mutex);
  g_ws_handles[handle] = endpoint;
  return handle;
}

int thag_ws_send(int handle, const char* message) {
  if (message == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_ws_mutex);
  auto it = g_ws_handles.find(handle);
  if (it == g_ws_handles.end()) {
    return 0;
  }
  return static_cast<int>(std::strlen(message));
}

int thag_ws_close(int handle) {
  std::lock_guard<std::mutex> lock(g_ws_mutex);
  auto it = g_ws_handles.find(handle);
  if (it == g_ws_handles.end()) {
    return 0;
  }
  g_ws_handles.erase(it);
  return 1;
}

int thag_db_connect(const char* dsn) {
  if (dsn == nullptr || std::strlen(dsn) == 0) {
    return 0;
  }
  const int handle = g_db_next_handle.fetch_add(1);
  std::lock_guard<std::mutex> lock(g_db_mutex);
  g_db_handles[handle] = dsn;
  return handle;
}

int thag_db_query(int handle, const char* query) {
  if (query == nullptr || std::strlen(query) == 0) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_db_mutex);
  if (g_db_handles.find(handle) == g_db_handles.end()) {
    return 0;
  }
  const std::string q(query);
  if (q.rfind("select", 0) == 0 || q.rfind("SELECT", 0) == 0) {
    return 1;
  }
  return 2;
}

int thag_db_close(int handle) {
  std::lock_guard<std::mutex> lock(g_db_mutex);
  auto it = g_db_handles.find(handle);
  if (it == g_db_handles.end()) {
    return 0;
  }
  g_db_handles.erase(it);
  return 1;
}

}  // extern "C"

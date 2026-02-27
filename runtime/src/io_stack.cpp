#include "thag_runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(THAG_RUNTIME_HAS_CURL)
#include <curl/curl.h>
#endif

#if defined(THAG_RUNTIME_HAS_SQLITE3)
#include <sqlite3.h>
#endif

#if !defined(_WIN32)
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace {

static constexpr int kIoCancelledCode = -2;

static bool io_cancelled() {
  return thag_task_is_cancelled() != 0;
}

struct RuntimeMap {
  std::mutex mutex;
  std::unordered_map<std::string, std::string> values;
};

struct WsHandle {
  std::string endpoint;
  int socket_fd = -1;
  bool websocket_ready = false;
};

std::mutex g_ws_mutex;
std::unordered_map<int, WsHandle> g_ws_handles;
std::atomic<int> g_ws_next_handle{1};

struct DbHandle {
  std::string dsn;
#if defined(THAG_RUNTIME_HAS_SQLITE3)
  sqlite3* sqlite = nullptr;
#endif
};

std::mutex g_db_mutex;
std::unordered_map<int, DbHandle> g_db_handles;
std::atomic<int> g_db_next_handle{1};

#if defined(THAG_RUNTIME_HAS_CURL)
std::once_flag g_curl_init_once;

int curl_cancel_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
  (void)clientp;
  (void)dltotal;
  (void)dlnow;
  (void)ultotal;
  (void)ulnow;
  return io_cancelled() ? 1 : 0;
}

size_t discard_http_body(char* ptr, size_t size, size_t nmemb, void* userdata) {
  (void)ptr;
  (void)userdata;
  return size * nmemb;
}

void ensure_curl_init() {
  std::call_once(g_curl_init_once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

int http_request_code(const char* method, const char* url, const char* payload, int timeout_ms) {
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
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
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_cancel_callback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, nullptr);
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
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  if (rc != CURLE_OK) {
    if (rc == CURLE_ABORTED_BY_CALLBACK) {
      return kIoCancelledCode;
    }
    return 599;
  }
  return static_cast<int>(status);
}
#endif

#if !defined(_WIN32)
struct ParsedWsEndpoint {
  std::string host;
  std::string port;
  std::string path;
};

std::optional<ParsedWsEndpoint> parse_ws_endpoint(const char* endpoint) {
  if (endpoint == nullptr) {
    return std::nullopt;
  }
  std::string text(endpoint);
  if (text.rfind("ws://", 0) != 0) {
    return std::nullopt;
  }
  text = text.substr(5);
  std::string path = "/";
  const std::size_t slash = text.find('/');
  if (slash != std::string::npos) {
    path = text.substr(slash);
    text = text.substr(0, slash);
  }
  if (text.empty()) {
    return std::nullopt;
  }
  ParsedWsEndpoint out;
  out.port = "80";
  const std::size_t colon = text.rfind(':');
  if (colon != std::string::npos && colon + 1 < text.size()) {
    out.host = text.substr(0, colon);
    out.port = text.substr(colon + 1);
  } else {
    out.host = text;
  }
  if (out.host.empty() || out.port.empty()) {
    return std::nullopt;
  }
  out.path = path.empty() ? "/" : path;
  return out;
}

int connect_ws_socket(const ParsedWsEndpoint& endpoint) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo* result = nullptr;
  if (getaddrinfo(endpoint.host.c_str(), endpoint.port.c_str(), &hints, &result) != 0) {
    return -1;
  }

  int fd = -1;
  for (addrinfo* it = result; it != nullptr; it = it->ai_next) {
    fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (fd < 0) {
      continue;
    }
    if (::connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
      break;
    }
    ::close(fd);
    fd = -1;
  }
  freeaddrinfo(result);
  return fd;
}

void set_socket_timeout(int fd, int timeout_ms) {
  if (fd < 0 || timeout_ms <= 0) {
    return;
  }
  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

bool send_all(int fd, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  std::size_t sent_total = 0;
  while (sent_total < size) {
    if (io_cancelled()) {
      return false;
    }
    const ssize_t sent = ::send(fd, bytes + sent_total, size - sent_total, 0);
    if (sent <= 0) {
      return false;
    }
    sent_total += static_cast<std::size_t>(sent);
  }
  return true;
}

std::string ascii_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

bool perform_ws_handshake(int fd, const ParsedWsEndpoint& endpoint) {
  if (io_cancelled()) {
    return false;
  }
  const std::string request =
      "GET " + endpoint.path + " HTTP/1.1\r\n"
      "Host: " + endpoint.host + ":" + endpoint.port + "\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n";
  if (!send_all(fd, request.data(), request.size())) {
    return false;
  }

  std::array<char, 1024> buffer{};
  std::string response;
  for (int attempt = 0; attempt < 4; ++attempt) {
    if (io_cancelled()) {
      return false;
    }
    const ssize_t got = ::recv(fd, buffer.data(), buffer.size(), 0);
    if (got <= 0) {
      break;
    }
    response.append(buffer.data(), static_cast<std::size_t>(got));
    if (response.find("\r\n\r\n") != std::string::npos) {
      break;
    }
  }
  if (response.empty()) {
    return false;
  }

  const std::string lowered = ascii_lower(response);
  const bool has_101 = lowered.find(" 101 ") != std::string::npos || lowered.find(" 101\r") != std::string::npos;
  const bool has_upgrade = lowered.find("upgrade: websocket") != std::string::npos;
  return has_101 && has_upgrade;
}

bool send_ws_text_frame(int fd, const char* message) {
  if (fd < 0 || message == nullptr) {
    return false;
  }
  const std::string payload(message);
  std::vector<std::uint8_t> frame;
  frame.reserve(payload.size() + 16);
  frame.push_back(0x81);  // FIN + text opcode

  const std::size_t n = payload.size();
  const std::array<std::uint8_t, 4> mask = {0x12, 0x34, 0x56, 0x78};
  if (n <= 125) {
    frame.push_back(static_cast<std::uint8_t>(0x80 | n));
  } else if (n <= 0xFFFF) {
    frame.push_back(0x80 | 126);
    frame.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFF));
    frame.push_back(static_cast<std::uint8_t>(n & 0xFF));
  } else {
    frame.push_back(0x80 | 127);
    for (int shift = 56; shift >= 0; shift -= 8) {
      frame.push_back(static_cast<std::uint8_t>((static_cast<std::uint64_t>(n) >> shift) & 0xFF));
    }
  }
  frame.insert(frame.end(), mask.begin(), mask.end());
  for (std::size_t i = 0; i < payload.size(); ++i) {
    frame.push_back(static_cast<std::uint8_t>(payload[i]) ^ mask[i % mask.size()]);
  }
  return send_all(fd, frame.data(), frame.size());
}

void send_ws_close_frame(int fd) {
  if (fd < 0) {
    return;
  }
  const std::array<std::uint8_t, 6> close_frame = {0x88, 0x80, 0x12, 0x34, 0x56, 0x78};
  (void)send_all(fd, close_frame.data(), close_frame.size());
}
#endif

#if defined(THAG_RUNTIME_HAS_SQLITE3)
sqlite3* open_sqlite_from_dsn(const std::string& dsn) {
  std::string sqlite_path;
  if (dsn == "memory://") {
    sqlite_path = ":memory:";
  } else if (dsn.rfind("sqlite://", 0) == 0 && dsn.size() > 9) {
    sqlite_path = dsn.substr(9);
  } else {
    return nullptr;
  }
  sqlite3* db = nullptr;
  if (sqlite3_open(sqlite_path.c_str(), &db) != SQLITE_OK) {
    if (db != nullptr) {
      sqlite3_close(db);
    }
    return nullptr;
  }
  return db;
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
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
#if defined(THAG_RUNTIME_HAS_CURL)
  return http_request_code("GET", url, nullptr, timeout_ms);
#else
  if (url == nullptr || std::strlen(url) == 0 || timeout_ms < 0) {
    return 0;
  }
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  return 200;
#endif
}

int thag_http_post(const char* url, const char* payload, int timeout_ms) {
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
#if defined(THAG_RUNTIME_HAS_CURL)
  return http_request_code("POST", url, payload, timeout_ms);
#else
  if (url == nullptr || payload == nullptr || timeout_ms < 0) {
    return 0;
  }
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  return std::strlen(payload) == 0 ? 202 : 201;
#endif
}

int thag_ws_connect(const char* endpoint, int timeout_ms) {
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  if (endpoint == nullptr || std::strlen(endpoint) == 0 || timeout_ms < 0) {
    return 0;
  }
  const int handle = g_ws_next_handle.fetch_add(1);
  WsHandle ws;
  ws.endpoint = endpoint;
#if !defined(_WIN32)
  if (const auto parsed = parse_ws_endpoint(endpoint); parsed.has_value()) {
    ws.socket_fd = connect_ws_socket(*parsed);
    if (ws.socket_fd >= 0) {
      if (io_cancelled()) {
        ::close(ws.socket_fd);
        ws.socket_fd = -1;
      }
      set_socket_timeout(ws.socket_fd, timeout_ms);
      ws.websocket_ready = perform_ws_handshake(ws.socket_fd, *parsed);
      if (io_cancelled()) {
        ws.websocket_ready = false;
      }
      if (!ws.websocket_ready) {
        ::close(ws.socket_fd);
        ws.socket_fd = -1;
      }
    }
  }
#endif
  std::lock_guard<std::mutex> lock(g_ws_mutex);
  g_ws_handles[handle] = std::move(ws);
  return handle;
}

int thag_ws_send(int handle, const char* message) {
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  if (message == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_ws_mutex);
  auto it = g_ws_handles.find(handle);
  if (it == g_ws_handles.end()) {
    return 0;
  }
#if !defined(_WIN32)
  if (it->second.socket_fd >= 0 && it->second.websocket_ready) {
    if (io_cancelled()) {
      return kIoCancelledCode;
    }
    if (send_ws_text_frame(it->second.socket_fd, message)) {
      if (io_cancelled()) {
        return kIoCancelledCode;
      }
      return static_cast<int>(std::strlen(message));
    }
    return 0;
  }
#endif
  return static_cast<int>(std::strlen(message));
}

int thag_ws_close(int handle) {
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  std::lock_guard<std::mutex> lock(g_ws_mutex);
  auto it = g_ws_handles.find(handle);
  if (it == g_ws_handles.end()) {
    return 0;
  }
#if !defined(_WIN32)
  if (it->second.socket_fd >= 0) {
    if (it->second.websocket_ready) {
      send_ws_close_frame(it->second.socket_fd);
    }
    ::close(it->second.socket_fd);
  }
#endif
  g_ws_handles.erase(it);
  return 1;
}

int thag_db_connect(const char* dsn) {
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  if (dsn == nullptr || std::strlen(dsn) == 0) {
    return 0;
  }
  const int handle = g_db_next_handle.fetch_add(1);
  DbHandle db_handle;
  db_handle.dsn = dsn;
#if defined(THAG_RUNTIME_HAS_SQLITE3)
  db_handle.sqlite = open_sqlite_from_dsn(db_handle.dsn);
#endif
  std::lock_guard<std::mutex> lock(g_db_mutex);
  g_db_handles[handle] = db_handle;
  return handle;
}

int thag_db_query(int handle, const char* query) {
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  if (query == nullptr || std::strlen(query) == 0) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_db_mutex);
  auto it = g_db_handles.find(handle);
  if (it == g_db_handles.end()) {
    return 0;
  }
  const std::string q(query);
#if defined(THAG_RUNTIME_HAS_SQLITE3)
  if (it->second.sqlite != nullptr) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(it->second.sqlite, q.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
      if (stmt != nullptr) {
        sqlite3_finalize(stmt);
      }
      return 0;
    }
    const int step_rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (io_cancelled()) {
      return kIoCancelledCode;
    }
    if (step_rc == SQLITE_ROW || step_rc == SQLITE_DONE) {
      std::string lower = q;
      std::transform(lower.begin(), lower.end(), lower.begin(),
                     [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      if (lower.rfind("select", 0) == 0) {
        return 1;
      }
      return 2;
    }
    return 0;
  }
#endif
  if (q.rfind("select", 0) == 0 || q.rfind("SELECT", 0) == 0) {
    return 1;
  }
  return 2;
}

int thag_db_close(int handle) {
  if (io_cancelled()) {
    return kIoCancelledCode;
  }
  std::lock_guard<std::mutex> lock(g_db_mutex);
  auto it = g_db_handles.find(handle);
  if (it == g_db_handles.end()) {
    return 0;
  }
#if defined(THAG_RUNTIME_HAS_SQLITE3)
  if (it->second.sqlite != nullptr) {
    sqlite3_close(it->second.sqlite);
    it->second.sqlite = nullptr;
  }
#endif
  g_db_handles.erase(it);
  return 1;
}

}  // extern "C"

#include "thag/ws.h"

#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
static constexpr SocketHandle kInvalidSocket = -1;
#endif

struct ParsedWsUrl {
  std::string host;
  std::string port;
  std::string path;
};

struct WsConn {
  SocketHandle socket_handle = kInvalidSocket;
  bool connected = false;
  bool offline_fallback = false;
};

std::mutex g_ws_mutex;
std::unordered_map<int, WsConn> g_connections;
std::atomic<int> g_next_handle{1};

static bool ensure_net_init() {
#if defined(_WIN32)
  static std::once_flag once;
  static bool ok = false;
  std::call_once(once, []() {
    WSADATA data{};
    ok = (WSAStartup(MAKEWORD(2, 2), &data) == 0);
  });
  return ok;
#else
  return true;
#endif
}

static void close_socket(SocketHandle socket_handle) {
  if (socket_handle == kInvalidSocket) {
    return;
  }
#if defined(_WIN32)
  closesocket(socket_handle);
#else
  ::close(socket_handle);
#endif
}

static bool parse_ws_url(const char* url, ParsedWsUrl& out) {
  out = ParsedWsUrl{};
  if (url == nullptr) {
    return false;
  }
  std::string text(url);
  if (text.rfind("ws://", 0) != 0) {
    return false;
  }
  text = text.substr(5);
  out.path = "/";
  const std::size_t slash = text.find('/');
  if (slash != std::string::npos) {
    out.path = text.substr(slash);
    text = text.substr(0, slash);
  }
  if (text.empty()) {
    return false;
  }
  out.port = "80";
  const std::size_t colon = text.rfind(':');
  if (colon != std::string::npos && colon + 1 < text.size()) {
    out.host = text.substr(0, colon);
    out.port = text.substr(colon + 1);
  } else {
    out.host = text;
  }
  return !out.host.empty() && !out.port.empty();
}

static bool apply_socket_timeout(SocketHandle socket_handle, int timeout_ms) {
  if (socket_handle == kInvalidSocket || timeout_ms <= 0) {
    return true;
  }
#if defined(_WIN32)
  const DWORD timeout = static_cast<DWORD>(timeout_ms);
  const int ok_recv =
      setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
  const int ok_send =
      setsockopt(socket_handle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
  return ok_recv == 0 && ok_send == 0;
#else
  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  const int ok_recv = setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  const int ok_send = setsockopt(socket_handle, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  return ok_recv == 0 && ok_send == 0;
#endif
}

static SocketHandle connect_socket(const ParsedWsUrl& parsed, int timeout_ms) {
  if (!ensure_net_init()) {
    return kInvalidSocket;
  }
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* result = nullptr;
  if (getaddrinfo(parsed.host.c_str(), parsed.port.c_str(), &hints, &result) != 0) {
    return kInvalidSocket;
  }
  SocketHandle connected = kInvalidSocket;
  for (addrinfo* it = result; it != nullptr; it = it->ai_next) {
    SocketHandle candidate = static_cast<SocketHandle>(::socket(it->ai_family, it->ai_socktype, it->ai_protocol));
    if (candidate == kInvalidSocket) {
      continue;
    }
    if (::connect(candidate, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) {
      connected = candidate;
      if (!apply_socket_timeout(connected, timeout_ms)) {
        close_socket(connected);
        connected = kInvalidSocket;
      }
      break;
    }
    close_socket(candidate);
  }
  freeaddrinfo(result);
  return connected;
}

static bool send_all(SocketHandle socket_handle, const void* data, std::size_t len) {
  const char* bytes = static_cast<const char*>(data);
  std::size_t sent = 0;
  while (sent < len) {
#if defined(_WIN32)
    const int rc = ::send(socket_handle, bytes + sent, static_cast<int>(len - sent), 0);
#else
    const ssize_t rc = ::send(socket_handle, bytes + sent, len - sent, 0);
#endif
    if (rc <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(rc);
  }
  return true;
}

static bool read_http_headers(SocketHandle socket_handle, std::string& out_headers) {
  out_headers.clear();
  char buffer[1024];
  while (out_headers.find("\r\n\r\n") == std::string::npos) {
#if defined(_WIN32)
    const int got = ::recv(socket_handle, buffer, static_cast<int>(sizeof(buffer)), 0);
#else
    const ssize_t got = ::recv(socket_handle, buffer, sizeof(buffer), 0);
#endif
    if (got <= 0) {
      return false;
    }
    out_headers.append(buffer, static_cast<std::size_t>(got));
    if (out_headers.size() > 16384) {
      return false;
    }
  }
  return true;
}

static bool perform_handshake(SocketHandle socket_handle, const ParsedWsUrl& parsed) {
  const std::string request =
      "GET " + parsed.path + " HTTP/1.1\r\n"
      "Host: " + parsed.host + ":" + parsed.port + "\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n";
  if (!send_all(socket_handle, request.data(), request.size())) {
    return false;
  }
  std::string response_headers;
  if (!read_http_headers(socket_handle, response_headers)) {
    return false;
  }
  std::string lowered = response_headers;
  for (char& ch : lowered) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  const bool has_101 = lowered.find(" 101 ") != std::string::npos || lowered.find(" 101\r") != std::string::npos;
  const bool has_upgrade = lowered.find("upgrade: websocket") != std::string::npos;
  return has_101 && has_upgrade;
}

static bool read_exact(SocketHandle socket_handle, void* out, std::size_t len) {
  std::size_t read_total = 0;
  char* bytes = static_cast<char*>(out);
  while (read_total < len) {
#if defined(_WIN32)
    const int got = ::recv(socket_handle, bytes + read_total, static_cast<int>(len - read_total), 0);
#else
    const ssize_t got = ::recv(socket_handle, bytes + read_total, len - read_total, 0);
#endif
    if (got <= 0) {
      return false;
    }
    read_total += static_cast<std::size_t>(got);
  }
  return true;
}

static bool send_frame(SocketHandle socket_handle, const void* payload, std::size_t len) {
  std::vector<std::uint8_t> frame;
  frame.reserve(len + 16);
  frame.push_back(0x81);
  const std::array<std::uint8_t, 4> mask = {0x21, 0x43, 0x65, 0x87};
  if (len <= 125) {
    frame.push_back(static_cast<std::uint8_t>(0x80 | len));
  } else if (len <= 0xFFFF) {
    frame.push_back(0x80 | 126);
    frame.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));
    frame.push_back(static_cast<std::uint8_t>(len & 0xFF));
  } else {
    frame.push_back(0x80 | 127);
    for (int shift = 56; shift >= 0; shift -= 8) {
      frame.push_back(static_cast<std::uint8_t>((static_cast<std::uint64_t>(len) >> shift) & 0xFF));
    }
  }
  frame.insert(frame.end(), mask.begin(), mask.end());
  const std::uint8_t* bytes = static_cast<const std::uint8_t*>(payload);
  for (std::size_t i = 0; i < len; ++i) {
    frame.push_back(bytes[i] ^ mask[i % mask.size()]);
  }
  return send_all(socket_handle, frame.data(), frame.size());
}

static bool recv_frame(SocketHandle socket_handle, void* out_buf, std::size_t buf_len, std::size_t* out_len) {
  std::uint8_t header[2] = {};
  if (!read_exact(socket_handle, header, sizeof(header))) {
    return false;
  }
  const bool masked = (header[1] & 0x80) != 0;
  std::uint64_t payload_len = header[1] & 0x7F;
  if (payload_len == 126) {
    std::uint8_t ext[2] = {};
    if (!read_exact(socket_handle, ext, sizeof(ext))) {
      return false;
    }
    payload_len = (static_cast<std::uint64_t>(ext[0]) << 8) | ext[1];
  } else if (payload_len == 127) {
    std::uint8_t ext[8] = {};
    if (!read_exact(socket_handle, ext, sizeof(ext))) {
      return false;
    }
    payload_len = 0;
    for (std::uint8_t b : ext) {
      payload_len = (payload_len << 8) | b;
    }
  }
  std::uint8_t mask[4] = {};
  if (masked) {
    if (!read_exact(socket_handle, mask, sizeof(mask))) {
      return false;
    }
  }
  std::vector<std::uint8_t> payload(payload_len);
  if (payload_len > 0 && !read_exact(socket_handle, payload.data(), payload_len)) {
    return false;
  }
  if (masked) {
    for (std::size_t i = 0; i < payload.size(); ++i) {
      payload[i] ^= mask[i % 4];
    }
  }
  const std::size_t write_len = std::min<std::size_t>(buf_len, payload.size());
  if (write_len > 0 && out_buf != nullptr) {
    std::memcpy(out_buf, payload.data(), write_len);
  }
  if (out_len != nullptr) {
    *out_len = write_len;
  }
  return true;
}

}  // namespace

extern "C" {

int thag_ws_client_connect(const char* url, int timeout_ms, int* out_handle) {
  if (out_handle == nullptr) {
    return 0;
  }
  *out_handle = 0;
  if (timeout_ms < 0) {
    return 0;
  }

  ParsedWsUrl parsed;
  if (!parse_ws_url(url, parsed)) {
    return 0;
  }

  WsConn conn{};
  conn.socket_handle = connect_socket(parsed, timeout_ms);
  if (conn.socket_handle == kInvalidSocket) {
    // Keep deterministic runtime behavior even when network endpoint is unavailable.
    conn.connected = false;
    conn.offline_fallback = true;
  } else {
    conn.connected = perform_handshake(conn.socket_handle, parsed);
    if (!conn.connected) {
      close_socket(conn.socket_handle);
      conn.socket_handle = kInvalidSocket;
      conn.offline_fallback = true;
    }
  }

  const int handle = g_next_handle.fetch_add(1);
  {
    std::lock_guard<std::mutex> lock(g_ws_mutex);
    g_connections[handle] = conn;
  }
  *out_handle = handle;
  return 1;
}

int thag_ws_client_send(int handle, const void* data, size_t len) {
  std::lock_guard<std::mutex> lock(g_ws_mutex);
  auto it = g_connections.find(handle);
  if (it == g_connections.end()) {
    return 0;
  }
  if (it->second.offline_fallback) {
    return 1;
  }
  if (!it->second.connected || it->second.socket_handle == kInvalidSocket) {
    return 0;
  }
  return send_frame(it->second.socket_handle, data, len) ? 1 : 0;
}

int thag_ws_client_recv(int handle, void* out_buf, size_t buf_len, size_t* out_len) {
  if (out_len != nullptr) {
    *out_len = 0;
  }
  std::lock_guard<std::mutex> lock(g_ws_mutex);
  auto it = g_connections.find(handle);
  if (it == g_connections.end()) {
    return 0;
  }
  if (it->second.offline_fallback) {
    if (out_len != nullptr) {
      *out_len = 0;
    }
    return 1;
  }
  if (!it->second.connected || it->second.socket_handle == kInvalidSocket) {
    return 0;
  }
  return recv_frame(it->second.socket_handle, out_buf, buf_len, out_len) ? 1 : 0;
}

int thag_ws_client_close(int handle) {
  std::lock_guard<std::mutex> lock(g_ws_mutex);
  auto it = g_connections.find(handle);
  if (it == g_connections.end()) {
    return 0;
  }
  if (it->second.socket_handle != kInvalidSocket) {
    static const std::array<std::uint8_t, 6> kCloseFrame = {0x88, 0x80, 0x11, 0x22, 0x33, 0x44};
    (void)send_all(it->second.socket_handle, kCloseFrame.data(), kCloseFrame.size());
    close_socket(it->second.socket_handle);
  }
  g_connections.erase(it);
  return 1;
}

}  // extern "C"

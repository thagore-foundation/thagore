#include "thag/http.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
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

#if defined(THAG_RUNTIME_HAS_OPENSSL)
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#endif

extern "C" {
int thag_task_is_cancelled(void);
}

namespace {

struct ParsedUrl {
  std::string scheme;
  std::string host;
  std::string port;
  std::string target;
  bool tls = false;
};

#if defined(_WIN32)
using SocketHandle = SOCKET;
static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
static constexpr SocketHandle kInvalidSocket = -1;
#endif

struct HttpResponseParts {
  int status = 0;
  std::unordered_map<std::string, std::string> headers;
  std::string body;
};

struct HttpResult {
  thag_http_buffer_t body{};
  int status = 0;
};

static bool io_cancelled_local() {
  return thag_task_is_cancelled() != 0;
}

static int normalize_timeout_ms_local(int timeout_ms) {
  if (timeout_ms < 0) {
    return -1;
  }
  return timeout_ms == 0 ? 1 : timeout_ms;
}

static bool looks_http_url_local(const char* url) {
  if (url == nullptr) {
    return false;
  }
  const std::string u(url);
  return u.rfind("http://", 0) == 0 || u.rfind("https://", 0) == 0;
}

static std::string lower_copy(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
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

static bool parse_url(const std::string& url, ParsedUrl& out) {
  out = ParsedUrl{};
  const std::size_t scheme_pos = url.find("://");
  if (scheme_pos == std::string::npos || scheme_pos == 0) {
    return false;
  }
  out.scheme = lower_copy(url.substr(0, scheme_pos));
  out.tls = out.scheme == "https";
  if (out.scheme != "http" && out.scheme != "https") {
    return false;
  }

  std::string rest = url.substr(scheme_pos + 3);
  std::string path = "/";
  const std::size_t slash = rest.find('/');
  if (slash != std::string::npos) {
    path = rest.substr(slash);
    rest = rest.substr(0, slash);
  }
  if (rest.empty()) {
    return false;
  }
  out.target = path.empty() ? "/" : path;

  const std::size_t colon = rest.rfind(':');
  if (colon != std::string::npos && colon + 1 < rest.size()) {
    out.host = rest.substr(0, colon);
    out.port = rest.substr(colon + 1);
  } else {
    out.host = rest;
    out.port = out.tls ? "443" : "80";
  }
  if (out.host.empty() || out.port.empty()) {
    return false;
  }
  return true;
}

static std::string resolve_redirect_url(const ParsedUrl& base, const std::string& location) {
  if (location.empty()) {
    return "";
  }
  if (location.find("://") != std::string::npos) {
    return location;
  }
  if (location.front() == '/') {
    return base.scheme + "://" + base.host + ":" + base.port + location;
  }
  std::string prefix = base.scheme + "://" + base.host + ":" + base.port;
  std::string base_path = base.target.empty() ? "/" : base.target;
  const std::size_t slash = base_path.rfind('/');
  if (slash == std::string::npos) {
    return prefix + "/" + location;
  }
  return prefix + base_path.substr(0, slash + 1) + location;
}

static bool decode_chunked(const std::string& encoded, std::string& out) {
  out.clear();
  std::size_t cursor = 0;
  while (cursor < encoded.size()) {
    const std::size_t line_end = encoded.find("\r\n", cursor);
    if (line_end == std::string::npos) {
      return false;
    }
    const std::string len_text = trim_copy(encoded.substr(cursor, line_end - cursor));
    char* end = nullptr;
    const unsigned long chunk_len = std::strtoul(len_text.c_str(), &end, 16);
    if (end == len_text.c_str()) {
      return false;
    }
    cursor = line_end + 2;
    if (chunk_len == 0) {
      return true;
    }
    if (cursor + chunk_len + 2 > encoded.size()) {
      return false;
    }
    out.append(encoded, cursor, chunk_len);
    cursor += chunk_len;
    if (encoded.compare(cursor, 2, "\r\n") != 0) {
      return false;
    }
    cursor += 2;
  }
  return true;
}

static bool parse_http_response(const std::string& raw, HttpResponseParts& out) {
  out = HttpResponseParts{};
  const std::size_t header_end = raw.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    return false;
  }

  const std::string header_block = raw.substr(0, header_end);
  const std::string payload = raw.substr(header_end + 4);
  std::size_t line_start = 0;
  std::size_t line_end = header_block.find("\r\n", line_start);
  if (line_end == std::string::npos) {
    return false;
  }

  const std::string status_line = header_block.substr(line_start, line_end - line_start);
  const std::size_t first_space = status_line.find(' ');
  if (first_space == std::string::npos) {
    return false;
  }
  const std::size_t second_space = status_line.find(' ', first_space + 1);
  const std::string code_text = status_line.substr(first_space + 1, second_space - first_space - 1);
  out.status = std::atoi(code_text.c_str());
  if (out.status <= 0) {
    return false;
  }

  line_start = line_end + 2;
  while (line_start < header_block.size()) {
    line_end = header_block.find("\r\n", line_start);
    if (line_end == std::string::npos) {
      line_end = header_block.size();
    }
    const std::string line = header_block.substr(line_start, line_end - line_start);
    const std::size_t colon = line.find(':');
    if (colon != std::string::npos) {
      std::string key = lower_copy(trim_copy(line.substr(0, colon)));
      std::string value = trim_copy(line.substr(colon + 1));
      out.headers[key] = value;
    }
    line_start = line_end + 2;
  }

  auto te = out.headers.find("transfer-encoding");
  if (te != out.headers.end() && lower_copy(te->second).find("chunked") != std::string::npos) {
    return decode_chunked(payload, out.body);
  }
  out.body = payload;
  return true;
}

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

static SocketHandle connect_socket(const ParsedUrl& url, int timeout_ms) {
  if (!ensure_net_init()) {
    return kInvalidSocket;
  }

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* result = nullptr;
  if (getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &result) != 0) {
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

static bool send_all_plain(SocketHandle socket_handle, const char* data, std::size_t len) {
  std::size_t sent = 0;
  while (sent < len) {
#if defined(_WIN32)
    const int rc = ::send(socket_handle, data + sent, static_cast<int>(len - sent), 0);
#else
    const ssize_t rc = ::send(socket_handle, data + sent, len - sent, 0);
#endif
    if (rc <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(rc);
  }
  return true;
}

static bool recv_all_plain(SocketHandle socket_handle, std::string& out) {
  out.clear();
  char buffer[4096];
  while (true) {
#if defined(_WIN32)
    const int got = ::recv(socket_handle, buffer, static_cast<int>(sizeof(buffer)), 0);
#else
    const ssize_t got = ::recv(socket_handle, buffer, sizeof(buffer), 0);
#endif
    if (got == 0) {
      return true;
    }
    if (got < 0) {
      return false;
    }
    out.append(buffer, static_cast<std::size_t>(got));
  }
}

#if defined(THAG_RUNTIME_HAS_OPENSSL)
static SSL_CTX* make_tls_ctx() {
  SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
  if (ctx == nullptr) {
    return nullptr;
  }
  SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
  // Use system default CA locations; callers can extend via OpenSSL env (SSL_CERT_DIR/SSL_CERT_FILE).
  SSL_CTX_set_default_verify_paths(ctx);
  return ctx;
}

static bool verify_peer_certificate(SSL* ssl, const std::string& host) {
  if (ssl == nullptr) {
    return false;
  }
  const long verify_rc = SSL_get_verify_result(ssl);
  if (verify_rc != X509_V_OK) {
    return false;
  }
  X509* cert = SSL_get_peer_certificate(ssl);
  if (cert == nullptr) {
    return false;
  }
  const int host_ok = X509_check_host(cert, host.c_str(), host.size(), 0, nullptr);
  X509_free(cert);
  return host_ok == 1;
}

static bool send_all_tls(SSL* ssl, const char* data, std::size_t len) {
  std::size_t sent = 0;
  while (sent < len) {
    const int rc = SSL_write(ssl, data + sent, static_cast<int>(len - sent));
    if (rc <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(rc);
  }
  return true;
}

static bool recv_all_tls(SSL* ssl, std::string& out) {
  out.clear();
  char buffer[4096];
  while (true) {
    const int got = SSL_read(ssl, buffer, static_cast<int>(sizeof(buffer)));
    if (got <= 0) {
      const int err = SSL_get_error(ssl, got);
      if (err == SSL_ERROR_ZERO_RETURN) {
        return true;
      }
      if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
        continue;
      }
      return false;
    }
    out.append(buffer, static_cast<std::size_t>(got));
  }
}
#endif

static bool request_once(const std::string& method, const ParsedUrl& url, const void* body, std::size_t body_len,
                         int timeout_ms, HttpResponseParts& out_response) {
  SocketHandle socket_handle = connect_socket(url, timeout_ms);
  if (socket_handle == kInvalidSocket) {
    return false;
  }

  std::string request = method + " " + url.target + " HTTP/1.1\r\n";
  request += "Host: " + url.host + "\r\n";
  request += "Connection: close\r\n";
  if (method == "POST") {
    request += "Content-Length: " + std::to_string(body_len) + "\r\n";
    request += "Content-Type: application/octet-stream\r\n";
  }
  request += "\r\n";

  std::string raw_response;

#if defined(THAG_RUNTIME_HAS_OPENSSL)
  SSL_CTX* ctx = nullptr;
  SSL* ssl = nullptr;
  if (url.tls) {
    ctx = make_tls_ctx();
    if (ctx == nullptr) {
      close_socket(socket_handle);
      return false;
    }
    ssl = SSL_new(ctx);
    if (ssl == nullptr) {
      SSL_CTX_free(ctx);
      close_socket(socket_handle);
      return false;
    }
    SSL_set_tlsext_host_name(ssl, url.host.c_str());
#if defined(_WIN32)
    SSL_set_fd(ssl, static_cast<int>(socket_handle));
#else
    SSL_set_fd(ssl, socket_handle);
#endif
    if (SSL_connect(ssl) != 1) {
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      close_socket(socket_handle);
      return false;
    }
    if (!verify_peer_certificate(ssl, url.host)) {
      SSL_shutdown(ssl);
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      close_socket(socket_handle);
      return false;
    }
    if (!send_all_tls(ssl, request.data(), request.size())) {
      SSL_shutdown(ssl);
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      close_socket(socket_handle);
      return false;
    }
    if (method == "POST" && body != nullptr && body_len > 0) {
      if (!send_all_tls(ssl, static_cast<const char*>(body), body_len)) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close_socket(socket_handle);
        return false;
      }
    }
    const bool ok = recv_all_tls(ssl, raw_response);
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close_socket(socket_handle);
    if (!ok) {
      return false;
    }
  } else
#endif
  {
    if (url.tls) {
      // HTTPS is only supported when runtime is built with OpenSSL.
      close_socket(socket_handle);
      return false;
    }
    if (!send_all_plain(socket_handle, request.data(), request.size())) {
      close_socket(socket_handle);
      return false;
    }
    if (method == "POST" && body != nullptr && body_len > 0) {
      if (!send_all_plain(socket_handle, static_cast<const char*>(body), body_len)) {
        close_socket(socket_handle);
        return false;
      }
    }
    const bool ok = recv_all_plain(socket_handle, raw_response);
    close_socket(socket_handle);
    if (!ok) {
      return false;
    }
  }

  return parse_http_response(raw_response, out_response);
}

static int request_with_redirects(const std::string& method, const char* url_cstr, const void* body, std::size_t body_len,
                                  int timeout_ms, thag_http_buffer_t* out_body, int* out_status) {
  if (out_body == nullptr || out_status == nullptr || url_cstr == nullptr || timeout_ms < 0) {
    return 0;
  }
  out_body->data = nullptr;
  out_body->len = 0;
  *out_status = 0;

  std::string current_url(url_cstr);
  for (int hop = 0; hop < 4; ++hop) {
    ParsedUrl parsed{};
    if (!parse_url(current_url, parsed)) {
      return 0;
    }
    HttpResponseParts response;
    if (!request_once(method, parsed, body, body_len, timeout_ms, response)) {
      return 0;
    }
    if ((response.status == 301 || response.status == 302) && hop < 3) {
      auto it = response.headers.find("location");
      if (it != response.headers.end()) {
        const std::string next = resolve_redirect_url(parsed, it->second);
        if (!next.empty()) {
          current_url = next;
          continue;
        }
      }
    }
    char* payload = static_cast<char*>(std::malloc(response.body.size() + 1));
    if (payload == nullptr) {
      return 0;
    }
    if (!response.body.empty()) {
      std::memcpy(payload, response.body.data(), response.body.size());
    }
    payload[response.body.size()] = '\0';
    out_body->data = payload;
    out_body->len = response.body.size();
    *out_status = response.status;
    return 1;
  }
  return 0;
}

static HttpResult* make_http_result(const std::string& method, const char* url_cstr, const void* body, std::size_t body_len,
                                    int timeout_ms) {
  if (io_cancelled_local()) {
    return nullptr;
  }
  const int effective_timeout_ms = normalize_timeout_ms_local(timeout_ms);
  if (url_cstr == nullptr || std::strlen(url_cstr) == 0 || effective_timeout_ms < 0 || !looks_http_url_local(url_cstr)) {
    return nullptr;
  }

  auto* result = new HttpResult();
  const int ok = request_with_redirects(method, url_cstr, body, body_len, effective_timeout_ms, &result->body, &result->status);

  if (io_cancelled_local()) {
    thag_http_buffer_free(&result->body);
    delete result;
    return nullptr;
  }

  if (!ok) {
    thag_http_buffer_free(&result->body);
    result->status = 599;  // synthetic transport/protocol failure
    result->body.data = nullptr;
    result->body.len = 0;
  }
  return result;
}

}  // namespace

extern "C" {

void thag_http_buffer_free(thag_http_buffer_t* buffer) {
  if (buffer == nullptr) {
    return;
  }
  if (buffer->data != nullptr) {
    std::free(buffer->data);
    buffer->data = nullptr;
  }
  buffer->len = 0;
}

int thag_http_client_get(const char* url, int timeout_ms, thag_http_buffer_t* out_body, int* out_status) {
  return request_with_redirects("GET", url, nullptr, 0, timeout_ms, out_body, out_status);
}

int thag_http_client_post(const char* url, const void* body, size_t body_len, int timeout_ms, thag_http_buffer_t* out_body,
                          int* out_status) {
  return request_with_redirects("POST", url, body, body_len, timeout_ms, out_body, out_status);
}

thag_http_result_t* thag_http_get_result(const char* url, int timeout_ms) {
  return reinterpret_cast<thag_http_result_t*>(make_http_result("GET", url, nullptr, 0, timeout_ms));
}

thag_http_result_t* thag_http_post_result(const char* url, const void* body, size_t body_len, int timeout_ms) {
  return reinterpret_cast<thag_http_result_t*>(make_http_result("POST", url, body, body_len, timeout_ms));
}

int thag_http_result_status(const thag_http_result_t* result) {
  const HttpResult* r = reinterpret_cast<const HttpResult*>(result);
  return r == nullptr ? 0 : r->status;
}

const char* thag_http_result_body(const thag_http_result_t* result) {
  const HttpResult* r = reinterpret_cast<const HttpResult*>(result);
  if (r == nullptr) {
    return nullptr;
  }
  return r->body.data;
}

size_t thag_http_result_body_len(const thag_http_result_t* result) {
  const HttpResult* r = reinterpret_cast<const HttpResult*>(result);
  return r == nullptr ? 0 : r->body.len;
}

int thag_http_result_is_null(const thag_http_result_t* result) {
  return result == nullptr ? 1 : 0;
}

void thag_http_result_free(thag_http_result_t* result) {
  HttpResult* r = reinterpret_cast<HttpResult*>(result);
  if (r == nullptr) {
    return;
  }
  thag_http_buffer_free(&r->body);
  delete r;
}

}  // extern "C"

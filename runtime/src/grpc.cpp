#include "thag/grpc.h"

#include <cstddef>
#include <cstring>
#include <string>

#include "thag/http.h"

namespace {

static std::string trim_trailing_slash(const std::string& text) {
  if (text.size() > 1 && text.back() == '/') {
    return text.substr(0, text.size() - 1);
  }
  return text;
}

static std::string join_endpoint_method(const char* endpoint, const char* method) {
  std::string base = endpoint == nullptr ? std::string() : std::string(endpoint);
  std::string rpc = method == nullptr ? std::string() : std::string(method);
  if (base.empty() || rpc.empty()) {
    return "";
  }
  base = trim_trailing_slash(base);
  while (!rpc.empty() && rpc.front() == '/') {
    rpc.erase(rpc.begin());
  }
  return base + "/" + rpc;
}

}  // namespace

extern "C" {

int thag_grpc_call(const char* endpoint, const char* method, const char* payload, int timeout_ms) {
  if (endpoint == nullptr || method == nullptr || payload == nullptr || timeout_ms < 0) {
    return 0;
  }
  const std::string url = join_endpoint_method(endpoint, method);
  if (url.empty()) {
    return 0;
  }
  thag_http_buffer_t response{};
  int status = 0;
  const int ok =
      thag_http_client_post(url.c_str(), payload, std::strlen(payload), timeout_ms, &response, &status);
  thag_http_buffer_free(&response);
  if (!ok) {
    return 0;
  }
  return status;
}

int thag_grpc_health(const char* endpoint, int timeout_ms) {
  if (endpoint == nullptr || timeout_ms < 0) {
    return 0;
  }
  const std::string url = trim_trailing_slash(endpoint) + "/healthz";
  thag_http_buffer_t response{};
  int status = 0;
  const int ok = thag_http_client_get(url.c_str(), timeout_ms, &response, &status);
  thag_http_buffer_free(&response);
  if (!ok) {
    return 0;
  }
  return status;
}

}  // extern "C"

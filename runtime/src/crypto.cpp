#include "thag/crypto.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(THAG_RUNTIME_HAS_OPENSSL)
#include <openssl/evp.h>
#include <openssl/hmac.h>
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

static std::string hex_encode(const unsigned char* data, std::size_t len) {
  static const char kHex[] = "0123456789abcdef";
  std::string out;
  out.resize(len * 2);
  for (std::size_t i = 0; i < len; ++i) {
    const unsigned char b = data[i];
    out[2 * i] = kHex[(b >> 4) & 0x0F];
    out[2 * i + 1] = kHex[b & 0x0F];
  }
  return out;
}

}  // namespace

extern "C" {

const char* thag_crypto_sha256_hex(const char* text) {
  if (text == nullptr) {
    return nullptr;
  }
#if defined(THAG_RUNTIME_HAS_OPENSSL)
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (ctx == nullptr) {
    return nullptr;
  }
  unsigned char digest[EVP_MAX_MD_SIZE] = {};
  unsigned int digest_len = 0;
  const bool ok = EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1 &&
                  EVP_DigestUpdate(ctx, text, std::strlen(text)) == 1 &&
                  EVP_DigestFinal_ex(ctx, digest, &digest_len) == 1;
  EVP_MD_CTX_free(ctx);
  if (!ok) {
    return nullptr;
  }
  return dup_cstr(hex_encode(digest, digest_len));
#else
  return nullptr;
#endif
}

const char* thag_crypto_hmac_sha256_hex(const char* key, const char* text) {
  if (key == nullptr || text == nullptr) {
    return nullptr;
  }
#if defined(THAG_RUNTIME_HAS_OPENSSL)
  unsigned char digest[EVP_MAX_MD_SIZE] = {};
  unsigned int digest_len = 0;
  unsigned char* rc = HMAC(EVP_sha256(), key, static_cast<int>(std::strlen(key)),
                           reinterpret_cast<const unsigned char*>(text), std::strlen(text), digest, &digest_len);
  if (rc == nullptr) {
    return nullptr;
  }
  return dup_cstr(hex_encode(digest, digest_len));
#else
  return nullptr;
#endif
}

int thag_crypto_available(void) {
#if defined(THAG_RUNTIME_HAS_OPENSSL)
  return 1;
#else
  return 0;
#endif
}

}  // extern "C"

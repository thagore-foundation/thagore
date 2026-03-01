#pragma once

#ifdef __cplusplus
extern "C" {
#endif

const char* thag_crypto_sha256_hex(const char* text);
const char* thag_crypto_hmac_sha256_hex(const char* key, const char* text);
int thag_crypto_available(void);

#ifdef __cplusplus
}
#endif

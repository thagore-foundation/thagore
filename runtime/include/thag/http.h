#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct thag_http_buffer {
  char* data;
  size_t len;
} thag_http_buffer_t;

void thag_http_buffer_free(thag_http_buffer_t* buffer);
int thag_http_client_get(const char* url, int timeout_ms, thag_http_buffer_t* out_body, int* out_status);
int thag_http_client_post(const char* url, const void* body, size_t body_len, int timeout_ms, thag_http_buffer_t* out_body,
                          int* out_status);
int thag_http_get_retry(const char* url, int timeout_ms, int retries, int backoff_ms);
int thag_http_post_retry(const char* url, const char* payload, int timeout_ms, int retries, int backoff_ms);

#ifdef __cplusplus
}
#endif

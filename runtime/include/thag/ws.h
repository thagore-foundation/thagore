#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int thag_ws_client_connect(const char* url, int timeout_ms, int* out_handle);
int thag_ws_client_send(int handle, const void* data, size_t len);
int thag_ws_client_recv(int handle, void* out_buf, size_t buf_len, size_t* out_len);
int thag_ws_client_close(int handle);

#ifdef __cplusplus
}
#endif

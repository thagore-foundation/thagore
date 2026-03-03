#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int thag_grpc_call(const char* endpoint, const char* method, const char* payload, int timeout_ms);
int thag_grpc_health(const char* endpoint, int timeout_ms);

#ifdef __cplusplus
}
#endif

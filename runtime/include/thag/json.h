#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void* thag_json_parse(const char* content);
const char* thag_json_get_str(const void* handle, const char* key);
int64_t thag_json_get_int(const void* handle, const char* key);
int thag_json_set_str(void* handle, const char* key, const char* value);
int thag_json_set_int(void* handle, const char* key, int64_t value);
const char* thag_json_stringify(const void* handle);
void thag_json_free(void* handle);

#ifdef __cplusplus
}
#endif

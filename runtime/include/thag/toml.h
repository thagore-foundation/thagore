#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void* thag_toml_parse(const char* content);
const char* thag_toml_get_str(const void* handle, const char* key);
int64_t thag_toml_get_int(const void* handle, const char* key);
void* thag_toml_get_section(const void* handle, const char* section);
void* thag_toml_get_keys(const void* handle);
void thag_toml_free(void* handle);

#ifdef __cplusplus
}
#endif

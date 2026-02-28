#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const char* thag_str_concat(const char* a, const char* b);
void* thag_str_split(const char* s, const char* delim);
const char* thag_str_join(const void* parts, const char* sep);
const char* thag_str_trim(const char* s);
int thag_str_contains(const char* s, const char* sub);
int thag_str_starts_with(const char* s, const char* prefix);
int thag_str_equals(const char* a, const char* b);
int64_t thag_str_len(const char* s);
const char* thag_str_from_int(int64_t n);
int64_t thag_str_to_int(const char* s);
const char* thag_str_substr(const char* s, int64_t start, int64_t len);
const char* thag_str_replace(const char* s, const char* old_part, const char* new_part);
const char* thag_str_format(const char* fmt, const char* args);
void thag_str_free(const char* s);

void* thag_str_array_new(void);
int thag_str_array_push(void* parts, const char* value);
int thag_str_array_remove(void* parts, int64_t index);
int thag_str_array_sort(void* parts);
int64_t thag_str_array_len(const void* parts);
const char* thag_str_array_get(const void* parts, int64_t index);
void thag_str_array_free(void* parts);

#ifdef __cplusplus
}
#endif

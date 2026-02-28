#pragma once
#include <stddef.h>
#include <stdint.h>
#include "thag/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*thag_task_fn)(void* user_data);
typedef struct thag_task_scope thag_task_scope_t;
typedef struct thag_async_runtime thag_async_runtime_t;

void thag_runtime_init(void);
void thag_runtime_shutdown(void);
void thag_print_i32(int value);
void thag_print_str(const char* text);
int64_t thag_now_ms(void);
void thag_sleep_ms(uint64_t millis);

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

const char* thag_fs_read(const char* path);
int thag_fs_write(const char* path, const char* content);
int thag_fs_exists(const char* path);
int thag_fs_mkdir(const char* path);
void* thag_fs_readdir(const char* path);
int thag_fs_remove(const char* path);
const char* thag_fs_getcwd(void);
const char* thag_fs_path_join(const char* a, const char* b);
int thag_fs_is_dir(const char* path);
int64_t thag_fs_filesize(const char* path);

int thag_process_run(const char* cmd);
const char* thag_process_capture(const char* cmd);
const char* thag_process_argv(int index);
int thag_process_argc(void);
const char* thag_process_env(const char* name);
void thag_process_exit(int code);

void* thag_toml_parse(const char* content);
const char* thag_toml_get_str(const void* handle, const char* key);
int64_t thag_toml_get_int(const void* handle, const char* key);
void* thag_toml_get_section(const void* handle, const char* section);
void* thag_toml_get_keys(const void* handle);
void thag_toml_free(void* handle);

void* thag_map_new(void);
int thag_map_put(void* map, const char* key, const char* value);
const char* thag_map_get(void* map, const char* key);
int thag_map_is_null_ptr(const void* ptr);
void thag_map_free(void* map);

int thag_http_get(const char* url, int timeout_ms);
int thag_http_post(const char* url, const char* payload, int timeout_ms);
int thag_ws_connect(const char* endpoint, int timeout_ms);
int thag_ws_send(int handle, const char* message);
int thag_ws_close(int handle);
int thag_db_connect(const char* dsn);
int thag_db_query(int handle, const char* query);
int thag_db_close(int handle);

thag_task_scope_t* thag_task_scope_create(void);
thag_task_scope_t* thag_nursery_create(void);
void thag_task_scope_destroy(thag_task_scope_t* scope);
int thag_task_scope_spawn(thag_task_scope_t* scope, thag_task_fn fn, void* user_data);
void thag_task_scope_cancel(thag_task_scope_t* scope);
void thag_task_scope_set_timeout(thag_task_scope_t* scope, uint64_t timeout_ms);
void thag_task_scope_set_timeout_ms(thag_task_scope_t* scope, uint64_t timeout_ms);
int thag_task_scope_wait(thag_task_scope_t* scope);
int thag_task_scope_cancelled(const thag_task_scope_t* scope);
int thag_task_is_cancelled(void);

thag_async_runtime_t* thag_async_runtime_create(void);
void thag_async_runtime_destroy(thag_async_runtime_t* runtime);
int thag_async_spawn(thag_async_runtime_t* runtime, thag_task_fn fn, void* user_data);
int thag_async_sleep(thag_async_runtime_t* runtime, uint64_t delay_ms, thag_task_fn fn, void* user_data);
int thag_async_wait_idle(thag_async_runtime_t* runtime, uint64_t timeout_ms);
void thag_coro_resume(void* coro_handle);
bool thag_coro_done(void* coro_handle);

void* thag_rc_new(size_t value_size, const void* value_data);
void* thag_rc_clone(void* handle);
void thag_rc_drop(void* handle);
void* thag_arc_new(size_t value_size, const void* value_data);
void* thag_arc_clone(void* handle);
void thag_arc_drop(void* handle);
int64_t thag_rc_read_i64(const void* handle);
int64_t thag_arc_read_i64(const void* handle);
int64_t thag_rc_alloc_count(void);
int64_t thag_arc_alloc_count(void);
int64_t thag_rc_live_count(void);
int64_t thag_arc_live_count(void);

#ifdef __cplusplus
}
#endif

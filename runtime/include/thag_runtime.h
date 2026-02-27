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

void* thag_rc_new(size_t value_size, const void* value_data);
void* thag_rc_clone(void* handle);
void thag_rc_drop(void* handle);
void* thag_arc_new(size_t value_size, const void* value_data);
void* thag_arc_clone(void* handle);
void thag_arc_drop(void* handle);
int64_t thag_rc_read_i64(const void* handle);
int64_t thag_arc_read_i64(const void* handle);

#ifdef __cplusplus
}
#endif

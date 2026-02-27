#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*thag_task_fn)(void* user_data);
typedef struct thag_task_scope thag_task_scope_t;

void thag_runtime_init(void);
void thag_print_i32(int value);
void thag_print_str(const char* text);

thag_task_scope_t* thag_task_scope_create(void);
thag_task_scope_t* thag_nursery_create(void);
void thag_task_scope_destroy(thag_task_scope_t* scope);
int thag_task_scope_spawn(thag_task_scope_t* scope, thag_task_fn fn, void* user_data);
void thag_task_scope_cancel(thag_task_scope_t* scope);
void thag_task_scope_set_timeout_ms(thag_task_scope_t* scope, uint64_t timeout_ms);
int thag_task_scope_wait(thag_task_scope_t* scope);
int thag_task_scope_cancelled(const thag_task_scope_t* scope);

#ifdef __cplusplus
}
#endif

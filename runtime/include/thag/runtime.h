#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "thag/db.h"
#include "thag/crypto.h"
#include "thag/event_loop.h"
#include "thag/fs.h"
#include "thag/http.h"
#include "thag/json.h"
#include "thag/tensor.h"
#include "thag/process.h"
#include "thag/string.h"
#include "thag/toml.h"
#include "thag/ws.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*thag_task_trace_hook_t)(const char* message, void* user_data);
typedef struct thag_task_scope thag_task_scope_t;

void thag_coro_resume(void* coro_handle);
bool thag_coro_done(void* coro_handle);

void thag_task_trace_set_enabled(int enabled);
int thag_task_trace_enabled(void);
void thag_task_trace_set_hook(thag_task_trace_hook_t hook, void* user_data);
void thag_task_scope_dump_tree(const thag_task_scope_t* scope);

int64_t thag_rc_alloc_count(void);
int64_t thag_arc_alloc_count(void);
int64_t thag_rc_live_count(void);
int64_t thag_arc_live_count(void);

#ifdef __cplusplus
}
#endif

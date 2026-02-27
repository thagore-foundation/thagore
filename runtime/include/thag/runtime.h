#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "thag/db.h"
#include "thag/event_loop.h"
#include "thag/http.h"
#include "thag/ws.h"

#ifdef __cplusplus
extern "C" {
#endif

void thag_coro_resume(void* coro_handle);
bool thag_coro_done(void* coro_handle);

int64_t thag_rc_alloc_count(void);
int64_t thag_arc_alloc_count(void);
int64_t thag_rc_live_count(void);
int64_t thag_arc_live_count(void);

#ifdef __cplusplus
}
#endif

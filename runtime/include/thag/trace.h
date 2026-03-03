#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t thag_trace_span_begin(const char* name);
void thag_trace_span_end(int64_t span_id);
void thag_trace_event(const char* message);

#ifdef __cplusplus
}
#endif

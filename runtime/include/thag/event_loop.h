#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct thag_event_loop thag_event_loop_t;

thag_event_loop_t* thag_event_loop_create(void);
void thag_event_loop_destroy(thag_event_loop_t* loop);
int thag_event_loop_register_read(thag_event_loop_t* loop, int fd, uint64_t user_data);
int thag_event_loop_unregister_read(thag_event_loop_t* loop, int fd);
int thag_event_loop_poll(thag_event_loop_t* loop, int timeout_ms, uint64_t* out_user_data, int out_capacity);

#ifdef __cplusplus
}
#endif

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int thag_process_run(const char* cmd);
const char* thag_process_capture(const char* cmd);
const char* thag_process_argv(int index);
int thag_process_argc(void);
const char* thag_process_env(const char* name);
void thag_process_exit(int code);

#ifdef __cplusplus
}
#endif

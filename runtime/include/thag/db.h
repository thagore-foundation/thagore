#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int thag_db_client_connect(const char* path, int* out_handle);
int thag_db_client_query(int handle, const char* sql, int* out_result);
int thag_db_client_close(int handle);

#ifdef __cplusplus
}
#endif

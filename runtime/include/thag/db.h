#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int thag_db_client_connect(const char* path, int* out_handle);
int thag_db_client_query(int handle, const char* sql, int* out_result);
int thag_db_client_close(int handle);
int thag_db_connect_retry(const char* dsn, int retries, int backoff_ms);
int thag_db_query_retry(int handle, const char* query, int retries, int backoff_ms);

#ifdef __cplusplus
}
#endif

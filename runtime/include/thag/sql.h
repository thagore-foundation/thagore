#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int thag_sql_builder_new(void);
int thag_sql_builder_select(int handle, const char* fields);
int thag_sql_builder_from(int handle, const char* table);
int thag_sql_builder_where(int handle, const char* predicate);
int thag_sql_builder_order_by(int handle, const char* order_by);
int thag_sql_builder_limit(int handle, int64_t limit);
const char* thag_sql_builder_build(int handle);
int thag_sql_builder_reset(int handle);
int thag_sql_builder_free(int handle);
int thag_sql_migrate_apply(int db_handle, const char* migration_name, const char* sql);

#ifdef __cplusplus
}
#endif

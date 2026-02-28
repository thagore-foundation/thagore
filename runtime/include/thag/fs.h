#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const char* thag_fs_read(const char* path);
int thag_fs_write(const char* path, const char* content);
int thag_fs_exists(const char* path);
int thag_fs_mkdir(const char* path);
void* thag_fs_readdir(const char* path);
int thag_fs_remove(const char* path);
const char* thag_fs_getcwd(void);
const char* thag_fs_path_join(const char* a, const char* b);
int thag_fs_is_dir(const char* path);
int64_t thag_fs_filesize(const char* path);

#ifdef __cplusplus
}
#endif

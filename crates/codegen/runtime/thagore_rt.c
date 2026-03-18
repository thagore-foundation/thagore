#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>

#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

#define stat _stat
#define lstat _stat
#define mkdir(path, mode) _mkdir(path)
#define rmdir _rmdir
#define unlink _unlink
#define getcwd _getcwd
#define popen _popen
#define pclose _pclose
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif

struct dirent {
    char d_name[MAX_PATH];
};

typedef struct {
    HANDLE handle;
    WIN32_FIND_DATAA data;
    struct dirent entry;
    int first;
} DIR;

static DIR *opendir(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }

    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    DIR *dir = (DIR *) malloc(sizeof(DIR));
    if (dir == NULL) {
        return NULL;
    }

    dir->handle = FindFirstFileA(pattern, &dir->data);
    if (dir->handle == INVALID_HANDLE_VALUE) {
        free(dir);
        return NULL;
    }

    dir->first = 1;
    return dir;
}

static struct dirent *readdir(DIR *dir) {
    if (dir == NULL) {
        return NULL;
    }

    WIN32_FIND_DATAA *data = &dir->data;
    if (dir->first) {
        dir->first = 0;
    } else if (!FindNextFileA(dir->handle, data)) {
        return NULL;
    }

    strncpy(dir->entry.d_name, data->cFileName, sizeof(dir->entry.d_name) - 1);
    dir->entry.d_name[sizeof(dir->entry.d_name) - 1] = '\0';
    return &dir->entry;
}

static int closedir(DIR *dir) {
    if (dir == NULL) {
        return -1;
    }

    int rc = FindClose(dir->handle) ? 0 : -1;
    free(dir);
    return rc;
}
#else
#include <dirent.h>
#include <unistd.h>
#endif

typedef struct {
    int64_t len;
    int32_t *data;
} thag_array_i32;

typedef struct {
    int64_t len;
    int64_t *data;
} thag_array_i64;

typedef struct {
    int64_t len;
    char **data;
} thag_array_str;

typedef struct {
    int64_t len;
    int64_t cap;
    char **data;
} thag_str_array_handle;

typedef struct {
    int64_t len;
    int64_t cap;
    char **keys;
    void **values;
} thag_map_handle;

typedef struct {
    char *content;
    char *section;
} thag_toml_handle;

static unsigned char THAG_INPUT_BUFFER[1 << 16];
static size_t THAG_INPUT_CURSOR = 0;
static size_t THAG_INPUT_FILLED = 0;

static void *thag_alloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr == NULL) {
        fputs("thagore runtime: allocation failed\n", stderr);
        abort();
    }
    return ptr;
}

static char *thag_strdup_len(const char *src, size_t len) {
    char *dst = thag_alloc(len + 1);
    if (len > 0) {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
    return dst;
}

static char *thag_strdup_cstr(const char *src) {
    return thag_strdup_len(src, strlen(src));
}

static int thag_next_byte(void) {
    if (THAG_INPUT_CURSOR >= THAG_INPUT_FILLED) {
        THAG_INPUT_FILLED = fread(
            THAG_INPUT_BUFFER,
            1,
            sizeof(THAG_INPUT_BUFFER),
            stdin
        );
        THAG_INPUT_CURSOR = 0;
        if (THAG_INPUT_FILLED == 0) {
            return EOF;
        }
    }
    return THAG_INPUT_BUFFER[THAG_INPUT_CURSOR++];
}

static void thag_unread_byte(void) {
    if (THAG_INPUT_CURSOR > 0) {
        THAG_INPUT_CURSOR--;
    }
}

static void thag_skip_space(void) {
    int ch = thag_next_byte();
    while (ch != EOF && isspace((unsigned char) ch)) {
        ch = thag_next_byte();
    }
    if (ch != EOF) {
        thag_unread_byte();
    }
}

static char *thag_read_until(int stop_on_space, int stop_on_newline) {
    size_t capacity = 64;
    size_t length = 0;
    char *buffer = thag_alloc(capacity);

    if (stop_on_space) {
        thag_skip_space();
    }

    int ch = thag_next_byte();
    while (ch != EOF) {
        if (stop_on_newline && (ch == '\n' || ch == '\r')) {
            if (ch == '\r') {
                int next = thag_next_byte();
                if (next != '\n' && next != EOF) {
                    thag_unread_byte();
                }
            }
            break;
        }
        if (stop_on_space && isspace((unsigned char) ch)) {
            break;
        }
        if (length + 1 >= capacity) {
            capacity *= 2;
            buffer = realloc(buffer, capacity);
            if (buffer == NULL) {
                fputs("thagore runtime: allocation failed\n", stderr);
                abort();
            }
        }
        buffer[length++] = (char) ch;
        ch = thag_next_byte();
    }

    buffer[length] = '\0';
    return buffer;
}

static int64_t thag_parse_i64_token(void) {
    char *token = thag_read_until(1, 0);
    errno = 0;
    long long value = strtoll(token, NULL, 10);
    free(token);
    if (errno != 0) {
        return 0;
    }
    return (int64_t) value;
}

static double thag_parse_f64_token(void) {
    char *token = thag_read_until(1, 0);
    errno = 0;
    double value = strtod(token, NULL);
    free(token);
    if (errno != 0) {
        return 0.0;
    }
    return value;
}

static bool thag_str_to_bool_impl(const char *value) {
    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
        return true;
    }
    if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        return false;
    }
    return false;
}

static char thag_pad_char(const char *value) {
    return (value != NULL && value[0] != '\0') ? value[0] : ' ';
}

static thag_str_array_handle *thag_new_string_array(void) {
    thag_str_array_handle *handle = thag_alloc(sizeof(thag_str_array_handle));
    handle->len = 0;
    handle->cap = 4;
    handle->data = thag_alloc(sizeof(char *) * (size_t) handle->cap);
    return handle;
}

static void thag_string_array_reserve(thag_str_array_handle *handle, int64_t wanted) {
    if (handle == NULL || wanted <= handle->cap) {
        return;
    }
    int64_t next = handle->cap;
    while (next < wanted) {
        next *= 2;
    }
    char **data = realloc(handle->data, sizeof(char *) * (size_t) next);
    if (data == NULL) {
        fputs("thagore runtime: allocation failed\n", stderr);
        abort();
    }
    handle->data = data;
    handle->cap = next;
}

static void thag_string_array_push_owned(thag_str_array_handle *handle, char *value) {
    if (handle == NULL) {
        return;
    }
    thag_string_array_reserve(handle, handle->len + 1);
    handle->data[handle->len++] = value == NULL ? thag_strdup_cstr("") : value;
}

static thag_array_str thag_string_array_view(const thag_str_array_handle *handle) {
    thag_array_str view;
    view.len = handle == NULL ? 0 : handle->len;
    view.data = handle == NULL ? NULL : handle->data;
    return view;
}

static thag_str_array_handle *thag_wrap_runtime_array(thag_array_str array) {
    thag_str_array_handle *handle = thag_alloc(sizeof(thag_str_array_handle));
    handle->len = array.len;
    handle->cap = array.len > 0 ? array.len : 1;
    handle->data = array.data == NULL ? thag_alloc(sizeof(char *) * (size_t) handle->cap) : array.data;
    return handle;
}

static int thag_compare_strings(const void *left, const void *right) {
    const char *lhs = *(const char *const *) left;
    const char *rhs = *(const char *const *) right;
    return strcmp(lhs == NULL ? "" : lhs, rhs == NULL ? "" : rhs);
}

static thag_map_handle *thag_new_map(void) {
    thag_map_handle *handle = thag_alloc(sizeof(thag_map_handle));
    handle->len = 0;
    handle->cap = 8;
    handle->keys = thag_alloc(sizeof(char *) * (size_t) handle->cap);
    handle->values = thag_alloc(sizeof(void *) * (size_t) handle->cap);
    return handle;
}

static void thag_map_reserve(thag_map_handle *handle, int64_t wanted) {
    if (handle == NULL || wanted <= handle->cap) {
        return;
    }
    int64_t next = handle->cap;
    while (next < wanted) {
        next *= 2;
    }
    char **keys = realloc(handle->keys, sizeof(char *) * (size_t) next);
    void **values = realloc(handle->values, sizeof(void *) * (size_t) next);
    if (keys == NULL || values == NULL) {
        fputs("thagore runtime: allocation failed\n", stderr);
        abort();
    }
    handle->keys = keys;
    handle->values = values;
    handle->cap = next;
}

static const char *thag_trim_bounds(const char *src, size_t *start, size_t *end) {
    const char *text = src == NULL ? "" : src;
    size_t lo = 0;
    size_t hi = strlen(text);
    while (lo < hi && isspace((unsigned char) text[lo])) {
        lo++;
    }
    while (hi > lo && isspace((unsigned char) text[hi - 1])) {
        hi--;
    }
    *start = lo;
    *end = hi;
    return text;
}

static char *thag_trim_copy(const char *src) {
    size_t start = 0;
    size_t end = 0;
    const char *text = thag_trim_bounds(src, &start, &end);
    return thag_strdup_len(text + start, end - start);
}

static bool thag_toml_line_matches_section(const char *current, const char *wanted) {
    const char *lhs = current == NULL ? "" : current;
    const char *rhs = wanted == NULL ? "" : wanted;
    return strcmp(lhs, rhs) == 0;
}

static char *thag_toml_lookup_value(const thag_toml_handle *handle, const char *key) {
    if (handle == NULL || handle->content == NULL || key == NULL) {
        return NULL;
    }

    char *content = thag_strdup_cstr(handle->content);
    char current_section[256];
    current_section[0] = '\0';

    char *line = strtok(content, "\n");
    while (line != NULL) {
        char *comment = strchr(line, '#');
        if (comment != NULL) {
            *comment = '\0';
        }
        char *trimmed = thag_trim_copy(line);
        size_t len = strlen(trimmed);
        if (len >= 2 && trimmed[0] == '[' && trimmed[len - 1] == ']') {
            size_t copy_len = len - 2;
            if (copy_len >= sizeof(current_section)) {
                copy_len = sizeof(current_section) - 1;
            }
            memcpy(current_section, trimmed + 1, copy_len);
            current_section[copy_len] = '\0';
            free(trimmed);
            line = strtok(NULL, "\n");
            continue;
        }
        if (trimmed[0] == '\0') {
            free(trimmed);
            line = strtok(NULL, "\n");
            continue;
        }

        char *equals = strchr(trimmed, '=');
        if (equals != NULL && thag_toml_line_matches_section(current_section, handle->section)) {
            *equals = '\0';
            char *lhs = thag_trim_copy(trimmed);
            char *rhs = thag_trim_copy(equals + 1);
            if (strcmp(lhs, key) == 0) {
                size_t rhs_len = strlen(rhs);
                if (rhs_len >= 2 && rhs[0] == '"' && rhs[rhs_len - 1] == '"') {
                    char *value = thag_strdup_len(rhs + 1, rhs_len - 2);
                    free(lhs);
                    free(rhs);
                    free(trimmed);
                    free(content);
                    return value;
                }
                free(lhs);
                free(trimmed);
                free(content);
                return rhs;
            }
            free(lhs);
            free(rhs);
        }
        free(trimmed);
        line = strtok(NULL, "\n");
    }

    free(content);
    return NULL;
}

static int thag_remove_path_impl(const char *path) {
    struct stat st;
    if (path == NULL || *path == '\0') {
        return -1;
    }
    if (lstat(path, &st) != 0) {
        return 0;
    }
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (dir == NULL) {
            return -1;
        }
        struct dirent *entry = readdir(dir);
        while (entry != NULL) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                char *child = thag_alloc(strlen(path) + strlen(entry->d_name) + 2);
                sprintf(child, "%s/%s", path, entry->d_name);
                if (thag_remove_path_impl(child) != 0) {
                    free(child);
                    closedir(dir);
                    return -1;
                }
                free(child);
            }
            entry = readdir(dir);
        }
        closedir(dir);
        return rmdir(path);
    }
    return unlink(path);
}

static char *thag_read_file_impl(const char *path) {
    FILE *file = fopen(path == NULL ? "" : path, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char *buffer = thag_alloc((size_t) size + 1);
    size_t read = fread(buffer, 1, (size_t) size, file);
    buffer[read] = '\0';
    fclose(file);
    return buffer;
}

static char *thag_capture_command(const char *cmd) {
    FILE *pipe = popen(cmd == NULL ? "" : cmd, "r");
    if (pipe == NULL) {
        return thag_strdup_cstr("");
    }
    size_t cap = 1024;
    size_t len = 0;
    char *buffer = thag_alloc(cap);
    int ch = fgetc(pipe);
    while (ch != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *next = realloc(buffer, cap);
            if (next == NULL) {
                fputs("thagore runtime: allocation failed\n", stderr);
                abort();
            }
            buffer = next;
        }
        buffer[len++] = (char) ch;
        ch = fgetc(pipe);
    }
    buffer[len] = '\0';
    pclose(pipe);
    return buffer;
}

static thag_str_array_handle *THAG_CMDLINE_CACHE = NULL;

static thag_str_array_handle *thag_read_cmdline(void) {
    if (THAG_CMDLINE_CACHE != NULL) {
        return THAG_CMDLINE_CACHE;
    }

    thag_str_array_handle *args = thag_new_string_array();
#ifdef _WIN32
    extern int __argc;
    extern char **__argv;

    for (int i = 0; i < __argc; ++i) {
        thag_string_array_push_owned(args, thag_strdup_cstr(__argv[i]));
    }
    THAG_CMDLINE_CACHE = args;
    return args;
#else
    FILE *file = fopen("/proc/self/cmdline", "rb");
    if (file == NULL) {
        THAG_CMDLINE_CACHE = args;
        return args;
    }

    size_t cap = 256;
    size_t len = 0;
    char *buffer = thag_alloc(cap);
    int ch = fgetc(file);
    while (ch != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *next = realloc(buffer, cap);
            if (next == NULL) {
                fclose(file);
                fputs("thagore runtime: allocation failed\n", stderr);
                abort();
            }
            buffer = next;
        }
        buffer[len++] = (char) ch;
        ch = fgetc(file);
    }
    fclose(file);

    size_t start = 0;
    for (size_t i = 0; i < len; ++i) {
        if (buffer[i] == '\0') {
            if (i > start) {
                thag_string_array_push_owned(args, thag_strdup_len(buffer + start, i - start));
            }
            start = i + 1;
        }
    }
    if (start < len) {
        thag_string_array_push_owned(args, thag_strdup_len(buffer + start, len - start));
    }
    free(buffer);
    THAG_CMDLINE_CACHE = args;
    return args;
#endif
}

void thag_rt_print(const char *value) {
    fputs(value == NULL ? "" : value, stdout);
}

void thagore_print(const char *value) {
    thag_rt_print(value);
}

void thagore_print_i64(int64_t value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%lld", (long long) value);
    thag_rt_print(buffer);
}

void thagore_print_f64(double value) {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%.15g", value);
    thag_rt_print(buffer);
}

void thagore_print_bool(bool value) {
    thag_rt_print(value ? "true" : "false");
}

void thag_rt_println(const char *value) {
    fputs(value == NULL ? "" : value, stdout);
    fputc('\n', stdout);
}

void thagore_println(const char *value) {
    thag_rt_println(value);
}

void thagore_println_i64(int64_t value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%lld", (long long) value);
    thag_rt_println(buffer);
}

void thagore_println_f64(double value) {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%.15g", value);
    thag_rt_println(buffer);
}

void thagore_println_bool(bool value) {
    thag_rt_println(value ? "true" : "false");
}

void thag_rt_flush(void) {
    fflush(stdout);
}

void thagore_flush(void) {
    thag_rt_flush();
}

void thag_rt_eprint(const char *value) {
    fputs(value == NULL ? "" : value, stderr);
}

void thagore_eprint(const char *value) {
    thag_rt_eprint(value);
}

void thagore_eprint_i64(int64_t value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%lld", (long long) value);
    thag_rt_eprint(buffer);
}

void thagore_eprint_f64(double value) {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%.15g", value);
    thag_rt_eprint(buffer);
}

void thagore_eprint_bool(bool value) {
    thag_rt_eprint(value ? "true" : "false");
}

void thag_rt_eprintln(const char *value) {
    fputs(value == NULL ? "" : value, stderr);
    fputc('\n', stderr);
}

void thagore_eprintln(const char *value) {
    thag_rt_eprintln(value);
}

void thagore_eprintln_i64(int64_t value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%lld", (long long) value);
    thag_rt_eprintln(buffer);
}

void thagore_eprintln_f64(double value) {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%.15g", value);
    thag_rt_eprintln(buffer);
}

void thagore_eprintln_bool(bool value) {
    thag_rt_eprintln(value ? "true" : "false");
}

char *thag_rt_read_line(void) {
    return thag_read_until(0, 1);
}

int32_t thag_rt_read_int(void) {
    return (int32_t) thag_parse_i64_token();
}

int64_t thag_rt_read_i64(void) {
    return thag_parse_i64_token();
}

double thag_rt_read_f64(void) {
    return thag_parse_f64_token();
}

char *thag_rt_read_word(void) {
    return thag_read_until(1, 0);
}

char *thag_rt_read_all(void) {
    size_t capacity = 4096;
    size_t length = 0;
    char *buffer = thag_alloc(capacity);
    int ch = thag_next_byte();
    while (ch != EOF) {
        if (length + 1 >= capacity) {
            capacity *= 2;
            buffer = realloc(buffer, capacity);
            if (buffer == NULL) {
                fputs("thagore runtime: allocation failed\n", stderr);
                abort();
            }
        }
        buffer[length++] = (char) ch;
        ch = thag_next_byte();
    }
    buffer[length] = '\0';
    return buffer;
}

thag_array_i32 thag_rt_read_ints(int32_t count) {
    thag_array_i32 result;
    result.len = count < 0 ? 0 : count;
    result.data = thag_alloc(sizeof(int32_t) * (size_t) result.len);
    for (int64_t i = 0; i < result.len; ++i) {
        result.data[i] = thag_rt_read_int();
    }
    return result;
}

thag_array_i64 thag_rt_read_i64s(int32_t count) {
    thag_array_i64 result;
    result.len = count < 0 ? 0 : count;
    result.data = thag_alloc(sizeof(int64_t) * (size_t) result.len);
    for (int64_t i = 0; i < result.len; ++i) {
        result.data[i] = thag_rt_read_i64();
    }
    return result;
}

bool thag_rt_str_eq(const char *left, const char *right) {
    const char *lhs = left == NULL ? "" : left;
    const char *rhs = right == NULL ? "" : right;
    return strcmp(lhs, rhs) == 0;
}

char *thag_rt_concat(const char *left, const char *right) {
    const char *lhs = left == NULL ? "" : left;
    const char *rhs = right == NULL ? "" : right;
    size_t lhs_len = strlen(lhs);
    size_t rhs_len = strlen(rhs);
    char *buffer = thag_alloc(lhs_len + rhs_len + 1);
    memcpy(buffer, lhs, lhs_len);
    memcpy(buffer + lhs_len, rhs, rhs_len);
    buffer[lhs_len + rhs_len] = '\0';
    return buffer;
}

char *thag_rt_trim(const char *value) {
    const char *src = value == NULL ? "" : value;
    size_t start = 0;
    size_t end = strlen(src);
    while (start < end && isspace((unsigned char) src[start])) {
        start++;
    }
    while (end > start && isspace((unsigned char) src[end - 1])) {
        end--;
    }
    return thag_strdup_len(src + start, end - start);
}

char *thag_rt_from_int(int32_t value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return thag_strdup_cstr(buffer);
}

int32_t thag_rt_to_int(const char *value) {
    return (int32_t) strtol(value == NULL ? "0" : value, NULL, 10);
}

int32_t thag_rt_len(const char *value) {
    return (int32_t) strlen(value == NULL ? "" : value);
}

bool thag_rt_contains(const char *value, const char *sub) {
    return strstr(value == NULL ? "" : value, sub == NULL ? "" : sub) != NULL;
}

bool thag_rt_starts_with(const char *value, const char *prefix) {
    const char *src = value == NULL ? "" : value;
    const char *pre = prefix == NULL ? "" : prefix;
    size_t prefix_len = strlen(pre);
    return strncmp(src, pre, prefix_len) == 0;
}

bool thag_rt_ends_with(const char *value, const char *suffix) {
    const char *src = value == NULL ? "" : value;
    const char *suf = suffix == NULL ? "" : suffix;
    size_t src_len = strlen(src);
    size_t suffix_len = strlen(suf);
    if (suffix_len > src_len) {
        return 0;
    }
    return strcmp(src + src_len - suffix_len, suf) == 0;
}

int32_t thag_rt_find(const char *value, const char *sub) {
    const char *src = value == NULL ? "" : value;
    const char *needle = sub == NULL ? "" : sub;
    char *found = strstr(src, needle);
    if (found == NULL) {
        return -1;
    }
    return (int32_t) (found - src);
}

char *thag_rt_replace(const char *value, const char *old_value, const char *new_value) {
    const char *src = value == NULL ? "" : value;
    const char *old_part = old_value == NULL ? "" : old_value;
    const char *new_part = new_value == NULL ? "" : new_value;
    size_t old_len = strlen(old_part);
    size_t new_len = strlen(new_part);
    if (old_len == 0) {
        return thag_strdup_cstr(src);
    }

    size_t count = 0;
    const char *cursor = src;
    while ((cursor = strstr(cursor, old_part)) != NULL) {
        count++;
        cursor += old_len;
    }

    size_t src_len = strlen(src);
    size_t result_len = src_len + count * (new_len - old_len);
    char *result = thag_alloc(result_len + 1);
    char *out = result;
    cursor = src;

    const char *match = strstr(cursor, old_part);
    while (match != NULL) {
        size_t chunk = (size_t) (match - cursor);
        memcpy(out, cursor, chunk);
        out += chunk;
        memcpy(out, new_part, new_len);
        out += new_len;
        cursor = match + old_len;
        match = strstr(cursor, old_part);
    }
    strcpy(out, cursor);
    return result;
}

char *thag_rt_to_upper(const char *value) {
    const char *src = value == NULL ? "" : value;
    size_t len = strlen(src);
    char *result = thag_strdup_len(src, len);
    for (size_t i = 0; i < len; ++i) {
        result[i] = (char) toupper((unsigned char) result[i]);
    }
    return result;
}

char *thag_rt_to_lower(const char *value) {
    const char *src = value == NULL ? "" : value;
    size_t len = strlen(src);
    char *result = thag_strdup_len(src, len);
    for (size_t i = 0; i < len; ++i) {
        result[i] = (char) tolower((unsigned char) result[i]);
    }
    return result;
}

char *thag_rt_pad_left(const char *value, int32_t width, const char *ch) {
    const char *src = value == NULL ? "" : value;
    size_t src_len = strlen(src);
    size_t target = width > (int32_t) src_len ? (size_t) width : src_len;
    char *result = thag_alloc(target + 1);
    size_t padding = target - src_len;
    memset(result, thag_pad_char(ch), padding);
    memcpy(result + padding, src, src_len + 1);
    return result;
}

char *thag_rt_pad_right(const char *value, int32_t width, const char *ch) {
    const char *src = value == NULL ? "" : value;
    size_t src_len = strlen(src);
    size_t target = width > (int32_t) src_len ? (size_t) width : src_len;
    char *result = thag_alloc(target + 1);
    memcpy(result, src, src_len);
    memset(result + src_len, thag_pad_char(ch), target - src_len);
    result[target] = '\0';
    return result;
}

char *thag_rt_repeat(const char *value, int32_t count) {
    const char *src = value == NULL ? "" : value;
    if (count <= 0) {
        return thag_strdup_cstr("");
    }
    size_t src_len = strlen(src);
    size_t total = src_len * (size_t) count;
    char *result = thag_alloc(total + 1);
    char *cursor = result;
    for (int32_t i = 0; i < count; ++i) {
        memcpy(cursor, src, src_len);
        cursor += src_len;
    }
    result[total] = '\0';
    return result;
}

char *thag_rt_reverse(const char *value) {
    const char *src = value == NULL ? "" : value;
    size_t len = strlen(src);
    char *result = thag_alloc(len + 1);
    for (size_t i = 0; i < len; ++i) {
        result[i] = src[len - i - 1];
    }
    result[len] = '\0';
    return result;
}

char *thag_rt_strip(const char *value, const char *chars) {
    const char *src = value == NULL ? "" : value;
    const char *remove = chars == NULL ? "" : chars;
    size_t start = 0;
    size_t end = strlen(src);
    while (start < end && strchr(remove, src[start]) != NULL) {
        start++;
    }
    while (end > start && strchr(remove, src[end - 1]) != NULL) {
        end--;
    }
    return thag_strdup_len(src + start, end - start);
}

char *thag_rt_char_at(const char *value, int32_t index) {
    const char *src = value == NULL ? "" : value;
    size_t len = strlen(src);
    if (index < 0 || (size_t) index >= len) {
        return thag_strdup_cstr("");
    }
    return thag_strdup_len(src + index, 1);
}

double thag_rt_to_f64(const char *value) {
    return strtod(value == NULL ? "0" : value, NULL);
}

char *thag_rt_from_f64(double value) {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%.15g", value);
    return thag_strdup_cstr(buffer);
}

char *thag_rt_from_bool(bool value) {
    return thag_strdup_cstr(value ? "true" : "false");
}

bool thag_rt_to_bool(const char *value) {
    return thag_str_to_bool_impl(value == NULL ? "" : value);
}

bool thag_rt_is_empty(const char *value) {
    return value == NULL || value[0] == '\0';
}

thag_array_str thag_rt_split(const char *value, const char *sep) {
    const char *src = value == NULL ? "" : value;
    const char *delimiter = sep == NULL ? "" : sep;
    size_t delimiter_len = strlen(delimiter);
    thag_array_str result;

    if (delimiter_len == 0) {
        size_t len = strlen(src);
        result.len = (int64_t) len;
        result.data = thag_alloc(sizeof(char *) * len);
        for (size_t i = 0; i < len; ++i) {
            result.data[i] = thag_strdup_len(src + i, 1);
        }
        return result;
    }

    size_t count = 1;
    const char *cursor = src;
    while ((cursor = strstr(cursor, delimiter)) != NULL) {
        count++;
        cursor += delimiter_len;
    }

    result.len = (int64_t) count;
    result.data = thag_alloc(sizeof(char *) * count);
    cursor = src;
    size_t index = 0;
    const char *match = strstr(cursor, delimiter);
    while (match != NULL) {
        result.data[index++] = thag_strdup_len(cursor, (size_t) (match - cursor));
        cursor = match + delimiter_len;
        match = strstr(cursor, delimiter);
    }
    result.data[index] = thag_strdup_cstr(cursor);
    return result;
}

char *thag_rt_join(thag_array_str parts, const char *sep) {
    const char *delimiter = sep == NULL ? "" : sep;
    size_t delimiter_len = strlen(delimiter);
    size_t total = 0;
    for (int64_t i = 0; i < parts.len; ++i) {
        total += strlen(parts.data[i] == NULL ? "" : parts.data[i]);
        if (i + 1 < parts.len) {
            total += delimiter_len;
        }
    }

    char *result = thag_alloc(total + 1);
    char *cursor = result;
    for (int64_t i = 0; i < parts.len; ++i) {
        const char *part = parts.data[i] == NULL ? "" : parts.data[i];
        size_t len = strlen(part);
        memcpy(cursor, part, len);
        cursor += len;
        if (i + 1 < parts.len) {
            memcpy(cursor, delimiter, delimiter_len);
            cursor += delimiter_len;
        }
    }
    *cursor = '\0';
    return result;
}

double thag_rt_pow(double base, double exp) {
    return pow(base, exp);
}

double thag_rt_sqrt(double value) {
    return sqrt(value);
}

double thag_rt_floor(double value) {
    return floor(value);
}

double thag_rt_ceil(double value) {
    return ceil(value);
}

double thag_rt_round(double value) {
    return round(value);
}

double thag_rt_log(double value) {
    return log(value);
}

double thag_rt_log2(double value) {
    return log2(value);
}

double thag_rt_log10(double value) {
    return log10(value);
}

int32_t thag_rt_gcd(int32_t left, int32_t right) {
    int32_t a = left < 0 ? -left : left;
    int32_t b = right < 0 ? -right : right;
    while (b != 0) {
        int32_t next = a % b;
        a = b;
        b = next;
    }
    return a;
}

int32_t thag_rt_lcm(int32_t left, int32_t right) {
    int32_t gcd = thag_rt_gcd(left, right);
    if (gcd == 0) {
        return 0;
    }
    return (left / gcd) * right;
}

bool thag_rt_is_even(int32_t value) {
    return (value % 2) == 0;
}

bool thag_rt_is_odd(int32_t value) {
    return (value % 2) != 0;
}

int64_t thag_now_ms(void);
void thag_sleep_ms(int64_t millis);

int64_t thag_rt_now_ms(void) {
    return thag_now_ms();
}

int64_t thag_rt_monotonic_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&counter)) {
        return thag_now_ms(); // fallback
    }
    return (int64_t) ((counter.QuadPart * 1000LL) / freq.QuadPart);
#else
    struct timespec now;
#ifdef CLOCK_MONOTONIC_RAW
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
#else
    clock_gettime(CLOCK_MONOTONIC, &now);
#endif
    return (int64_t) now.tv_sec * 1000LL + (int64_t) now.tv_nsec / 1000000LL;
#endif
}

void thag_rt_sleep_ms(int64_t millis) {
    thag_sleep_ms(millis);
}

void thag_sleep_ms(int64_t millis) {
    if (millis <= 0) {
        return;
    }
#ifdef _WIN32
    Sleep((DWORD) millis);
#else
    struct timespec req;
    req.tv_sec = millis / 1000;
    req.tv_nsec = (long) ((millis % 1000) * 1000000LL);
    nanosleep(&req, NULL);
#endif
}

int64_t thag_now_ms(void) {
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER value;
    GetSystemTimeAsFileTime(&ft);
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return (int64_t) ((value.QuadPart - 116444736000000000ULL) / 10000ULL);
#else
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return (int64_t) now.tv_sec * 1000LL + (int64_t) now.tv_nsec / 1000000LL;
#endif
}

char *thag_str_concat(const char *a, const char *b) {
    return thag_rt_concat(a, b);
}

void *thag_str_split(const char *s, const char *delim) {
    return thag_wrap_runtime_array(thag_rt_split(s, delim));
}

char *thag_str_join(void *parts, const char *sep) {
    return thag_rt_join(thag_string_array_view((thag_str_array_handle *) parts), sep);
}

void thag_str_split_into(const char *s, const char *delim, int64_t *out_len, char ***out_data) {
    thag_array_str array = thag_rt_split(s, delim);
    if (out_len != NULL) {
        *out_len = array.len;
    }
    if (out_data != NULL) {
        *out_data = array.data;
    }
}

char *thag_str_join_parts(int64_t len, char **data, const char *sep) {
    thag_array_str array;
    array.len = len;
    array.data = data;
    return thag_rt_join(array, sep);
}

char *thag_str_trim(const char *s) {
    return thag_rt_trim(s);
}

int32_t thag_str_contains(const char *s, const char *sub) {
    return thag_rt_contains(s, sub) ? 1 : 0;
}

int32_t thag_str_starts_with(const char *s, const char *prefix) {
    return thag_rt_starts_with(s, prefix) ? 1 : 0;
}

int32_t thag_str_equals(const char *a, const char *b) {
    return thag_rt_str_eq(a, b) ? 1 : 0;
}

int64_t thag_str_len(const char *s) {
    return (int64_t) thag_rt_len(s);
}

char *thag_str_from_int(int64_t n) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%lld", (long long) n);
    return thag_strdup_cstr(buffer);
}

int64_t thag_str_to_int(const char *s) {
    return (int64_t) strtoll(s == NULL ? "0" : s, NULL, 10);
}

char *thag_str_substr(const char *s, int64_t start, int64_t len) {
    const char *src = s == NULL ? "" : s;
    size_t src_len = strlen(src);
    if (start < 0 || len <= 0 || (size_t) start >= src_len) {
        return thag_strdup_cstr("");
    }
    size_t offset = (size_t) start;
    size_t wanted = (size_t) len;
    if (offset + wanted > src_len) {
        wanted = src_len - offset;
    }
    return thag_strdup_len(src + offset, wanted);
}

char *thag_str_replace(const char *s, const char *old_part, const char *new_part) {
    return thag_rt_replace(s, old_part, new_part);
}

char *thag_str_format(const char *fmt, void *args) {
    if (args == NULL) {
        return thag_strdup_cstr(fmt == NULL ? "" : fmt);
    }
    return thag_str_join(args, fmt == NULL ? "" : fmt);
}

void *thag_str_array_new(void) {
    return thag_new_string_array();
}

int32_t thag_str_array_push(void *parts, const char *value) {
    thag_string_array_push_owned((thag_str_array_handle *) parts, thag_strdup_cstr(value == NULL ? "" : value));
    return 1;
}

int32_t thag_str_array_remove(void *parts, int64_t index) {
    thag_str_array_handle *handle = (thag_str_array_handle *) parts;
    if (handle == NULL || index < 0 || index >= handle->len) {
        return 0;
    }
    free(handle->data[index]);
    for (int64_t i = index; i + 1 < handle->len; ++i) {
        handle->data[i] = handle->data[i + 1];
    }
    handle->len--;
    return 1;
}

int32_t thag_str_array_sort(void *parts) {
    thag_str_array_handle *handle = (thag_str_array_handle *) parts;
    if (handle == NULL || handle->len <= 1) {
        return 1;
    }
    qsort(handle->data, (size_t) handle->len, sizeof(char *), thag_compare_strings);
    return 1;
}

int64_t thag_str_array_len(void *parts) {
    thag_str_array_handle *handle = (thag_str_array_handle *) parts;
    return handle == NULL ? 0 : handle->len;
}

char *thag_str_array_get(void *parts, int64_t index) {
    thag_str_array_handle *handle = (thag_str_array_handle *) parts;
    if (handle == NULL || index < 0 || index >= handle->len) {
        return NULL;
    }
    return handle->data[index];
}

int32_t thag_rt_array_len_str(thag_array_str parts) {
    if (parts.len < 0) {
        return 0;
    }
    if (parts.len > INT32_MAX) {
        return INT32_MAX;
    }
    return (int32_t) parts.len;
}

char *thag_rt_array_get_str(thag_array_str parts, int32_t index) {
    if (index < 0 || parts.data == NULL || index >= parts.len) {
        return thag_strdup_cstr("");
    }
    return parts.data[index] == NULL ? thag_strdup_cstr("") : parts.data[index];
}

void *thag_map_new(void) {
    return thag_new_map();
}

int32_t thag_map_put(void *map, const char *key, void *value) {
    thag_map_handle *handle = (thag_map_handle *) map;
    if (handle == NULL || key == NULL) {
        return 0;
    }
    for (int64_t i = 0; i < handle->len; ++i) {
        if (strcmp(handle->keys[i], key) == 0) {
            handle->values[i] = value;
            return 1;
        }
    }
    thag_map_reserve(handle, handle->len + 1);
    handle->keys[handle->len] = thag_strdup_cstr(key);
    handle->values[handle->len] = value;
    handle->len++;
    return 1;
}

void *thag_map_get(void *map, const char *key) {
    thag_map_handle *handle = (thag_map_handle *) map;
    if (handle == NULL || key == NULL) {
        return NULL;
    }
    for (int64_t i = 0; i < handle->len; ++i) {
        if (strcmp(handle->keys[i], key) == 0) {
            return handle->values[i];
        }
    }
    return NULL;
}

int32_t thag_map_is_null_ptr(void *value) {
    return value == NULL ? 1 : 0;
}

void thag_map_free(void *map) {
    thag_map_handle *handle = (thag_map_handle *) map;
    if (handle == NULL) {
        return;
    }
    for (int64_t i = 0; i < handle->len; ++i) {
        free(handle->keys[i]);
    }
    free(handle->keys);
    free(handle->values);
    free(handle);
}

char *thag_fs_read(const char *path) {
    char *content = thag_read_file_impl(path);
    return content == NULL ? NULL : content;
}

int32_t thag_fs_write(const char *path, const char *content) {
    FILE *file = fopen(path == NULL ? "" : path, "wb");
    if (file == NULL) {
        return 0;
    }
    const char *text = content == NULL ? "" : content;
    size_t len = strlen(text);
    size_t written = fwrite(text, 1, len, file);
    fclose(file);
    return written == len ? 1 : 0;
}

int32_t thag_fs_exists(const char *path) {
    struct stat st;
    return stat(path == NULL ? "" : path, &st) == 0 ? 1 : 0;
}

int32_t thag_fs_mkdir(const char *path) {
    if (path == NULL || *path == '\0') {
        return 0;
    }
    char *copy = thag_strdup_cstr(path);
    size_t len = strlen(copy);
    for (size_t i = 1; i < len; ++i) {
        if (copy[i] == '/') {
            copy[i] = '\0';
            mkdir(copy, 0777);
            copy[i] = '/';
        }
    }
    int rc = mkdir(copy, 0777);
    if (rc != 0 && errno != EEXIST) {
        free(copy);
        return 0;
    }
    free(copy);
    return 1;
}

void *thag_fs_readdir(const char *path) {
    DIR *dir = opendir(path == NULL ? "" : path);
    if (dir == NULL) {
        return NULL;
    }
    thag_str_array_handle *items = thag_new_string_array();
    struct dirent *entry = readdir(dir);
    while (entry != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            thag_string_array_push_owned(items, thag_strdup_cstr(entry->d_name));
        }
        entry = readdir(dir);
    }
    closedir(dir);
    return items;
}

int32_t thag_fs_remove(const char *path) {
    return thag_remove_path_impl(path) == 0 ? 1 : 0;
}

char *thag_fs_getcwd(void) {
    char buffer[PATH_MAX];
    if (getcwd(buffer, sizeof(buffer)) == NULL) {
        return thag_strdup_cstr("");
    }
    return thag_strdup_cstr(buffer);
}

char *thag_fs_path_join(const char *a, const char *b) {
    const char *lhs = a == NULL ? "" : a;
    const char *rhs = b == NULL ? "" : b;
    if (*lhs == '\0') {
        return thag_strdup_cstr(rhs);
    }
    if (*rhs == '\0') {
        return thag_strdup_cstr(lhs);
    }
    bool need_sep = lhs[strlen(lhs) - 1] != '/';
    size_t len = strlen(lhs) + strlen(rhs) + (need_sep ? 2 : 1);
    char *joined = thag_alloc(len);
    snprintf(joined, len, need_sep ? "%s/%s" : "%s%s", lhs, rhs);
    return joined;
}

int32_t thag_fs_is_dir(const char *path) {
    struct stat st;
    if (stat(path == NULL ? "" : path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

int64_t thag_fs_filesize(const char *path) {
    struct stat st;
    if (stat(path == NULL ? "" : path, &st) != 0) {
        return 0;
    }
    return (int64_t) st.st_size;
}

int32_t thag_process_run(const char *cmd) {
    int rc = system(cmd == NULL ? "" : cmd);
    if (rc == -1) {
        return 1;
    }
#ifdef _WIN32
    return rc;
#else
    if (WIFEXITED(rc)) {
        return WEXITSTATUS(rc);
    }
    return 1;
#endif
}

char *thag_process_capture(const char *cmd) {
    return thag_capture_command(cmd);
}

char *thag_process_argv(int32_t index) {
    thag_str_array_handle *args = thag_read_cmdline();
    if (index < 0 || index >= args->len) {
        return thag_strdup_cstr("");
    }
    return thag_strdup_cstr(args->data[index]);
}

int32_t thag_process_argc(void) {
    thag_str_array_handle *args = thag_read_cmdline();
    return (int32_t) args->len;
}

char *thag_process_env(const char *name) {
    const char *value = getenv(name == NULL ? "" : name);
    return thag_strdup_cstr(value == NULL ? "" : value);
}

void thag_process_exit(int32_t code) {
    exit(code);
}

void *thag_toml_parse(const char *content) {
    thag_toml_handle *handle = thag_alloc(sizeof(thag_toml_handle));
    handle->content = thag_strdup_cstr(content == NULL ? "" : content);
    handle->section = thag_strdup_cstr("");
    return handle;
}

char *thag_toml_get_str(void *raw_handle, const char *key) {
    return thag_toml_lookup_value((thag_toml_handle *) raw_handle, key);
}

int64_t thag_toml_get_int(void *raw_handle, const char *key) {
    char *value = thag_toml_lookup_value((thag_toml_handle *) raw_handle, key);
    if (value == NULL) {
        return 0;
    }
    int64_t parsed = (int64_t) strtoll(value, NULL, 10);
    free(value);
    return parsed;
}

void *thag_toml_get_section(void *raw_handle, const char *section) {
    thag_toml_handle *source = (thag_toml_handle *) raw_handle;
    if (source == NULL) {
        return NULL;
    }
    thag_toml_handle *next = thag_alloc(sizeof(thag_toml_handle));
    next->content = thag_strdup_cstr(source->content == NULL ? "" : source->content);
    next->section = thag_strdup_cstr(section == NULL ? "" : section);
    return next;
}

void *thag_toml_get_keys(void *raw_handle) {
    thag_toml_handle *handle = (thag_toml_handle *) raw_handle;
    if (handle == NULL || handle->content == NULL) {
        return NULL;
    }
    thag_str_array_handle *keys = thag_new_string_array();
    char *content = thag_strdup_cstr(handle->content);
    char current_section[256];
    current_section[0] = '\0';
    char *line = strtok(content, "\n");
    while (line != NULL) {
        char *comment = strchr(line, '#');
        if (comment != NULL) {
            *comment = '\0';
        }
        char *trimmed = thag_trim_copy(line);
        size_t len = strlen(trimmed);
        if (len >= 2 && trimmed[0] == '[' && trimmed[len - 1] == ']') {
            size_t copy_len = len - 2;
            if (copy_len >= sizeof(current_section)) {
                copy_len = sizeof(current_section) - 1;
            }
            memcpy(current_section, trimmed + 1, copy_len);
            current_section[copy_len] = '\0';
            free(trimmed);
            line = strtok(NULL, "\n");
            continue;
        }
        if (thag_toml_line_matches_section(current_section, handle->section)) {
            char *equals = strchr(trimmed, '=');
            if (equals != NULL) {
                *equals = '\0';
                thag_string_array_push_owned(keys, thag_trim_copy(trimmed));
            }
        }
        free(trimmed);
        line = strtok(NULL, "\n");
    }
    free(content);
    return keys;
}

void thag_toml_free(void *raw_handle) {
    thag_toml_handle *handle = (thag_toml_handle *) raw_handle;
    if (handle == NULL) {
        return;
    }
    free(handle->content);
    free(handle->section);
    free(handle);
}

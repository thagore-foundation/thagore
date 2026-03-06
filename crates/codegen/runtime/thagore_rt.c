#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void thag_rt_print(const char *value) {
    fputs(value == NULL ? "" : value, stdout);
}

void thag_rt_println(const char *value) {
    fputs(value == NULL ? "" : value, stdout);
    fputc('\n', stdout);
}

void thag_rt_flush(void) {
    fflush(stdout);
}

void thag_rt_eprint(const char *value) {
    fputs(value == NULL ? "" : value, stderr);
}

void thag_rt_eprintln(const char *value) {
    fputs(value == NULL ? "" : value, stderr);
    fputc('\n', stderr);
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

int64_t thag_rt_now_ms(void) {
    return 0;
}

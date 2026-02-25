/*
 * glibc_compat.c — GLIBC version compatibility shim.
 *
 * libthag_runtime.a is compiled with GCC 13+ which emits calls to
 * __isoc23_strtol / __isoc23_strtoll (C23 names, GLIBC 2.38+).
 * On older GLIBC these are identical to strtol / strtoll.
 *
 * We provide them here so the final binary links against GLIBC_2.2.5
 * (strtol) instead of GLIBC_2.38 (__isoc23_strtol).
 *
 * Must be linked BEFORE libthag_runtime.a so the linker resolves these
 * symbols from here rather than from libc.
 */
#include <stdlib.h>

long __isoc23_strtol(const char *nptr, char **endptr, int base) {
    return strtol(nptr, endptr, base);
}

long long __isoc23_strtoll(const char *nptr, char **endptr, int base) {
    return strtoll(nptr, endptr, base);
}

/* __libc_single_threaded: GLIBC 2.32 optimization hint.
 * Provide a local definition (value 0 = multi-threaded, safe default). */
char __libc_single_threaded = 0;

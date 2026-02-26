/*
 * thagc_main.cpp — standalone entry point for thagc compiler binary.
 *
 * Links against libthag_runtime.a (the full compiler backend) plus a
 * glibc_compat shim so the binary runs on GLIBC >= 2.17 (Ubuntu 14.04+).
 *
 * This produces a single self-contained thagc binary — no wrapper needed.
 *
 * __thg_cli_main_native() internally execs THAG_HELPER_BIN for LLVM IR
 * generation, then calls clang to link. Both steps need:
 *   - THAG_HELPER_BIN: path to stage1_helper (real codegen backend)
 *   - thag_runtime.lib in CWD (for the clang link step)
 *
 * We resolve THAG_HELPER_BIN relative to the installed bin dir, and ensure
 * thag_runtime.lib is symlinked into CWD from the installed lib dir.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

#if defined(__linux__)
#  include <unistd.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#  include <unistd.h>
#endif

extern "C" void __thg_init_env(int argc, char **argv);
extern "C" int  __thg_cli_main_native(void);

/* Get the directory of the running executable. */
static void get_exe_dir(char *buf, size_t size) {
    char exe[4096] = {0};
#if defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) { buf[0] = 0; return; }
    exe[n] = 0;
#elif defined(__APPLE__)
    uint32_t sz = sizeof(exe);
    if (_NSGetExecutablePath(exe, &sz) != 0) { buf[0] = 0; return; }
#else
    buf[0] = 0; return;
#endif
    /* Find last '/' */
    char *slash = strrchr(exe, '/');
    if (!slash) { buf[0] = 0; return; }
    size_t dlen = (size_t)(slash - exe);
    if (dlen + 1 >= size) dlen = size - 2;
    memcpy(buf, exe, dlen);
    buf[dlen] = 0;
}

/* Ensure THAG_HELPER_BIN is set to stage1_helper alongside this binary. */
static void set_helper_bin(const char *exe_dir) {
    if (getenv("THAG_HELPER_BIN")) return;
    if (!exe_dir || !exe_dir[0]) return;

    char helper[4096];
    snprintf(helper, sizeof(helper), "%s/stage1_helper", exe_dir);

    struct stat st;
    if (stat(helper, &st) == 0 && (st.st_mode & S_IXUSR)) {
        setenv("THAG_HELPER_BIN", helper, 0);
    }
}

/* Ensure thag_runtime.lib is available in CWD by creating a symlink.
 * stage1_helper's link step looks for thag_runtime.lib in CWD.
 * We look for it in: exe_dir/../lib/, exe_dir/, user-overridable env. */
static void ensure_runtime_lib_in_cwd(const char *exe_dir) {
    /* Already exists in CWD? */
    struct stat st;
    if (stat("thag_runtime.lib", &st) == 0 ||
        stat("libthag_runtime.a", &st) == 0) return;

    /* Check env override */
    const char *env_lib = getenv("THAG_RUNTIME_LIB");
    if (env_lib && stat(env_lib, &st) == 0) {
        symlink(env_lib, "thag_runtime.lib");
        return;
    }

    if (!exe_dir || !exe_dir[0]) return;

    /* Try multiple relative paths to handle both flat and nested install layouts:
     *   flat:   ~/.thagore/bin/thagc → lib at ~/.thagore/lib/
     *   nested: ~/.thagore/toolchains/stable/bin/thagc → lib at ~/.thagore/lib/
     */
    char lib_path[4096];
    static const char *names[] = {"thag_runtime.lib", "libthag_runtime.a", NULL};
    static const char *rels[]  = {"/../lib", "/../../lib", "/../../../lib", "/..", ".", NULL};

    for (int r = 0; rels[r]; r++) {
        for (int n = 0; names[n]; n++) {
            snprintf(lib_path, sizeof(lib_path), "%s%s/%s", exe_dir, rels[r], names[n]);
            if (stat(lib_path, &st) == 0) {
                char abs[4096];
                const char *target = realpath(lib_path, abs) ? abs : lib_path;
                symlink(target, "thag_runtime.lib");
                return;
            }
        }
    }
}

/* Prepend lib dirs to LIBRARY_PATH so clang -lstdc++ finds libstdc++.so
 * even on systems where the dev package is not installed.
 * The install layout may be:
 *   ~/.thagore/toolchains/stable/bin/thagc  → exe_dir = .../bin
 *   ~/.thagore/lib/libstdc++.so             → need .../../../lib
 * OR flat:
 *   ~/.thagore/bin/thagc                    → exe_dir = .../bin
 *   ~/.thagore/lib/libstdc++.so             → need ...//../lib
 * We try both.
 */
static void extend_library_path(const char *exe_dir) {
    if (!exe_dir || !exe_dir[0]) return;

    char lib1[4096], lib2[4096], lib3[4096];
    snprintf(lib1, sizeof(lib1), "%s/../lib", exe_dir);       /* flat: bin/../lib */
    snprintf(lib2, sizeof(lib2), "%s/../../lib", exe_dir);    /* nested: bin/../../lib */
    snprintf(lib3, sizeof(lib3), "%s/../../../lib", exe_dir); /* deep: bin/../../../lib */

    const char *cur = getenv("LIBRARY_PATH");
    char newval[8192];
    if (cur && cur[0]) {
        snprintf(newval, sizeof(newval), "%s:%s:%s:%s", lib1, lib2, lib3, cur);
    } else {
        snprintf(newval, sizeof(newval), "%s:%s:%s", lib1, lib2, lib3);
    }
    setenv("LIBRARY_PATH", newval, 1);
}

int main(int argc, char **argv) {
    char exe_dir[4096] = {0};
    get_exe_dir(exe_dir, sizeof(exe_dir));

    set_helper_bin(exe_dir);
    ensure_runtime_lib_in_cwd(exe_dir);
    extend_library_path(exe_dir);

    __thg_init_env(argc, argv);
    return __thg_cli_main_native();
}

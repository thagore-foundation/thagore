/*
 * thagc_main.cpp — standalone entry point for thagc compiler binary.
 *
 * Links against libthag_runtime.a (the full compiler backend) plus a
 * glibc_compat shim so the binary runs on GLIBC >= 2.17 (Ubuntu 14.04+).
 *
 * This produces a single self-contained thagc binary — no wrapper, no
 * stage1_helper subprocess needed. Exactly like rustc.
 */

extern "C" void __thg_init_env(int argc, char **argv);
extern "C" int  __thg_cli_main_native(void);

int main(int argc, char **argv) {
    __thg_init_env(argc, argv);
    return __thg_cli_main_native();
}

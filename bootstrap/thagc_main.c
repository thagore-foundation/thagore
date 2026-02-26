/*
 * thagc_main.c — standalone entry point for thagc compiler binary.
 *
 * Links against libthag_runtime.a which contains the full compiler backend.
 * This replaces the wrapper+stage1_helper two-binary pattern with a single
 * self-contained binary, exactly like rustc.
 *
 * __thg_codegen_emit_llvm_from_source(source_path, output_path, extra_args)
 *   — full pipeline: .tg → parse → IR → object → link via clang
 *
 * __thg_cli_main_native()
 *   — full CLI dispatcher (build/run/test/fix/intent/...) reads from env
 *     vars set by __thg_init_env(argc, argv)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Runtime ABI — defined in libthag_runtime.a */
extern void __thg_init_env(int argc, char **argv);
extern int  __thg_cli_main_native(void);

int main(int argc, char **argv) {
    __thg_init_env(argc, argv);
    return __thg_cli_main_native();
}

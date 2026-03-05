# Where To Change What

Quick map for contributors:

## I need to change parser behavior

- `compiler/include/thagc/frontend/parser.hpp`
- `compiler/src/frontend/parser.cpp`
- `compiler/src/frontend/expr.cpp`
- `compiler/src/frontend/syntax.cpp`

## I need to change type checks

- `compiler/include/thagc/frontend/typechecker.hpp`
- `compiler/src/frontend/builder.cpp`

## I need to change lowering/IR contracts

- `compiler/include/thagc/middleend/core_ir.hpp`
- `compiler/src/middleend/builder.cpp`
- `compiler/include/thagc/mir/mir.hpp`
- `compiler/include/thagc/middleend/mir_lowering.hpp`
- `compiler/src/middleend/mir_lowering.cpp`
- `compiler/include/thagc/middleend/ownership.hpp`
- `compiler/src/middleend/ownership.cpp`
- `compiler/include/thagc/hir/expr.hpp`
- `compiler/include/thagc/hir/typecheck.hpp`
- `compiler/src/middleend/hir_typecheck.cpp`
- `compiler/include/thagc/ty/ty.hpp`

## I need to change LLVM output

- `compiler/include/thagc/backend/llvm_emitter.hpp`
- `compiler/src/backend/expr.cpp`
- `compiler/src/backend/emit.cpp`
- `compiler/src/backend/module.cpp`
- `compiler/src/backend/tokens.cpp`
- `compiler/src/backend/types.cpp`
- `compiler/src/backend/llvm_emitter.cpp`

## I need to change CLI commands

- `compiler/include/thagc/driver/command_router.hpp`
- `compiler/src/driver/core.cpp` (dispatcher)
- `compiler/src/driver/parser.cpp` (command parsing)
- `compiler/src/driver/build.cpp`
- `compiler/src/driver/run.cpp`
- `compiler/src/driver/check.cpp`
- `compiler/src/driver/fmt.cpp`
- `compiler/src/driver/fix.cpp`
- `compiler/src/driver/repl.cpp`
- `compiler/src/driver/lsp.cpp`
- `compiler/src/driver/target.cpp`
- `compiler/src/driver/state.cpp`
- `compiler/src/driver/migrate.cpp`
- `compiler/src/driver/resolver.cpp`

## I need to update parity baseline contracts

- `tooling/baseline/*`
- `contracts/*`
- `tests/parity/*`

## I need to update HM inference / generics gates (v2.3)

- `tests/inference/*`
- `tests/generics/*`
- `compiler/src/middleend/hir_typecheck.cpp`
- `compiler/src/frontend/builder.cpp`

## I need to update ownership / borrow gates (v2.4)

- `tests/ownership/*`
- `tests/parity/test_v24_mir_ownership_pipeline.py`
- `compiler/include/thagc/mir/mir.hpp`
- `compiler/src/middleend/ownership.cpp`

## I need to change runtime concurrency / cancel / timeout

- `runtime/include/thag_runtime.h`
- `runtime/src/concurrency.cpp`
- `contracts/concurrency/*`
- `tests/integration/*` and `tests/soak/*`

## I need to change stdlib client surfaces (HTTP/WebSocket/DB/time/map)

- `stdlib/lib/*`
- `stdlib/std/*`
- `contracts/io/*`

## I need to update release/policy automation

- `.github/workflows/*`
- `tooling/policy/*`
- `tooling/packaging/*`
- `tooling/community/*`

# Where To Change What

Quick map for contributors:

## I need to change parser behavior

- `compiler/include/thagc/frontend/parser.hpp`
- `compiler/src/frontend/ir.cpp`

## I need to change type checks

- `compiler/include/thagc/frontend/typechecker.hpp`
- `compiler/src/frontend/builder.cpp`

## I need to change lowering/IR contracts

- `compiler/include/thagc/middleend/core_ir.hpp`
- `compiler/src/middleend/builder.cpp`

## I need to change LLVM output

- `compiler/include/thagc/backend/llvm_emitter.hpp`
- `compiler/src/backend/ir.cpp`

## I need to change CLI commands

- `compiler/include/thagc/driver/command_router.hpp`
- `compiler/src/driver/core.cpp` (dispatcher)
- `compiler/src/driver/parser.cpp` (command parsing)
- `compiler/src/driver/build.cpp`
- `compiler/src/driver/run.cpp`
- `compiler/src/driver/fix.cpp`
- `compiler/src/driver/intent.cpp`
- `compiler/src/driver/state.cpp`
- `compiler/src/driver/install.cpp`
- `compiler/src/driver/target.cpp`
- `compiler/src/driver/update.cpp`
- `compiler/src/driver/flow.cpp`

## I need to update parity baseline contracts

- `tooling/baseline/*`
- `contracts/*`
- `tests/parity/*`

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

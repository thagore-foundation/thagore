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
- `compiler/src/driver/core.cpp`
- `compiler/src/driver/builder.cpp`

## I need to update parity baseline contracts

- `tooling/baseline/*`
- `contracts/*`
- `tests/parity/*`

## I need to update release/policy automation

- `.github/workflows/*`
- `tooling/policy/*`
- `tooling/packaging/*`

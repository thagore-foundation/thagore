# Thagore

A statically-typed compiled language powered by LLVM, with a Stage1-only bootstrap policy.

## Getting Started

### Prerequisites

- LLVM/Clang 21+
- Python 3.x
- Stage1 seed compiler (`stage1` or `stage1.exe`)
- Runtime ABI library (`thag_runtime.lib` or `libthag_runtime.a`)

### Bootstrap

```bash
cmd /c scripts\bootstrap.bat
```

This runs Stage1 -> Stage2 -> Stage2b and validates sample output.
`scripts/build_runtime_abi.py` is executed to validate/materialize runtime ABI libraries before link.
In CI/Seed workflows, runtime ABI artifacts are published/consumed as:
- `thagore-runtime-windows.lib`
- `thagore-runtime-linux.a`
- `thagore-runtime-macos.a`

## Architecture

Pipeline:

`Lexer token stream -> Parser AST -> Typechecker Typed-IR -> Lowering Core-IR -> LLVM emitter`

Key modules:

- `src/frontend/native/lexer.tg`
- `src/frontend/native/parser.tg`
- `src/frontend/native/typechecker.tg`
- `src/frontend/lowering/pipeline.tg`
- `src/backend/native/emitter.tg`
- `src/cli/thagore.tg`

## Tests

```bash
stage2.exe build tests/test_syntax_matrix.tg -o syntax_matrix.exe
syntax_matrix.exe
python scripts/intent_suite.py --cli stage2.exe
```

## Benchmarks

```bash
python scripts/benchmark_fib.py
python scripts/benchmark_intent.py --cli stage2.exe --runs 7
```

## Policy

- No Stage0 fallback in CI/Selfhost/Release.
- No C++ runtime build path in workflows/scripts.
- Runtime linking is fail-hard when runtime ABI library is missing.

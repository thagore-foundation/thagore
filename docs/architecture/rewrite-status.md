# Rewrite Status

This dashboard tracks the current Rust toolchain in this repository.

Current release cut: `v0.9.0`.

## Frontend

- Lexer: implemented
- Parser: implemented
- Typechecker parity: implemented
- Generic function monomorphisation: implemented
- Return type inference: implemented
- Builtin root-scope functions: implemented

## Middleend

- Module graph and import table: implemented
- Session-based per-module compilation: implemented
- Typed IR model: implemented
- Per-module lowering: implemented
- Incremental object reuse: implemented baseline

## Backend

- LLVM IR emission: implemented
- Object emission: implemented
- Linker selection (`mold` -> `lld` -> `cc`): implemented
- Embedded runtime source for installed binaries: implemented

## Driver / CLI groups

- `thagc build`: implemented
- `thagc run`: implemented
- `thagc check`: implemented
- `thagc --json-errors`: implemented
- `thagc --print-target-list`: implemented with the release target matrix
- Legacy flatten pipeline: retained behind `--legacy-flatten`
- `thagore-fmt`: implemented
- `thagore-lsp`: implemented
- Playground WASM bridge: implemented

## Quality gates

- Unit tests: implemented
- Integration tests: implemented
- E2E fixture matrix: implemented
- Generic and const fixtures: implemented
- Playground WASM build: implemented
- Release packaging scripts: implemented
- Release and nightly GitHub Actions: implemented

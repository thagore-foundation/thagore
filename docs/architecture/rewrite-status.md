# Rewrite Status

This dashboard tracks full compiler rewrite progress against `contracts/manifest.json`.

Roadmap to product milestone `v2.5`: `ROADMAP.md`.
Current release cut: `v2.5.0` (production-complete baseline: query cache + explain + LSP hover/diagnostics).

## Frontend

- Lexer: implemented
- Parser: implemented
- Typechecker parity: implemented (HIR-assisted bidirectional checks)

## Middleend

- Core IR model: implemented
- Lowering parity: implemented
- HIR/type layer: implemented (`compiler/include/thagc/hir`, `compiler/include/thagc/ty`)
- HM inference/unifier: implemented baseline (`Unifier`, ty-level infer/check in HIR)
- Generic type validation: implemented for `Option<T>` / `Result<T,E>` inference/check paths
- MIR ownership layer: implemented baseline (`compiler/include/thagc/mir/mir.hpp`, `compiler/src/middleend/ownership.cpp`)

## Backend

- LLVM IR emission: implemented
- Object emission: implemented
- Link planner (no legacy runtime binding): implemented

## Driver / CLI groups

- Query incremental parse cache: implemented baseline (`compiler/include/thagc/query/query.hpp` + `parse_file` wiring in `compiler/src/driver/builder.cpp`)
- build: implemented (milestone quality)
- run: implemented (milestone quality)
- check: implemented (parse/type/lowering/codegen validation without final link)
- fmt: implemented (source normalization lane)
- fix: implemented (safe autofix lane for syntax normalization and missing block colon)
- repl: implemented (interactive execution lane)
- lsp: implemented (`--stdio` with completion + definition + hover + diagnostics)
- target: implemented (target init/list/show)
- state: implemented (typestate explain/doctor)
- migrate: implemented (legacy manifest/lock to drago format)

## Quality gates

- Unit tests: implemented
- Integration tests: implemented
- E2E parity: implemented
- Inference + generics suites: implemented (`tests/inference`, `tests/generics`)
- Ownership suites: implemented (`tests/ownership`)
- Deterministic gate: implemented
- Soak gate: implemented
- 3-OS matrix release gate: implemented

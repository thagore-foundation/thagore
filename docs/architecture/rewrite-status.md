# Rewrite Status

This dashboard tracks full compiler rewrite progress against `contracts/manifest.json`.

Roadmap to product milestone `v2.5`: `ROADMAP.md`.
Current release cut: `v2.2.0` (HIR + bidirectional type-checking baseline).

## Frontend

- Lexer: implemented
- Parser: implemented
- Typechecker parity: implemented (HIR-assisted bidirectional checks)

## Middleend

- Core IR model: implemented
- Lowering parity: implemented
- HIR/type layer: implemented (`compiler/include/thagc/hir`, `compiler/include/thagc/ty`)

## Backend

- LLVM IR emission: implemented
- Object emission: implemented
- Link planner (no legacy runtime binding): implemented

## Driver / CLI groups

- build: implemented (milestone quality)
- run: implemented (milestone quality)
- check: implemented (parse/type/lowering/codegen validation without final link)
- fmt: implemented (source normalization lane)
- fix: implemented (safe autofix lane for syntax normalization and missing block colon)
- repl: implemented (interactive execution lane)
- lsp: implemented (`--stdio` MVP with keyword completion + text-search definition lookup)
- target: implemented (target init/list/show)
- state: implemented (typestate explain/doctor)
- migrate: implemented (legacy manifest/lock to drago format)

## Quality gates

- Unit tests: implemented
- Integration tests: implemented
- E2E parity: implemented
- Deterministic gate: implemented
- Soak gate: implemented
- 3-OS matrix release gate: implemented

# Rewrite Status

This dashboard tracks full compiler rewrite progress against `contracts/manifest.json`.

Roadmap to product milestone `v1.5`: `docs/architecture/v1.5-roadmap.md`.
Current release cut: `v1.5.0` (Stable Release).

## Frontend

- Lexer: in progress
- Parser: in progress
- Typechecker parity: in progress

## Middleend

- Core IR model: in progress
- Lowering parity: in progress

## Backend

- LLVM IR emission: in progress
- Object emission: in progress
- Link planner (no legacy runtime binding): in progress

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

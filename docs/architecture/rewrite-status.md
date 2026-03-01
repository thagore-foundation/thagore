# Rewrite Status

This dashboard tracks full compiler rewrite progress against `contracts/manifest.json`.

Roadmap to product milestone `v1.5`: `docs/architecture/v1.5-roadmap.md`.
Current release cut: `v1.1.0` (Structured Concurrency GA).

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
- migrate: implemented (legacy manifest/lock to drago format)

## Quality gates

- Unit tests: implemented
- Integration tests: implemented
- E2E parity: implemented
- Deterministic gate: implemented
- Soak gate: implemented
- 3-OS matrix release gate: in progress

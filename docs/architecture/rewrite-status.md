# Rewrite Status

This dashboard tracks full compiler rewrite progress against `contracts/manifest.json`.

Roadmap to product milestone `v1.5`: `docs/architecture/v1.5-roadmap.md`.
Current release cut: `v0.6.0` (Concurrency Primitives Alpha).

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
- test: implemented (compile+run lane with workspace/json/fail-fast controls)
- fix: implemented (workspace apply/explain/rollback lanes)
- intent: implemented (doctor/explain/lock lane build verification)
- state: implemented (doctor/explain lane build verification)
- install: implemented (toolchain root + default target manifest bootstrap)
- target: implemented (manifest-based add/list/show/remove/doctor)
- update: implemented (check/apply/rollback with `--dry-run` and `--yes`)
- flow: implemented (doctor/explain/simulate + journal recover)

## Quality gates

- Unit tests: implemented
- Integration tests: implemented
- E2E parity: implemented
- Deterministic gate: implemented
- Soak gate: implemented
- 3-OS matrix release gate: in progress

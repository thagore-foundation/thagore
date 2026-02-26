# Rewrite Status

This dashboard tracks full compiler rewrite progress against `contracts/manifest.json`.

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
- test: implemented (compile+run lane, needs full suite semantics)
- fix: pending
- intent: pending
- state: pending
- install: pending
- target: pending
- update: pending
- flow: pending

## Quality gates

- Unit tests: partial
- Integration tests: partial
- E2E parity: partial
- Deterministic gate: partial
- Soak gate: pending
- 3-OS matrix release gate: pending


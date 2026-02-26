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
- fix: implemented (command skeleton + io behavior)
- intent: implemented (doctor/explain/lock lane build verification)
- state: implemented (doctor/explain lane build verification)
- install: implemented (toolchain marker lane)
- target: implemented (list/add/remove/ensure/doctor skeleton)
- update: implemented (check/apply/rollback skeleton)
- flow: implemented (doctor/explain/simulate/recover skeleton)

## Quality gates

- Unit tests: partial
- Integration tests: partial
- E2E parity: partial
- Deterministic gate: partial
- Soak gate: pending
- 3-OS matrix release gate: pending

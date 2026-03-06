# Contracts

This directory stores frozen parity contracts used to validate behavior against the baseline branch.

- `cli/`: command and diagnostics contract snapshots.
- `grammar/`: token/syntax and precedence contracts.
- `semantics/`: type/lowering behavior contracts.
- `concurrency/`: structured concurrency contracts.
  - includes `p0_p1_registry.json` gate (must have zero open P0/P1 issues).
- `memory/`: `Rc`/`Arc` and `Send`/`Sync` contracts.
- `io/`: HTTP/WebSocket/DB client surface contracts.
- `deploy/`: artifact/startup/footprint/cross-compile contracts.
- `perf/`: startup/binary-size/compile-latency performance budgets.

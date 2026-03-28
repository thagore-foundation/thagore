# Bootstrap Seed

This project is the first self-written bootstrap seed in Thagore.

It is intentionally small and limited to the bootstrap-safe language surface,
but it is more compiler-like than the generic `bootstrap_probe`:

- reads a `.tg` source file from argv
- normalizes line endings
- scans identifiers, numbers, and a small punctuation surface
- summarizes a function signature and first local binding from the token stream
- collects a tiny symbol table for functions and locals
- emits diagnostics-lite for missing function/return structure
- resolves flat and dotted imports, plus simple `from ... import ...`, from the working directory
- checks simple semantic issues like missing `main`, unknown callees, unknown import alias usage, call arity mismatches, primitive and simple call-based return/local/assignment type mismatches, assignment to unknown locals, non-bool conditions, duplicate imports and import aliases, duplicate imported symbols, local/func shadowing over imports and import aliases including later locals, unknown return identifiers, duplicate locals, duplicate funcs, and invalid imported symbols
- tracks line/column positions
- emits a deterministic token report

The point is not to replace `thagc` yet. The point is to prove that a
frontend-style helper can live in Thagore itself and stay under CI.

The reusable extraction target now starts under:

- `bootstrap/selfhost/frontend/`

`tests/bootstrap_seed/` remains the harness and regression corpus until the
self-host frontend slice fully switches over to those reusable modules.

The executable used by the bootstrap-seed CLI tests now builds from:

- `bootstrap/selfhost/frontend/main.tg`

The bootstrap-seed driver/output manifests now live under:

- `bootstrap/selfhost/corpus/bootstrap-analyze.txt`
- `bootstrap/selfhost/corpus/bootstrap-desugar.txt`
- `bootstrap/selfhost/corpus/bootstrap-report.txt`

The old seed-local copies of `chars.tg`, `lexer.tg`, `parser.tg`, `symbols.tg`,
`resolver.tg`, `diagnostics.tg`, and `main.tg` have been removed so reusable
frontend logic lives in only one place.

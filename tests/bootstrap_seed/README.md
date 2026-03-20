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
- tracks line/column positions
- emits a deterministic token report

The point is not to replace `thagc` yet. The point is to prove that a
frontend-style helper can live in Thagore itself and stay under CI.

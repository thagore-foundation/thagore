# Selfhost Frontend Differential Corpus

This corpus is the first narrow differential gate between:

- the Rust-hosted frontend surfaced through `thagc check`
- the Thagore-authored frontend slice under `bootstrap/selfhost/frontend/`

Scope:

- small core-syntax files only
- no top-layer sugar
- compare normalized frontend categories, not full AST parity yet
- assignment-target errors are normalized into `unknown identifier`
- unknown callee errors are normalized into `unknown identifier`
- assignment-from-call errors are normalized into `type mismatch`
- local binding errors are normalized into `type mismatch`
- return-from-call errors are normalized into `return type mismatch`
- selected fixtures also have golden `dump-report` outputs for richer parity
- bootstrap-seed fixtures now also gate `dump-report` for module-kind-sensitive
  cases (`library` vs synthesized executable root)
- `differential_corpus.txt` is now the contract for the narrow `thagc check`
  replacement surface
- `stage_chain_corpus.txt` is now the contract for the reusable
  `scan -> parse -> check` stage lane
- the contract now includes an explicit library-mode success fixture
  (`ok_library_module.tg`) so replacement confidence is not limited to
  executable-root files
- the contract now also includes import-resolution success fixtures in both
  executable and library mode, so Target 01 is no longer limited to local-only
  files
- the contract now includes a missing-import executable fixture, so module
  surface parity covers both success and failure paths

Current categories:

- `ok`
- `call arity mismatch`
- `type mismatch`
- `unknown identifier`
- `missing import`
- `condition type mismatch`
- `return type mismatch`

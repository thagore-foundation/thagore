# Selfhost Frontend Differential Corpus

This corpus is the first narrow differential gate between:

- the Rust-hosted frontend surfaced through `thagc check`
- the Thagore-authored frontend slice under `bootstrap/selfhost/frontend/`

Scope:

- small core-syntax files only
- no top-layer sugar
- compare normalized frontend categories, not full AST parity yet

Current categories:

- `ok`
- `call arity mismatch`
- `type mismatch`
- `unknown identifier`
- `condition type mismatch`
- `return type mismatch`

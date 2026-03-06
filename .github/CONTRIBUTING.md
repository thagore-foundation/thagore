# Contributing to Thagore

Thank you for contributing to the Thagore compiler and language toolchain.

**Read [AGENTS.md](../AGENTS.md) in full before making any change.** It is the authoritative
policy for this repository and takes precedence over all other conventions.

---

## Quick Start

**Prerequisite:** Rust stable toolchain, edition 2024.

```bash
cargo build --workspace
cargo test --workspace
```

---

## Workspace Layout

| Crate / Tool | Role | Status |
|---|---|---|
| `crates/lexer` | DFA tokeniser, string interning, error recovery | Implemented |
| `crates/ast` | Arena-allocated AST node types (`bumpalo`) | Implemented |
| `crates/parser` | Recursive-descent parser with error recovery | Implemented |
| `crates/typeck` | Scope analysis, type inference, structured diagnostics | Implemented |
| `crates/ir` | Intermediate representation | Scaffold |
| `crates/codegen` | Code generation backend (`inkwell` / LLVM) | Scaffold |
| `tools/thagore-cli` | CLI driver binary `thagore-cli` (`clap`) | In progress |
| `tools/thagore-lsp` | Language server binary `thagore-lsp` (`tower-lsp`) | In progress |

### Crate Dependency Graph

```
lexer   ─────────────────────────┐
ast     ─────────────────────────┤
                                 ↓
               parser ──── (ast, lexer)
               typeck ──── (ast, lexer)
               ir     ──── (ast, typeck)
               codegen ─── (ir)
               tools/* ─── (any crate above)
```

No crate may depend on a `tools/*` crate. Circular dependencies are a hard error.

### Approved External Dependencies (per crate)

| Crate | Approved externals |
|---|---|
| `lexer` | `phf`, `bumpalo` |
| `ast` | `bumpalo` |
| `parser` | — |
| `typeck` | `indexmap` |
| `ir` | — |
| `codegen` | `inkwell` |
| `tools/thagore-cli` | `clap` |
| `tools/thagore-lsp` | `tower-lsp` |

Every new external dependency requires an inline comment in `Cargo.toml` explaining why it
is needed.

---

## Testing

Tests live in `tests/{crate}_tests.rs` next to each crate's `src/` directory.
Do **not** place test code inside `src/` files.

```bash
# All tests
cargo test --workspace

# Single crate
cargo test -p thagore-typeck

# With output
cargo test -p thagore-parser -- --nocapture
```

Every public function must have at least one unit test. Required edge-case coverage per
crate is defined in `AGENTS.md` section 9.

---

## Commits

Every commit must follow this exact format (from `AGENTS.md` section 8):

```
<type>(<crate>): <short description>

- bullet point of what changed
- bullet point of why

Debt: <DEBT.md ref, or "none">
Validation: <what was run, e.g. "cargo test -p thagore-typeck">
```

Valid types: `feat`, `fix`, `refactor`, `test`, `docs`, `chore`, `debt`.

Never commit without running `cargo test` on the affected crate(s).

---

## Technical Debt

Any workaround, known limitation, or temporary fix must be logged in `DEBT.md` at the
project root before the change is committed. Use the format:

```
- [ ] [crate_name] Short description
      Introduced: <commit hash>
      Fix: <what needs to happen to resolve it>
```

Never introduce debt silently.

---

## Pull Request Checklist

- [ ] `cargo build --workspace` passes with no warnings.
- [ ] `cargo test --workspace` passes.
- [ ] No circular dependency introduced (verify with `cargo check --workspace`).
- [ ] Tests added or updated in `tests/{crate}_tests.rs`.
- [ ] `DEBT.md` updated if any debt was introduced or resolved.
- [ ] Docs updated if public-facing behaviour changed.
- [ ] Commit message follows the required format (Debt + Validation fields present).
- [ ] PR title follows Conventional Commits format.
- [ ] SemVer impact noted in the PR description (`major` / `minor` / `patch`).

Use [PULL_REQUEST_TEMPLATE.md](PULL_REQUEST_TEMPLATE.md) as the PR description base.

---

## Reference Documents

| Document | Path |
|---|---|
| Project policy | `AGENTS.md` |
| Technical debt register | `DEBT.md` |
| Static Intent Engine RFC | `docs/idea/intent.md` |
| Typestate RFC | `docs/idea/typestate.md` |
| Flow RFC | `docs/idea/flow.md` |
| PR template | `.github/PULL_REQUEST_TEMPLATE.md` |

---

## Code of Conduct

All contributors are expected to follow the [Code of Conduct](CODE_OF_CONDUCT.md).
Incidents may be reported to **support@thagore.io.vn**.

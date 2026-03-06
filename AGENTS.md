# AGENTS.md

This file is law. AI coding agents must follow it before creating, moving, renaming, or editing files in this repository.

## 1. Project Structure Law

- All compiler passes live in `crates/` as independent workspace members.
- All developer tooling lives in `tools/`.
- Standard library source lives in `stdlib/`.
- Integration and end-to-end tests live in `tests/fixtures/` as `.tg` files.
- No file may be created outside these designated locations without explicit approval.

## 2. File Naming Law

- All Rust source files must use `snake_case.rs`.
- All Thagore source files must use `snake_case.tg`.
- No abbreviations are allowed unless they appear in this approved list:
  - `lexer`
  - `typeck`
  - `codegen`
  - `ir`
  - `lsp`
  - `cli`
- Test files must be named `{module}_tests.rs`.
- Test files must live in `tests/` next to the corresponding `src/` directory.

## 3. Crate Dependency Law

- `lexer` must not depend on any other Thagore crate.
- `parser` may depend only on `lexer` and `ast`.
- `ast` must not depend on any other Thagore crate.
- `typeck` may depend only on `ast` and `lexer`.
- `ir` may depend only on `ast` and `typeck`.
- `codegen` may depend only on `ir`.
- `tools/*` may depend on any crate, but no crate may depend on `tools/*`.
- Circular dependencies are a hard error. Never introduce them.

## 4. New File Checklist

Before creating any new file, the agent must confirm:

- [ ] The file belongs in the correct designated directory.
- [ ] The file follows the naming convention.
- [ ] If this is a new crate, `Cargo.toml` is created and added to the workspace members list in the root `Cargo.toml`.
- [ ] If this is a new crate, a stub `lib.rs` with a doc comment is created.
- [ ] No existing file is silently overwritten.

## 5. Forbidden Actions

- Never create files in the project root except `Cargo.toml`, `Cargo.lock`, `AGENTS.md`, and `README.md`.
- Never use `mod.rs`. Use `module_name.rs` at the same level instead.
- Never place test code inside `src/` files. Tests belong in `tests/`.
- Never commit `target/` or any build artifact.
- Never add a dependency to any crate without updating the corresponding `Cargo.toml`.

## 6. Scaffold Law

When scaffolding a new crate that is not yet implemented:

- Create `Cargo.toml` with the correct crate name and version.
- Create `src/lib.rs` containing exactly:

```rust
//! Scaffold — not yet implemented.
```

- Do not generate placeholder logic, dummy structs, or TODO functions.

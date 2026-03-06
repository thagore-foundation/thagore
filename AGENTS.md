# AGENTS.md

This file is law. Every coding agent must read it in full before making any change to this repository. If an action is not clearly allowed here or by an explicit user instruction in the current conversation, stop and ask.

## 1. Project Structure Law

- All compiler passes live in `crates/` as independent workspace members.
- All developer tooling lives in `tools/`.
- Standard library source lives in `stdlib/`.
- Integration and end-to-end tests live in `tests/fixtures/` as `.tg` files.
- No file may be created outside these designated locations without explicit approval.
- Root-level exceptions are governed by section 5 only.

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
- Test files must be named `{crate}_tests.rs`.
- Test files must live in `tests/` next to the corresponding `src/` directory.

## 3. Crate Dependency Law

- `lexer` must not depend on any other Thagore crate.
- `parser` may depend only on `ast` and `lexer`.
- `ast` must not depend on any other Thagore crate.
- `typeck` may depend only on `ast` and `lexer`.
- `ir` may depend only on `ast` and `typeck`.
- `codegen` may depend only on `ir`.
- `tools/*` may depend on any crate, but no crate may depend on `tools/*`.
- Circular dependencies are a hard error. Never introduce them.

## 4. New File Checklist

Before creating any new file, the agent must confirm every item below:

- [ ] The file belongs in the correct designated directory.
- [ ] The file follows the naming convention.
- [ ] If this is a new crate, `Cargo.toml` is created and added to the workspace members list in the root `Cargo.toml`.
- [ ] If this is a new crate, a stub `src/lib.rs` with the required doc comment is created.
- [ ] If this is a new test file, it lives in `tests/` and is named `{crate}_tests.rs`.
- [ ] No existing file is silently overwritten.
- [ ] If the file introduces technical debt, a matching entry is added to `DEBT.md` before the change is considered complete.

## 5. Forbidden Actions

- Never create files in the project root except `Cargo.toml`, `Cargo.lock`, `AGENTS.md`, `README.md`, and `DEBT.md`.
- Never use `mod.rs`. Use `module_name.rs` at the same level instead.
- Never place test code inside `src/` files. Tests belong in `tests/`.
- Never commit `target/`, generated files, build artifacts, or `.DS_Store`.
- Never add a dependency to any crate without updating the corresponding `Cargo.toml`.
- Never rewrite an existing file unless the current conversation explicitly authorizes it.

## 6. Scaffold Law

When scaffolding a new crate that is not yet implemented:

- Create `Cargo.toml` with the correct crate name and version.
- Create `src/lib.rs` containing exactly:

```rust
//! Scaffold — not yet implemented.
```

- Do not generate placeholder logic.
- Do not generate dummy structs.
- Do not generate TODO functions.

## 7. Technical Debt Tracking Law

- Any workaround, temporary fix, or known limitation introduced by an agent must be logged in `DEBT.md` at the project root.
- An agent must never introduce technical debt silently. Log it or do not introduce it.
- Every debt entry must use this exact format:

```text
- [ ] [crate_name] Short description of the debt
      Introduced: <commit hash or PR>
      Fix: <what needs to happen to resolve it>
```

- `Introduced:` must name the commit hash or PR that created the debt. Use `uncommitted` only before the first commit of the change, then replace it before completion.
- `Fix:` must describe the concrete action required to remove the debt.
- If a debt item is resolved in the same change that introduced it, do not add it to `DEBT.md`.
- `DEBT.md` is mandatory repository state. Agents must update it when debt status changes.

## 8. Commit Law

- Every meaningful change must be committed immediately once the repository is stable and validated.
- Every commit message must follow this exact format:

```text
<type>(<crate>): <short description>

- bullet point of what changed
- bullet point of why

Debt: <DEBT.md ref if applicable, else "none">
Validation: <what was run, e.g. cargo test -p thagore-lexer>
```

- Valid `<type>` values are:
  - `feat`
  - `fix`
  - `refactor`
  - `test`
  - `docs`
  - `chore`
  - `debt`
- Never commit without running `cargo test` on the affected crate.
- If multiple crates are affected, run `cargo test` for each affected crate.
- If no crate is affected, run the most relevant validation for the change and state it exactly in `Validation:`.
- Never commit generated files, `target/`, or `.DS_Store`.
- Never use vague commit subjects.
- Never combine unrelated changes in one commit.

## 9. Testing Law

- Every crate must maintain a `tests/{crate}_tests.rs` file.
- Every public function must have at least one unit test.
- Every meaningful change must add or update tests unless the user explicitly forbids it.
- Required edge cases per crate are mandatory coverage:
  - `lexer`: TAB detection, invalid indent width, unterminated strings, EOF dedent flush, all keywords.
  - `parser`: missing parens on `if` and `while`, missing `:`, DEDENT mismatch, all declaration forms.
  - `ast`: visitor traversal, `Display` round-trip, every node type constructed.
  - `typeck`: type mismatch, unknown identifier, missing return type, inference on `let`.
  - `ir`: lowering of every AST node type.
  - `codegen`: LLVM IR validity, output for every primitive type.
- Fuzz testing via `cargo-fuzz` is required before any crate is marked production-stable.
- A crate is not production-stable until its fuzz target exists, runs, and is documented.

## 10. Dependency Law

- No new external crate dependency may be added without an inline comment in `Cargo.toml` explaining why that dependency is required.
- Agents may not add dependencies outside the approved sets below without explicit justification in the change and in the `Cargo.toml` comment.
- Approved dependencies per crate:
  - `lexer`: `phf`, `bumpalo`
  - `ast`: `bumpalo`
  - `parser`: `thagore-lexer`, `thagore-ast`
  - `typeck`: `thagore-ast`, `thagore-lexer`
  - `ir`: `thagore-ast`, `thagore-typeck`
  - `codegen`: `thagore-ir`, `inkwell`
  - `tools/thagore-cli`: any Thagore crate, `clap`
  - `tools/thagore-lsp`: any Thagore crate, `tower-lsp`
- Approved does not mean optional. Add only what the crate actually needs.
- Dependency law supplements section 3. Both must be satisfied.

## 11. Agent Behavior Law

- Before writing any code, the agent must read this file in full.
- Before creating a file, the agent must complete the New File Checklist in section 4.
- Before adding a dependency, the agent must check section 10.
- If the agent is unsure whether an action is allowed, it must stop and ask. Never assume.
- The agent must never silently skip a validation step.
- The agent must never silently skip a debt update.
- The agent must never rewrite a file that already exists without being explicitly told to do so in the current conversation.
- The agent must never silently preserve a known rule violation. It must either fix it, log it in `DEBT.md`, or stop and ask.
- The agent must treat this file as stricter than habit, preference, or default tool behavior.

## 12. Enforcement

- Any change that violates this file is invalid.
- Any commit that violates this file is invalid.
- If two rules conflict, follow this order:
  - explicit user instruction in the current conversation
  - this `AGENTS.md`
  - all other repository defaults

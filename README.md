# Thagore

Thagore is a statically typed, compiled programming language designed for expressive safety
guarantees and high-performance code generation. Source files use the `.tg` extension.

> **This branch is the clean-slate Rust rewrite of the compiler toolchain.**
> The legacy implementation is preserved on `codex/legacy-codebase`.

---

## Language Design Goals

### `intent` — Static Intent Engine

Describe *what* a function should achieve; the compiler selects and verifies the most
efficient deterministic implementation at build time. No runtime inference, no network
dependency.

```tg
intent func dedup_sorted(xs: [i32]) -> [i32]:
    goal: deduplicate_sorted
    constraints:
        time <= O(n)
        deterministic == true
```

### `typestate` — Compile-time Lifecycle Safety

Annotate API boundaries with `Type[State]` to let the compiler reject invalid call sequences.
Unannotated types are unaffected — the feature is fully opt-in.

```tg
state Session:
    Init
    Ready
    Closed

func open(cfg: Config) -> Session[Ready]:  ...
func send(s: Session[Ready], msg: String) -> Session[Ready]:  ...
func close(s: Session[Ready]) -> Session[Closed]:  ...
```

### `flow` — Saga as Language Primitive

Express multi-step side effects with built-in compensation, retry, timeout, and crash
recovery directly in source code.

```tg
flow deploy(input: DeployInput) -> Result<DeployOut, DeployErr>:
    step vm = cloud.create_vm(input.spec)
        undo cloud.delete_vm(vm.id)
        retry 2 backoff exp(200ms, 2.0)
        timeout 20s
        idempotent

    step pkg = artifact.upload(input.bundle)
        undo artifact.delete(pkg.id)
        timeout 30s

    return Ok(DeployOut(vm.id, pkg.id))
```

---

## Rewrite Status

| Crate | Status |
|---|---|
| `crates/lexer` | Implemented — DFA tokeniser, string interning, error recovery |
| `crates/ast` | Implemented — arena-allocated nodes for all declarations, expressions, statements, types |
| `crates/parser` | Implemented — recursive-descent parser with structured error recovery |
| `crates/typeck` | Implemented — scope analysis, type inference, structured diagnostics |
| `crates/ir` | Scaffold — not yet implemented |
| `crates/codegen` | Scaffold — not yet implemented |
| `tools/thagore-cli` | In progress |
| `tools/thagore-lsp` | In progress |

---

## Repository Layout

```
crates/
  lexer/          Tokeniser (DFA, string interning, error recovery)
  ast/            Arena-allocated AST node types
  parser/         Recursive-descent parser
  typeck/         Type checker and structured diagnostics
  ir/             Intermediate representation  [scaffold]
  codegen/        Code generation backend      [scaffold]

tools/
  thagore-cli/    Command-line driver
  thagore-lsp/    Language server (LSP)

stdlib/           Standard library source (planned)

docs/
  starlight/      Documentation website (Astro Starlight)
  idea/           Language design RFCs
  runbooks/       Operational runbooks

tests/fixtures/   End-to-end test programs (.tg files)
```

---

## Building from Source

**Prerequisite:** Rust stable toolchain, edition 2024.

```bash
cargo build --workspace
cargo test --workspace
```

Build the CLI binary:

```bash
cargo build -p thagore-cli --release
# binary: target/release/thagore-cli
```

---

## Documentation

Full language and toolchain docs are published from `docs/starlight/` and deployed
automatically on push to `main`. To preview locally:

```bash
cd docs/starlight
npm install
npm run dev
```

---

## Contributing

See [.github/CONTRIBUTING.md](.github/CONTRIBUTING.md).
Project policy is enforced by [AGENTS.md](AGENTS.md) — read it before making any change.

---

## License

Apache-2.0 — see [LICENSE](LICENSE).

## Security

Do not open public issues for security vulnerabilities.
Report privately to **support@thagore.io.vn**.
See [.github/SECURITY.md](.github/SECURITY.md) for the full disclosure policy.

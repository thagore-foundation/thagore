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
| `crates/ir` | Implemented — typed lowering and module-aware IR generation |
| `crates/codegen` | Implemented — LLVM 14 backend, object emission, linker integration |
| `crates/interpreter` | Implemented — browser-safe interpreter used by the playground |
| `crates/module_graph` | Implemented — module resolution, dependency graph, import table |
| `tools/thagore-cli` | Implemented — `thagc` / `thagore` compiler driver |
| `tools/thagore-fmt` | Implemented — AST-based formatter |
| `tools/thagore-lsp` | Implemented — stdio LSP server |
| `playground/wasm` | Implemented — browser WASM bridge for the static playground |

---

## Repository Layout

```
crates/
  lexer/          Tokeniser (DFA, string interning, error recovery)
  ast/            Arena-allocated AST node types
  parser/         Recursive-descent parser
  typeck/         Type checker and structured diagnostics
  ir/             Intermediate representation and lowering
  codegen/        LLVM backend and artifact emission
  interpreter/    Tree-walking interpreter for playground/WASM
  module_graph/   Module resolution, graph build, import table

tools/
  thagore-cli/    Command-line compiler driver (`thagc`, `thagore`)
  thagore-fmt/    Official formatter
  thagore-lsp/    Language server (LSP)
  thagore-bench/  Comparative benchmark runner

stdlib/           Standard library source
tooling/
  packaging/      Release target metadata shared by the CLI and workflows
  release/        Archive packager, manifest generator, installers
  community/      Release/support policy documents

docs/
  starlight/      Documentation website (Astro Starlight)
  architecture/   Architecture map and status docs
  contributor-guide/
  adr/

tests/fixtures/   End-to-end test programs (.tg files)
```

---

## Building from Source

**Prerequisite:** current Rust toolchain, edition 2024.

```bash
cargo build --workspace
cargo test --workspace
```

Build the core toolchain:

```bash
cargo build -p thagore-cli -p thagore-fmt -p thagore-lsp --release
```

The release system also supports packaged toolchain archives plus installer scripts:

- POSIX installer: `tooling/release/thagup.sh`
- PowerShell installer: `tooling/release/thagup.ps1`
- Release policy: `tooling/community/release-support-policy.md`

Recommended end-user install commands are pinned to a release tag, not `main`:

```bash
curl -fsSL https://github.com/thagore-foundation/thagore/releases/download/v0.9.6/thagup-v0.9.6.sh | sh
```

```powershell
irm https://github.com/thagore-foundation/thagore/releases/download/v0.9.6/thagup-v0.9.6.ps1 | iex
```

Current in-develop release targets:

- `x86_64-unknown-linux-gnu`
- `aarch64-unknown-linux-gnu`
- `x86_64-unknown-linux-musl`
- `aarch64-unknown-linux-musl`
- `x86_64-apple-darwin`
- `aarch64-apple-darwin`
- `x86_64-pc-windows-msvc`
- `aarch64-pc-windows-msvc`

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

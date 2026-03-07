# Where To Change What

Quick map for contributors:

## I need to change parser behavior

- `crates/lexer/src/token.rs`
- `crates/lexer/src/lexer.rs`
- `crates/parser/src/parser.rs`
- `crates/parser/src/decl.rs`
- `crates/parser/src/expr.rs`
- `crates/parser/src/stmt.rs`
- `crates/parser/src/types.rs`

## I need to change type checks

- `crates/typeck/src/check.rs`
- `crates/typeck/src/module_check.rs`
- `crates/typeck/src/infer.rs`
- `crates/typeck/src/return_infer.rs`
- `crates/typeck/src/func_check.rs`

## I need to change lowering/IR contracts

- `crates/ir/src/lower.rs`
- `crates/ir/src/module_lower.rs`
- `crates/ir/src/module.rs`
- `crates/ir/src/instr.rs`
- `crates/ir/src/block.rs`

## I need to change LLVM output

- `crates/codegen/src/context.rs`
- `crates/codegen/src/func.rs`
- `crates/codegen/src/instr.rs`
- `crates/codegen/src/module_emit.rs`
- `crates/codegen/src/output.rs`
- `crates/codegen/runtime/thagore_rt.c`

## I need to change module/session compilation behavior

- `crates/module_graph/src/lib.rs`
- `crates/module_graph/src/resolver.rs`
- `crates/module_graph/src/import_table.rs`
- `tools/thagore-cli/src/session.rs`
- `tools/thagore-cli/src/pipeline.rs`

## I need to change CLI commands

- `tools/thagore-cli/src/cli.rs`
- `tools/thagore-cli/src/main.rs`
- `tools/thagore-cli/src/pipeline.rs`
- `tools/thagore-cli/src/session.rs`
- `tools/thagore-fmt/src/main.rs`
- `tools/thagore-lsp/src/main.rs`

## I need to change browser execution or playground behavior

- `crates/interpreter/src/*`
- `playground/wasm/thagore_wasm.rs`
- `playground/index.html`
- `playground/main.js`
- `playground/style.css`

## I need to update stdlib or language examples

- `stdlib/*`
- `tests/fixtures/*`
- `docs/starlight/src/content/docs/stdlib/*`
- `docs/starlight/src/content/docs/language/*`

## I need to update release/policy automation

- `.github/workflows/*`
- `tooling/packaging/*`
- `tooling/release/*`
- `tooling/community/*`
- `docs/starlight/src/content/docs/install.mdx`

# Changelog

## v2.3.0 - 2026-03-06

- added HM unifier (`fresh`, `unify`, `apply`) and type-level HIR inference/check APIs (`infer_expression_ty`, `check_expression_ty`).
- integrated tree-type checking for generic `Option<T>`/`Result<T,E>` annotations in `let` and `return` validation paths.
- added dedicated v2.3 release gates under `tests/inference/` and `tests/generics/`.
- tightened generic mismatch diagnostics for payload/return typing with source spans preserved through the existing diagnostics pipeline.

## v2.2.0 - 2026-03-06

- typed HIR, bidirectional type checker, type-mismatch errors with source underlines.
- added `compiler/include/thagc/hir/expr.hpp` + `compiler/include/thagc/hir/typecheck.hpp` and `compiler/include/thagc/ty/ty.hpp`.
- integrated HIR inference/check path in `TypeChecker` for `let` annotations and function returns with compatibility fallback to existing expression typing rules.
- updated parity syntax-alignment tests for split frontend parser files (`parser.cpp` + `expr.cpp`).

## v2.0.0 - 2026-03-06

- replaced line-by-line parser with recursive-descent token-stream parser; 3-pass re-parse eliminated.
- parser statement coalescing now preserves `?` try-operator statements as standalone lines.
- backend AST-first expression lowering now falls back correctly for comptime-substituted atoms and print/defer expression paths.
- backend typed-pointer compatibility fixes for extern/runtime calls and struct method/field lowering across LLVM variants.

## v1.9.0 - 2026-03-05

- span-aware diagnostics: parser/module parse errors now carry span ranges and render caret underlines in `thagc check`.
- frontend token pipeline now preserves byte-offset spans end-to-end (`Token` -> parser context -> AST/Core IR).
- added frontend `SourceMap` for precise offset-to-line/column translation.

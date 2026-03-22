# Self-Host Frontend Slice

This directory is the first extraction target from `tests/bootstrap_seed/`.

Purpose:

- hold reusable Thagore frontend modules for self-host bootstrap
- keep compiler-like logic out of fixture glue
- become the source used by the first `stage0 -> stage1 -> stage2` bootstrap
  slice

Current contents:

- `chars.tg`
- `lexer.tg`
- `parser.tg`
- `symbols.tg`
- `resolver.tg`
- `diagnostics.tg`
- `driver.tg`
- `main.tg`

Current scope:

- token scan
- parser-lite summary
- symbol collection
- import resolution
- diagnostics-lite and semantic-lite used by the bootstrap seed
- module-kind-aware analysis for executable roots vs library modules
- implicit `main` synthesis for executable roots with top-level statements
- narrow static return-type inference for single-line funcs without `->`
- primitive/simple-call assignment diagnostics and non-bool condition checks

Boundary:

- this is reusable self-host bootstrap code
- `tests/bootstrap_seed/` remains the harness and fixture corpus
- top-layer bootstrap sugar is not implemented here yet

Near-term next steps:

1. switch seed harness to import these modules instead of owning duplicate logic
2. add desugared-output observability for synthesized executable roots and inferred signatures
3. widen return-type inference beyond literal-only cases when diagnostics remain fail-fast

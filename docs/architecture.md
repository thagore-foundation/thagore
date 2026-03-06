# Thagore Architecture

The workspace is split into isolated crates for lexing, parsing, AST ownership,
type checking, IR construction, and code generation, with tooling crates kept
under `tools/` to preserve compiler-layer boundaries.

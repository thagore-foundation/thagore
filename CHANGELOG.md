# Changelog

## v1.9.0 - 2026-03-05

- span-aware diagnostics: parser/module parse errors now carry span ranges and render caret underlines in `thagc check`.
- frontend token pipeline now preserves byte-offset spans end-to-end (`Token` -> parser context -> AST/Core IR).
- added frontend `SourceMap` for precise offset-to-line/column translation.

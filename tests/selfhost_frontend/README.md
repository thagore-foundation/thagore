# Selfhost Frontend Differential Corpus

This corpus is the first narrow differential gate between:

- the Rust-hosted frontend surfaced through `thagc check`
- the Thagore-authored frontend slice under `bootstrap/selfhost/frontend/`

Scope:

- small core-syntax files only
- no top-layer sugar
- compare normalized frontend categories, not full AST parity yet
- assignment-target errors are normalized into `unknown identifier`
- unknown callee errors are normalized into `unknown identifier`
- assignment-from-call errors are normalized into `type mismatch`
- local binding errors are normalized into `type mismatch`
- return-from-call errors are normalized into `return type mismatch`
- selected fixtures also have golden `dump-report` outputs for richer parity
- bootstrap-seed fixtures now also gate `dump-report` for module-kind-sensitive
  cases (`library` vs synthesized executable root)
- the canonical manifest contracts now live under
  `bootstrap/selfhost/corpus/`
- `bootstrap/selfhost/corpus/frontend-differential.txt` is now the contract for
  the narrow `thagc check` replacement surface
- `bootstrap/selfhost/corpus/frontend-stage-chain.txt` is now the contract for
  the reusable `scan -> parse -> check` stage lane
- `bootstrap/selfhost/corpus/frontend-report.txt`,
  `bootstrap/selfhost/corpus/frontend-parse.txt`, and
  `bootstrap/selfhost/corpus/frontend-scan.txt` now define the golden-locked
  fixture sets for the richer selfhost frontend stages
- the standalone CI runner consumes those manifests too, so they are shared
  contracts across Rust tests and selfhost-first validation lanes
- `bootstrap/selfhost/corpus/frontend-parse.txt` now also anchors Target 02 in
  `docs/plan/selfhost-replacement-target.md`, so parse-stage work stops being
  only a support detail under Target 01
- `bootstrap/selfhost/corpus/frontend-differential.txt` now carries explicit module-kind alongside
  expected labels, so library fixtures are checked as library fixtures instead
  of being implicitly treated as executable roots
- the contract now includes an explicit library-mode success fixture
  (`ok_library_module.tg`) so replacement confidence is not limited to
  executable-root files
- the contract now also includes import-resolution success fixtures in both
  executable and library mode, so Target 01 is no longer limited to local-only
  files
- the contract now includes a missing-import executable fixture, so module
  surface parity covers both success and failure paths
- the contract now includes an unresolved-imported-symbol executable fixture,
  so import semantics go beyond pure module existence checks
- those module-surface failure categories now also run in library mode, so
  Target 01 checks both executable and library module behavior instead of only
  executable failures
- the contract now also includes import-alias success fixtures in both module
  kinds, extending module-surface confidence without jumping into alias-error
  parity that the Rust side does not expose yet
- selected module-surface fixtures now also have normalized report goldens, so
  module behavior is locked at a richer level than category labels alone
- selected module-surface fixtures now also have scan/parse goldens, so token
  and pre-check drift on imports is caught before it reaches `check.tg`

Current categories:

- `ok`
- `call arity mismatch`
- `type mismatch`
- `unknown identifier`
- `missing import`
- `unknown imported symbol`
- `condition type mismatch`
- `return type mismatch`

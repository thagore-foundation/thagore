# Selfhost Replacement Target 01

Target:

- Rust-side entry: `tools/thagore-cli/src/session.rs`
- Surface: `check_file(...) -> check_all() -> check_module(...)`
- Scope: narrow `thagc check` frontend contract already represented in
  `tests/selfhost_frontend/differential_corpus.txt`

Why this target:

- highest leverage surface already exercised by users
- no codegen or linker dependency
- already has selfhost `check.tg` / `pipeline.tg` / corpus / dedicated workflow
- small enough to replace experimentally without claiming full compiler

Replacement rule:

- Rust remains source of truth outside the narrow manifest contract
- for fixtures inside `tests/selfhost_frontend/differential_corpus.txt`, the
  selfhost frontend lane must stay green on:
  - host/selfhost differential labels
  - `scan -> parse -> check` stage-chain corpus
  - first-pass vs second-pass rebuilt stage reports
  - explicit executable-root and library-mode success coverage
- the experimental routing hook lives in `tools/thagore-cli/src/session.rs`
  behind hidden `thagc check` flags (`--selfhost-replacement-bin`,
  `--selfhost-replacement-manifest`, `--selfhost-replacement-strict`,
  `--selfhost-replacement-report-out`), with env fallback kept for CI plumbing,
  so Target 01 can be exercised through the real Rust `check_file(...)` path
  without claiming production replacement yet

Exit criteria for Target 01:

- selfhost workflow remains green across Linux and Windows
- corpus manifests are stable and no longer changing every small refactor
- failures in Target 01 are treated as replacement regressions, not just seed
  regressions

Next target after 01:

- move from contract verification to experimental routing of this narrow surface
  through selfhost frontend execution in a dedicated trial path

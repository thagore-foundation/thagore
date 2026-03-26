# Selfhost Replacement Targets

## Target 01

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
  - explicit import-resolution success coverage in both executable and library
    mode
  - explicit import-alias success coverage in both executable and library mode
  - explicit missing-import failure coverage in both executable and library
    mode
  - explicit unresolved-imported-symbol coverage in both executable and
    library mode
- the experimental routing hook lives in `tools/thagore-cli/src/session.rs`
  behind hidden `thagc check` flags (`--selfhost-replacement-bin`,
  `--selfhost-replacement-manifest`, `--selfhost-replacement-kind`,
  `--selfhost-replacement-strict`,
  `--selfhost-replacement-report-out`), with env fallback kept for CI plumbing,
  so Target 01 can be exercised through the real Rust `check_file(...)` path
  without claiming production replacement yet

Exit criteria for Target 01:

- selfhost workflow remains green across Linux and Windows
- corpus manifests are stable and no longer changing every small refactor
- failures in Target 01 are treated as replacement regressions, not just seed
  regressions

## Target 02

Target:

- Selfhost-side entry: `bootstrap/selfhost/frontend/parse.tg`
- CI/runtime surface:
  - `tooling/ci/selfhost_frontend_stage.py`
  - `tests/selfhost_frontend/parse_corpus.txt`
  - `tests/selfhost_frontend/stage_chain_corpus.txt`
- Scope: the parse-stage contract for token/summary/symbol/import output before
  semantic diagnostics

Why this target:

- it sits directly under Target 01 and is already exercised on every selfhost
  frontend lane
- it is narrower than full `check.tg`, so regressions are easier to localize
- it has no dependence on final diagnostic composition, making it a cleaner
  replacement boundary

Replacement rule:

- `parse.tg` becomes the first stage-level replacement candidate, not just a
  helper beneath `check.tg`
- `.github/workflows/selfhost-frontend-parse-target.yml` is the dedicated CI
  lane for this boundary, and `tooling/ci/selfhost_frontend_parse_target.py`
  is the contract runner that owns parse-golden and stage-chain checks
- every fixture in `tests/selfhost_frontend/parse_corpus.txt` must remain green
  in:
  - Rust test harness golden checks
  - standalone selfhost stage runner checks
  - first-pass vs second-pass rebuilt stage reports
- every fixture in `tests/selfhost_frontend/stage_chain_corpus.txt` must show
  that `parse.tg` still extends `scan.tg` correctly before `check.tg` adds
  final diagnostics

Exit criteria for Target 02:

- parse-stage goldens stop changing during normal Target 01 work
- stage-runner failures can identify parse drift without depending on
  check-stage diagnostics
- parse output is stable enough to be treated as a reusable contract surface for
  later stage replacement

Next target after 02:

- promote `scan.tg` + `parse.tg` together into a stricter replacement lane, or
- define a dedicated session/driver boundary above them once Target 02 stops
  moving

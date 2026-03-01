# v1.0 Release Runbook (Thagore + Drago)

This runbook defines the release sequence for the v1.0 deploy baseline across:
- `thagore-foundation/thagore`
- `thagore-foundation/drago`

The objective is one deterministic release train with multi-OS binaries and updater/install compatibility.

## 1) Preconditions

- `main` is green in both repositories.
- No open P0/P1 issues for release scope.
- Tag/version plan is decided:
  - Thagore: `vX.Y.Z`
  - Drago: `vA.B.C`
- GitHub token has `contents:write` for both repositories.

## 2) Thagore release gate

Run in `thagore` repo:

```bash
cmake -S . -B build-llvm21 -G Ninja -DLLVM_DIR=/usr/lib/llvm-21/lib/cmake/llvm
cmake --build build-llvm21 -j"$(nproc)"
THAGC_BIN=build-llvm21/compiler/thagc python3 -m unittest \
  tests.e2e.test_build_and_run \
  tests.integration.test_language_feature_completion \
  tests.integration.test_import_system \
  tests.integration.test_concurrency_regression_native \
  tests.integration.test_structured_concurrency_beta \
  tests.integration.test_memory_model_send_sync \
  tests.integration.test_runtime_behavior_native
```

Then verify release workflow inputs and tag:
- `Release` workflow uploads Linux/macOS/Windows assets.
- `thagup.sh` and `thagup.ps1` are included in release assets.

## 3) Drago release gate

Run in `drago` repo:

```bash
THAGC=/media/lehungquangminh/QM_SSD/thagore/build-llvm21/compiler/thagc
$THAGC build src/main.tg -o drago.bin
./drago.bin check
./drago.bin test
```

Then trigger Drago release workflow:
- Matrix build lanes:
  - Linux x86_64
  - macOS arm64
  - Windows x86_64
- Expected assets:
  - `drago-<tag>-linux-x86_64.tar.gz`
  - `drago-<tag>-macos-arm64.tar.gz`
  - `drago-<tag>-windows-x86_64.zip`
  - `SHA256SUMS-linux-x86_64.txt`
  - `SHA256SUMS-macos-arm64.txt`
  - `SHA256SUMS-windows-x86_64.txt`

## 4) Installer/updater compatibility check

After both releases are published:

```bash
curl -fsSL https://thagore.org/thagup.sh | bash
thagc --version
drago --version
drago update
```

Validate:
- Installer deploys both `thagc` and `drago`.
- PATH shims exist in `~/.thagore/bin`.
- `drago update` completes and both `thagc --version` / `drago --version` remain healthy.

## 5) Landing sync

`thagore` release workflow syncs installer scripts to `thagore-foundation/landing`.
Post-release checks:
- `https://thagore.org/thagup.sh` returns the latest script.
- `https://thagore.org/thagup.ps1` returns the latest script.
- Checksums file is updated on landing.

## 6) Final sign-off

Release is complete when all are true:
- Thagore and Drago release artifacts are published.
- Installer + updater checks pass.
- `ROADMAP.md` v1.0 gate can be marked done with CI run links.
- Release notes are published in both repositories.

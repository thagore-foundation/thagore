# Changelog

All notable changes to Thagore are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- `scripts/fetch_llvm_prebuilt.py` — download LLVM prebuilt binaries per target triple from
  GitHub LLVM releases; used by CI to embed LLVM into target packs
- `scripts/install/thagup` — self-updater shell script (`thagup update`, `thagup target add`, etc.)
- `llvm_bundle` field in all 5 target `manifest.json` files — declares embedded LLVM 21.1.8
  paths (`bin_dir`, `lib_dir`, `link_driver`, `lld_driver`)

### Changed
- `core-ci.yml` — rewritten: now installs LLVM 21, downloads stage1 seed, runs bootstrap
  (`scripts/bootstrap.sh`), executes test suite, uploads stage2b binary. Policy checks
  (`no_cpp_dependency_gate`, `validate_target_registry`, etc.) moved to a parallel `policy` job.
- `core-selfhost.yml` — rewritten: builds stage2 from stage1, stage2b from stage2, then performs
  SHA256 reproducibility comparison (stage2 vs stage2b) analogous to rustc stage3 check.
- `core-release.yml` — rewritten: bootstraps thagc binary, downloads LLVM prebuilts via
  `fetch_llvm_prebuilt.py`, packages `thagc-core-<os>.tar.gz` and
  `thagc-target-<triple>-<os>.tar.gz` with embedded LLVM, generates `SHA256SUMS`, and publishes
  a GitHub Release via `gh release create`.
- `core-docs-pages.yml` — fixed to actually build Astro/Starlight docs and deploy to GitHub Pages
  using `actions/deploy-pages`.
- `scripts/install/thagup-init.sh` — completed download-and-install flow: downloads
  `thagc-core-<os>.tar.gz` + target packs from GitHub Releases, installs to
  `~/.thagore/toolchains/stable/`, sets up `~/.thagore/bin/thagc`, updates shell profiles.

---

## Template for future releases

```
## [vX.Y.Z] — YYYY-MM-DD

### Added
- ...

### Changed
- ...

### Fixed
- ...

### Removed
- ...
```

[Unreleased]: https://github.com/thagore-foundation/thagore/compare/HEAD...main

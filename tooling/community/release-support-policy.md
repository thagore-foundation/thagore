# Release Support Policy

This document defines the release tiers used by the Thagore toolchain.

## Official SemVer Release Targets

SemVer releases only publish targets that are verified end to end in GitHub Actions at the
time the tag is cut. The current official set is:

- `x86_64-unknown-linux-gnu`
- `aarch64-unknown-linux-gnu`
- `x86_64-apple-darwin`
- `aarch64-apple-darwin`

Targets are promoted into this set only after they build, package, and pass smoke verification
without manual intervention on public CI.

## Deferred Release Targets

These targets remain part of the wider support plan, but they are not included in semver
releases until a real builder lane exists and passes consistently:

- `x86_64-unknown-linux-musl`
- `aarch64-unknown-linux-musl`
- `x86_64-pc-windows-msvc`
- `aarch64-pc-windows-msvc`
- `armv7-unknown-linux-gnueabihf`
- `arm-unknown-linux-gnueabihf`
- `riscv64gc-unknown-linux-gnu`
- `powerpc64le-unknown-linux-gnu`
- `s390x-unknown-linux-gnu`
- `x86_64-unknown-freebsd`
- `aarch64-unknown-freebsd`
- `x86_64-unknown-openbsd`
- `x86_64-unknown-netbsd`

## Nightly

Nightly targets are best-effort preview builds. They are exposed through prerelease tags and
may lag behind the stable release automation. Failed nightly jobs do not block semver releases.

Nightly targets:

- `x86_64-unknown-dragonfly`
- `x86_64-unknown-illumos`
- `x86_64-pc-windows-gnu`
- `i686-pc-windows-msvc`
- `i686-pc-windows-gnu`
- `i686-unknown-linux-gnu`
- `x86_64-linux-android`
- `aarch64-linux-android`
- `wasm32-wasip1`
- `wasm32-unknown-unknown`

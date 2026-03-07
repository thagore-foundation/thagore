# Release Support Policy

This document defines the release tiers used by the Thagore toolchain.

## Stable

Stable targets are the primary distribution set for end users. Release automation treats
these artifacts as the default install surface exposed by `thagup.sh`, `thagup.ps1`, and
`thagc --print-target-list`.

Stable targets:

- `x86_64-unknown-linux-gnu`
- `aarch64-unknown-linux-gnu`
- `x86_64-unknown-linux-musl`
- `aarch64-unknown-linux-musl`
- `x86_64-apple-darwin`
- `aarch64-apple-darwin`
- `x86_64-pc-windows-msvc`
- `aarch64-pc-windows-msvc`

## Extended

Extended targets are published from the same semver release train when a builder is available.
The release manifest exposes them, but incident response and smoke coverage are lower priority
than the stable lane.

Extended targets:

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

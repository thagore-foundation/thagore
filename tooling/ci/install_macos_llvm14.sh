#!/usr/bin/env bash
set -euo pipefail

# Install LLVM 14 on macOS for the Trusting-Trust bootstrap proof.
# Mirrors tooling/ci/install_linux_llvm14.sh's contract:
#   - exports LLVM_SYS_140_PREFIX (via $GITHUB_ENV when available)
#   - prepends $prefix/bin to PATH (via $GITHUB_PATH when available)
#
# Phase 1 scope: macOS x86_64 only. Apple Silicon (arm64) has no official
# LLVM 14 prebuilt and is deferred to a later phase.

set_llvm_env() {
  local prefix=$1
  if [[ -n "${GITHUB_ENV:-}" ]]; then
    echo "LLVM_SYS_140_PREFIX=$prefix" >> "$GITHUB_ENV"
  else
    export LLVM_SYS_140_PREFIX=$prefix
  fi
  if [[ -n "${GITHUB_PATH:-}" ]]; then
    echo "$prefix/bin" >> "$GITHUB_PATH"
  else
    export PATH="$prefix/bin:${PATH}"
  fi
}

have_llvm_prefix() {
  local prefix=$1
  [[ -x "$prefix/bin/llvm-config" && -x "$prefix/bin/clang" && -f "$prefix/lib/libPolly.a" ]]
}

arch=$(uname -m)
if [[ "$arch" != "x86_64" ]]; then
  echo "install_macos_llvm14.sh: unsupported arch '$arch' (phase 1 covers x86_64 only)" >&2
  exit 1
fi

# Try Homebrew-managed LLVM 14 first (Intel default prefix, then explicit cellar path).
for brew_prefix in /usr/local/opt/llvm@14 /usr/local/Cellar/llvm@14/14.0.6; do
  if have_llvm_prefix "$brew_prefix"; then
    set_llvm_env "$brew_prefix"
    exit 0
  fi
done

prebuilt_root="${RUNNER_TEMP:-/tmp}/llvm-14.0.6-x86_64-apple-darwin-prebuilt"
prebuilt_prefix="$prebuilt_root/clang+llvm-14.0.6-x86_64-apple-darwin"
prebuilt_archive="${RUNNER_TEMP:-/tmp}/clang+llvm-14.0.6-x86_64-apple-darwin.tar.xz"
prebuilt_url="https://github.com/llvm/llvm-project/releases/download/llvmorg-14.0.6/clang%2Bllvm-14.0.6-x86_64-apple-darwin.tar.xz"

install_prebuilt() {
  rm -rf "$prebuilt_root"
  mkdir -p "$prebuilt_root"
  curl -fsSL --retry 5 --retry-delay 10 --retry-all-errors "$prebuilt_url" -o "$prebuilt_archive"
  tar -xJf "$prebuilt_archive" -C "$prebuilt_root"
}

if have_llvm_prefix "$prebuilt_prefix"; then
  set_llvm_env "$prebuilt_prefix"
  exit 0
fi

if install_prebuilt && have_llvm_prefix "$prebuilt_prefix"; then
  set_llvm_env "$prebuilt_prefix"
  exit 0
fi

# Final fallback: install via Homebrew. Slower than the prebuilt tarball but
# always available on GHA macOS runners.
if command -v brew >/dev/null 2>&1; then
  brew update >/dev/null
  brew install llvm@14
  for brew_prefix in /usr/local/opt/llvm@14 /usr/local/Cellar/llvm@14/14.0.6; do
    if have_llvm_prefix "$brew_prefix"; then
      set_llvm_env "$brew_prefix"
      exit 0
    fi
  done
fi

echo "install_macos_llvm14.sh: failed to provision LLVM 14" >&2
exit 1

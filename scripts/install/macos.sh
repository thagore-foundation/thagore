#!/usr/bin/env bash
set -euo pipefail

LLVM_VERSION="21.1.8"
ARCH="auto"
ASSUME_YES=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --llvm-version) LLVM_VERSION="${2:-}"; shift 2 ;;
    --arch) ARCH="${2:-}"; shift 2 ;;
    --yes|-y) ASSUME_YES=1; shift ;;
    --help|-h)
      echo "Usage: scripts/install/macos.sh [--llvm-version 21.1.8] [--arch x86_64|arm64] [--yes]"
      exit 0
      ;;
    *) echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done

if [[ "$LLVM_VERSION" != "21.1.8" ]]; then
  echo "ERROR: only LLVM 21.1.8 is supported in release installer." >&2
  exit 1
fi

if [[ "$ARCH" == "auto" ]]; then
  ARCH="$(uname -m || true)"
fi
if [[ "$ARCH" == "amd64" ]]; then ARCH="x86_64"; fi
if [[ "$ARCH" == "aarch64" ]]; then ARCH="arm64"; fi

if ! command -v brew >/dev/null 2>&1; then
  echo "ERROR: Homebrew is required." >&2
  exit 1
fi

if [[ "$ASSUME_YES" -ne 1 ]]; then
  read -r -p "Install LLVM ${LLVM_VERSION} on macOS (${ARCH}) via brew tap? [Y/n]: " reply
  reply="${reply:-Y}"
  case "$reply" in
    y|Y|yes|YES) ;;
    *) echo "Aborted."; exit 1 ;;
  esac
fi

brew update
brew tap thagore-foundation/llvm || true
if brew list llvm@21 >/dev/null 2>&1; then
  echo "llvm@21 already installed."
else
  brew install llvm@21 || brew install llvm
fi

if brew --prefix llvm@21 >/dev/null 2>&1; then
  export PATH="$(brew --prefix llvm@21)/bin:$PATH"
else
  export PATH="$(brew --prefix llvm)/bin:$PATH"
fi

clang --version
echo "[thagore-installer] LLVM ${LLVM_VERSION} install done for macos/${ARCH}"


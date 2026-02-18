#!/usr/bin/env bash
set -euo pipefail

LLVM_VERSION="21.1.8"
ARCH="auto"
ASSUME_YES=0
PREFIX="${THAGORE_LLVM_PREFIX:-$HOME/.thagore/llvm-21.1.8}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --llvm-version) LLVM_VERSION="${2:-}"; shift 2 ;;
    --arch) ARCH="${2:-}"; shift 2 ;;
    --yes|-y) ASSUME_YES=1; shift ;;
    --help|-h)
      echo "Usage: scripts/install/portable.sh [--llvm-version 21.1.8] [--arch x86_64|arm64] [--yes]"
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

if [[ "$ASSUME_YES" -ne 1 ]]; then
  read -r -p "Install LLVM ${LLVM_VERSION} using portable/build-from-source flow (${ARCH})? [Y/n]: " reply
  reply="${reply:-Y}"
  case "$reply" in
    y|Y|yes|YES) ;;
    *) echo "Aborted."; exit 1 ;;
  esac
fi

mkdir -p "$PREFIX"

if command -v clang >/dev/null 2>&1 && clang --version | grep -qE "version 21|21\.1\.8"; then
  echo "LLVM already available."
  exit 0
fi

if command -v cmake >/dev/null 2>&1 && command -v git >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1; then
  WORKDIR="$(mktemp -d)"
  trap 'rm -rf "$WORKDIR"' EXIT
  git clone --depth 1 --branch llvmorg-21.1.8 https://github.com/llvm/llvm-project.git "$WORKDIR/llvm-project"
  cmake -S "$WORKDIR/llvm-project/llvm" -B "$WORKDIR/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DLLVM_ENABLE_PROJECTS="clang;lld" \
    -DLLVM_TARGETS_TO_BUILD="AArch64;X86"
  cmake --build "$WORKDIR/build" --target install -j2
else
  echo "ERROR: portable mode requires git + cmake + ninja to build LLVM from source." >&2
  exit 1
fi

echo "LLVM installed under: $PREFIX"
echo "Add to PATH: export PATH=\"$PREFIX/bin:\$PATH\""
"$PREFIX/bin/clang" --version


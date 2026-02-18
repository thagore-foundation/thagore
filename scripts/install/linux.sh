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
      echo "Usage: scripts/install/linux.sh [--llvm-version 21.1.8] [--arch x86_64|arm64] [--yes]"
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
  read -r -p "Install LLVM ${LLVM_VERSION} on Linux (${ARCH})? [Y/n]: " reply
  reply="${reply:-Y}"
  case "$reply" in
    y|Y|yes|YES) ;;
    *) echo "Aborted."; exit 1 ;;
  esac
fi

if command -v clang >/dev/null 2>&1 && clang --version | grep -qE "version 21|21\.1\.8"; then
  echo "LLVM already available."
  exit 0
fi

if command -v apt-get >/dev/null 2>&1; then
  bash scripts/install/ubuntu.sh --llvm-version "$LLVM_VERSION" --arch "$ARCH" --yes
  exit 0
fi

if command -v dnf >/dev/null 2>&1; then
  sudo dnf install -y clang lld llvm llvm-devel
elif command -v yum >/dev/null 2>&1; then
  sudo yum install -y clang lld llvm llvm-devel
elif command -v pacman >/dev/null 2>&1; then
  sudo pacman -Sy --noconfirm clang lld llvm
elif command -v zypper >/dev/null 2>&1; then
  sudo zypper --non-interactive install clang llvm lld
elif command -v apk >/dev/null 2>&1; then
  sudo apk add llvm clang lld
else
  echo "No supported package manager found. Falling back to portable installer."
  bash scripts/install/portable.sh --llvm-version "$LLVM_VERSION" --arch "$ARCH" --yes
  exit 0
fi

clang --version || true
echo "[thagore-installer] LLVM ${LLVM_VERSION} install done for linux/${ARCH}"


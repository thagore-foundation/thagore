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
      echo "Usage: scripts/install/ubuntu.sh [--llvm-version 21.1.8] [--arch x86_64|arm64] [--yes]"
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
  read -r -p "Install LLVM ${LLVM_VERSION} on Ubuntu (${ARCH})? [Y/n]: " reply
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

sudo apt-get update
curl -fsSL https://apt.llvm.org/llvm.sh -o /tmp/llvm.sh
chmod +x /tmp/llvm.sh
sudo /tmp/llvm.sh 21 all

if ! command -v clang >/dev/null 2>&1; then
  if [[ -d /usr/lib/llvm-21/bin ]]; then
    export PATH="/usr/lib/llvm-21/bin:$PATH"
  fi
fi

clang --version
echo "[thagore-installer] LLVM ${LLVM_VERSION} install done for ubuntu/${ARCH}"


#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PREFIX="${PREFIX:-/usr/local/thagore}"

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew is required on macOS." >&2
  exit 1
fi

if ! command -v clang >/dev/null 2>&1 || ! clang --version | grep -q "version 21"; then
  echo "[thagore-installer] Installing LLVM 21 via Homebrew..."
  brew update
  brew install llvm
fi

echo "[thagore-installer] Installing Thagore to ${PREFIX}..."
sudo mkdir -p "${PREFIX}"
sudo cp -r "${ROOT_DIR}/dist/"* "${PREFIX}/"
sudo ln -sf "${PREFIX}/bin/thagore" /usr/local/bin/thagore
sudo ln -sf "${PREFIX}/bin/thagore" /usr/local/bin/thag

cat <<EOF
Thagore installed successfully.
Binary: /usr/local/bin/thagore
Alias: /usr/local/bin/thag
Stdlib: ${PREFIX}/lib/std
EOF

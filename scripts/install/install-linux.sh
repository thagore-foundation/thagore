#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PREFIX="${PREFIX:-/opt/thagore}"

if ! command -v clang >/dev/null 2>&1 || ! clang --version | grep -q "version 21"; then
  echo "[thagore-installer] Installing LLVM 21..."
  sudo apt-get update
  curl -fsSL https://apt.llvm.org/llvm.sh -o /tmp/llvm.sh
  chmod +x /tmp/llvm.sh
  sudo /tmp/llvm.sh 21 all
fi

echo "[thagore-installer] Installing Thagore to ${PREFIX}..."
sudo mkdir -p "${PREFIX}"
sudo cp -r "${ROOT_DIR}/dist/"* "${PREFIX}/"
sudo ln -sf "${PREFIX}/bin/thag" /usr/local/bin/thag

cat <<EOF
Thagore installed successfully.
Binary: /usr/local/bin/thag
Stdlib: ${PREFIX}/lib/std
EOF

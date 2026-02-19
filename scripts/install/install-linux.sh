#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PREFIX="${PREFIX:-/opt/thagore}"

bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/ubuntu.sh" "$@"

echo "[thagore-installer] Installing Thagore to ${PREFIX}..."
sudo mkdir -p "${PREFIX}"
sudo cp -r "${ROOT_DIR}/dist/"* "${PREFIX}/"
sudo ln -sf "${PREFIX}/bin/thagore" /usr/local/bin/thagore

cat <<EOF
Thagore installed successfully.
Binary: /usr/local/bin/thagore
Stdlib: ${PREFIX}/lib/std
EOF

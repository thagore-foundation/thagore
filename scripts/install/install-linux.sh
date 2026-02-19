#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -d "$SCRIPT_DIR/../bin" && -d "$SCRIPT_DIR/../lib" ]]; then
  ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
elif [[ -d "$SCRIPT_DIR/../../dist/bin" ]]; then
  ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
else
  echo "ERROR: cannot resolve Thagore package root from $SCRIPT_DIR" >&2
  exit 1
fi
PREFIX="${PREFIX:-/opt/thagore}"
SOURCE_ROOT="$ROOT_DIR"
if [[ -d "$ROOT_DIR/dist/bin" ]]; then
  SOURCE_ROOT="$ROOT_DIR/dist"
fi

bash "$SCRIPT_DIR/ubuntu.sh" "$@"

echo "[thagore-installer] Installing Thagore to ${PREFIX}..."
sudo mkdir -p "${PREFIX}"
sudo cp -r "${SOURCE_ROOT}/"* "${PREFIX}/"
sudo ln -sf "${PREFIX}/bin/thagore" /usr/local/bin/thagore

ensure_user_path_entry() {
  local entry="$1"
  local rc_file="$2"
  local line="export PATH=\"$entry:\$PATH\""
  if [[ ! -f "$rc_file" ]]; then
    printf '%s\n' "$line" >> "$rc_file"
    return
  fi
  if ! grep -Fq "$line" "$rc_file"; then
    printf '\n%s\n' "$line" >> "$rc_file"
  fi
}

if [[ ":$PATH:" != *":/usr/local/bin:"* ]]; then
  ensure_user_path_entry "/usr/local/bin" "$HOME/.profile"
  if [[ -f "$HOME/.bashrc" ]]; then
    ensure_user_path_entry "/usr/local/bin" "$HOME/.bashrc"
  fi
  if [[ -f "$HOME/.zshrc" ]]; then
    ensure_user_path_entry "/usr/local/bin" "$HOME/.zshrc"
  fi
fi

cat <<EOF
Thagore installed successfully.
Binary: /usr/local/bin/thagore
Stdlib: ${PREFIX}/lib/std
EOF

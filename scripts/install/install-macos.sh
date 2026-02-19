#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -n "${THAGORE_ROOT:-}" ]]; then
  ROOT_DIR="$THAGORE_ROOT"
elif [[ -d "$SCRIPT_DIR/../bin" && -d "$SCRIPT_DIR/../lib" ]]; then
  ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
elif [[ -d "$SCRIPT_DIR/../../dist/bin" ]]; then
  ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
else
  echo "ERROR: cannot resolve Thagore package root from $SCRIPT_DIR" >&2
  exit 1
fi
if [[ ! -d "$ROOT_DIR/bin" && ! -d "$ROOT_DIR/dist/bin" ]]; then
  echo "ERROR: invalid THAGORE_ROOT/ROOT_DIR ($ROOT_DIR), missing bin payload" >&2
  exit 1
fi
PREFIX="${PREFIX:-/usr/local/thagore}"
SOURCE_ROOT="$ROOT_DIR"
if [[ -d "$ROOT_DIR/dist/bin" ]]; then
  SOURCE_ROOT="$ROOT_DIR/dist"
fi

bash "$SCRIPT_DIR/macos.sh" "$@"

echo "[thagore-installer] Installing Thagore to ${PREFIX}..."
sudo mkdir -p "${PREFIX}"
sudo cp -r "${SOURCE_ROOT}/"* "${PREFIX}/"

BIN_LINK_DIR="/usr/local/bin"
if [[ -d "/opt/homebrew/bin" ]]; then
  BIN_LINK_DIR="/opt/homebrew/bin"
fi
sudo mkdir -p "$BIN_LINK_DIR"
sudo ln -sf "${PREFIX}/bin/thagore" "$BIN_LINK_DIR/thagore"

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

ensure_user_path_entry_to_rcs() {
  local entry="$1"
  shift
  local rc_file
  for rc_file in "$@"; do
    ensure_user_path_entry "$entry" "$rc_file"
  done
}

readonly COMMON_MACOS_RC_FILES=(
  "$HOME/.zprofile"
  "$HOME/.zshrc"
  "$HOME/.bash_profile"
  "$HOME/.bashrc"
  "$HOME/.profile"
)

if [[ ":$PATH:" != *":$BIN_LINK_DIR:"* ]]; then
  ensure_user_path_entry_to_rcs "$BIN_LINK_DIR" "${COMMON_MACOS_RC_FILES[@]}"
fi

cat <<EOF
Thagore installed successfully.
Binary: $BIN_LINK_DIR/thagore
Stdlib: ${PREFIX}/lib/std
EOF

#!/usr/bin/env bash
set -euo pipefail

LLVM_VERSION="21.1.8"
ARCH="auto"
ASSUME_YES=0
PREFIX="${PREFIX:-/opt/thagore}"
SKIP_LLVM=0
SKIP_PAYLOAD=0
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --llvm-version) LLVM_VERSION="${2:-}"; shift 2 ;;
    --arch) ARCH="${2:-}"; shift 2 ;;
    --prefix) PREFIX="${2:-}"; shift 2 ;;
    --skip-llvm) SKIP_LLVM=1; shift ;;
    --skip-payload) SKIP_PAYLOAD=1; shift ;;
    --yes|-y) ASSUME_YES=1; shift ;;
    --help|-h)
      echo "Usage: linux.sh [--llvm-version 21.1.8] [--arch x86_64|arm64] [--prefix /opt/thagore] [--skip-llvm] [--skip-payload] [--yes]"
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

detect_source_root() {
  if [[ -n "${THAGORE_ROOT:-}" ]]; then
    echo "$THAGORE_ROOT"
    return
  fi
  if [[ -d "$SCRIPT_DIR/../bin" && -d "$SCRIPT_DIR/../lib/std" ]]; then
    echo "$(cd "$SCRIPT_DIR/.." && pwd)"
    return
  fi
  if [[ -d "$SCRIPT_DIR/../../dist/bin" && -d "$SCRIPT_DIR/../../dist/lib/std" ]]; then
    echo "$(cd "$SCRIPT_DIR/../../dist" && pwd)"
    return
  fi
  echo ""
}

append_path_rc() {
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

ensure_path_persisted() {
  local entry="$1"
  if [[ ":$PATH:" != *":$entry:"* ]]; then
    append_path_rc "$entry" "$HOME/.profile"
    append_path_rc "$entry" "$HOME/.bash_profile"
    append_path_rc "$entry" "$HOME/.bash_login"
    append_path_rc "$entry" "$HOME/.bashrc"
    append_path_rc "$entry" "$HOME/.zprofile"
    append_path_rc "$entry" "$HOME/.zshrc"
    export PATH="$entry:$PATH"
  fi
}

install_llvm_if_needed() {
  if [[ "$SKIP_LLVM" -eq 1 ]]; then
    return
  fi
  if command -v clang >/dev/null 2>&1 && clang --version | grep -qE "version 21|21\.1\.8"; then
    echo "LLVM already available."
    return
  fi
  if command -v apt-get >/dev/null 2>&1; then
    bash "$SCRIPT_DIR/ubuntu.sh" --llvm-version "$LLVM_VERSION" --arch "$ARCH" --yes
    return
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
    bash "$SCRIPT_DIR/portable.sh" --llvm-version "$LLVM_VERSION" --arch "$ARCH" --skip-payload --yes
    return
  fi
  clang --version || true
  echo "[thagore-installer] LLVM ${LLVM_VERSION} install done for linux/${ARCH}"
}

if [[ "$ASSUME_YES" -ne 1 ]]; then
  read -r -p "Install Thagore on Linux (${ARCH}) with LLVM ${LLVM_VERSION}? [Y/n]: " reply
  reply="${reply:-Y}"
  case "$reply" in
    y|Y|yes|YES) ;;
    *) echo "Aborted."; exit 1 ;;
  esac
fi

install_llvm_if_needed

if [[ "$SKIP_PAYLOAD" -eq 1 ]]; then
  exit 0
fi

SOURCE_ROOT="$(detect_source_root)"
if [[ -z "$SOURCE_ROOT" ]]; then
  echo "[thagore-installer] No package payload near installer script; LLVM setup finished."
  exit 0
fi

FINAL_PREFIX="$PREFIX"
if [[ -z "${PREFIX:-}" ]]; then
  FINAL_PREFIX="/opt/thagore"
fi
if [[ "$FINAL_PREFIX" == "/opt/thagore" && ! -w "/opt" && "$(id -u)" -ne 0 ]]; then
  FINAL_PREFIX="$HOME/.local/share/thagore"
fi

PREFIX_PARENT="$(dirname "$FINAL_PREFIX")"
if [[ -d "$FINAL_PREFIX" && -w "$FINAL_PREFIX" ]] || [[ ! -e "$FINAL_PREFIX" && -w "$PREFIX_PARENT" ]]; then
  mkdir -p "$FINAL_PREFIX"
  cp -R "$SOURCE_ROOT/"* "$FINAL_PREFIX/"
else
  sudo mkdir -p "$FINAL_PREFIX"
  sudo cp -R "$SOURCE_ROOT/"* "$FINAL_PREFIX/"
fi

if [[ ! -x "$FINAL_PREFIX/bin/thagore" ]]; then
  echo "ERROR: installed payload missing executable: $FINAL_PREFIX/bin/thagore" >&2
  exit 1
fi

LINK_DIR="/usr/local/bin"
if [[ "$FINAL_PREFIX" == "$HOME/.local/share/thagore" ]] || [[ ! -w "/usr/local/bin" && "$(id -u)" -ne 0 ]]; then
  LINK_DIR="$HOME/.local/bin"
fi
if [[ "$LINK_DIR" == "$HOME/.local/bin" ]]; then
  mkdir -p "$LINK_DIR"
  ln -sf "$FINAL_PREFIX/bin/thagore" "$LINK_DIR/thagore"
else
  sudo mkdir -p "$LINK_DIR"
  sudo ln -sf "$FINAL_PREFIX/bin/thagore" "$LINK_DIR/thagore"
fi

ensure_path_persisted "$LINK_DIR"

cat <<EOF
Thagore installed successfully.
Binary: $LINK_DIR/thagore
Prefix: $FINAL_PREFIX
Stdlib: $FINAL_PREFIX/lib/std
EOF

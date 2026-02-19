#!/usr/bin/env bash
set -euo pipefail

LLVM_VERSION="21.1.8"
ARCH="auto"
ASSUME_YES=0
PREFIX="${THAGORE_LLVM_PREFIX:-$HOME/.thagore/llvm-21.1.8}"
PAYLOAD_PREFIX="${THAGORE_PREFIX:-$HOME/.local/share/thagore}"
SKIP_PAYLOAD=0
SKIP_LLVM=0
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --llvm-version) LLVM_VERSION="${2:-}"; shift 2 ;;
    --arch) ARCH="${2:-}"; shift 2 ;;
    --prefix) PAYLOAD_PREFIX="${2:-}"; shift 2 ;;
    --skip-llvm) SKIP_LLVM=1; shift ;;
    --skip-payload) SKIP_PAYLOAD=1; shift ;;
    --yes|-y) ASSUME_YES=1; shift ;;
    --help|-h)
      echo "Usage: portable.sh [--llvm-version 21.1.8] [--arch x86_64|arm64] [--prefix ~/.local/share/thagore] [--skip-llvm] [--skip-payload] [--yes]"
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

if [[ "$ASSUME_YES" -ne 1 ]]; then
  read -r -p "Install portable LLVM ${LLVM_VERSION} and Thagore payload (${ARCH})? [Y/n]: " reply
  reply="${reply:-Y}"
  case "$reply" in
    y|Y|yes|YES) ;;
    *) echo "Aborted."; exit 1 ;;
  esac
fi

if [[ "$SKIP_LLVM" -ne 1 ]]; then
  mkdir -p "$PREFIX"

  if command -v clang >/dev/null 2>&1 && clang --version | grep -qE "version 21|21\.1\.8"; then
    echo "LLVM already available."
  else
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
    "$PREFIX/bin/clang" --version
  fi
fi

if [[ "$SKIP_PAYLOAD" -eq 1 ]]; then
  exit 0
fi

SOURCE_ROOT="$(detect_source_root)"
if [[ -z "$SOURCE_ROOT" ]]; then
  echo "[thagore-installer] No package payload near installer script; LLVM setup finished."
  exit 0
fi

mkdir -p "$PAYLOAD_PREFIX"
cp -R "$SOURCE_ROOT/"* "$PAYLOAD_PREFIX/"
if [[ ! -x "$PAYLOAD_PREFIX/bin/thagore" ]]; then
  echo "ERROR: installed payload missing executable: $PAYLOAD_PREFIX/bin/thagore" >&2
  exit 1
fi

LOCAL_BIN="$HOME/.local/bin"
mkdir -p "$LOCAL_BIN"
ln -sf "$PAYLOAD_PREFIX/bin/thagore" "$LOCAL_BIN/thagore"

if [[ -d "$PREFIX/bin" ]]; then
  ensure_path_persisted "$PREFIX/bin"
fi
ensure_path_persisted "$LOCAL_BIN"

cat <<EOF
Thagore installed successfully.
Binary: $LOCAL_BIN/thagore
Prefix: $PAYLOAD_PREFIX
Stdlib: $PAYLOAD_PREFIX/lib/std
EOF

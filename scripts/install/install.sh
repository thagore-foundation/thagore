#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODE="auto"
PASS_ARGS=()

print_help() {
  cat <<'EOF'
Usage: install.sh [--mode auto|linux|ubuntu|macos|portable] [installer args...]

Examples:
  install.sh --mode auto --yes
  install.sh --mode linux --prefix /opt/thagore --yes
  install.sh --mode ubuntu --yes
  install.sh --mode macos --yes
  install.sh --mode portable --prefix "$HOME/.local/share/thagore" --yes

Supported forwarded args (depend on selected installer):
  --llvm-version <version>
  --arch <x86_64|arm64>
  --prefix <path>
  --skip-llvm
  --skip-payload
  --yes | -y
EOF
}

detect_mode() {
  local os_name
  os_name="$(uname -s || true)"

  if [[ "$os_name" == "Darwin" ]]; then
    echo "macos"
    return
  fi

  if [[ "$os_name" != "Linux" ]]; then
    echo "portable"
    return
  fi

  if [[ -n "${TERMUX_VERSION:-}" || -n "${ANDROID_ROOT:-}" || -n "${ANDROID_DATA:-}" ]]; then
    echo "portable"
    return
  fi

  if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    local os_id="${ID:-}"
    local os_like="${ID_LIKE:-}"
    if [[ "$os_id" == "ubuntu" || "$os_id" == "debian" || "$os_like" == *"ubuntu"* || "$os_like" == *"debian"* ]]; then
      echo "ubuntu"
      return
    fi
  fi

  echo "linux"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode)
      MODE="${2:-}"
      shift 2
      ;;
    --help|-h)
      print_help
      exit 0
      ;;
    *)
      PASS_ARGS+=("$1")
      shift
      ;;
  esac
done

if [[ "$MODE" == "auto" ]]; then
  MODE="$(detect_mode)"
fi

case "$MODE" in
  linux)
    exec bash "$SCRIPT_DIR/linux.sh" "${PASS_ARGS[@]}"
    ;;
  ubuntu)
    # Ubuntu/Debian should still use the full Linux installer so payload+PATH are handled.
    exec bash "$SCRIPT_DIR/linux.sh" "${PASS_ARGS[@]}"
    ;;
  macos)
    exec bash "$SCRIPT_DIR/macos.sh" "${PASS_ARGS[@]}"
    ;;
  portable)
    exec bash "$SCRIPT_DIR/portable.sh" "${PASS_ARGS[@]}"
    ;;
  *)
    echo "ERROR: unsupported mode '$MODE'. Expected auto|linux|ubuntu|macos|portable." >&2
    exit 1
    ;;
esac

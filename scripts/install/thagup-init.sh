#!/usr/bin/env bash
set -euo pipefail

REPO_OWNER="thagore-foundation"
REPO_NAME="thagore"
API_BASE="https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}"

REQUESTED_TAG=""
REQUESTED_MODE="auto"
REQUESTED_ARCH="auto"
REQUESTED_PREFIX=""
REQUESTED_LLVM_VERSION="21.1.8"
SKIP_LLVM=0
ASSUME_YES=1

print_help() {
  cat <<'EOF'
thagup-init.sh - One-shot Thagore installer bootstrap (rustup-style)

Usage:
  thagup-init.sh [options]

Options:
  --tag <vX.Y.Z>             Install a specific release tag (default: latest stable)
  --mode <auto|linux|ubuntu|macos|portable>
  --arch <auto|x86_64|arm64>
  --prefix <path>            Install prefix for toolchain payload
  --llvm-version <21.1.8>    LLVM version expected by installer
  --skip-llvm                Skip LLVM provisioning
  --interactive              Ask for confirmation (default: non-interactive)
  --yes                      Force non-interactive install
  --help

Examples:
  curl -fsSL https://raw.githubusercontent.com/thagore-foundation/thagore/main/scripts/install/thagup-init.sh | bash
  bash thagup-init.sh --tag v0.5.30 --mode linux --arch x86_64
EOF
}

normalize_arch() {
  local in="${1:-}"
  case "$in" in
    amd64) echo "x86_64" ;;
    aarch64) echo "arm64" ;;
    x86_64|arm64) echo "$in" ;;
    *) echo "$in" ;;
  esac
}

detect_mode() {
  local os_name=""
  os_name="$(uname -s || true)"
  if [[ "$os_name" == "Darwin" ]]; then
    echo "macos"
    return
  fi
  if [[ "$os_name" != "Linux" ]]; then
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

require_cmd() {
  local cmd="$1"
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "ERROR: required command not found: $cmd" >&2
    exit 1
  fi
}

resolve_asset_for_mode() {
  local mode="$1"
  local arch="$2"
  case "$mode" in
    linux|ubuntu)
      case "$arch" in
        x86_64) echo "thagore-linux-x86_64.tar.gz" ;;
        arm64) echo "thagore-linux-arm64.tar.gz" ;;
        *)
          echo "ERROR: unsupported Linux arch '$arch'." >&2
          exit 1
          ;;
      esac
      ;;
    macos)
      case "$arch" in
        arm64) echo "thagore-macos-arm64.tar.gz" ;;
        *)
          echo "ERROR: macOS currently supports only arm64 release asset." >&2
          exit 1
          ;;
      esac
      ;;
    portable)
      case "$arch" in
        x86_64) echo "thagore-linux-x86_64.tar.gz" ;;
        arm64) echo "thagore-linux-arm64.tar.gz" ;;
        *)
          echo "ERROR: unsupported portable arch '$arch'." >&2
          exit 1
          ;;
      esac
      ;;
    *)
      echo "ERROR: unsupported mode '$mode'." >&2
      exit 1
      ;;
  esac
}

fetch_release_meta() {
  local asset="$1"
  local checksum="$2"
  local tag="$3"
  local py_bin="$4"
  "$py_bin" - "$API_BASE" "$asset" "$checksum" "$tag" <<'PY'
import json
import re
import sys
import urllib.request

api_base, asset_name, checksum_name, requested_tag = sys.argv[1:5]
headers = {"User-Agent": "thagup-init"}
tag_re = re.compile(r"^v\d+\.\d+\.\d+$")

def load_json(url: str):
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req) as resp:
        return json.load(resp)

def emit_if_valid(rel: dict):
    if rel.get("draft") or rel.get("prerelease"):
        return False
    tag = str(rel.get("tag_name", "")).strip()
    if not tag_re.match(tag):
        return False
    assets = rel.get("assets", [])
    by_name = {str(a.get("name", "")): str(a.get("browser_download_url", "")) for a in assets}
    if asset_name not in by_name or checksum_name not in by_name:
        return False
    print(f"{tag}|{by_name[asset_name]}|{by_name[checksum_name]}")
    return True

if requested_tag:
    rel = load_json(f"{api_base}/releases/tags/{requested_tag}")
    if not emit_if_valid(rel):
        raise SystemExit(f"ERROR: release {requested_tag} missing required assets: {asset_name}, {checksum_name}")
    raise SystemExit(0)

rels = load_json(f"{api_base}/releases?per_page=100")
for rel in rels:
    if emit_if_valid(rel):
        raise SystemExit(0)
raise SystemExit(f"ERROR: no stable release found with assets: {asset_name}, {checksum_name}")
PY
}

verify_checksum() {
  local archive="$1"
  local checksum_file="$2"
  local asset="$3"
  local expected=""
  local actual=""
  expected="$(awk -v target="$asset" '{name=$2; sub(/^\*/, "", name); if (name == target) {print $1; exit}}' "$checksum_file")"
  if [[ -z "$expected" ]]; then
    echo "ERROR: checksum entry for $asset missing in $checksum_file" >&2
    exit 1
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    actual="$(sha256sum "$archive" | awk '{print $1}')"
  elif command -v shasum >/dev/null 2>&1; then
    actual="$(shasum -a 256 "$archive" | awk '{print $1}')"
  else
    echo "ERROR: no SHA256 tool available (sha256sum/shasum)." >&2
    exit 1
  fi
  if [[ "${expected,,}" != "${actual,,}" ]]; then
    echo "ERROR: checksum mismatch for $asset" >&2
    echo "expected=$expected" >&2
    echo "actual=$actual" >&2
    exit 1
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag)
      REQUESTED_TAG="${2:-}"
      shift 2
      ;;
    --mode)
      REQUESTED_MODE="${2:-}"
      shift 2
      ;;
    --arch)
      REQUESTED_ARCH="${2:-}"
      shift 2
      ;;
    --prefix)
      REQUESTED_PREFIX="${2:-}"
      shift 2
      ;;
    --llvm-version)
      REQUESTED_LLVM_VERSION="${2:-}"
      shift 2
      ;;
    --skip-llvm)
      SKIP_LLVM=1
      shift
      ;;
    --interactive)
      ASSUME_YES=0
      shift
      ;;
    --yes|-y)
      ASSUME_YES=1
      shift
      ;;
    --help|-h)
      print_help
      exit 0
      ;;
    *)
      echo "ERROR: unknown argument: $1" >&2
      print_help
      exit 1
      ;;
  esac
done

require_cmd curl
require_cmd tar
require_cmd mktemp
if command -v python3 >/dev/null 2>&1; then
  PY_BIN="python3"
elif command -v python >/dev/null 2>&1; then
  PY_BIN="python"
else
  echo "ERROR: python3/python is required for GitHub release metadata lookup." >&2
  exit 1
fi

MODE="$REQUESTED_MODE"
if [[ "$MODE" == "auto" ]]; then
  MODE="$(detect_mode)"
fi
ARCH="$REQUESTED_ARCH"
if [[ "$ARCH" == "auto" ]]; then
  ARCH="$(uname -m || true)"
fi
ARCH="$(normalize_arch "$ARCH")"

ASSET="$(resolve_asset_for_mode "$MODE" "$ARCH")"
ASSET_TAG="${ASSET#thagore-}"
ASSET_TAG="${ASSET_TAG%.tar.gz}"
CHECKSUM_ASSET="SHA256SUMS-${ASSET_TAG}.txt"

RELEASE_META="$(fetch_release_meta "$ASSET" "$CHECKSUM_ASSET" "$REQUESTED_TAG" "$PY_BIN")"
RELEASE_TAG=""
ASSET_URL=""
CHECKSUM_URL=""
IFS='|' read -r RELEASE_TAG ASSET_URL CHECKSUM_URL <<<"$RELEASE_META"
if [[ -z "$RELEASE_TAG" || -z "$ASSET_URL" || -z "$CHECKSUM_URL" ]]; then
  echo "ERROR: failed to resolve release metadata for asset $ASSET" >&2
  exit 1
fi

TMP_DIR="$(mktemp -d)"
cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

ARCHIVE_PATH="$TMP_DIR/$ASSET"
CHECKSUM_PATH="$TMP_DIR/$CHECKSUM_ASSET"
PAYLOAD_DIR="$TMP_DIR/payload"

echo "[thagup] release=$RELEASE_TAG mode=$MODE arch=$ARCH asset=$ASSET"
echo "[thagup] downloading payload..."
curl -fsSL "$ASSET_URL" -o "$ARCHIVE_PATH"
echo "[thagup] downloading checksum..."
curl -fsSL "$CHECKSUM_URL" -o "$CHECKSUM_PATH"
verify_checksum "$ARCHIVE_PATH" "$CHECKSUM_PATH" "$ASSET"

mkdir -p "$PAYLOAD_DIR"
tar -xzf "$ARCHIVE_PATH" -C "$PAYLOAD_DIR"
if [[ ! -f "$PAYLOAD_DIR/installer/install.sh" ]]; then
  echo "ERROR: payload missing installer script: installer/install.sh" >&2
  exit 1
fi

INSTALL_CMD=(bash "$PAYLOAD_DIR/installer/install.sh" --mode "$MODE" --arch "$ARCH" --llvm-version "$REQUESTED_LLVM_VERSION")
if [[ -n "$REQUESTED_PREFIX" ]]; then
  INSTALL_CMD+=(--prefix "$REQUESTED_PREFIX")
fi
if [[ "$SKIP_LLVM" == "1" ]]; then
  INSTALL_CMD+=(--skip-llvm)
fi
if [[ "$ASSUME_YES" == "1" ]]; then
  INSTALL_CMD+=(--yes)
fi

export THAGORE_ROOT="$PAYLOAD_DIR"
echo "[thagup] running installer..."
"${INSTALL_CMD[@]}"
echo "[thagup] done."

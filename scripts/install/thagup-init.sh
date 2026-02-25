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
REQUESTED_PROFILE=""
REQUESTED_TARGETS=""
SKIP_LLVM=0
ASSUME_YES=1

SUPPORTED_TARGETS=(
  "x86_64-unknown-linux-gnu"
  "x86_64-pc-windows-msvc"
  "aarch64-apple-darwin"
  "aarch64-unknown-linux-gnu"
  "x86_64-apple-darwin"
)

STANDARD_TARGETS=(
  "x86_64-unknown-linux-gnu"
  "x86_64-pc-windows-msvc"
  "aarch64-apple-darwin"
  "aarch64-unknown-linux-gnu"
  "x86_64-apple-darwin"
)

print_help() {
  cat <<'EOF'
thagup-init.sh - One-shot Thagore installer bootstrap (rustup-style)

Usage:
  thagup-init.sh [options]

Options:
  --tag <vX.Y.Z>                    Install a specific release tag (default: latest stable)
  --mode <auto|linux|ubuntu|macos|portable>
  --arch <auto|x86_64|arm64>
  --prefix <path>                   Install prefix for toolchain payload
  --llvm-version <21.1.8>           LLVM version expected by installer
  --profile <standard|custom>       Target profile
  --targets <triple,triple,...>     Target triples for custom profile
  --skip-llvm                       Skip LLVM provisioning
  --interactive                     Ask for confirmation/profile selection
  --yes                             Force non-interactive install
  --help

Examples:
  curl -fsSL https://raw.githubusercontent.com/thagore-foundation/thagore/main/scripts/install/thagup-init.sh | bash
  bash thagup-init.sh --tag v0.5.30 --mode linux --arch x86_64 --profile standard
  bash thagup-init.sh --profile custom --targets x86_64-unknown-linux-gnu,aarch64-unknown-linux-gnu
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
  local release_tag="$tag"
  if [[ -z "$release_tag" ]]; then
    local latest
    latest="$(curl -fsSL "${API_BASE}/releases/latest" | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1 || true)"
    [[ -n "$latest" ]] || {
      echo "ERROR: cannot resolve latest release tag from GitHub API." >&2
      exit 1
    }
    release_tag="$latest"
  fi
  local asset_url="https://github.com/${REPO_OWNER}/${REPO_NAME}/releases/download/${release_tag}/${asset}"
  local checksum_url="https://github.com/${REPO_OWNER}/${REPO_NAME}/releases/download/${release_tag}/${checksum}"
  echo "${release_tag}|${asset_url}|${checksum_url}"
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

join_by_comma() {
  local out=""
  local first=1
  local item=""
  for item in "$@"; do
    if [[ "$first" == "1" ]]; then
      out="$item"
      first=0
    else
      out="$out,$item"
    fi
  done
  echo "$out"
}

is_supported_target() {
  local needle="$1"
  local t=""
  for t in "${SUPPORTED_TARGETS[@]}"; do
    if [[ "$t" == "$needle" ]]; then
      return 0
    fi
  done
  return 1
}

normalize_targets_csv() {
  local raw="$1"
  local cleaned="${raw// /}"
  local IFS=','
  local parts=()
  read -r -a parts <<< "$cleaned"
  local out=()
  local p=""
  for p in "${parts[@]}"; do
    [[ -n "$p" ]] || continue
    if ! is_supported_target "$p"; then
      echo "ERROR: unsupported target triple: $p" >&2
      exit 1
    fi
    out+=("$p")
  done
  if [[ "${#out[@]}" -eq 0 ]]; then
    echo "ERROR: target list is empty." >&2
    exit 1
  fi
  join_by_comma "${out[@]}"
}

print_supported_targets() {
  local t=""
  for t in "${SUPPORTED_TARGETS[@]}"; do
    echo "  - $t"
  done
}

default_target_for_host() {
  local mode="$1"
  local arch="$2"
  if [[ "$mode" == "macos" ]]; then
    if [[ "$arch" == "arm64" ]]; then
      echo "aarch64-apple-darwin"
      return
    fi
    echo "x86_64-apple-darwin"
    return
  fi
  if [[ "$mode" == "linux" || "$mode" == "ubuntu" || "$mode" == "portable" ]]; then
    if [[ "$arch" == "arm64" ]]; then
      echo "aarch64-unknown-linux-gnu"
      return
    fi
    echo "x86_64-unknown-linux-gnu"
    return
  fi
  echo "x86_64-unknown-linux-gnu"
}

resolve_profile_and_targets() {
  local mode="$1"
  local arch="$2"
  local selected_profile="$REQUESTED_PROFILE"
  local selected_targets="$REQUESTED_TARGETS"

  if [[ -z "$selected_profile" ]]; then
    if [[ "$ASSUME_YES" == "1" ]]; then
      selected_profile="standard"
    else
      echo "[thagup] Choose install profile:"
      echo "  1) standard (recommended)"
      echo "  2) custom"
      read -r -p "Profile [1/2]: " choice
      if [[ "$choice" == "2" ]]; then
        selected_profile="custom"
      else
        selected_profile="standard"
      fi
    fi
  fi

  if [[ "$selected_profile" != "standard" && "$selected_profile" != "custom" ]]; then
    echo "ERROR: --profile must be standard or custom." >&2
    exit 1
  fi

  if [[ "$selected_profile" == "standard" ]]; then
    selected_targets="$(join_by_comma "${STANDARD_TARGETS[@]}")"
  else
    if [[ -z "$selected_targets" ]]; then
      if [[ "$ASSUME_YES" == "1" ]]; then
        echo "ERROR: custom profile requires --targets in non-interactive mode." >&2
        exit 1
      fi
      echo "[thagup] Supported targets:"
      print_supported_targets
      read -r -p "Enter comma-separated targets: " selected_targets
    fi
    selected_targets="$(normalize_targets_csv "$selected_targets")"
  fi

  REQUESTED_PROFILE="$selected_profile"
  REQUESTED_TARGETS="$selected_targets"
  REQUESTED_DEFAULT_TARGET="$(default_target_for_host "$mode" "$arch")"
}

write_user_config() {
  local release_tag="$1"
  local ctl="$PAYLOAD_DIR/scripts/toolchainctl.sh"
  if [[ ! -f "$ctl" ]]; then
    ctl="$PWD/scripts/toolchainctl.sh"
  fi
  if [[ ! -f "$ctl" ]]; then
    echo "ERROR: missing toolchainctl script." >&2
    exit 1
  fi
  bash "$ctl" init \
    --profile "$REQUESTED_PROFILE" \
    --targets "$REQUESTED_TARGETS" \
    --default-target "$REQUESTED_DEFAULT_TARGET" \
    --toolchain-version "$release_tag" \
    --channel "stable"
}

THAGORE_HOME="${HOME}/.thagore"
TOOLCHAIN_DIR="${THAGORE_HOME}/toolchains/stable"
BIN_DIR="${THAGORE_HOME}/bin"

resolve_core_asset_for_mode() {
  local mode="$1"
  case "$mode" in
    linux|ubuntu|portable) echo "thagc-core-linux.tar.gz" ;;
    macos) echo "thagc-core-macos.tar.gz" ;;
    *) echo "ERROR: unsupported mode for core asset: $mode" >&2; exit 1 ;;
  esac
}

resolve_target_asset() {
  local triple="$1"
  local mode="$2"
  case "$mode" in
    linux|ubuntu|portable) echo "thagc-target-${triple}-linux.tar.gz" ;;
    macos) echo "thagc-target-${triple}-macos.tar.gz" ;;
    *) echo "ERROR: unsupported mode for target asset: $mode" >&2; exit 1 ;;
  esac
}

install_core_bundle() {
  local release_tag="$1"
  local mode="$2"
  local core_asset
  core_asset="$(resolve_core_asset_for_mode "$mode")"
  local core_checksum="SHA256SUMS-thagc-${mode}.txt"
  if [[ "$mode" == "ubuntu" || "$mode" == "portable" ]]; then
    core_checksum="SHA256SUMS-thagc-linux.txt"
    core_asset="thagc-core-linux.tar.gz"
  fi

  local core_url="https://github.com/${REPO_OWNER}/${REPO_NAME}/releases/download/${release_tag}/${core_asset}"
  local sum_url="https://github.com/${REPO_OWNER}/${REPO_NAME}/releases/download/${release_tag}/${core_checksum}"

  local archive_path="$TMP_DIR/$core_asset"
  local sum_path="$TMP_DIR/$core_checksum"

  echo "[thagup] Downloading core bundle: $core_asset"
  curl -fsSL "$core_url" -o "$archive_path"
  echo "[thagup] Downloading checksums: $core_checksum"
  curl -fsSL "$sum_url" -o "$sum_path"
  verify_checksum "$archive_path" "$sum_path" "$core_asset"

  mkdir -p "$TOOLCHAIN_DIR/bin"
  tar -xzf "$archive_path" -C "$TOOLCHAIN_DIR"
  echo "[thagup] Core bundle installed to $TOOLCHAIN_DIR"
}

install_target_pack() {
  local release_tag="$1"
  local triple="$2"
  local mode="$3"
  local target_asset
  target_asset="$(resolve_target_asset "$triple" "$mode")"
  local os_slug="$mode"
  if [[ "$mode" == "ubuntu" || "$mode" == "portable" ]]; then
    os_slug="linux"
    target_asset="thagc-target-${triple}-linux.tar.gz"
  fi
  local sum_file="SHA256SUMS-thagc-${os_slug}.txt"

  local asset_url="https://github.com/${REPO_OWNER}/${REPO_NAME}/releases/download/${release_tag}/${target_asset}"
  local sum_url_local="https://github.com/${REPO_OWNER}/${REPO_NAME}/releases/download/${release_tag}/${sum_file}"

  local archive_path="$TMP_DIR/$target_asset"
  local sum_path="$TMP_DIR/${sum_file}-${triple}"

  echo "[thagup] Downloading target pack: $target_asset ($triple)"
  curl -fsSL "$asset_url" -o "$archive_path"
  curl -fsSL "$sum_url_local" -o "$sum_path"
  verify_checksum "$archive_path" "$sum_path" "$target_asset"

  local pack_dir="$TOOLCHAIN_DIR/targets/${triple}"
  mkdir -p "$pack_dir"
  tar -xzf "$archive_path" -C "$pack_dir"
  echo "[thagup] Target pack installed: $triple -> $pack_dir"
}

install_target_packs() {
  local targets_csv="$REQUESTED_TARGETS"
  local IFS=','
  local targets_arr=()
  read -r -a targets_arr <<< "$targets_csv"
  for triple in "${targets_arr[@]}"; do
    [[ -n "$triple" ]] || continue
    install_target_pack "$RELEASE_TAG" "$triple" "$MODE"
  done
}

setup_bin_dir() {
  mkdir -p "$BIN_DIR"

  # Create thagc symlink/wrapper pointing to toolchain binary
  local thagc_src=""
  for candidate in "$TOOLCHAIN_DIR/bin/thagc" "$TOOLCHAIN_DIR/bin/thagc.exe"; do
    if [[ -f "$candidate" ]]; then
      thagc_src="$candidate"
      break
    fi
  done
  if [[ -n "$thagc_src" ]]; then
    ln -sf "$thagc_src" "$BIN_DIR/thagc" 2>/dev/null || cp "$thagc_src" "$BIN_DIR/thagc"
    chmod +x "$BIN_DIR/thagc" || true
    echo "[thagup] thagc linked to $BIN_DIR/thagc"
  fi

  # Install thagup self-updater — prefer bundle, fallback to direct download
  local thagup_installed=0
  if [[ -f "$TOOLCHAIN_DIR/thagup" ]]; then
    cp "$TOOLCHAIN_DIR/thagup" "$BIN_DIR/thagup"
    chmod +x "$BIN_DIR/thagup"
    echo "[thagup] thagup installed to $BIN_DIR/thagup"
    thagup_installed=1
  fi
  if [[ "$thagup_installed" == "0" ]]; then
    # Download thagup directly from the release tag
    local thagup_url="https://raw.githubusercontent.com/${REPO_OWNER}/${REPO_NAME}/refs/tags/${RELEASE_TAG}/scripts/install/thagup"
    echo "[thagup] Downloading thagup from $RELEASE_TAG..."
    if curl -fsSL "$thagup_url" -o "$BIN_DIR/thagup" 2>/dev/null; then
      chmod +x "$BIN_DIR/thagup"
      echo "[thagup] thagup installed to $BIN_DIR/thagup"
    else
      echo "[thagup] Warning: could not install thagup (non-fatal)"
    fi
  fi

  # Write env file
  cat > "$THAGORE_HOME/env" <<'ENVEOF'
# Thagore environment — source this file or add to your shell profile
export THAGORE_HOME="$HOME/.thagore"
export PATH="$THAGORE_HOME/bin:$PATH"
ENVEOF
}

update_shell_profile() {
  local source_line='[ -f "$HOME/.thagore/env" ] && . "$HOME/.thagore/env"'
  for profile in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
    if [[ -f "$profile" ]]; then
      if ! grep -qF ".thagore/env" "$profile" 2>/dev/null; then
        echo "" >> "$profile"
        echo "# Thagore" >> "$profile"
        echo "$source_line" >> "$profile"
        echo "[thagup] Added Thagore to PATH in $profile"
      fi
    fi
  done
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
    --profile)
      REQUESTED_PROFILE="${2:-}"
      shift 2
      ;;
    --targets)
      REQUESTED_TARGETS="${2:-}"
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

MODE="$REQUESTED_MODE"
if [[ "$MODE" == "auto" ]]; then
  MODE="$(detect_mode)"
fi
ARCH="$REQUESTED_ARCH"
if [[ "$ARCH" == "auto" ]]; then
  ARCH="$(uname -m || true)"
fi
ARCH="$(normalize_arch "$ARCH")"

resolve_profile_and_targets "$MODE" "$ARCH"

ASSET="$(resolve_asset_for_mode "$MODE" "$ARCH")"
ASSET_TAG="${ASSET#thagore-}"
ASSET_TAG="${ASSET_TAG%.tar.gz}"
CHECKSUM_ASSET="SHA256SUMS-${ASSET_TAG}.txt"

RELEASE_META="$(fetch_release_meta "$ASSET" "$CHECKSUM_ASSET" "$REQUESTED_TAG")"
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
echo "[thagup] profile=$REQUESTED_PROFILE targets=$REQUESTED_TARGETS default_target=$REQUESTED_DEFAULT_TARGET"
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

echo "[thagup] Installing Thagore $RELEASE_TAG..."
install_core_bundle "$RELEASE_TAG" "$MODE"
install_target_packs
setup_bin_dir
update_shell_profile

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Thagore $RELEASE_TAG installed successfully!"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "  thagc   →  $BIN_DIR/thagc"
echo "  thagup  →  $BIN_DIR/thagup"
echo "  targets →  $TOOLCHAIN_DIR/targets/"
echo ""
if [[ -f "$BIN_DIR/thagup" ]]; then
  echo "  thagup is ready. Commands:"
  echo "    thagup update             # update thagc to latest"
  echo "    thagup install <tag>      # install specific version"
  echo "    thagup target add <triple> # add cross-compile target"
  echo "    thagup target list        # list installed targets"
  echo "    thagup self-update        # update thagup itself"
  echo ""
fi
echo "  Activate in current shell:"
echo "    source ~/.thagore/env"
echo ""
echo "  Or restart your terminal."
echo ""
echo "  Verify: thagc --version"
echo ""

#!/usr/bin/env bash
# thagup-init.sh — One-shot Thagore installer (rustup-style)
# Usage:
#   curl -fsSL https://thagore.org/thagup.sh | bash
#   bash thagup-init.sh [options]
set -euo pipefail

REPO_OWNER="thagore-foundation"
REPO_NAME="thagore"
API_BASE="https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}"
RELEASE_BASE="https://github.com/${REPO_OWNER}/${REPO_NAME}/releases/download"

REQUESTED_TAG=""
REQUESTED_MODE="auto"
REQUESTED_ARCH="auto"
REQUESTED_PREFIX=""
REQUESTED_PROFILE=""
REQUESTED_TARGETS=""
ASSUME_YES=1

THAGORE_HOME="${THAGORE_HOME:-$HOME/.thagore}"
TOOLCHAIN_DIR="${THAGORE_HOME}/toolchains/stable"
BIN_DIR="${THAGORE_HOME}/bin"

# ── Supported triples ────────────────────────────────────────────────────────

SUPPORTED_TARGETS=(
  "x86_64-unknown-linux-gnu"
  "aarch64-unknown-linux-gnu"
  "aarch64-apple-darwin"
  "x86_64-apple-darwin"
  "x86_64-pc-windows-msvc"
)

# Profiles
# default  = host triple only
# standard = 3 most-used: Linux x64, macOS arm64, Windows x64
# full     = all 5 targets
# custom   = user-supplied comma-separated triples

STANDARD_TARGETS=(
  "x86_64-unknown-linux-gnu"
  "aarch64-apple-darwin"
  "x86_64-pc-windows-msvc"
)

# ── Help ─────────────────────────────────────────────────────────────────────

print_help() {
  cat <<'EOF'
thagup-init.sh — Thagore installer

Usage:
  thagup-init.sh [options]

Options:
  --tag <vX.Y.Z>         Install a specific release tag (default: latest)
  --mode <auto|linux|ubuntu|macos|portable>
  --arch <auto|x86_64|arm64>
  --profile <default|standard|full|custom>
                           default:  host OS only             (~90 MB)
                           standard: Linux x64 + macOS arm64 + Windows x64
                           full:     all 5 targets
                           custom:   use --targets
  --targets <a,b,...>    Comma-separated triples (custom profile)
  --prefix <path>        Override install root (default: ~/.thagore)
  --interactive          Prompt for profile selection
  --yes / -y             Non-interactive (default)
  --help

Examples:
  curl -fsSL https://thagore.org/thagup.sh | bash
  bash thagup-init.sh --profile standard
  bash thagup-init.sh --profile custom --targets x86_64-unknown-linux-gnu
EOF
}

# ── Utility ──────────────────────────────────────────────────────────────────

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "ERROR: required command not found: $1" >&2; exit 1; }
}

normalize_arch() {
  case "${1:-}" in
    amd64|x86_64) echo "x86_64" ;;
    aarch64|arm64) echo "arm64" ;;
    *) echo "${1:-}" ;;
  esac
}

detect_mode() {
  local s; s="$(uname -s 2>/dev/null || true)"
  if [[ "$s" == "Darwin" ]]; then echo "macos"; return; fi
  if [[ "$s" != "Linux" ]]; then echo "portable"; return; fi
  if [[ -r /etc/os-release ]]; then
    local id like
    # shellcheck disable=SC1091
    . /etc/os-release
    id="${ID:-}"; like="${ID_LIKE:-}"
    if [[ "$id" == "ubuntu" || "$id" == "debian" || "$like" == *"ubuntu"* || "$like" == *"debian"* ]]; then
      echo "ubuntu"; return
    fi
  fi
  echo "linux"
}

join_by_comma() {
  local out="" first=1
  for i in "$@"; do
    [[ "$first" == "1" ]] && out="$i" || out="$out,$i"
    first=0
  done
  echo "$out"
}

is_supported_target() {
  local t; for t in "${SUPPORTED_TARGETS[@]}"; do [[ "$t" == "$1" ]] && return 0; done
  return 1
}

default_triple_for_host() {
  local mode="$1" arch="$2"
  if [[ "$mode" == "macos" ]]; then
    [[ "$arch" == "arm64" ]] && echo "aarch64-apple-darwin" || echo "x86_64-apple-darwin"
    return
  fi
  [[ "$arch" == "arm64" ]] && echo "aarch64-unknown-linux-gnu" || echo "x86_64-unknown-linux-gnu"
}

# ── Profile resolution ───────────────────────────────────────────────────────

resolve_profile() {
  local mode="$1" arch="$2"
  local profile="${REQUESTED_PROFILE:-}"
  local targets="${REQUESTED_TARGETS:-}"

  if [[ -z "$profile" ]]; then
    if [[ "$ASSUME_YES" == "1" ]]; then
      profile="default"
    else
      echo ""
      echo "Choose install profile:"
      echo "  1) default   — host OS only (~90 MB, fastest)"
      echo "  2) standard  — Linux x64 + macOS arm64 + Windows x64"
      echo "  3) full      — all 5 targets"
      echo "  4) custom    — choose your own"
      read -r -p "Profile [1-4, default=1]: " _choice
      case "${_choice:-1}" in
        2) profile="standard" ;;
        3) profile="full" ;;
        4) profile="custom" ;;
        *) profile="default" ;;
      esac
    fi
  fi

  case "$profile" in
    default)
      targets="$(default_triple_for_host "$mode" "$arch")"
      ;;
    standard)
      targets="$(join_by_comma "${STANDARD_TARGETS[@]}")"
      ;;
    full)
      targets="$(join_by_comma "${SUPPORTED_TARGETS[@]}")"
      ;;
    custom)
      if [[ -z "$targets" ]]; then
        if [[ "$ASSUME_YES" == "1" ]]; then
          echo "ERROR: --profile custom requires --targets in non-interactive mode." >&2
          exit 1
        fi
        echo "Supported targets:"
        local t; for t in "${SUPPORTED_TARGETS[@]}"; do echo "  $t"; done
        read -r -p "Enter comma-separated targets: " targets
      fi
      # Validate
      local IFS=','
      local cleaned=()
      local p
      for p in $targets; do
        p="${p// /}"
        [[ -z "$p" ]] && continue
        if ! is_supported_target "$p"; then
          echo "ERROR: unsupported target triple: $p" >&2; exit 1
        fi
        cleaned+=("$p")
      done
      [[ "${#cleaned[@]}" -eq 0 ]] && { echo "ERROR: empty target list." >&2; exit 1; }
      targets="$(join_by_comma "${cleaned[@]}")"
      ;;
    *)
      echo "ERROR: unknown profile '$profile'. Use default|standard|full|custom." >&2; exit 1 ;;
  esac

  RESOLVED_PROFILE="$profile"
  RESOLVED_TARGETS="$targets"
  RESOLVED_DEFAULT_TRIPLE="$(default_triple_for_host "$mode" "$arch")"
}

# ── Asset names ──────────────────────────────────────────────────────────────

core_asset_for_mode() {
  local mode="$1" arch="${2:-x86_64}"
  case "$mode" in
    macos)
      # macOS arm64 = aarch64-apple-darwin runner (macos-latest)
      # macOS x64   = x86_64-apple-darwin runner (macos-13)
      [[ "$arch" == "x86_64" ]] && echo "thagc-core-macos-x64.tar.gz" || echo "thagc-core-macos.tar.gz"
      ;;
    *)
      # Linux arm64 = aarch64 runner; Linux x64 = ubuntu-latest
      [[ "$arch" == "arm64" ]] && echo "thagc-core-linux-arm64.tar.gz" || echo "thagc-core-linux.tar.gz"
      ;;
  esac
}

checksum_file_for_mode() {
  local mode="$1" arch="${2:-x86_64}"
  case "$mode" in
    macos)
      [[ "$arch" == "x86_64" ]] && echo "SHA256SUMS-thagc-macos-x64.txt" || echo "SHA256SUMS-thagc-macos.txt"
      ;;
    *)
      [[ "$arch" == "arm64" ]] && echo "SHA256SUMS-thagc-linux-arm64.txt" || echo "SHA256SUMS-thagc-linux.txt"
      ;;
  esac
}

target_asset_for_triple() {
  # Release asset naming: thagc-target-<triple>-<os_slug>.tar.gz
  # os_slug is derived from the triple itself (not the host OS)
  local triple="$1"
  local os_slug
  if [[ "$triple" == *"apple"* || "$triple" == *"darwin"* ]]; then
    os_slug="macos"
  elif [[ "$triple" == *"windows"* ]]; then
    os_slug="windows"
  else
    os_slug="linux"
  fi
  echo "thagc-target-${triple}-${os_slug}.tar.gz"
}

checksum_file_for_triple() {
  local triple="$1"
  if [[ "$triple" == *"apple"* || "$triple" == *"darwin"* ]]; then
    echo "SHA256SUMS-thagc-macos.txt"
  elif [[ "$triple" == *"windows"* ]]; then
    echo "SHA256SUMS-thagc-windows.txt"
  else
    echo "SHA256SUMS-thagc-linux.txt"
  fi
}

# ── Release tag resolution ───────────────────────────────────────────────────

resolve_release_tag() {
  if [[ -n "${REQUESTED_TAG:-}" ]]; then
    echo "$REQUESTED_TAG"; return
  fi
  local tag
  tag="$(curl -fsSL "${API_BASE}/releases/latest" \
    | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' \
    | head -n 1 || true)"
  [[ -n "$tag" ]] || { echo "ERROR: cannot resolve latest release from GitHub API." >&2; exit 1; }
  echo "$tag"
}

# ── Checksum verification ─────────────────────────────────────────────────────

verify_checksum() {
  local archive="$1" sum_file="$2" asset_name="$3"
  local expected actual
  expected="$(awk -v f="$asset_name" '
    {name=$2; sub(/^\*/, "", name); if (name==f){print $1; exit}}
  ' "$sum_file")"
  if [[ -z "$expected" ]]; then
    echo "[thagup] WARNING: no checksum entry for $asset_name in $sum_file — skipping verify"
    return 0
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    actual="$(sha256sum "$archive" | awk '{print $1}')"
  else
    actual="$(shasum -a 256 "$archive" | awk '{print $1}')"
  fi
  if [[ "${expected,,}" != "${actual,,}" ]]; then
    echo "ERROR: checksum mismatch for $asset_name" >&2
    echo "  expected: $expected" >&2
    echo "  actual:   $actual" >&2
    exit 1
  fi
  echo "[thagup] checksum OK: $asset_name"
}

# ── Cached checksum files (avoid re-downloading same SHA256SUMS) ──────────────

declare -A _DOWNLOADED_SUMS=()

get_sum_file() {
  local sum_asset="$1" tag="$2" dest="$3"
  local key="${tag}/${sum_asset}"
  if [[ -n "${_DOWNLOADED_SUMS[$key]:-}" ]]; then
    cp "${_DOWNLOADED_SUMS[$key]}" "$dest"
    return
  fi
  local url="${RELEASE_BASE}/${tag}/${sum_asset}"
  echo "[thagup] Downloading checksums: $sum_asset"
  curl -fsSL "$url" -o "$dest" || {
    echo "[thagup] WARNING: no checksum file at $url — skipping verification"
    touch "$dest"
  }
  _DOWNLOADED_SUMS["$key"]="$dest"
}

# ── Install functions ─────────────────────────────────────────────────────────

install_core_bundle() {
  local tag="$1" mode="$2" arch="${3:-x86_64}"
  local asset sum_asset
  asset="$(core_asset_for_mode "$mode" "$arch")"
  sum_asset="$(checksum_file_for_mode "$mode" "$arch")"

  local url="${RELEASE_BASE}/${tag}/${asset}"
  local archive="$TMP_DIR/$asset"
  local sum_file="$TMP_DIR/${sum_asset}"

  echo "[thagup] Downloading core bundle: $asset"
  curl -fsSL "$url" -o "$archive" || {
    echo "ERROR: failed to download $url" >&2
    echo "  Is tag $tag published? Check https://github.com/${REPO_OWNER}/${REPO_NAME}/releases" >&2
    exit 1
  }
  get_sum_file "$sum_asset" "$tag" "$sum_file"
  verify_checksum "$archive" "$sum_file" "$asset"

  mkdir -p "$TOOLCHAIN_DIR"
  tar -xzf "$archive" -C "$TOOLCHAIN_DIR"
  echo "[thagup] Core installed → $TOOLCHAIN_DIR"
}

install_target_pack() {
  local tag="$1" triple="$2"
  local asset sum_asset
  asset="$(target_asset_for_triple "$triple")"
  sum_asset="$(checksum_file_for_triple "$triple")"

  local url="${RELEASE_BASE}/${tag}/${asset}"
  local archive="$TMP_DIR/${asset}"
  local sum_file="$TMP_DIR/${sum_asset}-${triple}"

  echo "[thagup] Downloading target pack: $triple"
  # Target pack may not exist for all platforms yet — warn but don't fail
  if ! curl -fsSL "$url" -o "$archive" 2>/dev/null; then
    echo "[thagup] WARNING: target pack not available for $triple ($url) — skipping"
    return 0
  fi
  get_sum_file "$sum_asset" "$tag" "$sum_file"
  verify_checksum "$archive" "$sum_file" "$asset"

  local pack_dir="$TOOLCHAIN_DIR/targets/${triple}"
  mkdir -p "$pack_dir"
  tar -xzf "$archive" -C "$pack_dir"
  echo "[thagup] Target installed → $pack_dir"
}

install_target_packs() {
  local IFS=','
  local t
  for t in $RESOLVED_TARGETS; do
    [[ -n "$t" ]] && install_target_pack "$RELEASE_TAG" "$t"
  done
}

write_thagc_config() {
  # Write ~/.thagc/config.toml so the compiler knows its default target
  local cfg_dir="$HOME/.thagc"
  mkdir -p "$cfg_dir"
  local cfg="$cfg_dir/config.toml"
  # Build installed_targets list
  local targets_toml=""
  local IFS=','
  local t first=1
  for t in $RESOLVED_TARGETS; do
    [[ -z "$t" ]] && continue
    if [[ "$first" == "1" ]]; then
      targets_toml="\"$t\""
      first=0
    else
      targets_toml="$targets_toml, \"$t\""
    fi
  done
  cat > "$cfg" <<TOML
# Thagore toolchain config — managed by thagup
default_target = "$RESOLVED_DEFAULT_TRIPLE"
installed_targets = [$targets_toml]
toolchain_dir = "$TOOLCHAIN_DIR"
TOML
  echo "[thagup] Wrote $cfg"
}

setup_bin_dir() {
  mkdir -p "$BIN_DIR"

  # thagc symlink
  local thagc_src=""
  local c
  for c in "$TOOLCHAIN_DIR/bin/thagc" "$TOOLCHAIN_DIR/bin/thagc.exe"; do
    [[ -f "$c" ]] && thagc_src="$c" && break
  done
  if [[ -n "$thagc_src" ]]; then
    ln -sf "$thagc_src" "$BIN_DIR/thagc" 2>/dev/null || cp "$thagc_src" "$BIN_DIR/thagc"
    chmod +x "$BIN_DIR/thagc" || true
    echo "[thagup] thagc → $BIN_DIR/thagc"
  else
    echo "[thagup] WARNING: thagc binary not found in core bundle"
  fi

  # stage1_helper (needed by wrapper at runtime)
  local h1
  for h1 in "$TOOLCHAIN_DIR/bin/stage1_helper" "$TOOLCHAIN_DIR/bin/stage1_helper.exe"; do
    if [[ -f "$h1" ]]; then
      cp "$h1" "$BIN_DIR/stage1_helper"
      chmod +x "$BIN_DIR/stage1_helper" || true
      # Patch GLIBC version requirements so helper works on GLIBC_2.35+ (Ubuntu 22.04)
      # The seed helper was built on Ubuntu 24.04 which uses GLIBC_2.38 for C23 symbols.
      if [[ "$(uname -s)" == "Linux" ]] && command -v python3 &>/dev/null; then
        python3 - "$BIN_DIR/stage1_helper" <<'PATCH_PYEOF' 2>/dev/null || true
import struct, sys, os
def elf_hash(n):
    h = 0
    for c in n.encode():
        h = (h << 4) + c; g = h & 0xF0000000
        if g: h ^= g >> 24
        h &= ~g
    return h & 0xFFFFFFFF
PATCHES = [("GLIBC_2.38","GLIBC_2.35"),("GLIBCXX_3.4.31","GLIBCXX_3.4.30")]
path = sys.argv[1]
with open(path,"rb") as f: data = bytearray(f.read())
if data[:4] != b"\x7fELF": sys.exit(0)
for old,new in PATCHES:
    ob,nb = old.encode(),new.encode()
    p = data.find(ob)
    if p == -1: continue
    data[p:p+len(ob)] = nb
    oh,nh = struct.pack("<I",elf_hash(old)),struct.pack("<I",elf_hash(new))
    hp = data.find(oh)
    if hp != -1: data[hp:hp+4] = nh
with open(path,"wb") as f: f.write(data)
PATCH_PYEOF
      fi
      break
    fi
  done

  # ── thag_runtime.lib ────────────────────────────────────────────────────────
  # Required by stage1_helper to link compiled user programs.
  # thagc.bin (our libthag_runtime.a-linked binary) auto-detects this at
  # <exe_dir>/../lib/thag_runtime.lib and symlinks it into CWD at build time.
  local rtlib_dest="$THAGORE_HOME/lib/thag_runtime.lib"
  mkdir -p "$THAGORE_HOME/lib"
  local rtlib_src=""
  for rt in "$TOOLCHAIN_DIR/lib/thag_runtime.lib" \
            "$TOOLCHAIN_DIR/lib/libthag_runtime.a" \
            "$TOOLCHAIN_DIR/thag_runtime.lib" \
            "$TOOLCHAIN_DIR/libthag_runtime.a"; do
    [[ -f "$rt" ]] && rtlib_src="$rt" && break
  done
  if [[ -n "$rtlib_src" ]]; then
    cp "$rtlib_src" "$rtlib_dest"
    echo "[thagup] runtime lib → $rtlib_dest"
  else
    echo "[thagup] WARNING: thag_runtime.lib not found in bundle; build step may fail"
  fi

  # ── libstdc++.so symlink ─────────────────────────────────────────────────────
  # On Ubuntu 22.04, libstdc++.so (linker stub) may not exist; only libstdc++.so.6.
  # thagc.bin sets LIBRARY_PATH=<exe_dir>/../lib, so we provide the stub there.
  if [[ "$(uname -s)" == "Linux" ]]; then
    local stdcxx_so=""
    for p in /usr/lib/x86_64-linux-gnu/libstdc++.so.6 \
              /usr/lib/aarch64-linux-gnu/libstdc++.so.6 \
              /usr/lib/libstdc++.so.6; do
      [[ -f "$p" ]] && stdcxx_so="$p" && break
    done
    if [[ -n "$stdcxx_so" ]] && [[ ! -e "$THAGORE_HOME/lib/libstdc++.so" ]]; then
      ln -sf "$stdcxx_so" "$THAGORE_HOME/lib/libstdc++.so" 2>/dev/null || true
      echo "[thagup] libstdc++.so → $stdcxx_so"
    fi
  fi

  # ── clang symlink ────────────────────────────────────────────────────────────
  # stage1_helper looks for clang at /usr/bin/clang and /usr/local/bin/clang.
  # On Ubuntu 22.04+, only versioned clang-N exists. Create a symlink in our bin.
  if [[ "$(uname -s)" == "Linux" ]] && [[ ! -f "/usr/bin/clang" ]] && [[ ! -f "/usr/local/bin/clang" ]]; then
    for v in 21 20 19 18 17 16 15 14; do
      if command -v "clang-$v" &>/dev/null; then
        ln -sf "$(command -v "clang-$v")" "$BIN_DIR/clang" 2>/dev/null || true
        echo "[thagup] clang → clang-$v (symlink in $BIN_DIR)"
        break
      fi
    done
  fi

  # thagup self-updater
  local up
  for up in "$TOOLCHAIN_DIR/thagup" "$TOOLCHAIN_DIR/bin/thagup"; do
    if [[ -f "$up" ]]; then
      cp "$up" "$BIN_DIR/thagup"
      chmod +x "$BIN_DIR/thagup"
      echo "[thagup] thagup → $BIN_DIR/thagup"
      break
    fi
  done

  # env file
  cat > "$THAGORE_HOME/env" <<'ENVEOF'
# Thagore environment — source this or add to shell profile
export THAGORE_HOME="$HOME/.thagore"
export PATH="$THAGORE_HOME/bin:$PATH"
ENVEOF
}

update_shell_profile() {
  local line='[ -f "$HOME/.thagore/env" ] && . "$HOME/.thagore/env"'
  local p
  for p in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
    if [[ -f "$p" ]] && ! grep -qF ".thagore/env" "$p" 2>/dev/null; then
      printf '\n# Thagore\n%s\n' "$line" >> "$p"
      echo "[thagup] Added to $p"
    fi
  done
}

# ── Argument parsing ──────────────────────────────────────────────────────────

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag)         REQUESTED_TAG="${2:-}";     shift 2 ;;
    --mode)        REQUESTED_MODE="${2:-}";    shift 2 ;;
    --arch)        REQUESTED_ARCH="${2:-}";    shift 2 ;;
    --prefix)      REQUESTED_PREFIX="${2:-}";  shift 2 ;;
    --profile)     REQUESTED_PROFILE="${2:-}"; shift 2 ;;
    --targets)     REQUESTED_TARGETS="${2:-}"; shift 2 ;;
    --interactive) ASSUME_YES=0;               shift   ;;
    --yes|-y)      ASSUME_YES=1;               shift   ;;
    --help|-h)     print_help; exit 0 ;;
    *) echo "ERROR: unknown argument: $1" >&2; print_help; exit 1 ;;
  esac
done

# Override install root if --prefix supplied
if [[ -n "$REQUESTED_PREFIX" ]]; then
  THAGORE_HOME="$REQUESTED_PREFIX"
  TOOLCHAIN_DIR="${THAGORE_HOME}/toolchains/stable"
  BIN_DIR="${THAGORE_HOME}/bin"
fi

require_cmd curl
require_cmd tar
require_cmd mktemp

# Detect environment
MODE="$REQUESTED_MODE"
[[ "$MODE" == "auto" ]] && MODE="$(detect_mode)"
ARCH="$REQUESTED_ARCH"
[[ "$ARCH" == "auto" ]] && ARCH="$(normalize_arch "$(uname -m 2>/dev/null || echo x86_64)")"

# Resolve profile + targets
resolve_profile "$MODE" "$ARCH"

# Resolve release tag
RELEASE_TAG="$(resolve_release_tag)"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

echo ""
echo "  Thagore Installer"
echo "  ─────────────────────────────────────────"
echo "  release  : $RELEASE_TAG"
echo "  mode     : $MODE  arch: $ARCH"
echo "  profile  : $RESOLVED_PROFILE"
echo "  targets  : $RESOLVED_TARGETS"
echo "  install  : $THAGORE_HOME"
echo ""

# Install
install_core_bundle "$RELEASE_TAG" "$MODE" "$ARCH"
install_target_packs
write_thagc_config
setup_bin_dir
update_shell_profile

echo ""
echo "  ✓ Thagore $RELEASE_TAG installed successfully!"
echo ""
echo "  Compiler : $BIN_DIR/thagc"
echo "  Targets  : $TOOLCHAIN_DIR/targets/"
echo ""
echo "  Activate:"
echo "    source ~/.thagore/env"
echo "  Or restart your shell, then:"
echo "    thagc --version"
echo "    thagc build hello.tg"
echo ""

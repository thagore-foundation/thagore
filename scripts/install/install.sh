#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODE="auto"
LLVM_VERSION="21.1.8"
ARCH="auto"
ASSUME_YES=0
SKIP_LLVM=0
SKIP_PAYLOAD=0
INSTALL_PREFIX=""
LLVM_PREFIX="${THAGORE_LLVM_PREFIX:-$HOME/.thagore/llvm-21.1.8}"
LLVM_PREFIX_EXPLICIT=0
if [[ -n "${THAGORE_LLVM_PREFIX:-}" ]]; then
  LLVM_PREFIX_EXPLICIT=1
fi

print_help() {
  cat <<'EOF'
Usage: install.sh [--mode auto|linux|ubuntu|macos|portable] [options]

Examples:
  install.sh --mode auto --yes
  install.sh --mode linux --prefix /opt/thagore --yes
  install.sh --mode ubuntu --yes
  install.sh --mode macos --yes
  install.sh --mode portable --prefix "$HOME/.local/share/thagore --yes

Options:
  --mode <auto|linux|ubuntu|macos|portable>
  --llvm-version <version>
  --arch <x86_64|arm64>
  --prefix <path>
  --llvm-prefix <path>     (portable mode LLVM prefix)
  --skip-llvm
  --skip-payload
  --yes | -y
EOF
}

normalize_arch() {
  local in="$1"
  if [[ "$in" == "amd64" ]]; then echo "x86_64"; return; fi
  if [[ "$in" == "aarch64" ]]; then echo "arm64"; return; fi
  echo "$in"
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

resolve_release_asset() {
  case "$MODE" in
    linux|ubuntu)
      case "$ARCH" in
        x86_64) echo "thagore-linux-x86_64.tar.gz" ;;
        arm64) echo "thagore-linux-arm64.tar.gz" ;;
        *)
          echo "ERROR: unsupported Linux arch '$ARCH' for release payload." >&2
          return 1
          ;;
      esac
      ;;
    macos)
      case "$ARCH" in
        arm64) echo "thagore-macos-arm64.tar.gz" ;;
        *)
          echo "ERROR: unsupported macOS arch '$ARCH' for release payload." >&2
          return 1
          ;;
      esac
      ;;
    *)
      echo ""
      ;;
  esac
}

download_release_payload() {
  local outdir="$1"
  local asset
  asset="$(resolve_release_asset)"
  if [[ -z "$asset" ]]; then
    echo ""
    return
  fi
  if ! command -v curl >/dev/null 2>&1; then
    echo "ERROR: curl is required to download Thagore release payload." >&2
    exit 1
  fi
  if ! command -v tar >/dev/null 2>&1; then
    echo "ERROR: tar is required to extract Thagore release payload." >&2
    exit 1
  fi
  local archive="$outdir/$asset"
  local asset_tag="${asset#thagore-}"
  asset_tag="${asset_tag%.tar.gz}"
  local checksum_asset="SHA256SUMS-${asset_tag}.txt"
  local proof_asset="thagore-enduser-verified-${asset_tag}.txt"
  local checksum_file="$outdir/$checksum_asset"
  local extract_root="$outdir/payload"
  mkdir -p "$extract_root"
  local py_bin=""
  if command -v python3 >/dev/null 2>&1; then
    py_bin="python3"
  elif command -v python >/dev/null 2>&1; then
    py_bin="python"
  else
    echo "ERROR: python is required to resolve verified official release assets." >&2
    exit 1
  fi
  local release_meta
  release_meta="$("$py_bin" - "$asset" "$checksum_asset" "$proof_asset" <<'PY'
import json
import re
import sys
import urllib.request

asset = sys.argv[1]
checksum = sys.argv[2]
proof = sys.argv[3]
url = "https://api.github.com/repos/thagore-foundation/thagore/releases?per_page=100"
req = urllib.request.Request(url, headers={"User-Agent": "thagore-install"})
data = json.load(urllib.request.urlopen(req))
tag_re = re.compile(r"^v\d+\.\d+\.\d+$")
for rel in data:
    if rel.get("draft") or rel.get("prerelease"):
        continue
    tag = str(rel.get("tag_name", "")).strip()
    if not tag_re.match(tag):
        continue
    assets = rel.get("assets", [])
    names = {str(a.get("name", "")) for a in assets}
    if asset not in names or checksum not in names or proof not in names:
        continue
    by_name = {str(a.get("name", "")): str(a.get("browser_download_url", "")) for a in assets}
    print(f"{tag}|{by_name.get(asset,'')}|{by_name.get(checksum,'')}")
    break
PY
)"
  local release_tag=""
  local url=""
  local checksum_url=""
  IFS='|' read -r release_tag url checksum_url <<<"$release_meta"
  if [[ -z "$release_tag" || -z "$url" || -z "$checksum_url" ]]; then
    echo "ERROR: no verified official release found for $asset (missing $proof_asset)." >&2
    exit 1
  fi
  echo "[thagore-installer] Downloading compiler payload: $url"
  curl -fsSL "$url" -o "$archive"
  echo "[thagore-installer] Downloading checksum manifest: $checksum_url"
  curl -fsSL "$checksum_url" -o "$checksum_file"
  local expected_hash
  expected_hash="$(awk -v target="$asset" '$2 == target {print $1; exit}' "$checksum_file")"
  if [[ -z "$expected_hash" ]]; then
    echo "ERROR: checksum entry for $asset not found in $checksum_asset." >&2
    exit 1
  fi
  local actual_hash
  if command -v sha256sum >/dev/null 2>&1; then
    actual_hash="$(sha256sum "$archive" | awk '{print $1}')"
  elif command -v shasum >/dev/null 2>&1; then
    actual_hash="$(shasum -a 256 "$archive" | awk '{print $1}')"
  else
    echo "ERROR: no SHA256 tool available to verify payload checksum." >&2
    exit 1
  fi
  if [[ "${actual_hash,,}" != "${expected_hash,,}" ]]; then
    echo "ERROR: checksum mismatch for $asset (expected $expected_hash, got $actual_hash)." >&2
    exit 1
  fi
  tar -xzf "$archive" -C "$extract_root"
  if [[ -d "$extract_root/bin" && -d "$extract_root/lib/std" ]]; then
    if [[ ! -f "$extract_root/lib/runtime.a" && ! -f "$extract_root/lib/runtime.lib" ]]; then
      echo "ERROR: downloaded payload missing runtime library (expected lib/runtime.a or lib/runtime.lib)." >&2
      exit 1
    fi
    echo "$extract_root"
    return
  fi
  local nested
  nested="$(find "$extract_root" -maxdepth 4 -type d -name bin | head -n 1 || true)"
  if [[ -n "$nested" && -d "$(dirname "$nested")/lib/std" ]]; then
    if [[ ! -f "$(dirname "$nested")/lib/runtime.a" && ! -f "$(dirname "$nested")/lib/runtime.lib" ]]; then
      echo "ERROR: downloaded payload missing runtime library (expected lib/runtime.a or lib/runtime.lib)." >&2
      exit 1
    fi
    echo "$(dirname "$nested")"
    return
  fi
  echo "ERROR: downloaded payload has invalid structure (missing bin and lib/std)." >&2
  exit 1
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

ensure_local_llvm_layout() {
  local payload_prefix="$1"
  local llvm_root="$payload_prefix/llvm"
  local llvm_bin="$llvm_root/bin"
  mkdir -p "$llvm_bin"

  if command -v clang >/dev/null 2>&1; then
    ln -sf "$(command -v clang)" "$llvm_bin/clang"
  fi
  local cxx_name="clang""++"
  if command -v "$cxx_name" >/dev/null 2>&1; then
    ln -sf "$(command -v "$cxx_name")" "$llvm_bin/$cxx_name"
  fi
  if command -v llvm-ar >/dev/null 2>&1; then
    ln -sf "$(command -v llvm-ar)" "$llvm_bin/llvm-ar"
  elif command -v ar >/dev/null 2>&1; then
    ln -sf "$(command -v ar)" "$llvm_bin/llvm-ar"
  fi
  ensure_path_persisted "$llvm_bin"
}

validate_cli_install() {
  local cli_bin="$1"
  if [[ ! -x "$cli_bin" ]]; then
    echo "ERROR: CLI binary is not executable: $cli_bin" >&2
    exit 1
  fi
  local version_out
  local help_out
  local version_rc=0
  local help_rc=0
  version_out="$("$cli_bin" --version 2>&1)" || version_rc=$?
  help_out="$("$cli_bin" --help 2>&1)" || help_rc=$?
  if [[ "$version_rc" -ne 0 && "$help_rc" -ne 0 ]]; then
    echo "ERROR: installed CLI failed both --version (rc=$version_rc) and --help (rc=$help_rc)." >&2
    exit 1
  fi
  local merged_out
  merged_out="$version_out"$'\n'"$help_out"
  if [[ -z "${merged_out//[[:space:]]/}" ]]; then
    echo "WARN: installed CLI returned empty text for --version/--help; continuing to deeper smoke checks." >&2
  fi
  if [[ "${THAGORE_INSTALL_SKIP_CLI_VALIDATE:-0}" == "1" ]]; then
    echo "WARN: skipping strict CLI marker validation because THAGORE_INSTALL_SKIP_CLI_VALIDATE=1" >&2
    return 0
  fi
  if grep -Eqi "cannot read source file:\s*update|Unknown update mode 'update'|Empty file or file not found|Usage:\s*thg\.exe" <<<"$merged_out"; then
    echo "ERROR: installed CLI output matches legacy/wrapper markers." >&2
    exit 1
  fi
}

validate_helper_bundle_install() {
  local cli_bin="$1"
  local payload_prefix="$2"
  if [[ "${THAGORE_INSTALL_SKIP_HELPER_VALIDATE:-0}" == "1" ]]; then
    echo "WARN: skipping helper bundle validation because THAGORE_INSTALL_SKIP_HELPER_VALIDATE=1" >&2
    return 0
  fi
  local helper_bin="$payload_prefix/bin/stage1"
  if [[ ! -x "$helper_bin" ]]; then
    echo "ERROR: installed payload missing executable helper: $helper_bin" >&2
    exit 1
  fi
  local tmpdir
  tmpdir="$(mktemp -d)"
  cat > "$tmpdir/atomic_bundle_smoke.tg" <<'TG'
func main() -> i32:
    print("atomic package smoke")
    return 0
TG
  (
    set -euo pipefail
    cd "$tmpdir"
    if [[ -f "$payload_prefix/lib/runtime.lib" ]]; then
      cp "$payload_prefix/lib/runtime.lib" ./runtime.lib
    fi
    if [[ -f "$payload_prefix/lib/runtime.a" ]]; then
      cp "$payload_prefix/lib/runtime.a" ./runtime.a
    fi
    THAG_HELPER_BIN="$helper_bin" "$cli_bin" --emit-llvm-internal atomic_bundle_smoke.tg -o atomic_bundle_smoke.ll >/dev/null 2>&1
    if [[ ! -s atomic_bundle_smoke.ll ]]; then
      echo "ERROR: helper smoke failed: --emit-llvm-internal did not produce non-empty LLVM IR." >&2
      exit 1
    fi
    THAG_HELPER_BIN="$helper_bin" "$cli_bin" build atomic_bundle_smoke.tg -o atomic_bundle_smoke >/dev/null 2>&1
    if [[ ! -x atomic_bundle_smoke ]]; then
      echo "ERROR: helper smoke failed: build did not produce executable output." >&2
      exit 1
    fi
    out="$(./atomic_bundle_smoke 2>&1 || true)"
    if ! grep -Fq "atomic package smoke" <<<"$out"; then
      echo "ERROR: helper smoke failed: runtime output mismatch." >&2
      exit 1
    fi
  )
  rm -rf "$tmpdir"
}

confirm_install() {
  if [[ "$ASSUME_YES" -eq 1 ]]; then
    return
  fi
  local prompt="$1"
  read -r -p "$prompt [Y/n]: " reply
  reply="${reply:-Y}"
  case "$reply" in
    y|Y|yes|YES) ;;
    *) echo "Aborted."; exit 1 ;;
  esac
}

install_llvm_ubuntu() {
  if command -v clang >/dev/null 2>&1 && clang --version | grep -qE "version 21|21\.1\.8"; then
    echo "LLVM already available."
    return
  fi
  sudo apt-get update
  curl -fsSL https://apt.llvm.org/llvm.sh -o /tmp/llvm.sh
  chmod +x /tmp/llvm.sh
  sudo /tmp/llvm.sh 21 all
  if ! command -v clang >/dev/null 2>&1 && [[ -d /usr/lib/llvm-21/bin ]]; then
    export PATH="/usr/lib/llvm-21/bin:$PATH"
  fi
  clang --version || true
}

install_llvm_linux() {
  if [[ "$SKIP_LLVM" -eq 1 ]]; then
    return
  fi
  if command -v clang >/dev/null 2>&1 && clang --version | grep -qE "version 21|21\.1\.8"; then
    echo "LLVM already available."
    return
  fi
  if [[ "$MODE" == "ubuntu" ]] || command -v apt-get >/dev/null 2>&1; then
    install_llvm_ubuntu
  elif command -v dnf >/dev/null 2>&1; then
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
    echo "No supported package manager found. Falling back to portable LLVM install."
    install_llvm_portable
  fi
  clang --version || true
  echo "[thagore-installer] LLVM ${LLVM_VERSION} install done for linux/${ARCH}"
}

install_llvm_macos() {
  if [[ "$SKIP_LLVM" -eq 1 ]]; then
    return
  fi
  if command -v clang >/dev/null 2>&1 && clang --version | grep -qE "version 21|21\.1\.8"; then
    echo "LLVM already available."
    return
  fi
  if ! command -v brew >/dev/null 2>&1; then
    echo "ERROR: Homebrew is required to install LLVM automatically." >&2
    exit 1
  fi
  brew update
  brew tap thagore-foundation/llvm || true
  if brew list llvm@21 >/dev/null 2>&1; then
    echo "llvm@21 already installed."
  else
    brew install llvm@21 || brew install llvm
  fi
  if brew --prefix llvm@21 >/dev/null 2>&1; then
    export PATH="$(brew --prefix llvm@21)/bin:$PATH"
  else
    export PATH="$(brew --prefix llvm)/bin:$PATH"
  fi
  clang --version || true
  echo "[thagore-installer] LLVM ${LLVM_VERSION} install done for macos/${ARCH}"
}

install_llvm_portable() {
  if [[ "$SKIP_LLVM" -eq 1 ]]; then
    return
  fi
  mkdir -p "$LLVM_PREFIX"
  if command -v clang >/dev/null 2>&1 && clang --version | grep -qE "version 21|21\.1\.8"; then
    echo "LLVM already available."
    return
  fi
  if command -v cmake >/dev/null 2>&1 && command -v git >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1; then
    local workdir
    workdir="$(mktemp -d)"
    trap 'rm -rf "$workdir"' EXIT
    git clone --depth 1 --branch llvmorg-21.1.8 https://github.com/llvm/llvm-project.git "$workdir/llvm-project"
    cmake -S "$workdir/llvm-project/llvm" -B "$workdir/build" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$LLVM_PREFIX" \
      -DLLVM_ENABLE_PROJECTS="clang;lld" \
      -DLLVM_TARGETS_TO_BUILD="AArch64;X86"
    cmake --build "$workdir/build" --target install -j2
    "$LLVM_PREFIX/bin/clang" --version || true
    echo "LLVM installed under: $LLVM_PREFIX"
  else
    echo "ERROR: portable mode requires git + cmake + ninja to build LLVM from source." >&2
    exit 1
  fi
}

install_payload_with_prefix() {
  local payload_prefix="$1"
  if [[ "$SKIP_PAYLOAD" -eq 1 ]]; then
    return
  fi

  local source_root
  local cleanup_source_root=""
  source_root="$(detect_source_root)"
  if [[ -z "$source_root" ]]; then
    local tmp_payload
    tmp_payload="$(mktemp -d)"
    source_root="$(download_release_payload "$tmp_payload")"
    cleanup_source_root="$tmp_payload"
    if [[ -z "$source_root" ]]; then
      echo "ERROR: no local payload found and no downloadable payload for mode '$MODE'." >&2
      echo "HINT: pass --mode linux|ubuntu|macos, or set THAGORE_ROOT to a local payload root." >&2
      exit 1
    fi
  fi

  mkdir -p "$payload_prefix"
  cp -R "$source_root/"* "$payload_prefix/"
  if [[ ! -x "$payload_prefix/bin/thagore" ]]; then
    echo "ERROR: installed payload missing executable: $payload_prefix/bin/thagore" >&2
    exit 1
  fi

  local link_dir="$HOME/.local/bin"
  mkdir -p "$link_dir"
  ln -sf "$payload_prefix/bin/thagore" "$link_dir/thagore"
  if [[ -x "$payload_prefix/bin/stage1" ]]; then
    ln -sf "$payload_prefix/bin/stage1" "$link_dir/stage1"
  fi
  validate_cli_install "$link_dir/thagore"
  validate_helper_bundle_install "$link_dir/thagore" "$payload_prefix"

  ensure_local_llvm_layout "$payload_prefix"
  if [[ "$MODE" == "portable" && -d "$LLVM_PREFIX/bin" ]]; then
    ensure_path_persisted "$LLVM_PREFIX/bin"
  fi
  ensure_path_persisted "$link_dir"

  cat <<EOF
Thagore installed successfully.
Binary: $link_dir/thagore
Prefix: $payload_prefix
Stdlib: $payload_prefix/lib/std
EOF

  if [[ -n "$cleanup_source_root" ]]; then
    rm -rf "$cleanup_source_root"
  fi
}

install_payload_linux() {
  local final_prefix="${INSTALL_PREFIX:-${PREFIX:-/opt/thagore}}"
  if [[ "$final_prefix" == "/opt/thagore" && ! -w "/opt" && "$(id -u)" -ne 0 ]]; then
    final_prefix="$HOME/.local/share/thagore"
  fi
  install_payload_with_prefix "$final_prefix"
}

install_payload_macos() {
  local final_prefix="${INSTALL_PREFIX:-${PREFIX:-/usr/local/thagore}}"
  if [[ "$final_prefix" == "/usr/local/thagore" && ! -w "/usr/local" ]]; then
    final_prefix="$HOME/.local/share/thagore"
  fi
  install_payload_with_prefix "$final_prefix"
}

install_payload_portable() {
  local final_prefix="${INSTALL_PREFIX:-${THAGORE_PREFIX:-$HOME/.local/share/thagore}}"
  install_payload_with_prefix "$final_prefix"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode)
      MODE="${2:-}"
      shift 2
      ;;
    --llvm-version)
      LLVM_VERSION="${2:-}"
      shift 2
      ;;
    --arch)
      ARCH="${2:-}"
      shift 2
      ;;
    --prefix)
      INSTALL_PREFIX="${2:-}"
      shift 2
      ;;
    --llvm-prefix)
      LLVM_PREFIX="${2:-}"
      LLVM_PREFIX_EXPLICIT=1
      shift 2
      ;;
    --skip-llvm)
      SKIP_LLVM=1
      shift
      ;;
    --skip-payload)
      SKIP_PAYLOAD=1
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
      echo "Unknown arg: $1" >&2
      exit 1
      ;;
  esac
done

if [[ "$MODE" == "auto" ]]; then
  MODE="$(detect_mode)"
fi

if [[ "$LLVM_VERSION" != "21.1.8" ]]; then
  echo "ERROR: only LLVM 21.1.8 is supported in release installer." >&2
  exit 1
fi

if [[ "$ARCH" == "auto" ]]; then
  ARCH="$(uname -m || true)"
fi
ARCH="$(normalize_arch "$ARCH")"

case "$MODE" in
  linux)
    confirm_install "Install Thagore on Linux (${ARCH}) with LLVM ${LLVM_VERSION}?"
    install_llvm_linux
    install_payload_linux
    ;;
  ubuntu)
    confirm_install "Install Thagore on Ubuntu/Debian (${ARCH}) with LLVM ${LLVM_VERSION}?"
    install_llvm_linux
    install_payload_linux
    ;;
  macos)
    confirm_install "Install Thagore on macOS (${ARCH}) with LLVM ${LLVM_VERSION}?"
    install_llvm_macos
    install_payload_macos
    ;;
  portable)
    confirm_install "Install portable LLVM ${LLVM_VERSION} and Thagore payload (${ARCH})?"
    if [[ -z "$INSTALL_PREFIX" ]]; then
      INSTALL_PREFIX="${THAGORE_PREFIX:-$HOME/.local/share/thagore}"
    fi
    if [[ "$LLVM_PREFIX_EXPLICIT" -eq 0 ]]; then
      LLVM_PREFIX="$INSTALL_PREFIX/llvm"
    fi
    install_llvm_portable
    install_payload_portable
    ;;
  *)
    echo "ERROR: unsupported mode '$MODE'. Expected auto|linux|ubuntu|macos|portable." >&2
    exit 1
    ;;
esac

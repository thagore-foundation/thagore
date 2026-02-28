#!/usr/bin/env bash
set -euo pipefail

REPO_OWNER="thagore-foundation"
REPO_NAME="thagore"
DEFAULT_CHANNEL="stable"

TAG=""
CHANNEL="${DEFAULT_CHANNEL}"
INSTALL_ROOT="${HOME}/.thagore"
MODE="auto"
ARCH=""
DRY_RUN=0
FORCE=0

ASSET_NAME=""
CHECKSUM_NAME=""
BIN_RELATIVE_PATH=""
LINK_NAME=""

usage() {
  cat <<'EOF'
thagup.sh - Install Thagore compiler (thagc) from GitHub Releases.

Usage:
  thagup.sh [options]

Options:
  --tag <vX.Y.Z>         Install specific tag (default: latest release)
  --channel <name>       Install channel under ~/.thagore/toolchains (default: stable)
  --install-root <dir>   Install root (default: ~/.thagore)
  --mode <auto|linux|macos|windows>
                         Target installer mode (default: auto)
  --arch <x86_64|aarch64>
                         Override detected CPU architecture
  --dry-run              Print actions without changing files
  --force                Overwrite existing channel directory
  -h, --help             Show help

Examples:
  thagup.sh
  thagup.sh --tag v0.8.2
  thagup.sh --mode macos --tag v0.8.2
EOF
}

log() {
  printf '[thagup] %s\n' "$*"
}

fail() {
  printf '[thagup] error: %s\n' "$*" >&2
  exit 1
}

have_cmd() {
  command -v "$1" >/dev/null 2>&1
}

require_cmd() {
  local cmd="$1"
  if ! have_cmd "$cmd"; then
    fail "required command not found: ${cmd}"
  fi
}

run_cmd() {
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    log "[dry-run] $*"
    return 0
  fi
  "$@"
}

parse_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --tag)
        [[ $# -ge 2 ]] || fail "--tag requires a value"
        TAG="$2"
        shift 2
        ;;
      --channel)
        [[ $# -ge 2 ]] || fail "--channel requires a value"
        CHANNEL="$2"
        shift 2
        ;;
      --install-root)
        [[ $# -ge 2 ]] || fail "--install-root requires a value"
        INSTALL_ROOT="$2"
        shift 2
        ;;
      --mode)
        [[ $# -ge 2 ]] || fail "--mode requires a value"
        MODE="$2"
        shift 2
        ;;
      --arch)
        [[ $# -ge 2 ]] || fail "--arch requires a value"
        ARCH="$2"
        shift 2
        ;;
      --dry-run)
        DRY_RUN=1
        shift
        ;;
      --force)
        FORCE=1
        shift
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        fail "unknown option: $1"
        ;;
    esac
  done
}

normalize_mode() {
  if [[ "${MODE}" != "auto" ]]; then
    case "${MODE}" in
      linux|macos|windows) return 0 ;;
      *) fail "invalid --mode: ${MODE}" ;;
    esac
  fi

  local os
  os="$(uname -s)"
  case "${os}" in
    Linux) MODE="linux" ;;
    Darwin) MODE="macos" ;;
    MINGW*|MSYS*|CYGWIN*|Windows_NT) MODE="windows" ;;
    *) fail "unsupported operating system: ${os} (use --mode to override)" ;;
  esac
}

resolve_assets() {
  local raw_arch
  raw_arch="$(uname -m)"
  if [[ -z "${ARCH}" ]]; then
    case "${raw_arch}" in
      x86_64|amd64) ARCH="x86_64" ;;
      aarch64|arm64) ARCH="aarch64" ;;
      *) fail "unsupported CPU architecture: ${raw_arch} (use --arch to override)" ;;
    esac
  fi
  case "${ARCH}" in
    x86_64|aarch64) ;;
    *) fail "invalid --arch: ${ARCH}" ;;
  esac

  case "${MODE}" in
    linux)
      ASSET_NAME="thagc-core-linux-${ARCH}.tar.gz"
      CHECKSUM_NAME="SHA256SUMS-thagc.txt"
      BIN_RELATIVE_PATH="bin/thagc"
      LINK_NAME="thagc"
      ;;
    macos)
      ASSET_NAME="thagc-core-macos-${ARCH}.tar.gz"
      CHECKSUM_NAME="SHA256SUMS-thagc.txt"
      BIN_RELATIVE_PATH="bin/thagc"
      LINK_NAME="thagc"
      ;;
    windows)
      ASSET_NAME="thagc-core-windows-${ARCH}.tar.gz"
      CHECKSUM_NAME="SHA256SUMS-thagc.txt"
      BIN_RELATIVE_PATH="bin/thagc.exe"
      LINK_NAME="thagc.exe"
      ;;
    *)
      fail "internal error: unresolved mode ${MODE}"
      ;;
  esac
}

api_get_latest_tag() {
  local api_url payload tag
  api_url="https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/releases/latest"
  payload="$(curl -fsSL "${api_url}")" || return 1
  tag="$(printf '%s' "${payload}" | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)"
  [[ -n "${tag}" ]] || return 1
  printf '%s\n' "${tag}"
}

sha256_file() {
  local file="$1"
  if have_cmd sha256sum; then
    sha256sum "${file}" | awk '{print $1}'
    return 0
  fi
  if have_cmd shasum; then
    shasum -a 256 "${file}" | awk '{print $1}'
    return 0
  fi
  fail "missing checksum tool: install sha256sum or shasum"
}

verify_checksum() {
  local archive="$1"
  local checksum_file="$2"
  local expected actual
  expected="$(awk -v asset="${ASSET_NAME}" '$2 == asset {print $1}' "${checksum_file}" | head -n 1)"
  [[ -n "${expected}" ]] || fail "cannot parse expected checksum for ${ASSET_NAME}"
  actual="$(sha256_file "${archive}")"
  if [[ "${actual}" != "${expected}" ]]; then
    fail "checksum mismatch for ${ASSET_NAME}: expected ${expected}, got ${actual}"
  fi
}

install_link() {
  local target_path="$1"
  local link_dir="$2"
  local link_path="${link_dir}/${LINK_NAME}"
  run_cmd mkdir -p "${link_dir}"
  if [[ "${MODE}" == "windows" ]]; then
    run_cmd cp -f "${target_path}" "${link_path}"
  else
    run_cmd ln -sfn "${target_path}" "${link_path}"
  fi
}

download_first_available() {
  local output="$1"
  shift
  local url
  for url in "$@"; do
    if curl -fsI "${url}" >/dev/null 2>&1; then
      run_cmd curl -fsSL "${url}" -o "${output}"
      printf '%s\n' "${url}"
      return 0
    fi
  done
  return 1
}

main() {
  parse_args "$@"

  require_cmd curl
  require_cmd tar
  require_cmd mktemp

  normalize_mode
  resolve_assets

  if [[ -z "${TAG}" ]]; then
    log "resolving latest release tag..."
    TAG="$(api_get_latest_tag)" || fail "unable to resolve latest release tag"
  fi

  local base_url archive_url checksum_url legacy_asset legacy_checksum
  base_url="https://github.com/${REPO_OWNER}/${REPO_NAME}/releases/download/${TAG}"
  archive_url="${base_url}/${ASSET_NAME}"
  checksum_url="${base_url}/${CHECKSUM_NAME}"
  legacy_asset="${base_url}/thagc-core-${MODE}.tar.gz"
  legacy_checksum="${base_url}/SHA256SUMS-thagc-${MODE}.txt"

  local work_dir archive_path checksum_path
  work_dir="$(mktemp -d)"
  archive_path="${work_dir}/${ASSET_NAME}"
  checksum_path="${work_dir}/${CHECKSUM_NAME}"

  local toolchains_dir channel_dir target_bin_path link_dir
  toolchains_dir="${INSTALL_ROOT}/toolchains"
  channel_dir="${toolchains_dir}/${CHANNEL}"
  target_bin_path="${channel_dir}/${BIN_RELATIVE_PATH}"
  link_dir="${INSTALL_ROOT}/bin"

  log "release tag: ${TAG}"
  log "mode: ${MODE}"
  log "arch: ${ARCH}"
  log "channel: ${CHANNEL}"
  log "install root: ${INSTALL_ROOT}"
  log "download candidates: ${archive_url} (fallback: ${legacy_asset})"
  log "checksum candidates: ${checksum_url} (fallback: ${legacy_checksum})"

  archive_url="$(download_first_available "${archive_path}" "${archive_url}" "${legacy_asset}")" \
    || fail "unable to download core archive for mode=${MODE} arch=${ARCH}"
  checksum_url="$(download_first_available "${checksum_path}" "${checksum_url}" "${legacy_checksum}")" \
    || fail "unable to download checksum file for mode=${MODE} arch=${ARCH}"
  ASSET_NAME="$(basename "${archive_url}")"

  if [[ "${DRY_RUN}" -eq 0 ]]; then
    verify_checksum "${archive_path}" "${checksum_path}"
  else
    log "[dry-run] skip checksum verification"
  fi

  if [[ -d "${channel_dir}" && "${FORCE}" -eq 0 ]]; then
    fail "target channel exists: ${channel_dir} (use --force to overwrite)"
  fi
  if [[ -d "${channel_dir}" ]]; then
    run_cmd rm -rf "${channel_dir}"
  fi

  run_cmd mkdir -p "${channel_dir}"
  run_cmd tar -xzf "${archive_path}" -C "${channel_dir}"
  run_cmd chmod +x "${target_bin_path}"
  install_link "${target_bin_path}" "${link_dir}"

  if [[ "${DRY_RUN}" -eq 0 ]]; then
    run_cmd rm -rf "${work_dir}"
  else
    log "[dry-run] keep temp dir: ${work_dir}"
  fi

  log "install completed"
  log "binary: ${target_bin_path}"
  log "launcher: ${link_dir}/${LINK_NAME}"
  log "if needed, add PATH: export PATH=\"${INSTALL_ROOT}/bin:\$PATH\""
}

main "$@"

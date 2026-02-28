#!/usr/bin/env bash
set -euo pipefail

REPO_OWNER="thagore-foundation"
REPO_NAME="thagore"
DEFAULT_CHANNEL="stable"

TAG=""
CHANNEL="$DEFAULT_CHANNEL"
INSTALL_ROOT="${HOME}/.thagore"
DRY_RUN=0
FORCE=0

usage() {
  cat <<'EOF'
Install Thagore compiler (thagc) from GitHub Releases.

Usage:
  install_thagore.sh [options]

Options:
  --tag <vX.Y.Z>       Install a specific tag (default: latest release tag)
  --channel <name>     Channel folder under ~/.thagore/toolchains (default: stable)
  --install-root <dir> Override install root (default: ~/.thagore)
  --dry-run            Print actions without writing files
  --force              Overwrite existing channel directory
  -h, --help           Show this help

Examples:
  install_thagore.sh
  install_thagore.sh --tag v0.8.0
  install_thagore.sh --tag v0.8.0 --channel stable --force
EOF
}

log() {
  printf '[thagore-install] %s\n' "$*"
}

fail() {
  printf '[thagore-install] error: %s\n' "$*" >&2
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

detect_platform() {
  local os arch
  os="$(uname -s)"
  arch="$(uname -m)"

  if [[ "${os}" != "Linux" ]]; then
    fail "this installer currently supports Linux only (detected: ${os})"
  fi
  case "${arch}" in
    x86_64|amd64)
      ;;
    *)
      fail "unsupported architecture for current release asset: ${arch}"
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
  expected="$(awk '/thagc-core-linux\.tar\.gz$/ {print $1}' "${checksum_file}" | head -n 1)"
  [[ -n "${expected}" ]] || fail "cannot parse expected checksum for thagc-core-linux.tar.gz"
  actual="$(sha256_file "${archive}")"
  if [[ "${actual}" != "${expected}" ]]; then
    fail "checksum mismatch: expected ${expected}, got ${actual}"
  fi
}

main() {
  parse_args "$@"

  require_cmd curl
  require_cmd tar
  require_cmd mktemp
  detect_platform

  if [[ -z "${TAG}" ]]; then
    log "resolving latest release tag..."
    TAG="$(api_get_latest_tag)" || fail "unable to resolve latest release tag"
  fi

  local base_url archive_name checksum_name
  base_url="https://github.com/${REPO_OWNER}/${REPO_NAME}/releases/download/${TAG}"
  archive_name="thagc-core-linux.tar.gz"
  checksum_name="SHA256SUMS-thagc-linux.txt"

  local work_dir archive_path checksum_path
  work_dir="$(mktemp -d)"
  archive_path="${work_dir}/${archive_name}"
  checksum_path="${work_dir}/${checksum_name}"

  local toolchains_dir channel_dir target_bin_dir target_bin_path link_dir link_path
  toolchains_dir="${INSTALL_ROOT}/toolchains"
  channel_dir="${toolchains_dir}/${CHANNEL}"
  target_bin_dir="${channel_dir}/bin"
  target_bin_path="${target_bin_dir}/thagc"
  link_dir="${INSTALL_ROOT}/bin"
  link_path="${link_dir}/thagc"

  log "release tag: ${TAG}"
  log "channel: ${CHANNEL}"
  log "install root: ${INSTALL_ROOT}"
  log "download: ${base_url}/${archive_name}"
  log "download: ${base_url}/${checksum_name}"

  run_cmd curl -fsSL "${base_url}/${archive_name}" -o "${archive_path}"
  run_cmd curl -fsSL "${base_url}/${checksum_name}" -o "${checksum_path}"

  if [[ "${DRY_RUN}" -eq 0 ]]; then
    verify_checksum "${archive_path}" "${checksum_path}"
  else
    log "[dry-run] skip checksum verification step"
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
  run_cmd mkdir -p "${link_dir}"
  run_cmd ln -sfn "${target_bin_path}" "${link_path}"

  if [[ "${DRY_RUN}" -eq 0 ]]; then
    run_cmd rm -rf "${work_dir}"
  else
    log "[dry-run] keep temp dir: ${work_dir}"
  fi

  log "install completed"
  log "binary: ${target_bin_path}"
  log "symlink: ${link_path}"
  log "run: ${link_path} --version"
  log "if needed, add to PATH: export PATH=\"${INSTALL_ROOT}/bin:\$PATH\""
}

main "$@"

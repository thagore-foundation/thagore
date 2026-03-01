#!/usr/bin/env bash
set -euo pipefail

REPO_OWNER="thagore-foundation"
REPO_NAME="thagore"
DRAGO_REPO_OWNER="thagore-foundation"
DRAGO_REPO_NAME="drago"
DEFAULT_CHANNEL="stable"

TAG=""
DRAGO_TAG=""
CHANNEL="${DEFAULT_CHANNEL}"
INSTALL_ROOT="${HOME}/.thagore"
MODE="auto"
ARCH=""
DRY_RUN=0
FORCE=0
INSTALL_DRAGO=1

ASSET_NAME=""
CHECKSUM_NAME=""
BIN_RELATIVE_PATH=""
THAGC_LINK_NAME=""

usage() {
  cat <<'EOF'
thagup.sh - Install Thagore toolchain (thagc + drago) from GitHub Releases.

Usage:
  thagup.sh [options]

Options:
  --tag <vX.Y.Z>         Install specific thagc tag (default: latest release)
  --drago-tag <vX.Y.Z>   Install specific drago tag (default: latest release)
  --without-drago        Install thagc only
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
  thagup.sh --tag v1.0.0 --drago-tag v1.0.0
  thagup.sh --without-drago
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
      --drago-tag)
        [[ $# -ge 2 ]] || fail "--drago-tag requires a value"
        DRAGO_TAG="$2"
        shift 2
        ;;
      --without-drago)
        INSTALL_DRAGO=0
        shift
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
      THAGC_LINK_NAME="thagc"
      ;;
    macos)
      ASSET_NAME="thagc-core-macos-${ARCH}.tar.gz"
      CHECKSUM_NAME="SHA256SUMS-thagc.txt"
      BIN_RELATIVE_PATH="bin/thagc"
      THAGC_LINK_NAME="thagc"
      ;;
    windows)
      ASSET_NAME="thagc-core-windows-${ARCH}.tar.gz"
      CHECKSUM_NAME="SHA256SUMS-thagc.txt"
      BIN_RELATIVE_PATH="bin/thagc.exe"
      THAGC_LINK_NAME="thagc.exe"
      ;;
    *)
      fail "internal error: unresolved mode ${MODE}"
      ;;
  esac
}

api_get_latest_tag_for_repo() {
  local owner="$1"
  local name="$2"
  local api_url payload tag
  api_url="https://api.github.com/repos/${owner}/${name}/releases/latest"
  payload="$(curl -fsSL "${api_url}")" || return 1
  tag="$(printf '%s' "${payload}" | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)"
  [[ -n "${tag}" ]] || return 1
  printf '%s\n' "${tag}"
}

api_get_latest_tag() {
  api_get_latest_tag_for_repo "${REPO_OWNER}" "${REPO_NAME}"
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
  local asset_name="$3"
  local expected actual
  expected="$(awk -v asset="${asset_name}" '$2 == asset {print $1}' "${checksum_file}" | head -n 1)"
  [[ -n "${expected}" ]] || fail "cannot parse expected checksum for ${asset_name}"
  actual="$(sha256_file "${archive}")"
  if [[ "${actual}" != "${expected}" ]]; then
    fail "checksum mismatch for ${asset_name}: expected ${expected}, got ${actual}"
  fi
}

install_link() {
  local target_path="$1"
  local link_dir="$2"
  local link_name="$3"
  local link_path="${link_dir}/${link_name}"
  run_cmd mkdir -p "${link_dir}"
  if [[ "${MODE}" == "windows" ]]; then
    run_cmd cp -f "${target_path}" "${link_path}"
  else
    run_cmd ln -sfn "${target_path}" "${link_path}"
  fi
}

extract_archive() {
  local archive_path="$1"
  local dest_dir="$2"
  case "${archive_path}" in
    *.tar.gz|*.tgz)
      run_cmd tar -xzf "${archive_path}" -C "${dest_dir}"
      ;;
    *.zip)
      require_cmd unzip
      run_cmd unzip -oq "${archive_path}" -d "${dest_dir}"
      ;;
    *)
      fail "unsupported archive format: ${archive_path}"
      ;;
  esac
}

select_profile_file() {
  local shell_name
  shell_name="$(basename "${SHELL:-}")"
  case "${shell_name}" in
    zsh)
      printf '%s\n' "${HOME}/.zshrc"
      ;;
    bash)
      if [[ "$(uname -s)" == "Darwin" && -f "${HOME}/.bash_profile" ]]; then
        printf '%s\n' "${HOME}/.bash_profile"
      else
        printf '%s\n' "${HOME}/.bashrc"
      fi
      ;;
    *)
      printf '%s\n' "${HOME}/.profile"
      ;;
  esac
}

ensure_path_config() {
  local bin_dir="$1"
  local profile_file export_line
  profile_file="$(select_profile_file)"
  export_line="export PATH=\"${bin_dir}:\$PATH\""

  if [[ "${DRY_RUN}" -eq 1 ]]; then
    log "[dry-run] ensure PATH entry in ${profile_file}: ${export_line}"
    return 0
  fi

  run_cmd mkdir -p "$(dirname "${profile_file}")"
  run_cmd touch "${profile_file}"
  if grep -Fq "${bin_dir}" "${profile_file}"; then
    log "PATH already configured in ${profile_file}"
    return 0
  fi

  {
    printf '\n# thagore installer\n'
    printf '%s\n' "${export_line}"
  } >> "${profile_file}"
  log "added PATH entry to ${profile_file}"
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

install_drago() {
  local work_dir="$1"
  local channel_dir="$2"
  local link_dir="$3"
  local thagc_bin="$4"

  if [[ "${INSTALL_DRAGO}" -eq 0 ]]; then
    log "skip drago installation (--without-drago)"
    return 0
  fi

  if [[ -z "${DRAGO_TAG}" ]]; then
    log "resolving latest drago release tag..."
    DRAGO_TAG="$(api_get_latest_tag_for_repo "${DRAGO_REPO_OWNER}" "${DRAGO_REPO_NAME}")" \
      || fail "unable to resolve latest drago release tag"
  fi

  local drago_link_name drago_target_path
  if [[ "${MODE}" == "windows" ]]; then
    drago_link_name="drago.exe"
  else
    drago_link_name="drago"
  fi
  drago_target_path="${channel_dir}/bin/${drago_link_name}"
  run_cmd mkdir -p "$(dirname "${drago_target_path}")"

  local drago_base_url drago_source_base
  drago_base_url="https://github.com/${DRAGO_REPO_OWNER}/${DRAGO_REPO_NAME}/releases/download/${DRAGO_TAG}"
  drago_source_base="https://github.com/${DRAGO_REPO_OWNER}/${DRAGO_REPO_NAME}/archive/refs"

  local arch_labels=()
  case "${ARCH}" in
    x86_64) arch_labels=("x86_64" "X64" "amd64") ;;
    aarch64) arch_labels=("aarch64" "ARM64" "arm64") ;;
    *) arch_labels=("${ARCH}") ;;
  esac

  local drago_asset_names=()
  local label
  for label in "${arch_labels[@]}"; do
    drago_asset_names+=("drago-${DRAGO_TAG}-${MODE}-${label}.tar.gz")
    drago_asset_names+=("drago-${DRAGO_TAG}-${MODE}-${label}.zip")
  done
  drago_asset_names+=("drago-${DRAGO_TAG}-${MODE}.tar.gz")
  drago_asset_names+=("drago-${DRAGO_TAG}-${MODE}.zip")

  local drago_urls=()
  local asset
  for asset in "${drago_asset_names[@]}"; do
    drago_urls+=("${drago_base_url}/${asset}")
  done

  local drago_work_dir drago_download_tmp drago_download_url
  drago_work_dir="${work_dir}/drago"
  drago_download_tmp="${drago_work_dir}/download.tmp"
  run_cmd mkdir -p "${drago_work_dir}"

  log "drago tag: ${DRAGO_TAG}"
  if drago_download_url="$(download_first_available "${drago_download_tmp}" "${drago_urls[@]}")"; then
    local drago_asset_name drago_archive drago_extract_dir drago_release_bin
    drago_asset_name="$(basename "${drago_download_url}")"
    drago_archive="${drago_work_dir}/${drago_asset_name}"
    drago_extract_dir="${drago_work_dir}/release"

    if [[ "${DRY_RUN}" -eq 1 ]]; then
      log "[dry-run] install drago from release asset: ${drago_asset_name}"
      install_link "${drago_target_path}" "${link_dir}" "${drago_link_name}"
      return 0
    fi

    mv "${drago_download_tmp}" "${drago_archive}"
    run_cmd mkdir -p "${drago_extract_dir}"
    extract_archive "${drago_archive}" "${drago_extract_dir}"
    drago_release_bin="$(find "${drago_extract_dir}" -type f \( -name 'drago' -o -name 'drago.bin' -o -name 'drago.exe' \) | head -n 1 || true)"

    if [[ -n "${drago_release_bin}" ]]; then
      run_cmd cp -f "${drago_release_bin}" "${drago_target_path}"
      if [[ "${MODE}" != "windows" ]]; then
        run_cmd chmod +x "${drago_target_path}"
      fi
      install_link "${drago_target_path}" "${link_dir}" "${drago_link_name}"
      log "installed drago from release asset: ${drago_asset_name}"
      return 0
    fi

    log "drago release asset does not contain executable; fallback to source build"
  else
    log "drago release asset unavailable for mode=${MODE} arch=${ARCH}; fallback to source build"
  fi

  if [[ "${DRY_RUN}" -eq 1 ]]; then
    log "[dry-run] build drago from source with thagc"
    install_link "${drago_target_path}" "${link_dir}" "${drago_link_name}"
    return 0
  fi

  local drago_source_urls drago_source_tmp drago_source_url drago_source_archive drago_source_extract
  drago_source_urls=(
    "${drago_source_base}/tags/${DRAGO_TAG}.tar.gz"
    "${drago_source_base}/heads/main.tar.gz"
  )
  drago_source_tmp="${drago_work_dir}/source-download.tmp"
  drago_source_extract="${drago_work_dir}/source"
  drago_source_url="$(download_first_available "${drago_source_tmp}" "${drago_source_urls[@]}")" \
    || fail "unable to download drago source archive"
  drago_source_archive="${drago_work_dir}/$(basename "${drago_source_url}")"
  mv "${drago_source_tmp}" "${drago_source_archive}"
  run_cmd mkdir -p "${drago_source_extract}"
  extract_archive "${drago_source_archive}" "${drago_source_extract}"

  local drago_main drago_build_out
  drago_main="$(find "${drago_source_extract}" -type f -path '*/src/main.tg' | head -n 1 || true)"
  [[ -n "${drago_main}" ]] || fail "unable to locate drago src/main.tg from source archive"

  if [[ "${MODE}" == "windows" ]]; then
    drago_build_out="${drago_work_dir}/drago.exe"
  else
    drago_build_out="${drago_work_dir}/drago.bin"
  fi

  run_cmd "${thagc_bin}" build "${drago_main}" -o "${drago_build_out}"
  run_cmd cp -f "${drago_build_out}" "${drago_target_path}"
  if [[ "${MODE}" != "windows" ]]; then
    run_cmd chmod +x "${drago_target_path}"
  fi
  install_link "${drago_target_path}" "${link_dir}" "${drago_link_name}"
  log "installed drago from source archive"
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

  local toolchains_dir channel_dir target_bin_path link_dir core_asset_name
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
  core_asset_name="$(basename "${archive_url}")"

  if [[ "${DRY_RUN}" -eq 0 ]]; then
    verify_checksum "${archive_path}" "${checksum_path}" "${core_asset_name}"
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
  install_link "${target_bin_path}" "${link_dir}" "${THAGC_LINK_NAME}"

  install_drago "${work_dir}" "${channel_dir}" "${link_dir}" "${target_bin_path}"
  ensure_path_config "${link_dir}"

  if [[ "${DRY_RUN}" -eq 0 ]]; then
    run_cmd rm -rf "${work_dir}"
  else
    log "[dry-run] keep temp dir: ${work_dir}"
  fi

  log "install completed"
  log "thagc binary: ${target_bin_path}"
  log "thagc launcher: ${link_dir}/${THAGC_LINK_NAME}"
  if [[ "${INSTALL_DRAGO}" -eq 1 ]]; then
    if [[ "${MODE}" == "windows" ]]; then
      log "drago launcher: ${link_dir}/drago.exe"
    else
      log "drago launcher: ${link_dir}/drago"
    fi
    log "open a new terminal to use thagc and drago directly from PATH"
    return
  fi
  log "open a new terminal to use thagc directly from PATH"
}

main "$@"

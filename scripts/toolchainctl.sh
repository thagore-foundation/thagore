#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CFG_PATH="${THAGC_CONFIG:-$HOME/.thagc/config.toml}"
PACK_ROOT="${THAGC_TARGET_ROOT:-$HOME/.thagc/targets}"
SUPPORTED_FILE="$ROOT/targets/registry/supported.txt"
STANDARD_FILE="$ROOT/targets/registry/standard.txt"
PACK_SOURCE_ROOT="${THAGC_PACK_SOURCE_ROOT:-$ROOT/targets/packs}"

die() {
  echo "ERROR: $*" >&2
  exit 1
}

ensure_supported_file() {
  [[ -f "$SUPPORTED_FILE" ]] || die "missing supported target list: $SUPPORTED_FILE"
}

is_supported() {
  local t="$1"
  ensure_supported_file
  grep -Fxq "$t" "$SUPPORTED_FILE"
}

read_cfg_value() {
  local key="$1"
  if [[ ! -f "$CFG_PATH" ]]; then
    echo ""
    return
  fi
  sed -n "s/^${key} = \"\\(.*\\)\"$/\\1/p" "$CFG_PATH" | head -n 1
}

read_installed_targets() {
  if [[ ! -f "$CFG_PATH" ]]; then
    return
  fi
  local raw
  raw="$(sed -n 's/^installed_targets = \[\(.*\)\]$/\1/p' "$CFG_PATH" | head -n 1)"
  [[ -n "$raw" ]] || return
  echo "$raw" | tr -d '"' | tr ',' '\n' | sed 's/^ *//;s/ *$//' | sed '/^$/d'
}

write_config() {
  local channel="$1"
  local version="$2"
  local default_target="$3"
  local profile="$4"
  shift 4
  local targets=("$@")
  mkdir -p "$(dirname "$CFG_PATH")"
  local joined=""
  local t=""
  for t in "${targets[@]}"; do
    if [[ -z "$joined" ]]; then
      joined="\"$t\""
    else
      joined="$joined,\"$t\""
    fi
  done
  cat > "$CFG_PATH" <<EOF
channel = "$channel"
toolchain_version = "$version"
default_target = "$default_target"
installed_targets = [$joined]
profile = "$profile"
EOF
}

install_pack() {
  local target="$1"
  local src="$PACK_SOURCE_ROOT/$target"
  local dst="$PACK_ROOT/$target"
  [[ -d "$src" ]] || die "missing target pack source: $src"
  mkdir -p "$PACK_ROOT"
  rm -rf "$dst"
  cp -R "$src" "$dst"
  mkdir -p "$dst/runtime"
  if [[ -f "$ROOT/libthag_runtime.a" ]]; then cp -f "$ROOT/libthag_runtime.a" "$dst/runtime/libthag_runtime.a"; fi
  if [[ -f "$ROOT/runtime.a" ]]; then cp -f "$ROOT/runtime.a" "$dst/runtime/runtime.a"; fi
  if [[ -f "$ROOT/runtime.lib" ]]; then cp -f "$ROOT/runtime.lib" "$dst/runtime/runtime.lib"; fi
  if [[ -f "$ROOT/thag_runtime.lib" ]]; then cp -f "$ROOT/thag_runtime.lib" "$dst/runtime/thag_runtime.lib"; fi
}

list_targets() {
  ensure_supported_file
  local std=""
  if [[ -f "$STANDARD_FILE" ]]; then
    std="$(cat "$STANDARD_FILE")"
  fi
  while IFS= read -r t; do
    [[ -n "$t" ]] || continue
    if echo "$std" | grep -Fxq "$t"; then
      echo -e "$t\tstandard"
    else
      echo -e "$t\toptional"
    fi
  done < "$SUPPORTED_FILE"
}

installed_targets() {
  read_installed_targets
}

init_config() {
  local profile=""
  local targets_csv=""
  local default_target=""
  local toolchain_version=""
  local channel="stable"
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --profile) profile="${2:-}"; shift 2 ;;
      --targets) targets_csv="${2:-}"; shift 2 ;;
      --default-target) default_target="${2:-}"; shift 2 ;;
      --toolchain-version) toolchain_version="${2:-}"; shift 2 ;;
      --channel) channel="${2:-}"; shift 2 ;;
      *) die "unknown init arg: $1" ;;
    esac
  done
  [[ "$profile" == "standard" || "$profile" == "custom" ]] || die "--profile must be standard|custom"

  local targets=()
  if [[ "$profile" == "standard" ]]; then
    [[ -f "$STANDARD_FILE" ]] || die "missing standard profile file: $STANDARD_FILE"
    while IFS= read -r t; do
      [[ -n "$t" ]] || continue
      targets+=("$t")
    done < "$STANDARD_FILE"
  else
    [[ -n "$targets_csv" ]] || die "custom profile requires --targets"
    IFS=',' read -r -a targets <<< "$targets_csv"
  fi

  local clean_targets=()
  local t=""
  for t in "${targets[@]}"; do
    t="$(echo "$t" | sed 's/^ *//;s/ *$//')"
    [[ -n "$t" ]] || continue
    is_supported "$t" || die "unsupported target triple: $t"
    clean_targets+=("$t")
  done
  [[ "${#clean_targets[@]}" -gt 0 ]] || die "empty target list"

  if [[ -z "$default_target" ]]; then
    default_target="${clean_targets[0]}"
  fi
  is_supported "$default_target" || die "unsupported default target: $default_target"

  write_config "$channel" "$toolchain_version" "$default_target" "$profile" "${clean_targets[@]}"
  for t in "${clean_targets[@]}"; do
    install_pack "$t"
  done
  echo "config=$CFG_PATH"
  echo "profile=$profile"
  echo "default_target=$default_target"
  echo "installed_targets=$(IFS=,; echo "${clean_targets[*]}")"
}

add_target() {
  local target=""
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --target) target="${2:-}"; shift 2 ;;
      *) die "unknown add arg: $1" ;;
    esac
  done
  [[ -n "$target" ]] || die "missing --target"
  is_supported "$target" || die "unsupported target triple: $target"
  install_pack "$target"

  local channel version default_target profile
  channel="$(read_cfg_value channel)"
  version="$(read_cfg_value toolchain_version)"
  default_target="$(read_cfg_value default_target)"
  profile="$(read_cfg_value profile)"
  [[ -n "$channel" ]] || channel="stable"
  [[ -n "$profile" ]] || profile="custom"
  [[ -n "$default_target" ]] || default_target="$target"

  local current=()
  local t=""
  while IFS= read -r t; do
    [[ -n "$t" ]] || continue
    current+=("$t")
  done < <(read_installed_targets || true)
  local exists=0
  for t in "${current[@]}"; do
    [[ "$t" == "$target" ]] && exists=1
  done
  if [[ "$exists" == "0" ]]; then
    current+=("$target")
  fi
  write_config "$channel" "$version" "$default_target" "$profile" "${current[@]}"
  echo "added=$target"
}

remove_target() {
  local target=""
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --target) target="${2:-}"; shift 2 ;;
      *) die "unknown remove arg: $1" ;;
    esac
  done
  [[ -n "$target" ]] || die "missing --target"

  rm -rf "$PACK_ROOT/$target" || true

  local channel version default_target profile
  channel="$(read_cfg_value channel)"
  version="$(read_cfg_value toolchain_version)"
  default_target="$(read_cfg_value default_target)"
  profile="$(read_cfg_value profile)"
  [[ -n "$channel" ]] || channel="stable"
  [[ -n "$profile" ]] || profile="custom"

  local kept=()
  local t=""
  while IFS= read -r t; do
    [[ -n "$t" ]] || continue
    if [[ "$t" != "$target" ]]; then
      kept+=("$t")
    fi
  done < <(read_installed_targets || true)
  if [[ "$default_target" == "$target" ]]; then
    if [[ "${#kept[@]}" -gt 0 ]]; then
      default_target="${kept[0]}"
    else
      default_target=""
    fi
  fi
  write_config "$channel" "$version" "$default_target" "$profile" "${kept[@]}"
  echo "removed=$target"
}

ensure_target() {
  local target=""
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --target) target="${2:-}"; shift 2 ;;
      *) die "unknown ensure arg: $1" ;;
    esac
  done
  [[ -n "$target" ]] || die "missing --target"
  local found=0
  local t=""
  while IFS= read -r t; do
    [[ "$t" == "$target" ]] && found=1
  done < <(read_installed_targets || true)
  if [[ "$found" == "1" && -d "$PACK_ROOT/$target" ]]; then
    echo "ok=$target"
    return 0
  fi
  echo "missing=$target"
  return 2
}

cmd="${1:-}"
[[ -n "$cmd" ]] || die "missing command"
shift || true

case "$cmd" in
  list-targets) list_targets "$@" ;;
  installed) installed_targets "$@" ;;
  init) init_config "$@" ;;
  add) add_target "$@" ;;
  remove) remove_target "$@" ;;
  ensure) ensure_target "$@" ;;
  *) die "unknown command: $cmd" ;;
esac

#!/usr/bin/env bash
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

if [[ -x /usr/lib/llvm-14/bin/llvm-config && -x /usr/bin/clang ]]; then
  if [[ -n "${GITHUB_ENV:-}" ]]; then
    echo "LLVM_SYS_140_PREFIX=/usr/lib/llvm-14" >> "$GITHUB_ENV"
  else
    export LLVM_SYS_140_PREFIX=/usr/lib/llvm-14
  fi
  exit 0
fi

packages=(
  clang
  llvm-14-dev
  lld
  pkg-config
)

for extra in "$@"; do
  packages+=("$extra")
done

retry_apt() {
  local attempt=1
  while true; do
    if sudo apt-get -o DPkg::Lock::Timeout=300 -o Acquire::Retries=3 "$@"; then
      return 0
    fi
    if [[ $attempt -ge 3 ]]; then
      return 1
    fi
    sleep $((attempt * 15))
    attempt=$((attempt + 1))
  done
}

retry_apt update -y
retry_apt install -y --no-install-recommends "${packages[@]}"

if [[ -n "${GITHUB_ENV:-}" ]]; then
  echo "LLVM_SYS_140_PREFIX=/usr/lib/llvm-14" >> "$GITHUB_ENV"
else
  export LLVM_SYS_140_PREFIX=/usr/lib/llvm-14
fi

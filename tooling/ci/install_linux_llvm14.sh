#!/usr/bin/env bash
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

set_llvm_env() {
  local prefix=$1
  if [[ -n "${GITHUB_ENV:-}" ]]; then
    echo "LLVM_SYS_140_PREFIX=$prefix" >> "$GITHUB_ENV"
  else
    export LLVM_SYS_140_PREFIX=$prefix
  fi
  if [[ -n "${GITHUB_PATH:-}" ]]; then
    echo "$prefix/bin" >> "$GITHUB_PATH"
  else
    export PATH="$prefix/bin:${PATH}"
  fi
}

have_llvm_prefix() {
  local prefix=$1
  [[ -x "$prefix/bin/llvm-config" && -x "$prefix/bin/clang" && -f "$prefix/lib/libPolly.a" ]]
}

if have_llvm_prefix /usr/lib/llvm-14; then
  set_llvm_env /usr/lib/llvm-14
  exit 0
fi

prebuilt_root="${RUNNER_TEMP:-/tmp}/llvm-14.0.6-x86_64-linux-gnu-prebuilt"
prebuilt_prefix="$prebuilt_root/clang+llvm-14.0.6-x86_64-linux-gnu-rhel-8.4"
prebuilt_archive="${RUNNER_TEMP:-/tmp}/clang+llvm-14.0.6-x86_64-linux-gnu-rhel-8.4.tar.xz"
prebuilt_url="https://github.com/llvm/llvm-project/releases/download/llvmorg-14.0.6/clang%2Bllvm-14.0.6-x86_64-linux-gnu-rhel-8.4.tar.xz"

install_prebuilt() {
  rm -rf "$prebuilt_root"
  mkdir -p "$prebuilt_root"
  curl -fsSL --retry 5 --retry-delay 10 --retry-all-errors "$prebuilt_url" -o "$prebuilt_archive"
  tar -xJf "$prebuilt_archive" -C "$prebuilt_root"
}

if have_llvm_prefix "$prebuilt_prefix"; then
  set_llvm_env "$prebuilt_prefix"
  exit 0
fi

if install_prebuilt && have_llvm_prefix "$prebuilt_prefix"; then
  set_llvm_env "$prebuilt_prefix"
  exit 0
fi

packages=(
  clang
  llvm-14-dev
  llvm-14-tools
  libpolly-14-dev
  lld
  pkg-config
)

for extra in "$@"; do
  packages+=("$extra")
done

retry_apt() {
  local attempt=1
  while true; do
    if timeout 1800s sudo apt-get \
      -o DPkg::Lock::Timeout=300 \
      -o Acquire::Retries=3 \
      -o Acquire::http::Timeout=30 \
      -o Acquire::https::Timeout=30 \
      "$@"; then
      return 0
    fi
    if [[ $attempt -ge 2 ]]; then
      return 1
    fi
    sleep $((attempt * 15))
    attempt=$((attempt + 1))
  done
}

retry_apt update -y
retry_apt install -y --no-install-recommends "${packages[@]}"

set_llvm_env /usr/lib/llvm-14

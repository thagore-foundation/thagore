#!/usr/bin/env bash
set -euo pipefail

SCRIPT_URL="${THAGUP_INIT_URL:-https://thagore.org/thagup.sh}"
TMP_FILE="$(mktemp)"

cleanup() {
  rm -f "${TMP_FILE}"
}
trap cleanup EXIT

curl -fsSL "${SCRIPT_URL}" -o "${TMP_FILE}"
bash "${TMP_FILE}" "$@"


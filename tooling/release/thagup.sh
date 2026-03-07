#!/usr/bin/env bash
set -euo pipefail

REPO="thagore-foundation/thagore"
CHANNEL="stable"
TARGET=""
ARCH_OVERRIDE=""
PREFIX="${HOME}/.local"
TAG=""
WITHOUT_DRAGO=0
DRY_RUN=0
DRAGO_TAG=""

usage() {
  cat <<'EOF'
Usage: thagup.sh [options]

Options:
  --channel <stable|extended|nightly>
  --target <triple>
  --arch <name>
  --prefix <dir>
  --tag <release-tag>
  --drago-tag <release-tag>
  --without-drago
  --dry-run
  -h, --help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --channel) CHANNEL="$2"; shift 2 ;;
    --target) TARGET="$2"; shift 2 ;;
    --arch) ARCH_OVERRIDE="$2"; shift 2 ;;
    --prefix) PREFIX="$2"; shift 2 ;;
    --tag) TAG="$2"; shift 2 ;;
    --drago-tag) DRAGO_TAG="$2"; shift 2 ;;
    --without-drago) WITHOUT_DRAGO=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "error: unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

require_tool() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "error: required tool '$1' not found" >&2
    exit 2
  }
}

require_tool curl
require_tool python3

detect_target() {
  python3 - "$ARCH_OVERRIDE" <<'PY'
import platform
import sys

arch_override = sys.argv[1]
system = platform.system().lower()
machine = (arch_override or platform.machine()).lower()

arch_map = {
    "x86_64": "x86_64",
    "amd64": "x86_64",
    "aarch64": "aarch64",
    "arm64": "aarch64",
    "armv7l": "armv7",
    "armv7": "armv7",
    "arm": "arm",
    "i686": "i686",
    "i386": "i686",
}

arch = arch_map.get(machine, machine)

targets = {
    ("linux", "x86_64"): "x86_64-unknown-linux-gnu",
    ("linux", "aarch64"): "aarch64-unknown-linux-gnu",
    ("linux", "armv7"): "armv7-unknown-linux-gnueabihf",
    ("linux", "arm"): "arm-unknown-linux-gnueabihf",
    ("linux", "i686"): "i686-unknown-linux-gnu",
    ("darwin", "x86_64"): "x86_64-apple-darwin",
    ("darwin", "aarch64"): "aarch64-apple-darwin",
    ("freebsd", "x86_64"): "x86_64-unknown-freebsd",
    ("freebsd", "aarch64"): "aarch64-unknown-freebsd",
    ("openbsd", "x86_64"): "x86_64-unknown-openbsd",
    ("netbsd", "x86_64"): "x86_64-unknown-netbsd",
}

target = targets.get((system, arch), "")
print(target)
PY
}

if [[ -z "$TARGET" ]]; then
  TARGET="$(detect_target)"
fi

if [[ -z "$TARGET" ]]; then
  echo "error: could not determine a supported target triple; pass --target explicitly" >&2
  exit 2
fi

if [[ -z "$TAG" ]]; then
  if [[ "$CHANNEL" == "nightly" ]]; then
    TAG="$(curl -fsSL "https://api.github.com/repos/${REPO}/releases" | python3 - <<'PY'
import json
import sys

releases = json.load(sys.stdin)
for release in releases:
    if release.get("prerelease") and str(release.get("tag_name", "")).startswith("nightly-"):
        print(release["tag_name"])
        break
PY
)"
  else
    TAG="$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" | python3 - <<'PY'
import json
import sys
print(json.load(sys.stdin)["tag_name"])
PY
)"
  fi
fi

if [[ -z "$TAG" ]]; then
  echo "error: failed to resolve a release tag for channel '${CHANNEL}'" >&2
  exit 1
fi

MANIFEST_URL="https://github.com/${REPO}/releases/download/${TAG}/release-manifest-${TAG}.json"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT
MANIFEST_PATH="${TMPDIR}/manifest.json"
curl -fsSL "$MANIFEST_URL" -o "$MANIFEST_PATH"

ARTIFACT_JSON="$(python3 - "$MANIFEST_PATH" "$TARGET" "$CHANNEL" <<'PY'
import json
import sys

manifest_path, target, channel = sys.argv[1:4]
manifest = json.load(open(manifest_path, encoding="utf-8"))
tiers = {
    "stable": {"stable"},
    "extended": {"stable", "extended"},
    "nightly": {"nightly"},
}[channel]

for artifact in manifest["artifacts"]:
    if artifact["target"] == target and artifact["tier"] in tiers and artifact["available"]:
        print(json.dumps(artifact))
        break
else:
    sys.exit(1)
PY
)" || {
  echo "error: no release artifact for target ${TARGET} on channel ${CHANNEL}" >&2
  exit 1
}

ARCHIVE_URL="$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["url"])' "$ARTIFACT_JSON")"
ARCHIVE_NAME="$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["archive"])' "$ARTIFACT_JSON")"
ARCHIVE_SHA="$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["sha256"])' "$ARTIFACT_JSON")"

echo "Resolved release:"
echo "  repo:    ${REPO}"
echo "  channel: ${CHANNEL}"
echo "  tag:     ${TAG}"
echo "  target:  ${TARGET}"
echo "  prefix:  ${PREFIX}"
echo "  archive: ${ARCHIVE_NAME}"

if [[ "$DRY_RUN" -eq 1 ]]; then
  exit 0
fi

ARCHIVE_PATH="${TMPDIR}/${ARCHIVE_NAME}"
curl -fsSL "$ARCHIVE_URL" -o "$ARCHIVE_PATH"

python3 - "$ARCHIVE_PATH" "$ARCHIVE_SHA" "$PREFIX" <<'PY'
import hashlib
import os
import sys
import tarfile
import zipfile
from pathlib import Path

archive_path = Path(sys.argv[1])
expected = sys.argv[2]
prefix = Path(sys.argv[3]).expanduser()
prefix.mkdir(parents=True, exist_ok=True)

digest = hashlib.sha256()
with archive_path.open("rb") as handle:
    for chunk in iter(lambda: handle.read(1024 * 1024), b""):
        digest.update(chunk)
if digest.hexdigest() != expected:
    raise SystemExit("error: checksum verification failed")

def strip_first_component(name: str) -> str:
    parts = Path(name).parts
    return str(Path(*parts[1:])) if len(parts) > 1 else ""

if archive_path.suffix == ".zip":
    with zipfile.ZipFile(archive_path) as archive:
        for member in archive.infolist():
            relative = strip_first_component(member.filename)
            if not relative:
                continue
            destination = prefix / relative
            if member.is_dir():
                destination.mkdir(parents=True, exist_ok=True)
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(member) as source, destination.open("wb") as target:
                target.write(source.read())
else:
    with tarfile.open(archive_path, "r:*") as archive:
        for member in archive.getmembers():
            relative = strip_first_component(member.name)
            if not relative:
                continue
            member.name = relative
            archive.extract(member, prefix)

bin_dir = prefix / "bin"
print(f"Installed Thagore to {prefix}")
print(f"Add {bin_dir} to PATH if it is not already visible.")
PY

if [[ "$WITHOUT_DRAGO" -eq 0 ]]; then
  echo "note: thagup currently installs the Thagore toolchain only."
  if [[ -n "$DRAGO_TAG" ]]; then
    echo "note: drago tag ${DRAGO_TAG} was requested but companion drago installation is not yet automated."
  fi
fi

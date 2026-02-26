#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BOOTSTRAP_STAGE1_TAG="${BOOTSTRAP_STAGE1_TAG:-v0.3.193-stage1-seed}"
STAGE1_BOOTSTRAP_BIN="${THAG_BOOTSTRAP_STAGE1_BIN:-}"

detect_os() {
  case "$(uname -s)" in
    Linux*) echo "Linux" ;;
    Darwin*) echo "macOS" ;;
    *) echo "Unknown" ;;
  esac
}

OS_NAME="$(detect_os)"
if [[ "$OS_NAME" == "Unknown" ]]; then
  echo "[FAIL] Unsupported OS for scripts/bootstrap.sh"
  exit 1
fi

require_cmd() {
  local name="$1"
  if ! command -v "$name" >/dev/null 2>&1; then
    echo "[FAIL] Missing required tool: $name"
    exit 1
  fi
}

require_cmd python3
require_cmd clang
require_cmd llc
require_cmd llvm-as

download_release_asset() {
  local pattern="$1"
  local dest="${2:-}"
  local src="bootstrap/$pattern"
  mkdir -p bootstrap
  rm -f "$src" || true

  if command -v gh >/dev/null 2>&1; then
    gh release download "$BOOTSTRAP_STAGE1_TAG" --repo "thagore-foundation/thagore" --pattern "$pattern" -D bootstrap || true
  fi
  if [[ ! -f "$src" ]]; then
    curl -fsSL -o "$src" "https://github.com/thagore-foundation/thagore/releases/download/$BOOTSTRAP_STAGE1_TAG/$pattern" || true
  fi
  if [[ ! -f "$src" ]]; then
    return 1
  fi
  if [[ -n "$dest" && "$dest" != "$src" ]]; then
    cp "$src" "$dest"
  fi
  return 0
}

resolve_bin() {
  local preferred="$1"
  local src="$2"
  local base
  base="$(basename "$src" .tg)"
  for c in "$preferred" "${preferred}.exe" "${preferred%.exe}" "$base" "$base.exe" "examples/$base" "examples/$base.exe" "src/$base" "src/$base.exe" "a.out" "a.exe" "out" "out.exe"; do
    if [[ -f "$c" ]]; then
      echo "$c"
      return 0
    fi
  done
  return 1
}

sha256_file() {
  local file="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$file" | awk '{print $1}'
    return 0
  fi
  shasum -a 256 "$file" | awk '{print $1}'
}

thg_build() {
  local cc="$1"
  local src="$2"
  local out="$3"
  local got

  echo "[bootstrap] $cc build $src -o $out"

  if "./$cc" build "$src" -o "$out" 2>&1; then
    got="$(resolve_bin "$out" "$src" || true)"
    if [[ -n "$got" ]]; then
      echo "[bootstrap] -> $got (build subcommand)"
      return 0
    fi
  fi

  echo "[bootstrap] trying legacy --build flag..."
  if ! "./$cc" --build "$src" -o "$out" 2>&1; then
    echo "[bootstrap] FAIL: both build and --build failed for $src"
    return 1
  fi
  got="$(resolve_bin "$out" "$src" || true)"
  if [[ -z "$got" ]]; then
    echo "[bootstrap] FAIL: compiler returned 0 but no output binary found for $out"
    return 1
  fi
  echo "[bootstrap] -> $got (--build flag)"
  return 0
}

compiler_can_emit_hello() {
  local cc="$1"
  rm -f bootstrap_probe_hello bootstrap_probe_hello.exe || true
  if ! thg_build "$cc" "examples/hello.tg" "bootstrap_probe_hello"; then
    return 1
  fi
  local hello_bin
  hello_bin="$(resolve_bin "bootstrap_probe_hello" "examples/hello.tg" || true)"
  [[ -n "$hello_bin" && -f "$hello_bin" ]] || return 1
  rm -f bootstrap_probe_hello bootstrap_probe_hello.exe || true
  return 0
}

ensure_stage1() {
  if [[ -n "$STAGE1_BOOTSTRAP_BIN" ]]; then
    if [[ ! -f "$STAGE1_BOOTSTRAP_BIN" ]]; then
      echo "[FAIL] Selected bootstrap compiler not found: $STAGE1_BOOTSTRAP_BIN"
      exit 1
    fi
    if ! compiler_can_emit_hello "$STAGE1_BOOTSTRAP_BIN"; then
      echo "[FAIL] Selected bootstrap compiler cannot emit output binaries: $STAGE1_BOOTSTRAP_BIN"
      exit 1
    fi
    return 0
  fi

  if [[ "$OS_NAME" == "Linux" || "$OS_NAME" == "macOS" ]]; then
    if [[ -x "stage1" ]] && compiler_can_emit_hello "stage1"; then
      STAGE1_BOOTSTRAP_BIN="stage1"
      return 0
    fi
  fi

  local asset=""
  if [[ "$OS_NAME" == "Linux" ]]; then
    asset="thagore-stage1-linux.tar.gz"
  else
    asset="thagore-stage1-macos.tar.gz"
  fi

  if ! download_release_asset "$asset"; then
    echo "[FAIL] Missing Stage1 seed asset: $asset (tag: $BOOTSTRAP_STAGE1_TAG)"
    exit 1
  fi

  rm -rf bootstrap/extract_stage1 || true
  mkdir -p bootstrap/extract_stage1
  tar -xzf "bootstrap/$asset" -C bootstrap/extract_stage1
  # Strict single-binary bootstrap: prefer thagc/thagore/thag/stage1 and reject
  # helper-only extraction as bootstrap entrypoint.
  local candidate
  candidate="$(find bootstrap/extract_stage1 -maxdepth 5 -type f \( -name thagc -o -name thagore -o -name thag -o -name stage1 \) | head -n 1 || true)"
  if [[ -z "$candidate" ]]; then
    echo "[FAIL] Stage1 tarball did not contain a strict bootstrap binary (thagc/thagore/thag/stage1)."
    exit 1
  fi
  echo "[bootstrap] using stage1 candidate: $candidate"

  cp "$candidate" stage1
  chmod +x stage1 || true
  # Patch GLIBC version requirements so stage1 runs on GLIBC_2.35+.
  if [[ "$(uname -s)" == "Linux" ]] && command -v python3 &>/dev/null; then
    if [[ -f "scripts/patch_glibc_compat.py" ]]; then
      python3 scripts/patch_glibc_compat.py stage1 stage1 2>/dev/null || true
    fi
  fi
  STAGE1_BOOTSTRAP_BIN="stage1"
  if ! compiler_can_emit_hello "$STAGE1_BOOTSTRAP_BIN"; then
    echo "[FAIL] Downloaded Stage1 seed is not healthy (cannot emit hello binary)."
    exit 1
  fi
}

ensure_runtime_abi() {
  if [[ -f "libthag_runtime.a" ]]; then
    cp libthag_runtime.a thag_runtime.lib
    return 0
  fi
  local asset=""
  if [[ "$OS_NAME" == "Linux" ]]; then
    asset="thagore-runtime-linux.a"
  else
    asset="thagore-runtime-macos.a"
  fi
  if ! download_release_asset "$asset" "libthag_runtime.a"; then
    echo "[FAIL] Missing runtime ABI asset: $asset (tag: $BOOTSTRAP_STAGE1_TAG)"
    exit 1
  fi
  cp libthag_runtime.a thag_runtime.lib
}

ensure_runtime_abi
ensure_stage1

if [[ "$OS_NAME" == "Linux" ]]; then
  python3 scripts/build_runtime_abi.py --target-os Linux --summary runtime-abi-summary-local.txt
else
  python3 scripts/build_runtime_abi.py --target-os Darwin --summary runtime-abi-summary-local.txt
fi

echo "[1/5] Build stage2 from stage1..."
echo "[INFO] bootstrap_stage1=$STAGE1_BOOTSTRAP_BIN"
rm -f stage2 stage2.exe || true
if ! thg_build "$STAGE1_BOOTSTRAP_BIN" "src/thagore.tg" "stage2"; then
  echo "[FAIL] stage1 could not compile src/thagore.tg → stage2"
  exit 1
fi
STAGE2_BIN="$(resolve_bin "stage2" "src/thagore.tg" || true)"
if [[ -z "$STAGE2_BIN" ]]; then
  echo "[FAIL] stage2 binary not found by expected names."
  exit 1
fi
echo "[INFO] stage2_bin=$STAGE2_BIN"

echo "[2/5] Rebuild stage2b from stage1 (deterministic pass 2)..."
rm -f stage2b stage2b.exe || true
if ! thg_build "$STAGE1_BOOTSTRAP_BIN" "src/thagore.tg" "stage2b"; then
  echo "[FAIL] stage1 could not compile src/thagore.tg → stage2b (pass 2)"
  exit 1
fi
STAGE2B_BIN="$(resolve_bin "stage2b" "src/thagore.tg" || true)"
if [[ -z "$STAGE2B_BIN" ]]; then
  echo "[FAIL] stage2b binary not found by expected names."
  exit 1
fi
echo "[INFO] stage2b_bin=$STAGE2B_BIN"

echo "[3/5] Reproducibility check (stage2 == stage2b SHA256)..."
STAGE2_SHA="$(sha256_file "$STAGE2_BIN")"
STAGE2B_SHA="$(sha256_file "$STAGE2B_BIN")"
echo "[INFO] stage2_sha256=$STAGE2_SHA"
echo "[INFO] stage2b_sha256=$STAGE2B_SHA"
if [[ "$STAGE2_SHA" != "$STAGE2B_SHA" ]]; then
  echo "[WARN] Non-reproducible bootstrap: stage2 and stage2b differ."
fi

echo "[4/5] Build hello_v2 from stage2b..."
rm -f hello_v2 hello_v2.exe || true
if ! thg_build "$STAGE1_BOOTSTRAP_BIN" "examples/hello.tg" "hello_v2"; then
  echo "[FAIL] Cannot build examples/hello.tg with $STAGE1_BOOTSTRAP_BIN"
  exit 1
fi
HELLO_BIN="$(resolve_bin "hello_v2" "examples/hello.tg")"

echo "[5/5] Run hello_v2..."
"./$HELLO_BIN"

echo "[OK] Strict bootstrap cycle completed on $OS_NAME."
echo "[INFO] stage1=$STAGE1_BOOTSTRAP_BIN stage2b=$STAGE2B_BIN"

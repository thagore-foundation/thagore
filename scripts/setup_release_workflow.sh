#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

WORKFLOW_DIR=".github/workflows"
WORKFLOW_FILE="$WORKFLOW_DIR/release.yml"
INIT_TAG="v0.1.0-init"

if ! command -v git >/dev/null 2>&1; then
  echo "ERROR: git is required."
  exit 1
fi

if ! command -v gh >/dev/null 2>&1; then
  echo "ERROR: GitHub CLI (gh) is required."
  exit 1
fi

mkdir -p "$WORKFLOW_DIR"

cat > "$WORKFLOW_FILE" <<'YAML'
name: Release

on:
  push:
    tags:
      - "v*"
  workflow_dispatch:

permissions:
  contents: write

jobs:
  build-and-release:
    runs-on: ${{ matrix.os }}
    strategy:
      fail-fast: false
      matrix:
        os: [windows-latest, ubuntu-latest]

    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Install LLVM
        uses: KyleMayes/install-llvm-action@v2
        with:
          version: "21"

      - name: Smart Bootstrap (reuse release binary or fallback to stage0)
        shell: bash
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
        run: |
          set -euo pipefail

          if [[ "${{ runner.os }}" == "Windows" ]]; then
            STAGE1_BIN="stage1.exe"
            FINAL_BIN="thagore.exe"
            HELLO_BIN="hello.exe"
            FIB_BIN="fib.exe"
          else
            STAGE1_BIN="stage1"
            FINAL_BIN="thagore"
            HELLO_BIN="hello"
            FIB_BIN="fib"
          fi

          mkdir -p bootstrap
          BOOTSTRAP_OK=0

          if gh release download --repo "${{ github.repository }}" --pattern "thagore.exe" --pattern "thagore" -D bootstrap; then
            if [[ "${{ runner.os }}" == "Windows" && -f bootstrap/thagore.exe ]]; then
              cp bootstrap/thagore.exe "$STAGE1_BIN"
              BOOTSTRAP_OK=1
            elif [[ "${{ runner.os }}" != "Windows" && -f bootstrap/thagore ]]; then
              cp bootstrap/thagore "$STAGE1_BIN"
              chmod +x "$STAGE1_BIN"
              BOOTSTRAP_OK=1
            fi
          fi

          if [[ "$BOOTSTRAP_OK" -ne 1 ]]; then
            cmake -S legacy -B legacy/build -DCMAKE_BUILD_TYPE=Release
            cmake --build legacy/build --config Release

            if [[ "${{ runner.os }}" == "Windows" ]]; then
              STAGE0_BIN=""
              if [[ -f legacy/build/Release/thag.exe ]]; then
                STAGE0_BIN="legacy/build/Release/thag.exe"
              elif [[ -f legacy/build/thag.exe ]]; then
                STAGE0_BIN="legacy/build/thag.exe"
              fi
            else
              STAGE0_BIN=""
              if [[ -f legacy/build/thag ]]; then
                STAGE0_BIN="legacy/build/thag"
              elif [[ -f legacy/build/Release/thag ]]; then
                STAGE0_BIN="legacy/build/Release/thag"
              fi
            fi

            if [[ -z "$STAGE0_BIN" ]]; then
              echo "ERROR: Cannot locate stage0 binary after legacy build."
              exit 1
            fi

            chmod +x "$STAGE0_BIN" || true
            "$STAGE0_BIN" build src/thagore.tg -o "$STAGE1_BIN"
          fi

          chmod +x "$STAGE1_BIN" || true
          "$STAGE1_BIN" build src/thagore.tg -o "$FINAL_BIN"
          chmod +x "$FINAL_BIN" || true

      - name: Self-host tests
        shell: bash
        run: |
          set -euo pipefail

          if [[ "${{ runner.os }}" == "Windows" ]]; then
            FINAL_BIN="thagore.exe"
            HELLO_BIN="hello.exe"
            FIB_BIN="fib.exe"
          else
            FINAL_BIN="thagore"
            HELLO_BIN="hello"
            FIB_BIN="fib"
          fi

          "./$FINAL_BIN" build examples/hello.tg -o "$HELLO_BIN"
          "./$FINAL_BIN" build examples/fib.tg -o "$FIB_BIN"
          chmod +x "$HELLO_BIN" "$FIB_BIN" || true
          "./$HELLO_BIN"
          "./$FIB_BIN"

      - name: Package artifacts
        shell: bash
        run: |
          set -euo pipefail

          rm -rf dist
          mkdir -p dist/bin dist/lib

          if [[ "${{ runner.os }}" == "Windows" ]]; then
            cp thagore.exe dist/bin/
            ARTIFACT_NAME="thagore-windows"
            (cd dist && 7z a "../${ARTIFACT_NAME}.zip" ./*)
          else
            cp thagore dist/bin/
            chmod +x dist/bin/thagore
            ARTIFACT_NAME="thagore-linux"
            tar -C dist -czf "${ARTIFACT_NAME}.tar.gz" .
          fi

          if [[ -d std ]]; then
            cp -r std dist/lib/std
          fi

      - name: Release
        uses: softprops/action-gh-release@v2
        with:
          files: |
            thagore.exe
            thagore
            thagore-windows.zip
            thagore-linux.tar.gz
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
YAML

git add "$WORKFLOW_FILE"
if ! git diff --cached --quiet; then
  git commit -m "ci: add bootstrap-aware GitHub release workflow"
  git push
else
  echo "No workflow changes to commit."
fi

if ! git rev-parse "$INIT_TAG" >/dev/null 2>&1; then
  git tag -a "$INIT_TAG" -m "Initial bootstrap release trigger"
fi

if ! git ls-remote --tags origin "refs/tags/${INIT_TAG}" | grep -q "$INIT_TAG"; then
  git push origin "$INIT_TAG"
fi

if ! gh release view "$INIT_TAG" >/dev/null 2>&1; then
  gh release create "$INIT_TAG" \
    --prerelease \
    --title "$INIT_TAG" \
    --notes "Initial bootstrap release trigger."
else
  echo "Release $INIT_TAG already exists."
fi

echo "Done. Workflow created at $WORKFLOW_FILE"

#!/usr/bin/env bash
set -euo pipefail

mkdir -p target/v0/dist/bin
cp -f "$(command -v thagore)" target/v0/dist/bin/thagore
strip target/v0/dist/bin/thagore || true
tar -czf target/v0/dist/thagore-v0-linux-x86_64.tar.gz -C target/v0/dist bin

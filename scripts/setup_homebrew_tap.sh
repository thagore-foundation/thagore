#!/usr/bin/env bash
set -euo pipefail

TAP_REPO="thagore-foundation/homebrew-thagore"
WORKDIR="homebrew-thagore"

gh repo create "$TAP_REPO" --public --clone
cd "$WORKDIR"

mkdir -p Formula

cat > Formula/thagore.rb <<'RUBY'
class Thagore < Formula
  desc "Thagore self-hosted compiler"
  homepage "https://github.com/thagore-foundation/thagore"
  url "https://github.com/thagore-foundation/thagore/releases/download/v0.0.0/thagore-linux.tar.gz"
  sha256 "REPLACE_ME"
  license "MIT"
  version "0.0.0"

  def install
    bin.install "dist/bin/thagore"
    bin.install_symlink bin/"thagore" => "thag"
    libexec.install "dist/lib/std"
  end

  test do
    assert_match "thagore", shell_output("#{bin}/thagore --version 2>&1", 0)
  end
end
RUBY

git add Formula/thagore.rb
git commit -m "chore: initialize thagore formula"
git push origin main

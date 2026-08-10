#!/usr/bin/env python3
"""Render Formula/catcat.rb for the homebrew-catcat tap.

Reads release metadata from environment variables (set by the
update-homebrew job in .github/workflows/cmake-multi-platform.yml)
and prints the rendered formula to stdout.
"""

import os

VERSION = os.environ["CATCAT_VERSION"].removeprefix("v")

TEMPLATE = """\
class Catcat < Formula
  desc "Terminal tower defense with cats"
  homepage "https://github.com/DevinMcDonald/catcat"
  version "{version}"
  license "MIT" # Update if your project uses a different license

  on_macos do
    on_arm do
      url "{url_macos_arm64}"
      sha256 "{sha_macos_arm64}"
    end
    on_intel do
      url "{url_macos_x86_64}"
      sha256 "{sha_macos_x86_64}"
    end
  end

  on_linux do
    on_arm do
      url "{url_linux_arm64}"
      sha256 "{sha_linux_arm64}"
    end
    on_intel do
      url "{url_linux_x86_64}"
      sha256 "{sha_linux_x86_64}"
    end
  end

  def install
    bundle_root = (buildpath/"catcat_bundle").directory? ? buildpath/"catcat_bundle" : buildpath
    libexec.install bundle_root.children
    (bin/"catcat").write <<~EOS
      #!/bin/bash
      cd "#{{libexec}}"
      exec "./catcat" "$@"
    EOS
    chmod 0555, bin/"catcat"
  end

  test do
    assert_predicate bin/"catcat", :executable?
  end
end
"""

print(TEMPLATE.format(
    version=VERSION,
    url_macos_arm64=os.environ["URL_MACOS_ARM64"],
    sha_macos_arm64=os.environ["SHA_MACOS_ARM64"],
    url_macos_x86_64=os.environ["URL_MACOS_X86_64"],
    sha_macos_x86_64=os.environ["SHA_MACOS_X86_64"],
    url_linux_arm64=os.environ["URL_LINUX_ARM64"],
    sha_linux_arm64=os.environ["SHA_LINUX_ARM64"],
    url_linux_x86_64=os.environ["URL_LINUX_X86_64"],
    sha_linux_x86_64=os.environ["SHA_LINUX_X86_64"],
), end="")

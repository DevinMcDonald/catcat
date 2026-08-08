#!/usr/bin/env python3
"""Write a complete catcat Homebrew formula to stdout.

Reads these environment variables (all required):
  CATCAT_VERSION      — git tag, e.g. v1.2.3
  URL_MACOS_ARM64     SHA_MACOS_ARM64
  URL_MACOS_X86_64    SHA_MACOS_X86_64
  URL_LINUX_ARM64     SHA_LINUX_ARM64
  URL_LINUX_X86_64    SHA_LINUX_X86_64
"""

import os, sys

def require(name):
    val = os.environ.get(name, "")
    if not val:
        print(f"error: {name} is not set", file=sys.stderr)
        sys.exit(1)
    return val

raw_version  = require("CATCAT_VERSION")
version      = raw_version.lstrip("v")   # "v1.2.3" → "1.2.3"

url_mac_arm  = require("URL_MACOS_ARM64");  sha_mac_arm  = require("SHA_MACOS_ARM64")
url_mac_x86  = require("URL_MACOS_X86_64"); sha_mac_x86  = require("SHA_MACOS_X86_64")
url_lnx_arm  = require("URL_LINUX_ARM64");  sha_lnx_arm  = require("SHA_LINUX_ARM64")
url_lnx_x86  = require("URL_LINUX_X86_64"); sha_lnx_x86  = require("SHA_LINUX_X86_64")

# Note: {{ and }} are literal braces in Python f-strings.
# Ruby's #{...} interpolation uses # + braces, so we write #{{...}} here.
print(f"""\
class Catcat < Formula
  desc "Colorful terminal tower defense game"
  homepage "https://github.com/DevinMcDonald/catcat"
  license "MIT"
  version "{version}"

  on_macos do
    on_arm do
      url "{url_mac_arm}"
      sha256 "{sha_mac_arm}"
    end
    on_intel do
      url "{url_mac_x86}"
      sha256 "{sha_mac_x86}"
    end
  end

  on_linux do
    on_arm do
      url "{url_lnx_arm}"
      sha256 "{sha_lnx_arm}"
    end
    on_intel do
      url "{url_lnx_x86}"
      sha256 "{sha_lnx_x86}"
    end
  end

  def install
    # The game loads audio.json relative to CWD, so install everything into
    # libexec and cd there before exec'ing the binary.
    libexec.install Dir["*"]
    (bin/"catcat").write <<~SH
      #!/bin/sh
      cd "#{{libexec}}" && exec "./catcat" "$@"
    SH
  end

  test do
    assert_match version.to_s, shell_output("#{{bin}}/catcat --version 2>&1")
  end
end
""")

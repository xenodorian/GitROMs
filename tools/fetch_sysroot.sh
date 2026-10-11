#!/bin/bash
# Download Ubuntu 20.04 (glibc 2.31) libc packages so handheld binaries only
# reference old glibc symbols. A binary built against a newer glibc (2.34+)
# refuses to start on ArkOS images with an older one.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BASE=http://ports.ubuntu.com/ubuntu-ports/pool/main
for arch in arm64 armhf; do
  dir="$ROOT/build/sysroot-$arch"
  [ -f "$dir/usr/include/stdio.h" ] && continue
  mkdir -p "$dir" "$ROOT/build/dl"
  for pkg in g/glibc/libc6_2.31-0ubuntu9.18 g/glibc/libc6-dev_2.31-0ubuntu9.18 l/linux/linux-libc-dev_5.4.0-218.238; do
    f="$ROOT/build/dl/$(basename "$pkg")_$arch.deb"
    [ -f "$f" ] || curl -fsS -o "$f" "$BASE/${pkg}_$arch.deb"
    dpkg-deb -x "$f" "$dir"
  done
  echo "sysroot ready: $dir"
done

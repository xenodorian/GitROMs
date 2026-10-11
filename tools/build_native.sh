#!/bin/bash
# Build native/gitroms.c for the handheld (aarch64, armhf) and this PC (x86_64).
# SDL2 headers are not needed: the program declares SDL itself and links to a
# link-only stub with the real soname, so the device loads its own
# libSDL2-2.0.so.0 at run time. Needs gcc-aarch64-linux-gnu and
# gcc-arm-linux-gnueabihf (apt) for the handheld builds.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build"
mkdir -p "$OUT"
PROGS="gitroms"

# Stub exporting every SDL_ symbol the program uses.
STUB="$OUT/sdlstub.c"
{
  echo "/* generated: link-only SDL2 stub */"
  cat "$ROOT"/native/*.c | grep -oE '\bSDL_[A-Za-z]+\(' | tr -d '(' | sort -u | grep -vE '^SDL_(Event|Window|Renderer|Texture)$' |
    while read -r f; do echo "void *$f(void) { return 0; }"; done
} > "$STUB"

CFLAGS="-std=gnu99 -O2 -Wall -Wextra -Wno-format-truncation -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -s"

build() { # name cc [sysroot multiarch-dir]
  local name="$1" cc="$2" sr="${3:-}" ma="${4:-}"
  local sysroot=()
  # -B makes gcc use the old crt1.o from the sysroot (not the host's glibc 2.39 one).
  [ -n "$sr" ] && sysroot=(--sysroot="$sr" -B"$sr/usr/lib/$ma" -nostdinc
    -isystem "$("$cc" -print-file-name=include)" -isystem "$sr/usr/include/$ma" -isystem "$sr/usr/include")
  command -v "$cc" >/dev/null || { echo "skip $name ($cc not installed)"; return 0; }
  "$cc" "${sysroot[@]}" -shared -fPIC -Wl,-soname,libSDL2-2.0.so.0 -o "$OUT/libSDL2-stub.$name.so" "$STUB"
  for prog in $PROGS; do
    "$cc" "${sysroot[@]}" $CFLAGS -o "$OUT/$prog.$name" "$ROOT/native/$prog.c" -L"$OUT" -Wl,--no-as-needed \
      -l:libSDL2-stub.$name.so 2>&1 | head -20
    echo "built $OUT/$prog.$name"
  done
}
build x86_64 gcc
"$ROOT/tools/fetch_sysroot.sh"
build aarch64 aarch64-linux-gnu-gcc "$OUT/sysroot-arm64" aarch64-linux-gnu
build armhf arm-linux-gnueabihf-gcc "$OUT/sysroot-armhf" arm-linux-gnueabihf

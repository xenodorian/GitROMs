#!/bin/bash
# ONLINE test: GitROMs lists a release using the real BibooSGBA release JSON
# (tests/fixtures) and downloads the real asset from github.com, then checks
# the sha256 against the digest GitHub reported. Needs internet. PC only.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/gitroms.x86_64"
[ -x "$BIN" ] || { echo "build first: tools/build_native.sh"; exit 2; }
T="$(mktemp -d)"
python3 "$ROOT/tests/mock_github.py" 8769 >/dev/null 2>&1 &
MOCK=$!
trap 'kill $MOCK 2>/dev/null; rm -rf "${T:?}"' EXIT
sleep 1
mkdir -p "$T/roms" "$T/home"
export NO_PROXY=127.0.0.1 no_proxy=127.0.0.1 GITROMS_API=http://127.0.0.1:8769
export GITROMS_HOME="$T/home" GITROMS_ROMS="$T/roms" GITROMS_WINDOWED=1 SDL_VIDEODRIVER=dummy
# repo list (BibooSGBA is the first entry) -> repo root -> [Releases] -> third asset (.sgba) -> save
GITROMS_TEST="idle;shot=$T/list.ppm;act=confirm;idle;act=confirm;idle;shot=$T/rel.ppm;act=down;act=down;act=confirm;act=confirm;idle;wait=1;shot=$T/done.ppm;act=confirm;wait=0.2" \
  timeout 300 "$BIN" >/dev/null 2>&1
F="$T/roms/gba/parry-perry.sgba"
fail=0
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
chk "release listed with 3 usable assets" "grep -q 'release latest: 3 usable assets' \"$T/home/gitroms.log\""
chk "downloaded real asset (19110700 bytes)" "[ \"\$(stat -c %s \"$F\" 2>/dev/null)\" = 19110700 ]"
chk "sha256 matches GitHub's digest" "[ \"\$(sha256sum \"$F\" 2>/dev/null | cut -d' ' -f1)\" = b187b1d7c4eda1b89333f1acbbd5e4ab290ccc7b3bb30e19de72a534c88b5c91 ]"
chk "manifest tracks it with the asset id" "grep -q '634856327' \"$T/home/installed.txt\""
chk "no partial files left" "[ -z \"\$(find \"$T/roms\" -name '*.gitroms-part')\" ]"
chk "no fatal errors in log" "! grep -q FATAL \"$T/home/gitroms.log\""
mkdir -p "${TMPDIR:-/tmp}/gitroms-real-shots" && cp "$T"/*.ppm "${TMPDIR:-/tmp}/gitroms-real-shots/" 2>/dev/null
exit $fail

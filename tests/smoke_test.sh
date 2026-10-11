#!/bin/bash
# Headless functional run of the native build against a mock GitHub API.
# Uses SDL's dummy video driver. Needs libSDL2-2.0.so.0, python3, curl.
# Checks listing, pagination, download, collision handling and cleanup.
# PC only: not a device test.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/gitroms.x86_64"
[ -x "$BIN" ] || { echo "build first: tools/build_native.sh"; exit 2; }
T="$(mktemp -d)"
python3 "$ROOT/tests/mock_github.py" 8766 >/dev/null 2>&1 &
MOCK=$!
trap 'kill $MOCK 2>/dev/null; rm -rf "${T:?}"' EXIT
sleep 1
mkdir -p "$T/roms" "$T/home"
export NO_PROXY=127.0.0.1 no_proxy=127.0.0.1 GITROMS_API=http://127.0.0.1:8766
export GITROMS_HOME="$T/home" GITROMS_ROMS="$T/roms" GITROMS_WINDOWED=1 SDL_VIDEODRIVER=dummy
P=$(printf 'act=pgdn;%.0s' 1 2 3 4 5 6 7 8 9)
# open GBA-Roms, save game.gba twice (second as a numbered copy), open a
# folder, go back, open the history screen
GITROMS_TEST="idle;${P}act=up;act=confirm;idle;act=down;act=down;act=confirm;act=confirm;idle;wait=0.5;act=confirm;act=confirm;act=confirm;act=confirm;idle;wait=0.5;act=confirm;act=up;act=up;act=confirm;idle;act=cancel;idle;act=start;wait=0.3" \
  timeout 120 "$BIN"
RC=$?
fail=0
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
chk "app exited cleanly" "[ $RC = 0 ]"
chk "downloaded game.gba (1500000 bytes)" "[ \"\$(stat -c %s \"$T/roms/gba/game.gba\" 2>/dev/null)\" = 1500000 ]"
chk "second download saved as numbered copy" "[ -f \"$T/roms/gba/game (1).gba\" ]"
chk "no partial files left" "[ -z \"\$(find \"$T/roms\" -name '*.gitroms-part')\" ]"
chk "history written" "grep -q 'game.gba' \"$T/home/history.txt\""
chk "two repo pages fetched" "[ \"\$(grep -c 'users/xenodorian/repos' \"$T/home/gitroms.log\")\" = 2 ]"
chk "back navigation used the cache (repos fetched once)" "[ \"\$(grep -c 'contents?ref' \"$T/home/gitroms.log\")\" = 1 ]"
chk "subfolder listing fetched" "grep -q 'contents/hacks' \"$T/home/gitroms.log\""
exit $fail

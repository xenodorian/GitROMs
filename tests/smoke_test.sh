#!/bin/bash
# Headless functional run of the LOVE app against a mock GitHub API (needs
# love, xvfb-run, python3, curl). Checks listing, pagination, download,
# collision handling and cleanup. PC only: not a device test.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
T="$(mktemp -d)"
trap 'kill $MOCK 2>/dev/null; rm -rf "${T:?}"' EXIT
python3 "$ROOT/tests/mock_github.py" 8766 >/dev/null 2>&1 &
MOCK=$!
sleep 1
mkdir -p "$T/roms" "$T/home"
export NO_PROXY=127.0.0.1 no_proxy=127.0.0.1 GITROMS_API=http://127.0.0.1:8766
export GITROMS_HOME="$T/home" GITROMS_ROMS="$T/roms" GITROMS_WINDOWED=1
P=$(printf 'act=pgdn;%.0s' 1 2 3 4 5 6 7 8 9)
# open GBA-Roms, download game.gba twice (second as a numbered copy), back out
GITROMS_TEST="idle;${P}act=up;act=confirm;idle;act=down;act=down;act=confirm;act=confirm;idle;wait=0.5;act=confirm;act=confirm;act=confirm;act=confirm;idle;wait=0.5;act=confirm;wait=0.3" \
  timeout 120 xvfb-run -a -s "-screen 0 800x600x24" love "$ROOT/port/gitroms/game" >/dev/null 2>&1
fail=0
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
chk "downloaded game.gba (1500000 bytes)" "[ \"\$(stat -c %s \"$T/roms/gba/game.gba\" 2>/dev/null)\" = 1500000 ]"
chk "second download saved as numbered copy" "[ -f \"$T/roms/gba/game (1).gba\" ]"
chk "no partial files left" "[ -z \"\$(find \"$T/roms\" -name '*.gitroms-part')\" ]"
chk "history written" "grep -q 'game.gba' \"$T/home/history.txt\""
chk "two repo pages fetched" "grep -c 'users/xenodorian/repos' \"$T/home/gitroms.log\" | grep -q '^2$'"
chk "no fatal errors in log" "! grep -q FATAL \"$T/home/gitroms.log\""
exit $fail

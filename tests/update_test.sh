#!/bin/bash
# Headless test of update tracking: download, simulate a new commit and a new
# release build on the mock GitHub, then update in place. PC only.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/gitroms.x86_64"
[ -x "$BIN" ] || { echo "build first: tools/build_native.sh"; exit 2; }
T="$(mktemp -d)"
python3 "$ROOT/tests/mock_github.py" 8767 >/dev/null 2>&1 &
MOCK=$!
trap 'kill $MOCK 2>/dev/null; rm -rf "${T:?}"' EXIT
sleep 1
mkdir -p "$T/roms" "$T/home"
export NO_PROXY=127.0.0.1 no_proxy=127.0.0.1 GITROMS_API=http://127.0.0.1:8767
export GITROMS_HOME="$T/home" GITROMS_ROMS="$T/roms" GITROMS_WINDOWED=1 SDL_VIDEODRIVER=dummy
P=$(printf 'act=pgdn;%.0s' 1 2 3 4 5 6 7 8 9)
OPEN="idle;${P}act=up;act=confirm;idle"            # repo list -> GBA-Roms root
# root list order: [Releases], hacks/, Big Name.sgba, game.gba
RUN1="$OPEN;act=down;act=down;act=down;act=confirm;act=confirm;idle;wait=0.5;act=confirm;wait=0.2"   # download game.gba
RUN2="$OPEN;act=down;act=down;act=down;act=confirm;act=confirm;act=confirm;idle;wait=0.5;act=confirm;wait=0.2"  # update (replace) game.gba
REL="$OPEN;act=confirm;idle;act=confirm;act=confirm;idle;wait=0.5;act=confirm;wait=0.2"  # open [Releases], save zip
shot() { GITROMS_TEST="$1;shot=$T/$2.ppm" timeout 60 "$BIN" >/dev/null 2>&1; }
fail=0
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }

GITROMS_TEST="$RUN1" timeout 60 "$BIN" >/dev/null 2>&1
F="$T/roms/gba/game.gba"
chk "first download saved (1500000 bytes)" "[ \"\$(stat -c %s \"$F\" 2>/dev/null)\" = 1500000 ]"
chk "manifest records the file" "grep -q 'game.gba' \"$T/home/installed.txt\""
H1=$(sha1sum "$F" | cut -c1-12)

curl -s --noproxy '*' http://127.0.0.1:8767/__bump >/dev/null   # new commit upstream
GITROMS_TEST="$RUN2" timeout 60 "$BIN" >/dev/null 2>&1
chk "update replaced the file in place (new size 1500100)" "[ \"\$(stat -c %s \"$F\" 2>/dev/null)\" = 1500100 ]"
chk "content actually changed" "[ \"\$(sha1sum \"$F\" | cut -c1-12)\" != \"$H1\" ]"
chk "no extra copy was created" "[ \"\$(ls \"$T/roms/gba\" | grep -c 'game')\" = 1 ]"
chk "no partial files left" "[ -z \"\$(find \"$T/roms\" -name '*.gitroms-part')\" ]"
chk "history notes the update" "grep -q 'updated' \"$T/home/history.txt\""

GITROMS_TEST="$REL" timeout 60 "$BIN" >/dev/null 2>&1
Z="$T/roms/arcade/SGBA-arkos-aarch64.zip"
chk "release asset downloaded (20001 bytes)" "[ \"\$(stat -c %s \"$Z\" 2>/dev/null)\" = 20001 ]"
chk "release listing fetched" "grep -q 'releases/latest' \"$T/home/gitroms.log\""
curl -s --noproxy '*' http://127.0.0.1:8767/__bump >/dev/null   # new release build
GITROMS_TEST="$REL;act=confirm;act=confirm;idle;wait=0.5;act=confirm" timeout 60 "$BIN" >/dev/null 2>&1
chk "release asset updated in place (20002 bytes)" "[ \"\$(stat -c %s \"$Z\" 2>/dev/null)\" = 20002 ]"
chk "no fatal errors in log" "! grep -q FATAL \"$T/home/gitroms.log\""
exit $fail

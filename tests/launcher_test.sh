#!/bin/bash
# GitROMs.sh: an empty, junk or missing program file is repaired from a verified copy before it runs; a bad checksum or no
# network leaves things alone and says so; a healthy program starts without touching the network. PC only.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/gitroms.x86_64"
[ -x "$BIN" ] || { echo "build first: tools/build_native.sh"; exit 2; }
T="$(mktemp -d)"
PORT=8791
mkdir -p "$T/srv" "$T/ports/gitroms/data" "$T/roms"
cp "$BIN" "$T/srv/gitroms.x86_64"
( cd "$T/srv" && sha256sum gitroms.x86_64 > gitroms.x86_64.sha256 )
( cd "$T/srv" && exec python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1 ) &
SRV=$!
trap 'kill $SRV 2>/dev/null; rm -rf "${T:?}"' EXIT
sleep 1
cp "$ROOT/port/GitROMs.sh" "$T/ports/GitROMs.sh"
P="$T/ports/gitroms/gitroms.x86_64"
export NO_PROXY=127.0.0.1 no_proxy=127.0.0.1 DEVICE_ARCH=x86_64 GITROMS_TEST="idle" GITROMS_WINDOWED=1 SDL_VIDEODRIVER=dummy GITROMS_NODEFAULTS=1 GITROMS_API=http://127.0.0.1:1 GITROMS_REPAIR_MIN=1000
fail=0
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
run() { GITROMS_REPAIR_BASE="http://127.0.0.1:${1:-$PORT}" timeout 60 bash "$T/ports/GitROMs.sh" >/dev/null 2>&1; echo $?; }
L="$T/ports/gitroms/data/launch.log"

: > "$P"
rc=$(run)
chk "an empty program file is replaced by the verified copy and starts (exit 0)" "[ $rc = 0 ] && cmp -s \"$P\" \"$T/srv/gitroms.x86_64\" && grep -q 'repaired' \"$L\""
chk "no partial file is left behind" "[ ! -e \"$P.part\" ]"

head -c 80000 /dev/urandom > "$P"
rc=$(run)
chk "a program file full of junk is replaced too" "[ $rc = 0 ] && cmp -s \"$P\" \"$T/srv/gitroms.x86_64\""

rm -f "$P"
rc=$(run)
chk "a missing program file is fetched" "[ $rc = 0 ] && cmp -s \"$P\" \"$T/srv/gitroms.x86_64\""

rc=$(run 1)
chk "a healthy program starts without any network use" "[ $rc = 0 ] && ! grep -q 'damaged' \"$L\""

head -c 80000 /dev/urandom > "$P"; cp "$P" "$T/junk.keep"
echo "0000000000000000000000000000000000000000000000000000000000000000  gitroms.x86_64" > "$T/srv/gitroms.x86_64.sha256"
rc=$(run)
chk "a copy with the wrong checksum is refused (exit 1) and the old file is untouched" "[ $rc = 1 ] && cmp -s \"$P\" \"$T/junk.keep\" && [ ! -e \"$P.part\" ] && grep -q 'repair failed' \"$L\""

( cd "$T/srv" && sha256sum gitroms.x86_64 > gitroms.x86_64.sha256 )
rc=$(run 1)
chk "no network: exit 1, a clear message in the log, nothing replaced" "[ $rc = 1 ] && cmp -s \"$P\" \"$T/junk.keep\" && grep -q 'repair failed' \"$L\""
exit $fail

#!/bin/bash
# GitROMs' own diagnostics: a crash report with registers and the last log lines, an "unclean exit" report after a kill, the
# previous log kept across starts, leftovers of interrupted updates removed at start, and the gitroms-* reports being posted. PC only.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/gitroms.x86_64"
[ -x "$BIN" ] || { echo "build first: tools/build_native.sh"; exit 2; }
T="$(mktemp -d)"
PORT=8786
python3 "$ROOT/tests/mock_reports.py" $PORT >/dev/null 2>&1 &
MOCK=$!
trap 'kill $MOCK $APP 2>/dev/null; rm -rf "${T:?}"' EXIT
sleep 1
export NO_PROXY=127.0.0.1 no_proxy=127.0.0.1 GITROMS_NODEFAULTS=1 GITROMS_API="http://127.0.0.1:$PORT" GITROMS_WINDOWED=1 SDL_VIDEODRIVER=dummy
PORTS="$T/ports"; H="$PORTS/gitroms/data"; R="$PORTS/sgba/reports"
mkdir -p "$H" "$T/roms"
fail=0; APP=""
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
run() { GITROMS_HOME="$H" GITROMS_PORTS="$PORTS" GITROMS_ROMS="$T/roms" GITROMS_TEST="$1" timeout 60 "$BIN" >/dev/null 2>&1; echo $?; }
posts() { curl -s --noproxy '*' "http://127.0.0.1:$PORT/__posts"; }

# 1. a crash
rc=$(run "idle;crash")
chk "a crash ends with the segmentation fault status (139), not a clean exit" "[ $rc = 139 ]"
C=$(ls "$R"/gitroms-crash-*.txt 2>/dev/null | head -1)
chk "a gitroms-crash report was written to sgba/reports" "[ -n \"$C\" ]"
chk "it names the version, source hash, signal and fault address" "grep -q '^version: ' \"$C\" && grep -q '^source: ' \"$C\" && grep -q '^signal: 11' \"$C\" && grep -q '^fault address: 0x0' \"$C\""
chk "it has the program mapping, pc and lr" "grep -q '^program mapping: ' \"$C\" && grep -q '^pc: 0x' \"$C\" && grep -q '^lr: 0x' \"$C\""
chk "it carries the last log lines, ending at the crash" "grep -q 'test: crashing on purpose' \"$C\" && grep -q 'GitROMs .* start' \"$C\""
chk "a crash leaves no running marker (so no second, unclean report)" "[ ! -e \"$H/.running\" ]"
rc=$(run "idle")
chk "the next start is clean and writes no unclean report" "[ $rc = 0 ] && [ -z \"\$(ls \"$R\"/gitroms-unclean-*.txt 2>/dev/null)\" ]"
chk "the log of the crashed run is kept as gitroms.prev.log" "grep -q 'crashing on purpose' \"$H/gitroms.prev.log\""

# 2. killed (power loss, ES hotkey): the marker stays
rm -f "$R"/gitroms-*.txt
GITROMS_HOME="$H" GITROMS_PORTS="$PORTS" GITROMS_ROMS="$T/roms" GITROMS_TEST="idle;wait=30" "$BIN" >/dev/null 2>&1 &
APP=$!
sleep 2
chk "a running app has a marker with its state" "grep -q '^state=' \"$H/.running\" && grep -q '^version=' \"$H/.running\""
kill -9 $APP; wait $APP 2>/dev/null
chk "kill -9 leaves the marker" "[ -e \"$H/.running\" ]"
rc=$(run "idle")
U=$(ls "$R"/gitroms-unclean-*.txt 2>/dev/null | head -1)
chk "the next start writes an unclean report with the marker and the old log" "[ -n \"$U\" ] && grep -q '^state=' \"$U\" && grep -q 'start' \"$U\""
chk "and clears the marker again after a clean exit" "[ ! -e \"$H/.running\" ]"

# 3. leftovers of interrupted updates
mkdir -p "$PORTS/sgba/games" "$T/roms/gba"
echo x > "$PORTS/sgba/games/game.sgba.gitroms-part"; echo x > "$T/roms/gba/Hack.gba.gitroms-part"; echo keep > "$T/roms/gba/Hack.gba"
echo x > "$H/tmp/up.r0"
rc=$(run "idle")
chk "half-downloaded parts and old temp files are removed at start" "[ ! -e \"$PORTS/sgba/games/game.sgba.gitroms-part\" ] && [ ! -e \"$T/roms/gba/Hack.gba.gitroms-part\" ] && [ ! -e \"$H/tmp/up.r0\" ]"
chk "a real ROM next to a part is untouched" "[ \"\$(cat \"$T/roms/gba/Hack.gba\")\" = keep ]"
chk "the log says what was removed and how the card is" "grep -q 'housekeeping: removed 3' \"$H/gitroms.log\" && grep -q 'card: writable' \"$H/gitroms.log\""

# 4. the reports go out under their own kinds
printf 'repo=owner/repo\ntoken=secret123\n' > "$H/upload.conf"
printf 'GitROMs report: crash\nversion: 3.10\nsignal: 11\n' > "$R/gitroms-crash-1.txt"
printf 'GitROMs report: unclean\nversion now: 3.10\n' > "$R/gitroms-unclean-20261015-010101.txt"
curl -s --noproxy '*' "http://127.0.0.1:$PORT/__reset" >/dev/null
rc=$(run "idle;rpopen;act=confirm;idle;idle")
chk "gitroms-crash and gitroms-unclean reports are posted with those kinds in the title" "posts | python3 -c 'import sys,json;t=[p[\"title\"] for p in json.load(sys.stdin)];raise SystemExit(0 if any(x.startswith(\"[gitroms-crash]\") for x in t) and any(x.startswith(\"[gitroms-unclean]\") for x in t) else 1)'"
chk "the status report includes the previous GitROMs log" "posts | python3 -c 'import sys,json;b=[p for p in json.load(sys.stdin) if p[\"title\"].startswith(\"[status]\")][0][\"body\"];raise SystemExit(0 if \"gitroms.prev.log\" in b else 1)'"
exit $fail

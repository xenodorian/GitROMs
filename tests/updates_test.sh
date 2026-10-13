#!/bin/bash
# Update engine test against a fake release host (tests/mock_updates.py):
# first install, up-to-date check, delta update fetching only changed files,
# keep-list, exec bits, corruption and path-traversal safety, and a server
# that ignores byte ranges. PC only.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/gitroms.x86_64"
[ -x "$BIN" ] || { echo "build first: tools/build_native.sh"; exit 2; }
T="$(mktemp -d)"
PORT=8781
python3 "$ROOT/tests/mock_updates.py" $PORT >/dev/null 2>&1 &
MOCK=$!
trap 'kill $MOCK $MOCK2 2>/dev/null; rm -rf "${T:?}"' EXIT
MOCK2=""
sleep 1
export NO_PROXY=127.0.0.1 no_proxy=127.0.0.1 GITROMS_NODEFAULTS=1 GITROMS_API=http://127.0.0.1:1 GITROMS_WINDOWED=1 SDL_VIDEODRIVER=dummy
PORTS="$T/ports"; mkdir -p "$PORTS/gitroms/data" "$T/roms"
cat > "$T/components.conf" <<CONF
Bundle|zip|http://127.0.0.1:$PORT/bundle.zip|.|README.txt,gitroms/gitroms.conf|self
Game|file|http://127.0.0.1:$PORT/game.sgba|sgba/games/game.sgba||
CONF
fail=0
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
srv() { curl -s --noproxy '*' "http://127.0.0.1:${2:-$PORT}$1" >/dev/null; }
stat_() { curl -s --noproxy '*' "http://127.0.0.1:$PORT/__stats" | python3 -c "import sys,json;d=json.load(sys.stdin);print(d['by'].get('/$1',0) if '$1' in ('bundle.zip','game.sgba') else d['$1'])"; }
app() { # steps...
  rm -f "$PORTS/gitroms/data/gitroms.log"
  GITROMS_HOME="$PORTS/gitroms/data" GITROMS_PORTS="$PORTS" GITROMS_ROMS="$T/roms" GITROMS_COMPONENTS="${COMPS:-$T/components.conf}" \
    GITROMS_TEST="$1" timeout 120 "$BIN" >/dev/null 2>&1
  LOG="$PORTS/gitroms/data/gitroms.log"
}
st() { grep -o "upd\[$1\] .*" "$LOG" | tail -1; }          # last logged line for component $1
OPEN="idle;act=info;idle"                                    # GitHub API unreachable -> error screen -> X = updates -> wait for the check
HERE="$ROOT/tests"

# 1. fresh card: everything is "Not installed", then push-button install
srv /__reset
app "$OPEN;logscr;logupd"
chk "update screen opens (screen 10)" "grep -q 'scr=10 sp=0' \"\$LOG\""
chk "bundle reported as Not installed (125 files)" "st 0 | grep -q 'st=4 nneed=125'"
chk "game reported as Not installed" "st 1 | grep -q 'st=4'"
app "$OPEN;act=confirm;idle;logupd"
chk "install: bundle then re-check says up to date" "st 0 | grep -q 'st=2 .*msg=Updated\\|st=2 nneed=0'"
chk "install: game up to date" "st 1 | grep -q 'st=2'"
chk "all bundle files written correctly" "python3 - <<PY
import zipfile,urllib.request,os,io
ports='$PORTS'
data=urllib.request.build_opener(urllib.request.ProxyHandler({})).open('http://127.0.0.1:$PORT/bundle.zip').read()
z=zipfile.ZipFile(io.BytesIO(data)); bad=0
for i in z.infolist():
    p=os.path.join(ports,i.filename)
    if not os.path.exists(p) or open(p,'rb').read()!=z.read(i.filename): bad+=1
raise SystemExit(bad)
PY"
chk "executable bit set on SGBA.sh and the python binary" "[ -x \"$PORTS/SGBA.sh\" ] && [ -x \"$PORTS/sgba/python/bin/python3\" ] && [ ! -x \"$PORTS/README.txt\" ]"
chk "game installed to sgba/games/" "[ -s \"$PORTS/sgba/games/game.sgba\" ]"
chk "no temp or partial files left" "[ -z \"\$(find \"$PORTS\" -name '*.gitroms-part')\" ] && [ -z \"\$(ls \"$PORTS/gitroms/data/tmp\" 2>/dev/null | grep -v '^up.err\$')\" ]"

# 2. nothing changed: a check must not download any file contents
srv /__reset
app "$OPEN;logupd"
chk "second check: bundle up to date" "st 0 | grep -q 'st=2'"
chk "second check: game up to date" "st 1 | grep -q 'st=2'"
B=$(stat_ bundle.zip); chk "second check read the file list but no file contents ($B bytes)" "[ $B -lt 300000 ]"
chk "up to date message shows the modified date" "st 0 | grep -q 'modified .* UTC'"
chk "modified times were recorded in updates.txt" "grep -q '^Bundle	' \"$PORTS/gitroms/data/updates.txt\" && grep -q '^Game	' \"$PORTS/gitroms/data/updates.txt\""

# 2b. same content but a newer modified time: bundle re-compares and stays up to date, game is offered
srv "/__touch?path=/bundle.zip"; srv "/__touch?path=/game.sgba"; srv /__reset
app "$OPEN;logupd"
chk "touched bundle: file compare finds nothing to update" "st 0 | grep -q 'st=2 nneed=0'"
chk "touched game: offered as an update (modified time is what counts)" "st 1 | grep -q 'st=3'"
srv /__reset
app "$OPEN;act=confirm;idle;logupd"
app "$OPEN;logupd"
chk "after the game refresh both are up to date again" "st 0 | grep -q 'st=2' && st 1 | grep -q 'st=2'"

# 2c. files changed or deleted by hand with the stamp unchanged: every bundle file is still scanned
rm -f "$PORTS/sgba/lib/mod119.py" "$PORTS/sgba/lib/mod000.py" "$PORTS/sgba/data/conf.json"
python3 - <<PY
p="$PORTS/sgba/lib/mod050.py"; d=bytearray(open(p,'rb').read()); d[40]^=1; open(p,'wb').write(d)   # same size, one byte different
PY
app "$OPEN;logupd"
chk "hand-changed and missing files are found without a new modified time (4 files)" "st 0 | grep -q 'st=3 nneed=4'"
app "$OPEN;act=confirm;idle;logupd"
chk "all four files restored" "st 0 | grep -q 'st=2' && [ -f \"$PORTS/sgba/lib/mod119.py\" ] && [ -f \"$PORTS/sgba/lib/mod000.py\" ] && [ -f \"$PORTS/sgba/data/conf.json\" ] && grep -q 'line 0' \"$PORTS/sgba/lib/mod050.py\"" 

# 3. upstream changes 2 files, adds 1; local README was edited (kept), conf is kept
echo "my local readme" > "$PORTS/README.txt"
srv "/__set?name=sgba/lib/mod007.py&text=changed%20v2"
srv "/__set?name=SGBA.sh&text=%23!/bin/sh%0Aecho%20v2"
srv "/__add?name=sgba/lib/newmod.py&text=brand%20new"
srv "/__set?name=README.txt&text=readme%20v2"
srv "/__game?text=print('game%20v2')"
srv /__reset
app "$OPEN;logupd"
chk "check sees 3 changed files in the bundle (README is on the keep list)" "st 0 | grep -q 'st=3 nneed=3'"
chk "check sees the new game version" "st 1 | grep -q 'st=3'"
srv /__reset
app "$OPEN;act=confirm;idle;logupd"
chk "update applied; bundle and game up to date" "st 0 | grep -q 'st=2' && st 1 | grep -q 'st=2'"
chk "changed files have the new content" "grep -q 'changed v2' \"$PORTS/sgba/lib/mod007.py\" && grep -q 'v2' \"$PORTS/SGBA.sh\" && [ -f \"$PORTS/sgba/lib/newmod.py\" ] && unzip -p \"$PORTS/sgba/games/game.sgba\" main.py | grep -q 'game v2'"
chk "kept file was NOT overwritten" "grep -q 'my local readme' \"$PORTS/README.txt\""
chk "exec bit survived the update" "[ -x \"$PORTS/SGBA.sh\" ]"
B=$(stat_ bundle.zip); chk "update fetched only the changed bundle files ($B of ~2.7 MB bytes, including two file-list reads)" "[ $B -lt 450000 ]"
chk "unchanged files were left alone (mtime of mod008.py unchanged since first install)" "[ \$(find \"$PORTS/sgba/lib/mod008.py\" -newer \"$PORTS/sgba/lib/mod007.py\" | wc -l) = 0 ]"

# 4. a damaged download is rejected and the old file survives
srv "/__set?name=sgba/lib/mod010.py&text=v3%20content"
srv "/__corrupt?name=sgba/lib/mod010.py"
cp "$PORTS/sgba/lib/mod010.py" "$T/mod010.before"
app "$OPEN;act=confirm;idle;logupd"
chk "corrupt download: bundle reports an error" "st 0 | grep -q 'st=5'"
chk "corrupt download: error mentions the checksum" "st 0 | grep -qi 'checksum\\|corrupt'"
chk "corrupt download: the old file is untouched" "cmp -s \"$PORTS/sgba/lib/mod010.py\" \"$T/mod010.before\""
chk "corrupt download: no partial file left" "[ -z \"\$(find \"$PORTS\" -name '*.gitroms-part')\" ]"
srv /__clean

# 5. path traversal entry is refused and nothing escapes the ports folder
srv /__evil
app "$OPEN;logupd"
chk "unsafe path: refused" "st 0 | grep -q 'st=5' && st 0 | grep -qi 'unsafe'"
chk "unsafe path: nothing written outside the ports folder" "[ ! -e \"$T/evil.txt\" ] && [ ! -e \"$PORTS/../evil.txt\" ]"
srv /__clean

# 6. a server that ignores byte ranges: still works (downloads the whole file)
kill $MOCK 2>/dev/null; sleep 0.5
python3 "$ROOT/tests/mock_updates.py" $PORT --norange >/dev/null 2>&1 & MOCK2=$!
sleep 1
rm -rf "$PORTS/sgba" "$PORTS/SGBA.sh"
app "$OPEN;act=confirm;idle;logupd"                         # fresh install through a whole-file download
chk "no-range server: fresh install works" "st 0 | grep -q 'st=2'"
srv "/__set?name=sgba/lib/mod020.py&text=norange%20change"
app "$OPEN;logupd"
chk "no-range server: check still works" "st 0 | grep -q 'st=3 nneed=1'"
app "$OPEN;act=confirm;idle;logupd"
chk "no-range server: update applied" "grep -q 'norange change' \"$PORTS/sgba/lib/mod020.py\" && st 0 | grep -q 'st=2'"
chk "no fatal errors" "! grep -q FATAL \"$LOG\""
exit $fail

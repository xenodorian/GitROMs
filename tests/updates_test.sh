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
GameDir|zip|http://127.0.0.1:$PORT/game.sgba|sgba/games/gamedir||||sgba/games/legacy.sgba,sgba/games/legacydir
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
mkdir -p "$PORTS/sgba/games/legacydir/sub"; echo old > "$PORTS/sgba/games/legacy.sgba"; echo old > "$PORTS/sgba/games/legacydir/sub/x.txt"
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
chk "a .sgba can be unpacked into a game folder (main.py there)" "[ -s \"$PORTS/sgba/games/gamedir/main.py\" ] && [ -s \"$PORTS/sgba/games/gamedir/manifest.json\" ]"
chk "the old file and the old folder (with contents) were removed after the verified install" "[ ! -e \"$PORTS/sgba/games/legacy.sgba\" ] && [ ! -e \"$PORTS/sgba/games/legacydir\" ] && st 2 | grep -q 'st=2'"
chk "game installed to sgba/games/" "[ -s \"$PORTS/sgba/games/game.sgba\" ]"
chk "no temp or partial files left" "[ -z \"\$(find \"$PORTS\" -name '*.gitroms-part')\" ] && [ -z \"\$(ls \"$PORTS/gitroms/data/tmp\" 2>/dev/null | grep -v '^up.err\$')\" ]"

# 2. nothing changed: a check must not download any file contents
srv /__reset
app "$OPEN;logupd"
chk "second check: bundle up to date" "st 0 | grep -q 'st=2'"
chk "second check: game up to date" "st 1 | grep -q 'st=2'"
B=$(stat_ bundle.zip); chk "second check read the file list but no file contents ($B bytes)" "[ $B -lt 300000 ]"
chk "modified date is kept apart from the message (drawn on the name line)" "st 0 | grep -q 'date=\\[[0-9]* [A-Z][a-z][a-z] [0-9:]*\\] msg=Up to date (125 files)\$'"
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
# 5. a commit-dated file whose GitHub lookup fails (the API host is unreachable here): the header stamp must not make it "changed" forever
srv /__reset
cat > "$T/commit.conf" <<CONF
Cm|file|http://127.0.0.1:$PORT/game.sgba|sgba/games/cm.sgba|||commit:o/r:p
CONF
COMPS="$T/commit.conf" app "$OPEN;act=confirm;idle;logupd"
chk "commit-dated file installs while the GitHub lookup is down" "[ -s \"$PORTS/sgba/games/cm.sgba\" ]"
COMPS="$T/commit.conf" app "$OPEN;logupd"
chk "and the next check says up to date, not changed" "st 0 | grep -q 'st=2'"
printf 'Cm\t2026-10-13T22:31:53Z\n' > "$PORTS/gitroms/data/updates.txt"
COMPS="$T/commit.conf" app "$OPEN;logupd"
chk "a recorded commit date and a failed lookup (header stamp): same size file stands as up to date" "st 0 | grep -q 'st=2'"
COMPS="$T/commit.conf" app "$OPEN;logupd"
chk "and it stays up to date on the next check" "st 0 | grep -q 'st=2'"
# 6. a cut-off copy (an interrupted or damaged write) has the wrong size: offered as an update whatever the stamps say, and repaired
head -c 100 "$PORTS/sgba/games/cm.sgba" > "$PORTS/sgba/games/cm.cut" && mv "$PORTS/sgba/games/cm.cut" "$PORTS/sgba/games/cm.sgba"
COMPS="$T/commit.conf" app "$OPEN;logupd"
chk "a truncated single file is reported as incomplete" "st 0 | grep -q 'st=3' && grep -q 'Incomplete or damaged' \"$LOG\""
COMPS="$T/commit.conf" app "$OPEN;act=confirm;idle;logupd"
chk "and the update puts the whole file back" "st 0 | grep -q 'st=2' && [ \"\$(wc -c < \"$PORTS/sgba/games/cm.sgba\")\" -gt 100 ]"
# 7. same size but different content (damage, a block of old data): noticed through the CRC recorded at install, and repaired
python3 - "$PORTS/sgba/games/cm.sgba" <<'PY'
import sys
p=sys.argv[1]; d=bytearray(open(p,'rb').read()); d[len(d)//2]^=0xFF; open(p,'wb').write(d)
PY
COMPS="$T/commit.conf" app "$OPEN;logupd"
chk "a same-size damaged single file is reported as damaged" "st 0 | grep -q 'st=3' && grep -q 'Damaged' \"$LOG\""
COMPS="$T/commit.conf" app "$OPEN;act=confirm;idle;logupd"
chk "and the update replaces it with the right bytes" "st 0 | grep -q 'st=2'"
COMPS="$T/commit.conf" app "$OPEN;logupd"
chk "the record has a checksum for the installed file" "grep -q 'crc:[0-9a-f]\{8\}' \"$PORTS/gitroms/data/updates.txt\""
chk "and the repaired file now checks out as up to date" "st 0 | grep -q 'st=2'"
# 8. an empty program file (what a lost write leaves) in a bundle is found by size and CRC and rewritten
COMPS="" app "$OPEN;act=confirm;idle;logupd"
: > "$PORTS/sgba/lib/mod020.py"
app "$OPEN;logupd"
chk "bundle: an emptied file is found by the check" "st 0 | grep -q 'st=3 nneed=1'"
app "$OPEN;act=confirm;idle;logupd"
chk "bundle: and rewritten, read back and verified" "[ -s \"$PORTS/sgba/lib/mod020.py\" ] && st 0 | grep -q 'st=2'"
# 9. updates are decided by commits, not by a changed Last-Modified or a rebuilt file
cat > "$T/gate.conf" <<CONF
GateFile|file|http://127.0.0.1:$PORT/game.sgba|sgba/games/gate.sgba|||commit:o/r:p
GateBundle|zip|http://127.0.0.1:$PORT/bundle.zip|gatebundle|||commit:o/r:
CONF
rm -f "$PORTS/gitroms/data/updates.txt"
gapp() { GITROMS_API="http://127.0.0.1:$PORT" COMPS="$T/gate.conf" app "$@"; }
srv "/__commit?date=2026-10-10T10:00:00Z"
gapp "$OPEN;act=confirm;idle;logupd"
chk "gate: first install of a commit-dated file and bundle" "st 0 | grep -q 'st=2' && st 1 | grep -q 'st=2' && [ -s \"$PORTS/sgba/games/gate.sgba\" ] && [ -s \"$PORTS/gatebundle/README.txt\" ]"
chk "gate: the record keeps the commit date and the install time" "grep -q '^GateFile	2026-10-10T10:00:00Z' \"$PORTS/gitroms/data/updates.txt\" && grep -q 'at:20' \"$PORTS/gitroms/data/updates.txt\""
srv "/__touch?path=/game.sgba"; srv "/__touch?path=/bundle.zip"
gapp "$OPEN;logupd"
chk "gate: a new Last-Modified without a new commit offers nothing" "st 0 | grep -q 'st=2' && st 1 | grep -q 'st=2'"
srv "/__commit?date=2026-10-12T10:00:00Z"; srv "/__game?text=changed"
gapp "$OPEN;logupd"
chk "gate: a newer commit offers the file as a new version" "st 0 | grep -q 'st=3' && grep -q 'is newer than the installed 2026-10-10T10:00:00Z' \"$LOG\""
gapp "$OPEN;act=confirm;idle;logupd"
chk "gate: after the update the commit date is recorded" "st 0 | grep -q 'st=2' && grep -q '^GateFile	2026-10-12T10:00:00Z' \"$PORTS/gitroms/data/updates.txt\""
srv "/__commit?date=2026-10-01T00:00:00Z"
gapp "$OPEN;logupd"
chk "gate: an older commit date never offers a downgrade" "st 0 | grep -q 'st=2'"
srv "/__commit?date=2026-10-12T10:00:00Z"
srv "/__set?name=README.txt&text=v2"
gapp "$OPEN;logupd"
chk "gate: a changed bundle with a newer commit is offered as an update, not a repair" "st 1 | grep -q 'st=3' && ! grep -q 'Repair:' \"$LOG\""
gapp "$OPEN;act=confirm;idle;logupd"
chk "gate: after that update the bundle's record is the new commit" "st 1 | grep -q 'st=2' && grep -q '^GateBundle	2026-10-12T10:00:00Z' \"$PORTS/gitroms/data/updates.txt\""
echo junk > "$PORTS/gatebundle/README.txt"
gapp "$OPEN;logupd"
chk "gate: a damaged bundle file with no new commit is a repair, not an update" "st 1 | grep -q 'st=3' && grep -q 'Repair: 1 of' \"$LOG\""
printf 'repo=o/r\ntoken=abc123\n' > "$PORTS/gitroms/data/upload.conf"
srv /__reset
gapp "$OPEN;logupd"
chk "gate: with a token in upload.conf the commit lookups are authenticated" "grep -q 'api: GitHub requests are authenticated' \"$LOG\" && [ \"\$(curl -s --noproxy '*' http://127.0.0.1:$PORT/__stats | python3 -c 'import sys,json;d=json.load(sys.stdin);print(d[\"authed\"]>0 and d[\"authed\"]==d[\"commit_calls\"])')\" = True ]"
chk "gate: the token never appears in the log" "! grep -q abc123 \"$LOG\""
# 10. a file found by name: not published yet, then found wherever it is committed (accents and case do not matter), installed, and kept current by commits
cat > "$T/find.conf" <<CONF
Hack|file|find:o/r:Pok\xc3\xa9monWarAndMurder.gba|../gba/PokemonWarAndMurder.gba
CONF
printf 'Hack|file|find:o/r:Pok\xc3\xa9monWarAndMurder.gba|../gba/PokemonWarAndMurder.gba\n' > "$T/find.conf"
rm -f "$PORTS/gitroms/data/updates.txt"; mkdir -p "$T/roms/gba"
fapp() { GITROMS_RAW="http://127.0.0.1:$PORT/o" GITROMS_API="http://127.0.0.1:$PORT" COMPS="$T/find.conf" app "$@"; }
srv "/__tree?paths=README.md,roms/Pokemon%20War%20and%20Murder.gba,roms/c3_build_base.gba"
fapp "$OPEN;logupd"
chk "find: a file that is not in the repo reads 'Not published yet' and offers nothing" "st 0 | grep -q 'st=2' && st 0 | grep -q 'Not published yet' && [ ! -e \"$T/gba/PokemonWarAndMurder.gba\" ]"
chk "find: the old ROM with spaces in its name is not mistaken for the new file" "! grep -q 'found ' \"$LOG\""
srv "/__tree?paths=README.md,builds/PokemonWarAndMurder.gba,deep/er/PokemonWarAndMurder.gba"
srv "/__commit?date=2026-10-14T10:00:00Z"
fapp "$OPEN;logupd"
chk "find: once committed (without the accent, in a subfolder) it is found, the nearest to the top wins" "st 0 | grep -q 'st=4' && grep -q 'found Pok.*at builds/PokemonWarAndMurder.gba (2 matches)' \"$LOG\""
fapp "$OPEN;act=confirm;idle;logupd"
chk "find: it installs into the gba folder from the resolved path and records the commit date" "[ -s \"$T/gba/PokemonWarAndMurder.gba\" ] && grep -q '^Hack	2026-10-14T10:00:00Z' \"$PORTS/gitroms/data/updates.txt\" && curl -s --noproxy '*' http://127.0.0.1:$PORT/__stats | grep -q 'builds/PokemonWarAndMurder.gba'"
fapp "$OPEN;logupd"
chk "find: checked again with no new commit it is up to date" "st 0 | grep -q 'st=2'"
srv "/__commit?date=2026-10-15T08:00:00Z"
fapp "$OPEN;logupd"
chk "find: a newer commit offers the new version" "st 0 | grep -q 'st=3'"
# 10b. alternative names (a;b): the first name listed wins, the other is used when it is the only one there
printf 'Hack|file|find:o/r:Pok\xc3\xa9monWarAndMurder.gba;Pokemon War and Murder.gba|../gba/PokemonWarAndMurder.gba\n' > "$T/find.conf"
rm -f "$PORTS/gitroms/data/updates.txt" "$T/gba/PokemonWarAndMurder.gba"
srv "/__tree?paths=README.md,roms/Pokemon%20War%20and%20Murder.gba,roms/c3_build_base.gba"
fapp "$OPEN;logupd"
chk "find: the second listed name is found when it is the only one in the repo" "st 0 | grep -q 'st=4' && grep -q 'at roms/Pokemon War and Murder.gba (1 match)' \"$LOG\""
srv "/__tree?paths=roms/Pokemon%20War%20and%20Murder.gba,deep/er/x/PokemonWarAndMurder.gba"
fapp "$OPEN;logupd"
chk "find: the first listed name wins over a nearer copy of the second" "grep -q 'at deep/er/x/PokemonWarAndMurder.gba (2 matches)' \"$LOG\""
chk "no fatal errors" "! grep -q FATAL \"$LOG\""
exit $fail

#!/bin/bash
# Headless test of the raw joystick mapping (R36S defaults from BibooSGBA's
# controls-arkos.json), every optional button, the d-pad hat fallback, the
# gptokeyb "keys only" mode and the gitroms.conf overrides. PC only.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/gitroms.x86_64"
[ -x "$BIN" ] || { echo "build first: tools/build_native.sh"; exit 2; }
T="$(mktemp -d)"
python3 "$ROOT/tests/mock_github.py" 8771 >/dev/null 2>&1 &
MOCK=$!
trap 'kill $MOCK 2>/dev/null; rm -rf "${T:?}"' EXIT
sleep 1
export NO_PROXY=127.0.0.1 no_proxy=127.0.0.1 GITROMS_API=http://127.0.0.1:8771 GITROMS_WINDOWED=1 SDL_VIDEODRIVER=dummy
fail=0
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
run() { # name steps [extra env...]
  local d="$T/$1"; mkdir -p "$d/roms" "$d/home"; shift; local steps="$1"; shift
  env GITROMS_HOME="$d/home" GITROMS_ROMS="$d/roms" "$@" GITROMS_TEST="$steps" timeout 60 "$BIN" >/dev/null 2>&1
  SEL=(); SCR=()
  mapfile -t SEL < <(grep -o 'sel=[-0-9]*' "$d/home/gitroms.log" | cut -d= -f2)
  mapfile -t SCR < <(grep -o 'scr=[0-9]* sp=[0-9]*' "$d/home/gitroms.log")
  LOG="$d/home/gitroms.log"
}
BTN() { echo "jb=$1;wait=0.05;ju=$1;wait=0.15"; }   # press and release a raw button

# 1. defaults: d-pad buttons, page buttons and the four action buttons
run raw "idle;logsel;logscr;$(BTN 9);logsel;$(BTN 8);logsel;$(BTN 5);logsel;$(BTN 4);logsel;$(BTN 1);idle;logscr;$(BTN 0);idle;logscr;$(BTN 13);logscr;$(BTN 13);logscr;$(BTN 2);logscr;$(BTN 0);logscr;$(BTN 12);idle;logscr"
chk "raw button 9 (down) moves down 1" "[ \$(( \${SEL[1]:-0} - \${SEL[0]:-0} )) = 1 ]"
chk "raw button 8 (up) moves up 1" "[ \${SEL[2]:-x} = \${SEL[0]:-y} ]"
chk "raw button 5 (R1) pages down 12" "[ \$(( \${SEL[3]:-0} - \${SEL[2]:-0} )) = 12 ]"
chk "raw button 4 (L1) pages up 12" "[ \${SEL[4]:-x} = \${SEL[0]:-y} ]"
chk "raw button 1 (A) opens the repo" "[ \"\${SCR[1]}\" = 'scr=1 sp=1' ]"
chk "raw button 0 (B) goes back" "[ \"\${SCR[2]}\" = 'scr=1 sp=0' ]"
chk "raw button 13 (Start) opens history" "[ \"\${SCR[3]}\" = 'scr=7 sp=0' ]"
chk "Start again closes history" "[ \"\${SCR[4]}\" = 'scr=1 sp=0' ]"
chk "raw button 2 (X) opens system info" "[ \"\${SCR[5]}\" = 'scr=9 sp=0' ]"
chk "B leaves system info" "[ \"\${SCR[6]}\" = 'scr=1 sp=0' ]"
chk "raw button 12 (Select) refreshes and returns to the list" "[ \"\${SCR[7]}\" = 'scr=1 sp=0' ]"

# 2. d-pad as a hat (some firmware) works too
run hat "idle;logsel;hat=4;wait=0.05;hat=0;wait=0.15;logsel;hat=1;wait=0.05;hat=0;wait=0.15;logsel"
chk "hat down then up moves +1 then -1" "[ \$(( \${SEL[1]:-0} - \${SEL[0]:-0} )) = 1 ] && [ \${SEL[2]:-x} = \${SEL[0]:-y} ]"

# 3. keys-only mode (gptokeyb running): raw buttons ignored, keys work
run keys "idle;logsel;$(BTN 9);logsel;key=81;wait=0.05;keyu=81;wait=0.15;logsel" GITROMS_INPUT=keys
chk "keys-only: raw button is ignored" "[ \${SEL[1]:-x} = \${SEL[0]:-y} ]"
chk "keys-only: arrow key still moves" "[ \$(( \${SEL[2]:-0} - \${SEL[1]:-0} )) = 1 ]"

# 4. gitroms.conf override: pad_down=20 makes raw button 3 move down (and 9 stops doing so)
printf 'pad_down=20\n' > "$T/over.conf"
run conf "idle;logsel;$(BTN 20);logsel" GITROMS_CONF="$T/over.conf"
chk "pad_down=20 in gitroms.conf remaps the down button" "[ \$(( \${SEL[1]:-0} - \${SEL[0]:-0} )) = 1 ]"
chk "log records the active mapping" "grep -q 'input: raw joystick + keys; buttons up8 down20' \"\$LOG\""
chk "no fatal errors" "! grep -q FATAL \"\$LOG\""
exit $fail

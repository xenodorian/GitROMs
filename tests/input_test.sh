#!/bin/bash
# Headless input test. Simulates the R36S double delivery (gptokeyb key + SDL
# controller button for one physical press, arriving tens of ms apart),
# holds, and rapid taps; checks the list cursor moves exactly as expected.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/gitroms.x86_64"
[ -x "$BIN" ] || { echo "build first: tools/build_native.sh"; exit 2; }
T="$(mktemp -d)"
python3 "$ROOT/tests/mock_github.py" 8770 >/dev/null 2>&1 &
MOCK=$!
trap 'kill $MOCK 2>/dev/null; rm -rf "${T:?}"' EXIT
sleep 1
mkdir -p "$T/roms" "$T/home"
export NO_PROXY=127.0.0.1 no_proxy=127.0.0.1 GITROMS_API=http://127.0.0.1:8770
export GITROMS_HOME="$T/home" GITROMS_ROMS="$T/roms" GITROMS_WINDOWED=1 SDL_VIDEODRIVER=dummy
TAP="kd=down;ku=down;wait=0.15"
S="idle;logsel"                                                            # 0
S="$S;kd=down;ku=down;wait=0.01;pd=down;wait=0.05;pu=down;wait=0.3;logsel" # key tap, pad copy ~100 ms later -> +1
S="$S;pd=down;wait=0.08;kd=down;wait=0.05;pu=down;ku=down;wait=0.3;logsel" # pad held, key copy 80 ms later -> +1
S="$S;kd=down;wait=1.0;ku=down;wait=0.3;logsel"                            # 1 s hold -> first step + steady repeats
S="$S;$TAP;$TAP;$TAP;$TAP;$TAP;wait=0.2;logsel"                            # five separate taps -> +5
S="$S;kd=up;ku=up;wait=0.01;pd=up;wait=0.05;pu=up;wait=0.3;logsel"         # up, doubled -> -1
S="$S;kd=right;ku=right;wait=0.01;pd=right;wait=0.05;pu=right;wait=0.3;logsel" # page jump once (+12)
GITROMS_TEST="$S" timeout 60 "$BIN" >/dev/null 2>&1
mapfile -t V < <(grep -o 'sel=[-0-9]*' "$T/home/gitroms.log" | cut -d= -f2)
echo "cursor after each step: ${V[*]}"
fail=0
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
chk "tap seen twice (key, then pad copy ~100 ms later) moves 1 row" "[ \$(( \${V[1]:-0} - \${V[0]:-0} )) = 1 ]"
chk "pad held, key copy 80 ms later: moves 1 row" "[ \$(( \${V[2]:-0} - \${V[1]:-0} )) = 1 ]"
d=$(( ${V[3]:-0} - ${V[2]:-0} ))
chk "holding 1 s repeats steadily ($d rows, expect 7 to 11)" "[ $d -ge 7 ] && [ $d -le 11 ]"
chk "five separate taps move exactly 5 rows" "[ \$(( \${V[4]:-0} - \${V[3]:-0} )) = 5 ]"
chk "doubled 'up' moves exactly 1 row up" "[ \$(( \${V[4]:-0} - \${V[5]:-0} )) = 1 ]"
chk "doubled page-right jumps once (12 rows)" "[ \$(( \${V[6]:-0} - \${V[5]:-0} )) = 12 ]"
chk "input trace is written to the log (fire, merged, released)" "grep -q 'in down key fire' \"$T/home/gitroms.log\" && grep -q 'in down +pad (merged)' \"$T/home/gitroms.log\" && grep -q 'in down key released' \"$T/home/gitroms.log\""
chk "no fatal errors in log" "! grep -q FATAL \"$T/home/gitroms.log\""
exit $fail

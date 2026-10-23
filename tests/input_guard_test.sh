#!/bin/bash
# input_guard.sh: stop_gptokeyb finds and stops running gptokeyb processes (including a second name), does nothing when none runs,
# and write_ra_input_cfg clears every keyboard bind of the game buttons without touching the pad binds. PC only.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
T="$(mktemp -d)"; trap 'kill $(pidof gptokeyb gptokeyb2) 2>/dev/null; rm -rf "${T:?}"' EXIT
fail=0
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
cp /bin/sleep "$T/gptokeyb"; cp /bin/sleep "$T/gptokeyb2"
LOGF="$T/log"; log() { echo "$*" >> "$LOGF"; }
. "$ROOT/port/gitroms/input_guard.sh"
stop_gptokeyb; chk "nothing running: no output, no error" "[ ! -s \"$LOGF\" ]"
"$T/gptokeyb" 300 & "$T/gptokeyb2" 300 & sleep 0.3
chk "both are running before" "[ -n \"\$(pidof gptokeyb)\" ] && [ -n \"\$(pidof gptokeyb2)\" ]"
stop_gptokeyb
chk "both are stopped" "[ -z \"\$(pidof gptokeyb gptokeyb2)\" ]"
chk "the log says what was done" "grep -q 'gptokeyb is running' \"$LOGF\" && grep -q 'gptokeyb stopped' \"$LOGF\""
write_ra_input_cfg "$T/ra.cfg" >/dev/null
chk "keyboard binds of a and b are nul" "grep -qx 'input_player1_a = \"nul\"' \"$T/ra.cfg\" && grep -qx 'input_player1_b = \"nul\"' \"$T/ra.cfg\""
chk "no pad bind (_btn) is written" "! grep -q '_btn' \"$T/ra.cfg\""
chk "sixteen keyboard binds" "[ \"\$(wc -l < \"$T/ra.cfg\")\" = 16 ]"
exit $fail

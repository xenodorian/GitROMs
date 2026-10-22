#!/bin/bash
# Starts one GBA ROM from the ports menu: play_gba.sh <rom file name in the gba folder> <title>
# The ROM is downloaded and kept current by GitROMs into <roms>/gba. It is played by ArkOS's own GBA emulator: the command that
# EmulationStation itself would run for the gba system is read from es_systems.cfg. If that cannot be used, RetroArch is
# started directly with an mGBA or gpSP core. Everything is logged with times to gitroms/data/play.log.
ROMFILE="$1"; TITLE="${2:-$1}"
PORTSDIR="$(cd "$(dirname "$0")/.." && pwd)"
GAMEDIR="$PORTSDIR/gitroms"
ROM="${GITROMS_GBA_DIR:-$(dirname "$PORTSDIR")/gba}/$ROMFILE"
XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
for controlfolder in /opt/system/Tools/PortMaster /opt/tools/PortMaster "$XDG_DATA_HOME/PortMaster" /roms/ports/PortMaster; do [ -d "$controlfolder" ] && break; done
[ -f "$controlfolder/control.txt" ] && source "$controlfolder/control.txt" 2>/dev/null

mkdir -p "$GAMEDIR/data"
LOG="$GAMEDIR/data/play.log"
[ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt 131072 ] && mv -f "$LOG" "$GAMEDIR/data/play.prev.log"
log() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG"; }
say() {
  echo "$*"
  if type pm_message >/dev/null 2>&1; then pm_message "$*"; else echo "$*" > /dev/tty1 2>/dev/null; fi
}
# Controller context for the report: what ArkOS and RetroArch map to the GBA A and B buttons. Every part is optional.
sysinfo() {
  echo "=== launcher side (ArkOS / RetroArch) ==="
  echo "date: $(date '+%Y-%m-%d %H:%M:%S')"
  echo "kernel: $(uname -srm 2>/dev/null)"
  [ -f /etc/os-release ] && grep -E '^(PRETTY_NAME|VERSION)=' /etc/os-release 2>/dev/null
  for f in /home/ark/.config/.version /opt/system/version; do [ -f "$f" ] && echo "$f: $(head -c 80 "$f" 2>/dev/null)"; done
  echo "rom: $ROMFILE ($(wc -c < "$ROM" 2>/dev/null) bytes)"
  echo "es gba command: ${CMD:-unknown}"
  echo "retroarch: ${RA:-unknown} core: ${CORE:-unknown}"
  echo "--- input devices (/proc/bus/input/devices)"
  grep -E '^(N: Name|H: Handlers)' /proc/bus/input/devices 2>/dev/null | head -40
  local cfg d
  for cfg in "$HOME/.config/retroarch/retroarch.cfg" /home/ark/.config/retroarch/retroarch.cfg /home/ark/.config/retroarch32/retroarch.cfg /opt/retroarch/retroarch.cfg; do
    [ -f "$cfg" ] || continue
    echo "--- $cfg (input lines for player 1, drivers, A/B related)"
    grep -E '^(input_joypad_driver|input_driver|input_device|input_player1_(a|b|x|y|start|select|l|r|up|down|left|right)_(btn|axis)|input_player1_joypad_index|input_autodetect_enable|input_remap_binds_enable|input_menu_toggle|menu_swap_ok_cancel_buttons|input_swap_ok_cancel|input_enable_hotkey_btn)' "$cfg" 2>/dev/null | head -50
    d="$(dirname "$cfg")"
    for f in "$d"/config/remaps/*mGBA*/* "$d"/config/remaps/*gpSP*/* "$d"/config/remaps/*gba*/* "$d"/config/*/*.rmp; do
      [ -f "$f" ] || continue
      echo "--- remap file $f"; head -c 1500 "$f" 2>/dev/null
    done
    for f in "$d"/config/mGBA/*.cfg "$d"/config/gpSP/*.cfg; do
      [ -f "$f" ] || continue
      echo "--- override $f"; grep -E 'input_|menu_swap' "$f" 2>/dev/null | head -30
    done
  done
  for f in /home/ark/.config/retroarch/autoconfig/udev/*.cfg /home/ark/.config/retroarch/autoconfig/*.cfg /opt/retroarch/autoconfig/udev/*.cfg /etc/retroarch/autoconfig/udev/*.cfg; do
    [ -f "$f" ] || continue
    echo "--- autoconfig $f"; grep -E '^(input_device|input_driver|input_(a|b)_btn|input_(a|b)_axis)' "$f" 2>/dev/null | head -8
  done
  [ -f /dev/shm/runcommand.log ] && { echo "--- /dev/shm/runcommand.log (tail)"; tail -n 30 /dev/shm/runcommand.log 2>/dev/null; }
  echo "--- play.log (tail)"; tail -n 25 "$LOG" 2>/dev/null
}
# Lift a controller report the game stored in the save (flash sector 31) into sgba/reports, with the controller context above.
# The same report stays in the save, so it is only lifted again when its text changed.
extract_report() {
  local sav="" cand
  for cand in "${ROM%.gba}.srm" "${ROM%.gba}.sav"; do
    [ -f "$cand" ] && sav="$cand" && break
  done
  [ -n "$sav" ] || return 0
  local out="$PORTSDIR/sgba/reports" body
  body="$(python3 - "$sav" <<'PY' 2>/dev/null
import sys
data = open(sys.argv[1], "rb").read()
i = data.find(b"PWAMRPT")
if i >= 0:
    sys.stdout.write(data[i:i+3000].split(b"\x00", 1)[0].decode("latin1", "replace"))
PY
)"
  [ -n "$body" ] || return 0
  local sum last="$GAMEDIR/data/last_report.sum"
  sum="$(printf '%s' "$body" | cksum | cut -d' ' -f1,2)"
  [ -f "$last" ] && [ "$(cat "$last" 2>/dev/null)" = "$sum" ] && return 0
  mkdir -p "$out"
  { echo "Pokemon War and Murder controller report"
    echo "How to read it: the game samples the raw GBA key register every frame while the player presses A, then B. Bit 0001 is the GBA A button, 0002 is B."
    echo "If pressing A shows a different bit or nothing, the frontend (ArkOS / RetroArch) maps that button elsewhere; the launcher part below shows the mapping."
    echo "$body"
    sysinfo; } > "$out/controller-$(date +%s).txt" 2>/dev/null && echo "$sum" > "$last"
  log "controller report written to $out"
}

done_() { type pm_finish >/dev/null 2>&1 && pm_finish; exit "${1:-0}"; }

log "start: $TITLE rom=$ROM"
if [ ! -s "$ROM" ]; then
  log "rom missing or empty"
  say "$TITLE is not on this card yet. Open GitROMs and download it first."
  sleep 6; done_ 1
fi

run_logged() { log "run: $*"; ( eval "$@" ) >> "$LOG" 2>&1; rc=$?; log "exit code $rc"; return $rc; }

# 1. The command EmulationStation uses for the gba system.
CFG=""
for f in "${ES_SYSTEMS_CFG:-}" /etc/emulationstation/es_systems.cfg "$HOME/.emulationstation/es_systems.cfg" /opt/system/es_systems.cfg; do
  [ -n "$f" ] && [ -f "$f" ] && CFG="$f" && break
done
CMD=""
if [ -n "$CFG" ]; then
  CMD="$(sed 's/<system>/\n<system>/g' "$CFG" | awk '/<system>/{blk=""} {blk=blk $0 "\n"} /<\/system>/{ if (blk ~ /<name>gba<\/name>/ && match(blk, /<command>[^<]*<\/command>/)) { s=substr(blk, RSTART+9, RLENGTH-19); print s; exit } }')"
fi
log "es_systems: ${CFG:-none}; command: ${CMD:-none}"
if [ -n "$CMD" ] && ! echo "$CMD" | grep -q '%EMULATOR%\|%CORE%\|%INJECT%\|%SYSTEM%\|%GAMEDIR%'; then
  q="'${ROM//\'/\'\\\'\'}'"
  CMD="${CMD//%ROM_RAW%/$ROM}"; CMD="${CMD//%ROM%/$q}"; CMD="${CMD//%BASENAME%/$(basename "$ROMFILE" .gba)}"
  run_logged "$CMD"; rc=$?; extract_report
  [ "$rc" -eq 0 ] && done_ 0
  log "es command failed, trying RetroArch"
fi

# 2. RetroArch directly.
RA=""
for b in /opt/retroarch/bin/retroarch /usr/local/bin/retroarch /usr/bin/retroarch "$(command -v retroarch 2>/dev/null)"; do
  [ -n "$b" ] && [ -x "$b" ] && RA="$b" && break
done
CORE=""
for d in "$HOME/.config/retroarch/cores" /home/ark/.config/retroarch/cores /opt/retroarch/cores /usr/lib/libretro /usr/lib/aarch64-linux-gnu/libretro "${RA_CORES:-}"; do
  for n in mgba gpsp vba_next; do [ -n "$d" ] && [ -f "$d/${n}_libretro.so" ] && CORE="$d/${n}_libretro.so" && break 2; done
done
log "retroarch: ${RA:-none} core: ${CORE:-none}"
if [ -n "$RA" ] && [ -n "$CORE" ]; then
  run_logged "\"$RA\" -L \"$CORE\" \"$ROM\""; rc=$?; extract_report
  [ "$rc" -eq 0 ] && done_ 0
fi
say "Could not start $TITLE. See gitroms/data/play.log, or start it from the GBA system."
sleep 6; done_ 1

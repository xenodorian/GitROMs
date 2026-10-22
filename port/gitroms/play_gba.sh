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
# Lift a controller report the game stored in the save into sgba/reports.
extract_report() {
  local sav="" cand
  for cand in "${ROM%.gba}.srm" "${ROM%.gba}.sav"; do
    [ -f "$cand" ] && sav="$cand" && break
  done
  [ -n "$sav" ] || return 0
  local out="$PORTSDIR/sgba/reports"
  mkdir -p "$out"
  python3 - "$sav" "$out" <<'PY' 2>/dev/null || true
import sys, time
data = open(sys.argv[1], "rb").read()
i = data.find(b"PWAMRPT")
if i < 0:
    raise SystemExit
chunk = data[i:i+200]
open("%s/controller-%d.txt" % (sys.argv[2], int(time.time())), "w").write(
    "Pokemon War and Murder controller report\n" + chunk.split(b"\x00", 1)[0].decode("latin1", "replace") + "\n")
PY
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
  run_logged "$CMD" && { extract_report; done_ 0; }
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
  run_logged "\"$RA\" -L \"$CORE\" \"$ROM\"" && { extract_report; done_ 0; }
fi
say "Could not start $TITLE. See gitroms/data/play.log, or start it from the GBA system."
sleep 6; done_ 1

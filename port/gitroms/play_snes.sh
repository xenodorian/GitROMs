#!/bin/bash
# Starts one SNES ROM from the ports menu: play_snes.sh <rom file name> <title>
# The ROM is downloaded and kept current by GitROMs into <roms>/snes. A .zip is unpacked to a .sfc next to it, because
# some ArkOS builds list .sfc in the SNES menu and skip .zip. It is played by the command EmulationStation uses for the
# snes system, or RetroArch with a snes9x core. Everything is logged with times to gitroms/data/play.log.
ROMFILE="$1"; TITLE="${2:-$1}"
PORTSDIR="$(cd "$(dirname "$0")/.." && pwd)"
GAMEDIR="$PORTSDIR/gitroms"
ROMROOT="$(dirname "$PORTSDIR")"
SNESDIR="${GITROMS_SNES_DIR:-$ROMROOT/snes}"
VISIBLE="$SNESDIR/Bat Kombat.sfc"
extract_sfc() {
  # print the extracted .sfc/.smc path, or nothing
  local zip="$1" dest="$2" name="" out=""
  mkdir -p "$dest"
  if command -v unzip >/dev/null 2>&1; then
    name="$(unzip -Z1 "$zip" 2>/dev/null | awk 'BEGIN{IGNORECASE=1} $0 ~ /\.(sfc|smc)$/ {print; exit}')"
    [ -n "$name" ] && unzip -o -j -qq "$zip" "$name" -d "$dest" && out="$dest/$(basename "$name")"
  fi
  if [ ! -s "$out" ] && command -v python3 >/dev/null 2>&1; then
    out="$(python3 -c 'import os,sys,zipfile
z=zipfile.ZipFile(sys.argv[1])
for n in z.namelist():
    if n.lower().endswith((".sfc",".smc")) and not n.endswith("/"):
        p=os.path.join(sys.argv[2], os.path.basename(n)); open(p,"wb").write(z.read(n)); print(p); break
' "$zip" "$dest")"
  fi
  [ -s "$out" ] && printf '%s' "$out"
}
ZIP=""
for c in "$SNESDIR/UMK3_Batman.zip" "$SNESDIR/Bat Kombat.zip" "$ROMROOT/arcade/UMK3_Batman.zip" "$ROMROOT/arcade/Bat Kombat.zip"; do
  [ -s "$c" ] && ZIP="$c" && break
done
SFC=""
[ -s "$VISIBLE" ] && SFC="$VISIBLE"
[ -z "$SFC" ] && [ -s "$SNESDIR/UMK3_Batman.sfc" ] && SFC="$SNESDIR/UMK3_Batman.sfc"
if [ -n "$ZIP" ] && { [ -z "$SFC" ] || [ "$ZIP" -nt "$SFC" ]; }; then
  got="$(extract_sfc "$ZIP" "$SNESDIR")"
  [ -n "$got" ] && SFC="$got"
fi
if [ -n "$SFC" ] && [ "$SFC" != "$VISIBLE" ]; then
  mv -f "$SFC" "$VISIBLE" && SFC="$VISIBLE"
fi
ROM="${SFC:-$ZIP}"
note_game() {
  local gl="$1" rel="$2" name="$3" tmp
  [ -f "$gl" ] || return 0
  grep -F -q "$rel" "$gl" && return 0
  tmp="$gl.gitroms-part"
  awk -v p="$rel" -v n="$name" '
    /<\/gameList>/ && !done {
      print "\t<game>"
      print "\t\t<path>" p "</path>"
      print "\t\t<name>" n "</name>"
      print "\t</game>"
      done = 1
    }
    { print }
  ' "$gl" > "$tmp" && mv -f "$tmp" "$gl"
}
[ -s "$VISIBLE" ] && note_game "$SNESDIR/gamelist.xml" "./Bat Kombat.sfc" "Bat Kombat"
[ -s "$VISIBLE" ] && note_game "${HOME:-/home/ark}/.emulationstation/gamelists/snes/gamelist.xml" "./Bat Kombat.sfc" "Bat Kombat"
[ -s "$PORTSDIR/Bat Kombat.sh" ] && note_game "$PORTSDIR/gamelist.xml" "./Bat Kombat.sh" "Bat Kombat"
[ -s "$PORTSDIR/Bat Kombat.sh" ] && note_game "${HOME:-/home/ark}/.emulationstation/gamelists/ports/gamelist.xml" "./Bat Kombat.sh" "Bat Kombat"
if [ "${GITROMS_PUBLISH_ONLY:-}" = 1 ]; then exit 0; fi
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
# inputsnap, sysinfo and lift_report live in lift_report.sh (also used when GitROMs starts).
. "$GAMEDIR/lift_report.sh" 2>/dev/null || { inputsnap() { :; }; lift_report() { :; }; }
extract_report() { ROMROOT="$(dirname "$ROM")" lift_report; }

done_() { type pm_finish >/dev/null 2>&1 && pm_finish; exit "${1:-0}"; }

log "start: $TITLE rom=$ROM"
inputsnap > "$GAMEDIR/data/input_launch.txt" 2>/dev/null
# A leftover gptokeyb (fake keyboard) doubles every A and B press: stop it before the game starts. See input_guard.sh.
if [ -f "$GAMEDIR/input_guard.sh" ]; then . "$GAMEDIR/input_guard.sh"; stop_gptokeyb; fi
if [ ! -s "$ROM" ]; then
  log "rom missing or empty"
  say "$TITLE is not on this card yet. Open GitROMs and download it first."
  sleep 6; done_ 1
fi

run_logged() { log "run: $*"; ( eval "$@" ) >> "$LOG" 2>&1; rc=$?; log "exit code $rc"; return $rc; }

# 1. The command EmulationStation uses for the snes system.
CFG=""
for f in "${ES_SYSTEMS_CFG:-}" /etc/emulationstation/es_systems.cfg "$HOME/.emulationstation/es_systems.cfg" /opt/system/es_systems.cfg; do
  [ -n "$f" ] && [ -f "$f" ] && CFG="$f" && break
done
CMD=""
if [ -n "$CFG" ]; then
  CMD="$(sed 's/<system>/\n<system>/g' "$CFG" | awk '/<system>/{blk=""} {blk=blk $0 "\n"} /<\/system>/{ if (blk ~ /<name>snes<\/name>/ && match(blk, /<command>[^<]*<\/command>/)) { s=substr(blk, RSTART+9, RLENGTH-19); print s; exit } }')"
fi
log "es_systems: ${CFG:-none}; command: ${CMD:-none}"
if [ -n "$CMD" ] && ! echo "$CMD" | grep -q '%EMULATOR%\|%CORE%\|%INJECT%\|%SYSTEM%\|%GAMEDIR%'; then
  q="'${ROM//\'/\'\\\'\'}'"
  bn="$(basename "$ROM")"; bn="${bn%.*}"
  CMD="${CMD//%ROM_RAW%/$ROM}"; CMD="${CMD//%ROM%/$q}"; CMD="${CMD//%BASENAME%/$bn}"
  run_logged "$CMD"; rc=$?; extract_report
  [ "$rc" -eq 0 ] && done_ 0
  log "es command failed, trying RetroArch"
fi

# 2. RetroArch directly.
RA=""
# /usr/local/bin/retroarch is ArkOS's own wrapper, the one EmulationStation runs; the program under /opt is the fallback.
for b in /usr/local/bin/retroarch /opt/retroarch/bin/retroarch /usr/bin/retroarch "$(command -v retroarch 2>/dev/null)"; do
  [ -n "$b" ] && [ -x "$b" ] && RA="$b" && break
done
CORE=""
for d in "$HOME/.config/retroarch/cores" /home/ark/.config/retroarch/cores /opt/retroarch/cores /usr/lib/libretro /usr/lib/aarch64-linux-gnu/libretro "${RA_CORES:-}"; do
  for n in snes9x snes9x2010 snes9x2005_plus snes9x2005; do [ -n "$d" ] && [ -f "$d/${n}_libretro.so" ] && CORE="$d/${n}_libretro.so" && break 2; done
done
log "retroarch: ${RA:-none} core: ${CORE:-none}"
if [ -n "$RA" ] && [ -n "$CORE" ]; then
  APPEND=""
  if type write_ra_input_cfg >/dev/null 2>&1 && write_ra_input_cfg "$GAMEDIR/data/ra_input.cfg" >/dev/null; then
    APPEND="--appendconfig \"$GAMEDIR/data/ra_input.cfg\""
    log "keyboard binds of the game buttons are cleared for this run (data/ra_input.cfg)"
    type find_input_overrides >/dev/null 2>&1 && find_input_overrides | while IFS= read -r l; do log "WARNING keyboard bind in an override file, it beats the cleared binds: $l"; done
  fi
  run_logged "\"$RA\" -L \"$CORE\" $APPEND \"$ROM\""; rc=$?; extract_report
  [ "$rc" -eq 0 ] && done_ 0
fi
say "Could not start $TITLE. See gitroms/data/play.log, or start it from the SNES system."
sleep 6; done_ 1

#!/bin/bash
# Lifts the controller report the game stored in its save (flash sector 31, text starting "PWAMRPT") into sgba/reports for [Send reports].
# Sourced by play_gba.sh (when the game exits) and by GitROMs.sh (every start), so it works however the game was started.
# Needs PORTSDIR and GAMEDIR; ROMROOT is the gba folder (default: next to the ports folder). No python needed.

lift_log() {
  mkdir -p "$GAMEDIR/data" 2>/dev/null
  echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$GAMEDIR/data/lift.log"
  [ "$(wc -c < "$GAMEDIR/data/lift.log" 2>/dev/null || echo 0)" -gt 20000 ] && tail -n 40 "$GAMEDIR/data/lift.log" > "$GAMEDIR/data/lift.log.tmp" && mv -f "$GAMEDIR/data/lift.log.tmp" "$GAMEDIR/data/lift.log"
}

# What can feed keys to the game right now: input-related processes (a leftover gptokeyb turns the pad into keyboard keys) and input devices with their handlers.
inputsnap() {
  echo "processes: $(ps -eo pid=,etimes=,comm= 2>/dev/null | grep -E 'gptokeyb|retroarch|ogage|oga_controls|emulationstation|joy2key|evtest|sdl' | awk '{printf "%s(pid %s, age %ss) ", $3, $1, $2}')"
  echo "input devices (name [handlers]):"
  awk 'BEGIN { RS = ""; FS = "\n" } { n = ""; h = ""; for (i = 1; i <= NF; i++) { if ($i ~ /^N: Name=/) { n = $i; sub(/^N: Name=/, "", n) } if ($i ~ /^H: Handlers=/) { h = $i; sub(/^H: Handlers=/, "", h) } } print "  " n " [" h "]" }' /proc/bus/input/devices 2>/dev/null | head -12
}
# Controller context for the report: only what is needed to see how ArkOS and RetroArch map the GBA A and B buttons.
sysinfo() {
  echo "=== launcher side (ArkOS / RetroArch) ==="
  echo "date: $(date '+%Y-%m-%d %H:%M:%S')"
  [ -f /etc/os-release ] && grep -E '^PRETTY_NAME=' /etc/os-release 2>/dev/null
  for f in /home/ark/.config/.version /opt/system/version; do [ -f "$f" ] && echo "arkos version file $f: $(head -c 60 "$f" 2>/dev/null)"; done
  if [ -n "$CMD" ]; then echo "started with: $CMD"; elif [ -n "$RA" ]; then echo "started with: RetroArch $RA core $CORE"; else echo "started with: unknown (not started from the Ports entry, or lifted when GitROMs started)"; fi
  if [ -s "$GAMEDIR/data/input_launch.txt" ]; then
    echo "--- input state when the game was started from the Ports entry ($(date -r "$GAMEDIR/data/input_launch.txt" '+%Y-%m-%d %H:%M:%S' 2>/dev/null))"
    cat "$GAMEDIR/data/input_launch.txt"
  fi
  echo "--- input state now ($(date '+%H:%M:%S'); a report is lifted when the game exits from the Ports entry, else the next time GitROMs starts)"
  inputsnap
  local cfg d f n names found=0 rn=0
  for cfg in "$HOME/.config/retroarch/retroarch.cfg" /home/ark/.config/retroarch/retroarch.cfg /home/ark/.config/retroarch32/retroarch.cfg /opt/retroarch/retroarch.cfg; do
    [ -f "$cfg" ] || continue
    echo "--- $cfg"
    grep -E '^(input_joypad_driver|input_player1_(a|b)_(btn|axis)|input_player1_joypad_index|menu_swap_ok_cancel_buttons)' "$cfg" 2>/dev/null | head -10
    d="$(dirname "$cfg")"
    for f in "$d"/config/remaps/*mGBA*/* "$d"/config/remaps/*gpSP*/* "$d"/config/remaps/*gba*/*; do
      [ -f "$f" ] || continue
      rn=$((rn + 1)); [ "$rn" -le 3 ] || break
      echo "--- remap $f"; grep -E '^input_player1_btn_(a|b) ' "$f" 2>/dev/null | head -4
    done
  done
  # the controller profile RetroArch picked for a connected device: its A and B lines only
  names="$(sed -n 's/^N: Name="\(.*\)"$/\1/p' /proc/bus/input/devices 2>/dev/null)"
  while IFS= read -r n; do
    [ -n "$n" ] || continue
    for f in /home/ark/.config/retroarch/autoconfig/udev/*.cfg /home/ark/.config/retroarch/autoconfig/*.cfg /opt/retroarch/autoconfig/udev/*.cfg /etc/retroarch/autoconfig/udev/*.cfg; do
      [ -f "$f" ] && grep -q -F "input_device = \"$n\"" "$f" 2>/dev/null || continue
      [ "$found" -lt 2 ] || break 2
      found=$((found + 1))
      echo "--- profile $f"; grep -E '^input_(a|b)_(btn|axis) ' "$f" 2>/dev/null | head -4
    done
  done <<< "$names"
}

lift_report() {
  local root="${ROMROOT:-${GITROMS_GBA_DIR:-$(dirname "$PORTSDIR")/gba}}" sav="" cand n=0
  for cand in "$root"/PokemonWarAndMurder.srm "$root"/PokemonWarAndMurder.sav; do
    [ -f "$cand" ] && { n=$((n + 1)); [ -z "$sav" ] && sav="$cand"; grep -a -q PWAMRPT "$cand" 2>/dev/null && { sav="$cand"; break; }; }
  done
  if [ -z "$sav" ]; then lift_log "no save file next to the ROM in $root"; return 0; fi
  local body
  body="$(LC_ALL=C tr '\000' '\n' < "$sav" 2>/dev/null | LC_ALL=C sed -n '/^PWAMRPT/,/^$/{/^$/q;p;}' 2>/dev/null | head -c 3000)"
  if [ -z "$body" ]; then lift_log "save $sav ($(wc -c < "$sav") bytes): no report in it"; return 0; fi
  local sum last="$GAMEDIR/data/last_report.sum" out="$PORTSDIR/sgba/reports"
  sum="$(printf '%s' "$body" | cksum | cut -d' ' -f1,2)"
  if [ -f "$last" ] && [ "$(cat "$last" 2>/dev/null)" = "$sum" ]; then lift_log "save $sav: report already lifted"; return 0; fi
  mkdir -p "$out"
  { echo "Pokemon War and Murder controller report"
    echo "How to read it: the game logs the raw GBA key register every frame while the player presses A, then B (0001 = GBA A, 0002 = GBA B). A different bit or nothing means ArkOS / RetroArch maps that button elsewhere; the launcher part below shows the mapping."
    echo "$body"
    sysinfo | head -c 6000; } > "$out/controller-$(date +%s).txt" 2>/dev/null && echo "$sum" > "$last"
  lift_log "save $sav: report lifted to $out ($(printf '%s' "$body" | wc -c) bytes)"
}

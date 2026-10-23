#!/bin/bash
# Keeps stray keyboard input away from the GBA game. gptokeyb (a fake keyboard made from the pad) maps pad A to the key z and pad B to the key x;
# RetroArch's default keyboard binds are x = GBA A and z = GBA B, so while one is running every press of A or B arrives twice (the pad
# and the key) and the game sees A and B together. Used by GitROMs.sh (after the program ends) and play_gba.sh (before the game starts).
# Needs: a log function named ig_log (optional).

ig_log() { type log >/dev/null 2>&1 && log "$*"; return 0; }

# Stops every gptokeyb that is running (also ones started with sudo) and says what it found.
stop_gptokeyb() {
  local pids p n=0 left
  pids="$(pidof gptokeyb gptokeyb2 2>/dev/null)"
  [ -z "$pids" ] && pids="$(ps -eo pid=,comm= 2>/dev/null | awk '$2 ~ /gptokeyb/ {print $1}')"
  [ -z "$pids" ] && return 0
  ig_log "input guard: gptokeyb is running (pid $pids), stopping it"
  for p in $pids; do kill "$p" 2>/dev/null || sudo -n kill "$p" 2>/dev/null; done
  while [ $n -lt 10 ]; do
    left="$(pidof gptokeyb gptokeyb2 2>/dev/null)"
    [ -z "$left" ] && { ig_log "input guard: gptokeyb stopped"; return 0; }
    sleep 0.2; n=$((n + 1))
  done
  for p in $left; do kill -9 "$p" 2>/dev/null || sudo -n kill -9 "$p" 2>/dev/null; done
  sleep 0.3
  left="$(pidof gptokeyb gptokeyb2 2>/dev/null)"
  [ -z "$left" ] && ig_log "input guard: gptokeyb stopped (kill -9)" || ig_log "input guard: gptokeyb STILL running (pid $left)"
}

# RetroArch --appendconfig file: the keyboard binds of the game buttons are cleared, so only the pad drives the game.
# The pad binds (input_player1_a_btn and friends) are not touched.
write_ra_input_cfg() {
  local f="$1" k
  : > "$f" || return 1
  for k in a b x y l r l2 r2 l3 r3 start select up down left right; do echo "input_player1_$k = \"nul\"" >> "$f"; done
  echo "$f"
}

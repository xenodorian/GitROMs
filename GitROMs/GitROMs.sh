#!/bin/bash
# GitROMs launcher for ArkOS / R36S Ports menu
# Safe version: never modifies system files or runs apt.

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR" || exit 1

LOG="$DIR/gitroms.log"
echo "===== GitROMs start $(date) =====" > "$LOG"

say() {
  echo "$*" | tee -a "$LOG"
}

# Prefer python3
PYTHON=python3
if ! command -v python3 >/dev/null 2>&1; then
  PYTHON=python
fi

say "Python: $($PYTHON --version 2>&1)"
say "Working dir: $DIR"

# Check for pygame
if ! "$PYTHON" -c "import pygame" 2>>"$LOG"; then
  say "ERROR: pygame is not installed."
  say "On most ArkOS devices it is already present."
  say "If missing, install once from a terminal:"
  say "  sudo apt-get update && sudo apt-get install -y python3-pygame"
  echo "GitROMs needs pygame. See gitroms.log" > /dev/tty1 2>/dev/null || true
  sleep 8
  exit 1
fi

export HOME="${HOME:-/home/ark}"

# Try video drivers in order that work best on R36S / ArkOS
for DRIVER in kmsdrm fbcon x11; do
  say "Trying SDL_VIDEODRIVER=$DRIVER"
  export SDL_VIDEODRIVER="$DRIVER"
  if [ "$DRIVER" = "x11" ]; then
    export DISPLAY="${DISPLAY:-:0}"
  fi

  "$PYTHON" -u main.py >>"$LOG" 2>&1
  RC=$?
  if [ $RC -eq 0 ]; then
    say "Exited cleanly with $DRIVER"
    exit 0
  fi
  say "Exit code $RC with $DRIVER"
done

say "All video drivers failed."
tail -n 30 "$LOG" > /dev/tty1 2>/dev/null || true
sleep 8
exit 1

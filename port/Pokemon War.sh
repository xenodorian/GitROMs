#!/bin/bash
# Pokemon War: its own ROM, separate from Pokemon War and Murder.
# This file must sit directly in the ports folder so EmulationStation lists it.
HERE="$(cd "$(dirname "$0")" && pwd)"
GBA="${GITROMS_GBA_DIR:-$(dirname "$HERE")/gba}"
BASE="Pokemon War"
mkdir -p "$GBA"
for ext in srm state; do
  src="$HERE/gitroms/saves/${BASE}.${ext}"
  dst="$GBA/${BASE}.${ext}"
  if [ -s "$src" ] && [ ! -s "$dst" ]; then
    cp -f "$src" "$dst"
  fi
done
exec bash "$HERE/gitroms/play_gba.sh" "${BASE}.gba" "Pokemon War"

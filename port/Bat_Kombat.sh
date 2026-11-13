#!/bin/bash
# Bat Kombat: starts the ROM that GitROMs keeps current in the snes folder. This file must sit directly in the ports folder.
# Play it through ArkOS's own SNES emulator; see gitroms/play_snes.sh.
exec bash "$(cd "$(dirname "$0")" && pwd)/gitroms/play_snes.sh" "UMK3_Batman.zip" "Bat Kombat"

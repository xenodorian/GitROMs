#!/bin/bash
# Pokemon Fire Emblem: starts the ROM that GitROMs keeps current in the gba folder. This file must sit directly in the ports folder.
# Play it through ArkOS's own GBA emulator; see gitroms/play_gba.sh.
exec bash "$(cd "$(dirname "$0")" && pwd)/gitroms/play_gba.sh" "PokemblemTheSacredStones.gba" "Pokemon Fire Emblem"

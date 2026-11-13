#!/bin/bash
# Bat Kombat. This file must sit directly in the ports folder. The name, spaces included, is what ArkOS shows under Ports.
exec bash "$(cd "$(dirname "$0")" && pwd)/gitroms/play_snes.sh" "Bat Kombat.sfc" "Bat Kombat"

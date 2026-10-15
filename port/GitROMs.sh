#!/bin/bash
# GitROMs: PortMaster-style launcher for ArkOS (R36S), native SDL2 build.
# This file must sit directly in the ports folder, next to the gitroms folder.
# That is what makes ArkOS list it under Ports.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

PORTSDIR="$(cd "$(dirname "$0")" && pwd)"
GAMEDIR="$PORTSDIR/gitroms"

# Standard PortMaster controller setup (skipped cleanly if not installed).
if [ -f "$controlfolder/control.txt" ]; then
  # shellcheck disable=SC1091
  source "$controlfolder/control.txt"
  [ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
  type get_controls >/dev/null 2>&1 && get_controls
fi

mkdir -p "$GAMEDIR/data"
LOG="$GAMEDIR/data/launch.log"
[ -s "$LOG" ] && mv -f "$LOG" "$GAMEDIR/data/launch.prev.log"   # keep the launch before this one
: > "$LOG"
exec > >(tee -a "$LOG") 2>&1

say() {
  echo "$*"
  if type pm_message >/dev/null 2>&1; then pm_message "$*"; else echo "$*" > /dev/tty1 2>/dev/null; fi
}

finish() {
  type pm_finish >/dev/null 2>&1 && pm_finish
}

echo "GitROMs launch $(date)"
echo "ports dir: $PORTSDIR  controlfolder: $controlfolder"
echo "uname: $(uname -a)  device arch: ${DEVICE_ARCH:-unset}"
echo "SDL2 libs: $( (ldconfig -p 2>/dev/null | grep -i 'libSDL2-2.0' | head -2 | tr '\n' ' ') || true)"
echo "curl: $(command -v curl || echo MISSING)"

cd "$GAMEDIR" || exit 1

# The roms partition is the parent of the ports folder (/roms, /roms2, ...).
ROMS_PARENT="$(dirname "$PORTSDIR")"
[ -d "$ROMS_PARENT" ] && export GITROMS_ROMS="$ROMS_PARENT"
export GITROMS_HOME="$GAMEDIR/data"
export GITROMS_CONF="$GAMEDIR/gitroms.conf"
export GITROMS_PORTS="$PORTSDIR"
export GITROMS_COMPONENTS="$GAMEDIR/components.conf"
export GITROMS_COLLECT="$GAMEDIR/collect.sh"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
export HOME="${HOME:-/home/ark}"
export PATH="$PATH:/usr/bin:/bin:/usr/local/bin"
export SDL_HINT_RENDER_SCALE_QUALITY=0

RAW="${DEVICE_ARCH:-$(uname -m)}"
case "$RAW" in
  aarch64|arm64) ARCH=aarch64 ;;
  armv7l|armv8l|arm|armhf) ARCH=armhf ;;
  x86_64|amd64) ARCH=x86_64 ;;
  *) ARCH="$RAW" ;;
esac

BIN="$GAMEDIR/gitroms.$ARCH"

# A program file that is empty or not a program (a write that never reached the card) would "run" and exit with 0, silently.
# Check it, and if it is damaged or missing, fetch a fresh copy from GitHub and verify it before it replaces anything.
elf_ok() { [ -s "$1" ] && [ "$(head -c 4 "$1" 2>/dev/null | tail -c 3)" = "ELF" ]; }
REPAIR_BASE="${GITROMS_REPAIR_BASE:-https://github.com/xenodorian/GitROMs/raw/main/dist}"
sha_ok() {
  command -v sha256sum >/dev/null 2>&1 || return 0
  want="$(curl -fsSL --max-time 30 "$REPAIR_BASE/gitroms.$ARCH.sha256" 2>/dev/null | cut -d' ' -f1)"
  have="$(sha256sum "$1" | cut -d' ' -f1)"
  [ -n "$want" ] && [ "$want" = "$have" ]
}
if ! elf_ok "$BIN"; then
  echo "program file missing or damaged: $BIN ($(wc -c < "$BIN" 2>/dev/null || echo 0) bytes)"
  say "GitROMs: its program file is damaged. Repairing it from GitHub..."
  rm -f "$BIN.part"
  if curl -fsSL --max-time 120 -o "$BIN.part" "$REPAIR_BASE/gitroms.$ARCH" \
     && elf_ok "$BIN.part" && [ "$(wc -c < "$BIN.part")" -ge "${GITROMS_REPAIR_MIN:-50000}" ] && sha_ok "$BIN.part"; then
    sync
    chmod +x "$BIN.part" 2>/dev/null
    mv -f "$BIN.part" "$BIN"
    sync
    echo "repaired $BIN"
  else
    rm -f "$BIN.part"
    echo "repair failed"
    say "GitROMs could not repair its program file (is Wi-Fi on?). Copy gitroms/gitroms.$ARCH from GitROMs.zip, then eject the card properly."
    sleep 8; finish; exit 1
  fi
fi
[ -d "$GAMEDIR/libs.$ARCH" ] && export LD_LIBRARY_PATH="$GAMEDIR/libs.$ARCH:$LD_LIBRARY_PATH"

# Some SD card formats drop the executable bit. Run a copy from /tmp then.
if [ ! -x "$BIN" ]; then
  chmod +x "$BIN" 2>/dev/null
fi
if [ ! -x "$BIN" ]; then
  cp "$BIN" "/tmp/gitroms.$ARCH" && chmod +x "/tmp/gitroms.$ARCH" && BIN="/tmp/gitroms.$ARCH"
fi
echo "binary: $BIN"

# Input comes from ONE place. If gptokeyb starts, the pad arrives as keys and
# GitROMs ignores raw joystick events (they would double every press). If it
# does not start, GitROMs reads the raw joystick buttons itself.
if [ -n "$GPTOKEYB" ]; then
  $GPTOKEYB "$(basename "$BIN")" -c "$GAMEDIR/gitroms.gptk" &
  GPT_PID=$!
  sleep 0.5
  if kill -0 "$GPT_PID" 2>/dev/null; then
    export GITROMS_INPUT=keys
    echo "input: gptokeyb running (pid $GPT_PID), using keys"
  else
    echo "input: gptokeyb did not start, using raw joystick buttons"
  fi
else
  echo "input: no gptokeyb, using raw joystick buttons"
fi
type pm_platform_helper >/dev/null 2>&1 && pm_platform_helper "$BIN" >/dev/null 2>&1

# Exit code 3 = SDL could not open a display. Retry once with the DRM driver.
"$BIN"
RC=$?
if [ $RC -eq 3 ] && [ -z "$SDL_VIDEODRIVER" ]; then
  echo "Default video driver failed, retrying with kmsdrm"
  SDL_VIDEODRIVER=kmsdrm "$BIN"
  RC=$?
fi
echo "gitroms exited with $RC"

if [ $RC -ne 0 ]; then
  say "GitROMs could not start (code $RC). See gitroms/data/launch.log and gitroms.log"
  sleep 6
fi

finish
exit $RC

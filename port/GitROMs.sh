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
if [ ! -f "$BIN" ]; then
  say "GitROMs: no program for this CPU ($RAW). Looked for gitroms.$ARCH in gitroms/."
  sleep 8; finish; exit 1
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

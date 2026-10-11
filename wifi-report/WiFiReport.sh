#!/bin/bash
# WiFiReport: PortMaster-style launcher for ArkOS (R36S), native SDL2 build.
# Read-only: shows kernel / USB / Wi-Fi support and saves it to a text file.
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
GAMEDIR="$PORTSDIR/wifireport"

# Standard PortMaster controller setup (skipped cleanly if not installed).
if [ -f "$controlfolder/control.txt" ]; then
  # shellcheck disable=SC1091
  source "$controlfolder/control.txt"
  [ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
  type get_controls >/dev/null 2>&1 && get_controls
fi

mkdir -p "$GAMEDIR/data"
LOG="$GAMEDIR/data/launch.log"
: > "$LOG"
exec > >(tee -a "$LOG") 2>&1

say() {
  echo "$*"
  if type pm_message >/dev/null 2>&1; then pm_message "$*"; else echo "$*" > /dev/tty1 2>/dev/null; fi
}

finish() {
  type pm_finish >/dev/null 2>&1 && pm_finish
}

echo "WiFiReport launch $(date)"
echo "ports dir: $PORTSDIR  controlfolder: $controlfolder"
echo "uname: $(uname -a)  device arch: ${DEVICE_ARCH:-unset}"
echo "SDL2 libs: $( (ldconfig -p 2>/dev/null | grep -i 'libSDL2-2.0' | head -2 | tr '\n' ' ') || true)"

cd "$GAMEDIR" || exit 1

export WIFIREPORT_OUT="$GAMEDIR/WiFi_Report.txt"
export WIFIREPORT_SCRIPT="$GAMEDIR/collect.sh"
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

BIN="$GAMEDIR/wifireport.$ARCH"
if [ ! -f "$BIN" ]; then
  say "WiFiReport: no program for this CPU ($RAW). Looked for wifireport.$ARCH."
  sleep 8; finish; exit 1
fi
[ -d "$GAMEDIR/libs.$ARCH" ] && export LD_LIBRARY_PATH="$GAMEDIR/libs.$ARCH:$LD_LIBRARY_PATH"

# Some SD card formats drop the executable bit. Run a copy from /tmp then.
if [ ! -x "$BIN" ]; then
  chmod +x "$BIN" 2>/dev/null
fi
if [ ! -x "$BIN" ]; then
  cp "$BIN" "/tmp/wifireport.$ARCH" && chmod +x "/tmp/wifireport.$ARCH" && BIN="/tmp/wifireport.$ARCH"
fi
echo "binary: $BIN"

if [ -n "$GPTOKEYB" ]; then
  $GPTOKEYB "$(basename "$BIN")" -c "$GAMEDIR/wifireport.gptk" &
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
echo "wifireport exited with $RC"

if [ $RC -ne 0 ]; then
  say "WiFiReport could not start (code $RC). See wifireport/data/launch.log"
  sleep 6
fi

finish
exit $RC

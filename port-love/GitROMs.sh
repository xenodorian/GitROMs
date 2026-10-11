#!/bin/bash
# GitROMs: PortMaster-style launcher for ArkOS (R36S).
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
: > "$LOG"
exec > >(tee -a "$LOG") 2>&1

say() {
  echo "$*"
  if type pm_message >/dev/null 2>&1; then pm_message "$*"; else echo "$*" > /dev/tty1 2>/dev/null; fi
}

finish() {
  [ -n "$MOUNTED" ] && ${ESUDO} umount "$MOUNTED" 2>/dev/null
  type pm_finish >/dev/null 2>&1 && pm_finish
}

echo "GitROMs launch $(date)"
echo "ports dir: $PORTSDIR  controlfolder: $controlfolder"
echo "arch: $(uname -m)  device arch: ${DEVICE_ARCH:-unset}"

cd "$GAMEDIR" || exit 1

# The roms partition is the parent of the ports folder (/roms, /roms2, ...).
ROMS_PARENT="$(dirname "$PORTSDIR")"
[ -d "$ROMS_PARENT" ] && export GITROMS_ROMS="$ROMS_PARENT"
export GITROMS_HOME="$GAMEDIR/data"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
export HOME="${HOME:-/home/ark}"

ARCH="${DEVICE_ARCH:-$(uname -m)}"
case "$ARCH" in arm64) ARCH=aarch64 ;; amd64) ARCH=x86_64 ;; esac

if ! command -v curl >/dev/null 2>&1; then
  say "GitROMs needs curl, which was not found on this system."
  sleep 5; finish; exit 1
fi

# Find a LOVE 11.x runtime, in this order:
#  1. a binary bundled in gitroms/ (love.aarch64 or love.x86_64)
#  2. PortMaster's shared runtime love_11.5.squashfs
#  3. a system-wide love
LOVE_BIN=""
MOUNTED=""
runtime="love_11.5"

if [ -x "$GAMEDIR/love.$ARCH" ]; then
  LOVE_BIN="$GAMEDIR/love.$ARCH"
  [ -d "$GAMEDIR/libs.$ARCH" ] && export LD_LIBRARY_PATH="$GAMEDIR/libs.$ARCH:$LD_LIBRARY_PATH"
else
  sq="$controlfolder/libs/${runtime}.squashfs"
  if [ ! -f "$sq" ] && [ -x "$controlfolder/harbourmaster" ]; then
    echo "Runtime $runtime missing, asking PortMaster to fetch it (needs Wi-Fi)."
    ${ESUDO} "$controlfolder/harbourmaster" --quiet --no-check runtime_check "${runtime}.squashfs"
  fi
  if [ -f "$sq" ]; then
    love_dir="/tmp/love"
    [ "$PM_CAN_MOUNT" != "N" ] && ${ESUDO} umount "$love_dir" 2>/dev/null
    ${ESUDO} mkdir -p "$love_dir"
    if ${ESUDO} mount "$sq" "$love_dir"; then
      MOUNTED="$love_dir"
      for c in "$love_dir/love" "$love_dir/bin/love"; do
        [ -x "$c" ] && LOVE_BIN="$c" && break
      done
      [ -d "$love_dir/libs.$ARCH" ] && export LD_LIBRARY_PATH="$love_dir/libs.$ARCH:$LD_LIBRARY_PATH"
    else
      echo "Could not mount $sq"
    fi
  fi
fi
if [ -z "$LOVE_BIN" ]; then
  LOVE_BIN="$(command -v love || command -v love2d || true)"
fi

if [ -z "$LOVE_BIN" ]; then
  say "GitROMs: no LOVE runtime found. Open PortMaster once with Wi-Fi on so it can download love_11.5, then try again. Details: gitroms/data/launch.log"
  sleep 8; finish; exit 1
fi
echo "love binary: $LOVE_BIN"

if [ -n "$GPTOKEYB" ]; then
  $GPTOKEYB "love" -c "$GAMEDIR/gitroms.gptk" &
fi
type pm_platform_helper >/dev/null 2>&1 && pm_platform_helper "$LOVE_BIN" >/dev/null 2>&1

"$LOVE_BIN" "$GAMEDIR/game"
RC=$?
echo "love exited with $RC"

finish
exit $RC

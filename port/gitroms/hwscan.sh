#!/bin/bash
# Hardware scan for GitROMs / R36S. Manual only: runs when you start it, and uploads only when you pass --upload.
#   ./hwscan.sh              scan the hardware and write reports/hwscan-<time>.txt
#   ./hwscan.sh --buttons    also record joystick button numbers for 20 seconds (press each button once)
#   ./hwscan.sh --upload URL send the newest report to URL with an HTTP POST (body = report text)
# Reads only. It does not change system files, does not install anything, and does not read Wi-Fi or account settings.

DIR="$(cd "$(dirname "$0")" && pwd)"
REPORTS="$DIR/reports"
mkdir -p "$REPORTS"

if [ "$1" = "--upload" ]; then
  URL="$2"
  [ -n "$URL" ] || { echo "usage: hwscan.sh --upload URL"; exit 1; }
  LATEST="$(ls -t "$REPORTS"/hwscan-*.txt 2>/dev/null | head -1)"
  [ -n "$LATEST" ] || { echo "no report yet: run hwscan.sh first"; exit 1; }
  echo "Uploading $LATEST to $URL"
  if command -v curl >/dev/null 2>&1; then
    curl -fsS --data-binary @"$LATEST" -H "Content-Type: text/plain" "$URL" && echo && echo "Upload done."
  else
    python3 - "$LATEST" "$URL" <<'PY' && echo "Upload done."
import sys, urllib.request
body = open(sys.argv[1], "rb").read()
req = urllib.request.Request(sys.argv[2], data=body, headers={"Content-Type": "text/plain"}, method="POST")
urllib.request.urlopen(req, timeout=30).read()
PY
  fi
  exit $?
fi

STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$REPORTS/hwscan-$STAMP.txt"

sec() { echo; echo "== $* =="; }
{
  echo "hwscan $(date)"
  sec "kernel"
  uname -a 2>/dev/null
  sec "cpu"
  grep -m1 -i "model name\|hardware\|cpu implementer" /proc/cpuinfo 2>/dev/null
  grep -m1 "processor" /proc/cpuinfo 2>/dev/null | sed 's/^/processors: /'
  nproc 2>/dev/null | sed 's/^/nproc: /'
  sec "memory"
  grep -E "MemTotal|MemAvailable" /proc/meminfo 2>/dev/null
  sec "display"
  for f in /sys/class/graphics/fb*/virtual_size /sys/class/graphics/fb*/name; do
    [ -r "$f" ] && echo "$f: $(cat "$f")"
  done
  sec "input devices (/proc/bus/input/devices)"
  cat /proc/bus/input/devices 2>/dev/null
  sec "/dev/input"
  ls -l /dev/input 2>/dev/null
  sec "usb devices"
  if command -v lsusb >/dev/null 2>&1; then lsusb; else echo "lsusb not installed"; fi
  sec "storage (mounts)"
  df -h 2>/dev/null | grep -v tmpfs
  sec "python and pygame joysticks"
  PYTHON=python3; command -v python3 >/dev/null 2>&1 || PYTHON=python
  "$PYTHON" --version 2>&1
  "$PYTHON" - "$@" <<'PY' 2>&1
import os, sys, time
os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
os.environ.setdefault("SDL_AUDIODRIVER", "dummy")
try:
    import pygame
except Exception as e:
    print("pygame not importable:", e)
    sys.exit(0)
pygame.init()
pygame.joystick.init()
print("pygame", pygame.version.ver, "SDL", pygame.version.SDL)
n = pygame.joystick.get_count()
print("joysticks:", n)
joys = []
for i in range(n):
    j = pygame.joystick.Joystick(i)
    joys.append(j)
    print("  %d: name=%r buttons=%d axes=%d hats=%d guid=%s" % (
        i, j.get_name(), j.get_numbuttons(), j.get_numaxes(), j.get_numhats(),
        getattr(j, "get_guid", lambda: "n/a")()))
if "--buttons" in sys.argv[1:] and joys:
    print("button probe: press each button once in the next 20 seconds")
    pygame.display.init()
    pygame.display.set_mode((64, 64))
    end = time.time() + 20
    seen = []
    while time.time() < end:
        for ev in pygame.event.get():
            if ev.type == pygame.JOYBUTTONDOWN:
                print("  button %d (joystick %d)" % (ev.button, ev.joy)); seen.append(ev.button)
            elif ev.type == pygame.JOYHATMOTION:
                print("  hat %d value %s (joystick %d)" % (ev.hat, ev.value, ev.joy))
            elif ev.type == pygame.JOYAXISMOTION and abs(ev.value) > 0.8:
                print("  axis %d value %.2f (joystick %d)" % (ev.axis, ev.value, ev.joy))
        time.sleep(0.01)
    print("buttons seen:", sorted(set(seen)))
PY
  sec "retroarch (names only, no file contents)"
  command -v retroarch >/dev/null 2>&1 && echo "retroarch: $(command -v retroarch)" || echo "retroarch: not on PATH"
  for d in "$HOME/.config/retroarch" "/home/ark/.config/retroarch" "/opt/retroarch"; do
    [ -d "$d" ] || continue
    echo "config dir: $d"
    ls "$d" 2>/dev/null | head -50
    for sub in cores remaps overrides config; do
      [ -d "$d/$sub" ] && echo "  $sub/:" && ls "$d/$sub" 2>/dev/null | head -100 | sed 's/^/    /'
    done
  done
  sec "gitroms folder (names only)"
  ls -l "$DIR" 2>/dev/null
} > "$OUT" 2>&1

echo "Report written to: $OUT"
echo "To send it later: ./hwscan.sh --upload <URL>"

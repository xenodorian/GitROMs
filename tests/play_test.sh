#!/bin/bash
# Port launchers for the two GBA ROMs: missing ROM is reported, the ES gba command is used with the ROM substituted, a command that
# still needs %EMULATOR% falls back to RetroArch, and nothing runs when neither exists.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/roms/ports" "$T/roms/gba" "$T/bin"
cp -r "$ROOT/port/." "$T/roms/ports/"
P="$T/roms/ports"; pass=0; fail=0
ok() { if [ "$2" = "$3" ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL $1: got '$2' want '$3'"; fi; }
cfg() { printf '<systemList><system><name>nes</name><command>echo wrong</command></system><system>\n<name>gba</name>\n<command>%s</command>\n</system></systemList>\n' "$1" > "$T/es.cfg"; }
run() { ES_SYSTEMS_CFG="$T/es.cfg" HOME="$T/home" bash "$P/$1" >/dev/null 2>&1; echo $?; }

ok "missing rom" "$(run Pokemon_Fire_Emblem.sh)" 1
cfg "echo %ROM% >> $T/ran"
echo x > "$T/roms/gba/PokemblemTheSacredStones.gba"; echo x > "$T/roms/gba/PokemonWarAndMurder.gba"
ok "es command rc" "$(run Pokemon_Fire_Emblem.sh)" 0
ok "es command rom" "$(tail -1 "$T/ran")" "$T/roms/gba/PokemblemTheSacredStones.gba"
ok "war rc" "$(run Pokemon_War_and_Murder.sh)" 0
ok "war rom" "$(tail -1 "$T/ran")" "$T/roms/gba/PokemonWarAndMurder.gba"
cfg "emu %EMULATOR% %CORE% %ROM%"
cat > "$T/bin/retroarch" <<EOT
#!/bin/bash
echo "\$@" > $T/ra
EOT
chmod +x "$T/bin/retroarch"; mkdir -p "$T/cores"; : > "$T/cores/mgba_libretro.so"
ok "fallback rc" "$(PATH="$T/bin:$PATH" RA_CORES="$T/cores" run Pokemon_Fire_Emblem.sh)" 0
ok "fallback args" "$(cat "$T/ra")" "-L $T/cores/mgba_libretro.so $T/roms/gba/PokemblemTheSacredStones.gba"
ok "nothing to run" "$(PATH=/usr/bin:/bin run Pokemon_Fire_Emblem.sh)" 1
cfg "false"
ok "failing command, no retroarch" "$(PATH=/usr/bin:/bin run Pokemon_War_and_Murder.sh)" 1
ok "log dated" "$(grep -cE '^20[0-9]{2}-[0-9]{2}-[0-9]{2} ' "$P/gitroms/data/play.log" | awk '{print ($1>5)}')" 1
[ -n "$KEEP" ] && cat "$P/gitroms/data/play.log"
echo "play_test: $pass passed, $fail failed"; [ $fail = 0 ]

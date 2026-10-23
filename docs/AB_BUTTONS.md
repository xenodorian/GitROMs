# A and B press together on the R36S (2026-10-23)

Status: appeared fixed after the 4.14 changes. Not known whether that was the fix or an ordinary reset. If it comes back, start at "If it returns".

## Symptom
- Pokemon War and Murder (mGBA core in RetroArch on ArkOS, device name rg351mp, pad "GO-Super Gamepad").
- Physical A and B both acted as A in most places, and in some menus as A plus B at once (in the party screen, A did not open the Pokemon submenu: the A half opened it and the B half closed it again).
- X and Y were clean. mGBA reads RetroPad X and Y as turbo A and turbo B, so they were never affected.
- Intermittent. A reset sometimes changed it. REMAP in the game could not help because both buttons read the same.

## Best explanation (inferred, not proven on the device)
Every A or B press arrived twice, once from the pad and once from a keyboard key, and the key was the opposite button.
- `gitroms.gptk` maps pad A to the key `z` and pad B to `x`. RetroArch's default keyboard binds are `x` = GBA A and `z` = GBA B.
- X and Y map to `c` and `Tab`, which RetroArch does not use, which is why they were clean.
- Reproduced on a PC with the real RetroArch 1.18 and mGBA core (mouse buttons standing in for the pad): pad stand-in plus `z` gave A and B together every time.
- Where the keys came from on the device was never confirmed. Candidates: a leftover `gptokeyb`, or any device that reports as a keyboard.

## What is in place (GitROMs, files under port/)
1. `gitroms/input_guard.sh` `stop_gptokeyb`: stops every gptokeyb when GitROMs ends (`GitROMs.sh`) and before the game starts (`play_gba.sh`). Logged in `data/play.log` as "input guard".
2. `play_gba.sh` starts RetroArch with `--appendconfig data/ra_input.cfg`: all 25 keyboard binds of the game buttons (buttons, analog keys, turbo) set to "nul". Pad binds are untouched. Log line: "keyboard binds of the game buttons are cleared".
3. `play_gba.sh` prefers `/usr/local/bin/retroarch` (the ArkOS wrapper EmulationStation runs) over `/opt/retroarch/bin/retroarch`.
4. Core and per-game override files (`config/mGBA/*.cfg`, `PokemonWarAndMurder.cfg`) that rebind a game button are logged as "WARNING keyboard bind in an override file". They are applied after the appendconfig and beat it, so they must be edited by hand.
5. The game has REPORT and REMAP in the start menu (story/start_native.c, tools/start_menu_fix*.py). REPORT logs the raw GBA key register while you press A then B. REMAP accepts any two different keys.

## If it returns
1. In the game: start menu, REPORT, press A then B when asked, wait for "Controller report saved".
2. Open GitROMs (it lifts the report from the save), then Send reports. The status report starts with `last_controller.txt`, `lift.log` and `play.log`.
3. Read it like this:
   - `A press ... raw 0003` or the B line also 0003: each press reads A plus B, so something else feeds keys. Look at "input state" (processes, devices), "WARNING keyboard bind", and the RetroArch lines in the launcher part.
   - Raw A line shows 0002 or B shows 0001: the buttons are swapped by the pad profile or a remap file (see "remap" lines).
   - "no key seen in 30 seconds": the button sends nothing to the GBA at all.
   - Raw is clean (0001 and 0002) but the game misbehaves: the ROM or the game option "button mode" (shown in the game line) is the cause, not the frontend.
4. Check `data/play.log` for "input guard: gptokeyb is running" (a leftover was found) and whether the game was started from the Ports entry. A game started from the GBA list in EmulationStation does not go through `play_gba.sh`, so none of the guards apply.

## What was ruled out or learned
- The ROM hook: with no remap the key hook returns the keys unchanged (frames identical with and without it in the emulator).
- `input_remap_binds_enable = false` is not a fix: with a remap file present it makes the remapped button dead.
- Several appendconfigs are separated with `|`, not commas.
- GitROMs logs "gptokeyb did not start" although it does: the check uses `kill -0` on a PID that may belong to sudo. It does not matter for this issue.
- A report over 56 KB used to post with an empty body (fixed, now cut with a note). Reports are now slim (about 2 KB).

## Open questions
- Was a stray keyboard source really present? Nothing from the device showed one.
- Why did a reset change it? Not explained.
- Was it the guard, or a normal reset? If the problem is gone for good after several launches, the guard is likely the fix.

Related: `Current_Work.md` entries 4.9 to 4.14, `tests/input_guard_test.sh`, PC experiment files were in a scratch folder and are not kept (the recipe is item 2 above).

# GitROMs 2.0 (LOVE port)

Browse public GitHub repositories on the R36S and download ROM files into the
right system folders. A dedicated app: no web browser, no login, no tokens.

## Install

1. Extract `GitROMs.zip` into the **ports** folder on the roms partition
   (the folder that holds your other `.sh` ports, usually `/roms/ports`).
   You should end up with `ports/GitROMs.sh` and `ports/gitroms/`.
2. Put the card back in the handheld. Open **Ports**, then **GitROMs**.

The `GitROMs.sh` file has to sit directly in `ports`, not inside a subfolder.
That is how ArkOS finds it.

## Needs

- Wi-Fi (only while browsing or downloading).
- The PortMaster LOVE 11.5 runtime (`love_11.5.squashfs`). If it is missing the
  launcher asks PortMaster to fetch it (needs Wi-Fi), or uses `gitroms/love.aarch64`
  if you drop one there.
- `curl`, which ArkOS includes.

## Controls

- D-pad up/down: move. Left/right or L1/R1: page.
- A: open folder / pick file / confirm. B: back / cancel.
- Select: refresh the current list. Start: downloads history.
- On the download screen, left/right changes the destination system folder.

Files are saved to `<roms>/<system>/`. Existing files are never overwritten;
you can save a numbered copy instead. Downloads go to a `.gitroms-part` file
first and are renamed only when complete.

## Change the GitHub user or file types

Edit `gitroms/game/src/config.lua` on a PC.

## If something goes wrong

Read `gitroms/data/launch.log` (launcher) and `gitroms/data/gitroms.log` (app).
If the error mentions a secure connection or certificate, check the handheld's
date and time: handhelds without a clock battery often start with a wrong date,
and HTTPS fails when the date is wrong.

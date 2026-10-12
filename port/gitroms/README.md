# GitROMs 3.0 (native SDL2)

Browse a GitHub user's public repositories on the R36S and download ROM files
into the right system folders. A dedicated app: no web browser, no login, no
tokens, no extra runtime to install.

## Install

1. Extract `GitROMs.zip` into the **ports** folder on the roms partition
   (the folder that holds your other `.sh` ports, usually `/roms/ports`).
   You should end up with `ports/GitROMs.sh` and `ports/gitroms/`.
2. Put the card back in the handheld. Open **Ports**, then **GitROMs**.
   If Ports does not show it, restart EmulationStation.

`GitROMs.sh` has to sit directly in `ports`, not inside a subfolder.

## Needs

- Wi-Fi (only while browsing or downloading).
- `curl` and SDL2, which ArkOS includes.

## Controls

- D-pad up/down: move. Left/right or L1/R1: page.
- A: open folder / pick file / confirm. B: back / cancel.
- Select: refresh the list. Start: downloads history.
- X: system info screen (kernel, USB devices, Wi-Fi support, firmware). It also
  saves `gitroms/data/SystemInfo.txt`. Read-only; changes nothing.
- On the save screen, left/right changes the destination system folder.

Files go to `<roms>/<system>/`. Existing files are never overwritten (you can
save a numbered copy). Downloads write to a `.gitroms-part` file and are
renamed only when complete. Folder lists are cached for the session, so going
back does not use up GitHub's 60-requests-per-hour anonymous limit.

## Updating files

GitROMs remembers what it saved (`gitroms/data/installed.txt`). In a list, a
file you saved earlier shows `OK` (unchanged) or `UPD` (changed upstream since
you saved it). Open it and choose "Update" to replace the saved file in place.
Nothing is replaced unless you pick that option; "Save as copy" and "Keep
existing" are always offered.

Built files (zips, `.sgba` packages) that a repo publishes as GitHub
**Releases** appear under `[Releases]` at the top of the repo's file list (it
reads the repo's latest release). Files saved before this version are not
tracked yet; replace them once and they will be.

## Change the GitHub user

Edit `gitroms/gitroms.conf` on a PC (`user=name`).

## If something goes wrong

Read `gitroms/data/launch.log` (launcher) and `gitroms/data/gitroms.log` (app).
If the message mentions a secure connection or certificate, check the
handheld's date and time: without a clock battery the date can be wrong, and
HTTPS fails when it is.

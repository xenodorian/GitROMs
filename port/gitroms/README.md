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

Only four buttons are needed: **D-pad up/down, A and B**.

- D-pad up/down: move. A: open / pick / confirm. B: back or cancel (on the
  first list it opens "Quit?", and A quits).
- Optional: D-pad left/right or L1/R1 page 12 rows (on the save screen, left/right
  change the system folder). Select (or Y) refreshes the list. Start shows the
  downloads history. X opens the system info screen.

Input comes from one place. Under PortMaster, `gptokeyb` turns the pad into keys
(`gitroms.gptk`) and GitROMs ignores raw joystick events, so a press is never
counted twice. If `gptokeyb` is not running, GitROMs reads the raw joystick
buttons itself, using the R36S numbers from BibooSGBA's `controls-arkos.json`
(d-pad = buttons 8-11, B = 0, A = 1, X = 2, Y = 3, L1/R1 = 4/5, Select/Start =
12/13). Change any of them in `gitroms.conf` (`pad_a=0` and so on). The log
`gitroms/data/gitroms.log` records which mode and numbers are in use.

Holding up/down repeats at a steady speed; the OS key-repeat is ignored. The left
stick is not mapped on purpose: a worn stick near its centre sends rapid false
presses. To use it, add `left_analog_up = up` (and down/left/right) to
`gitroms/gitroms.gptk`.

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

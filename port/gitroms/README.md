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

## Updates (the push-button way)

Open the first entry in the repository list, **[Check for updates]** (or press
**X** on the error screen if GitHub's list will not load). GitROMs checks:

- **GitROMs** itself,
- the **SGBA emulator**,
- your games (Parry Perry).

Each one shows "Up to date", "N of M files changed, X KB", or "Not installed".
Press **A** and it downloads only the files that changed and writes them over
the old ones in the right folders (`ports/` for GitROMs and SGBA,
`ports/sgba/games/` for games). Then it checks again to confirm. If GitROMs
itself was updated it says so; close and reopen it.

How "only what changed" works: for a zip bundle GitROMs reads just the list of
files inside, compares each file on your card (size and checksum), and fetches
only the differing files with byte-range requests. Every file is checked against
its checksum before it replaces the old one, so a bad download never overwrites
a good file. Your own `gitroms.conf` and `components.conf` are never overwritten.
It needs no accounts and uses none of GitHub's limited API requests. A single
file (a game) is compared by its size and its last 128 KB, so use `.sgba` or
`.zip` files for games.

To add another game or bundle, add a line to `gitroms/components.conf`
(the file explains the format).

## Updating files you saved from a repo

Files saved from a repo's file list or `[Releases]` show `OK` or `UPD`, and
saving one again offers "Update: replace with new version". This works for any
file, but the push-button screen above is the easy way.

## Change the GitHub user

Edit `gitroms/gitroms.conf` on a PC (`user=name`).

## If something goes wrong

Read `gitroms/data/launch.log` (launcher) and `gitroms/data/gitroms.log` (app).
If the message mentions a secure connection or certificate, check the
handheld's date and time: without a clock battery the date can be wrong, and
HTTPS fails when it is.

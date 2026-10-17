# GitROMs

Controller-driven GitHub ROM browser for the R36S (ArkOS). A native SDL2
program: no web browser, no login, no LOVE or Python runtime to install.
It downloads files into the matching `roms/<system>/` folders.

**Download:** [dist/GitROMs.zip](https://github.com/xenodorian/GitROMs/raw/main/dist/GitROMs.zip)
(checksum: `dist/GitROMs.zip.sha256`)

Extract it into the **ports** folder on the roms partition so you get
`ports/GitROMs.sh` and `ports/gitroms/`, then open Ports > GitROMs.
Details: [port/gitroms/README.md](port/gitroms/README.md).

**Updates:** open `[Check for updates]` at the top of the repository list. It checks GitROMs, the SGBA emulator and your games, and one press of A downloads only the changed files and installs them.

## Status

Built and tested on a PC only (x86_64 build, headless SDL, mock GitHub API;
the aarch64/armhf builds were load-tested under qemu with a glibc 2.31
sysroot). Not yet run on a real R36S. See [docs/DECISIONS.md](docs/DECISIONS.md).

## Repo layout

- `native/gitroms.c` the whole program (SDL2 declared by hand, like the CryMon port)
- `docs/ci/` workflow snippets that publish builds as a rolling GitHub Release (what GitROMs updates from)
- `port/` package contents: launcher, gptokeyb map, config, docs
- `tools/build_native.sh` cross-builds aarch64, armhf and x86_64 into `build/`
- `tools/fetch_sysroot.sh` downloads Ubuntu 20.04 glibc 2.31 so binaries run on older ArkOS
- `tools/build_zip.py` builds `dist/GitROMs.zip`
- `tests/` `check_package.py` (static), `smoke_test.sh` and `update_test.sh`, `real_release_test.sh` (headless runs; the last one is online and downloads a real release asset), `mock_github.py`
- `port-love/` the LOVE version. Superseded: it needs a LOVE runtime the device lacked.
- `GitROMs/` the first Python/pygame attempt. Incomplete (`main.py` never committed).

## Sending diagnostic reports
The SGBA emulator saves crash, error and frame rate reports in `sgba/reports/`. Open `[Send reports]` at the top of the repo list to post them as issues in a GitHub repository.

GitROMs reports on itself too, into the same folder, so the same button sends them:
- `gitroms-crash-<time>.txt` after a crash: version, source hash, signal, fault address, what it was doing, registers, callers and the last log lines.
- `gitroms-unclean-<time>.txt` when the last run was killed, lost power or hung (it leaves `gitroms/data/.running` while it runs).
- `gitroms/data/gitroms.prev.log` and `launch.prev.log` keep the run before this one, so a crash can still be read after the next start. The status report includes them.

The two ROM hacks are looked up by file name, wherever they are committed: `PokémonWarAndMurder.gba` in `xenodorian/Pokemon-War-and-Murder` and `PokemblemTheSacredStones.gba` in `xenodorian/PoekEmblem`. GitROMs reads the repo's file list (accents and case ignored, spaces not; the copy nearest the top of the repo wins), then installs and updates that file by its commits like everything else. Until a file exists the item says "Not published yet". They are saved in the `gba` folder as `PokemonWarAndMurder.gba` and `PokemblemTheSacredStones.gba` (no accents). The older `Pokemon War and Murder.gba` is no longer updated and is left on the card.

Updates are decided by commits. Each item has a commit source (`commit:owner/repo:path`); it is offered as a new version only when its latest commit is newer than the one recorded at the last install, so a rebuilt release or a changed Last-Modified alone offers nothing, and an older commit never offers a downgrade. If GitHub cannot be reached, nothing new is offered. A damaged or cut-off file is still found (size and checksum) and offered as a repair, and a bundle with no new commit says "Repair: N files damaged or changed" instead of calling it an update. The token in `upload.conf` also authenticates these lookups (5000 an hour instead of 60), through a private file, never on a command line.

Everything is dated. Reports now carry the time of the incident in the body and the title, and reports older than `max_age_days` (default 7, set in `upload.conf`) are moved to `sgba/reports/old/` instead of being posted. `updates.txt` records the commit date and the install time (`at:`) of each item, and `history.txt` has full dates. SGBA writes one log per game in `sgba/logs/<game>.log` (session start and end, a status line every minute, errors, crashes, unclean exits), and the status report includes their tails plus ArkOS's `/dev/shm/runcommand.log` when it exists.

Files are now written safely: each new file is flushed to the card, read back from the card and checked (against the package's CRC32, or against a second read for single files) before it replaces the old one, the folder entry is flushed, and everything is synced when an update finishes. A single-file item also records its checksum at install, so a copy that was damaged later (same size, wrong bytes) is offered as an update. `GitROMs.sh` checks its own program file at every launch; if it is empty, missing or not a program it downloads `dist/gitroms.<arch>`, checks its SHA-256 and only then replaces it. Shut the device down fully, and eject the card safely on a PC, before pulling it: that is what keeps writes from being lost.

At every start GitROMs also deletes what interrupted updates leave behind (temporary files and half-downloaded `.gitroms-part` files) and checks that the card is writable with room to spare; a warning shows on the Updates screen. A single-file item whose size differs from the server's (cut off or damaged) is offered as an update even when its dates match.
First use: GitROMs creates `gitroms/data/upload.conf`. On a computer, set `repo=` and paste a fine-grained GitHub token (Issues: read and write on that repo) after `token=`, then press SELECT on the screen to reload. The default repo is public, so anything sent there is public.

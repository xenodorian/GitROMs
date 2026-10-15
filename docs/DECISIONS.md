# GitROMs decisions and evidence

## Why earlier versions did not launch (inferred, not confirmed on device)

1. The Python version's `GitROMs.sh` sat inside a `GitROMs/` subfolder. ArkOS
   lists scripts that sit directly in `ports/`.
2. That launcher ran `main.py`, which is not in the repo history.
3. It needed system Python plus pygame; nothing shows the device has them.
4. The LOVE version (`port-love/`) launched but reported "no love runtime
   found" on the device: the PortMaster LOVE 11.5 runtime was not present.
   That is why 3.0 is native SDL2.

## Why native SDL2 (3.0)

- No runtime to install. The only dependencies are libSDL2 and curl, both on
  ArkOS. The CryMon SDL2 port took the same approach (SDL declared by hand,
  real libSDL2-2.0.so.0 loaded on the device).
- HTTPS is delegated to the system `curl` run as a child process (fork/exec,
  no shell), so no TLS library is bundled and the UI never blocks. Cancel sends
  SIGTERM to the curl process.
- The launcher mirrors `CryMon.sh`: PortMaster `control.txt` + `get_controls`,
  `gptokeyb` keyboard map, binary chosen by CPU (`gitroms.aarch64`,
  `gitroms.armhf`).

## glibc compatibility

A binary built on a modern host links `__libc_start_main@GLIBC_2.34` and will
not start on ArkOS images with an older glibc. `tools/build_native.sh` builds
against an Ubuntu 20.04 glibc 2.31 sysroot (`-B`, `-nostdinc`, sysroot headers)
so the result needs only GLIBC_2.17 (aarch64) or GLIBC_2.4 (armhf).
`tests/check_package.py` fails if a newer version creeps in.

## Verified on a PC

- Launcher flow in a fake SD layout.
- Native x86_64 build under SDL dummy video against a mock GitHub API:
  two-page repo listing, folder navigation, extension filtering, download to
  `.gitroms-part` then rename, numbered copy on conflict, cached back
  navigation, history file, error screens (`tests/smoke_test.sh`).
- aarch64 and armhf binaries load and start under qemu-user with the glibc 2.31
  sysroot; they stop at window creation only because the link stub has no real
  SDL (expected).
- Package static checks (`tests/check_package.py`).

## NOT verified (needs the R36S)

- Real SDL2 on the device: window/renderer creation (the code tries accelerated,
  then software; exits with code 3 on failure and the launcher retries with
  `SDL_VIDEODRIVER=kmsdrm`), colors, and 640x480 fit.
- That the device is aarch64 (an armhf build is included as a fallback).
- Controller mapping through `gptokeyb` and SDL controller events.
- Real GitHub API calls (blocked in the build sandbox; the mock follows the
  documented response shape), Wi-Fi, and the device clock (a wrong date breaks
  HTTPS).
- That the roms partition is the parent of `ports/` (assumed; the app also
  scans /roms, /roms2 and others).
- The device's glibc version is unknown; the build targets 2.17 or newer.

## Push-button updates (3.5)

- Components (GitROMs, SGBA bundle, a game) are listed in `gitroms/components.conf`.
- A zip bundle is checked by reading only its central directory over HTTP byte
  ranges (GitHub release assets accept `bytes=a-b`, but not `bytes=-N` suffix
  ranges; the file size comes from a `Range: bytes=0-0` probe). When the stamp
  differs, every file on the
  card is compared by size and CRC32; only differing members are fetched (nearby
  ones merged into few requests; the whole file if more than 60% is needed or the
  server ignores ranges), inflated by the built-in `zipx.h`, CRC-checked, written
  to `*.gitroms-part` and renamed over the old file. Executable bits come from the
  zip. The recorded stamp only gates the compare.
- Verified against the real SGBA release: 668 files, 93 MB, all inflate and match;
  after damaging 5 files the update fetched 16 KB in 5 requests.
- Decision input is the remote modified time, not size (user request). Release
  assets send `Last-Modified` on the same `Range: bytes=0-0` probe. raw.githubusercontent
  sends only an ETag, so the GitROMs component uses `commit:owner/repo:path`, which
  reads the latest commit date from the commits API (falls back to the ETag if the
  API is unreachable). The stamp is saved in `gitroms/data/updates.txt` with up to
  three sentinel files. Single files: same stamp means up to date, a different one means re-download.
  Zip bundles: the stamp is display only; every check reads the file list and
  compares every file (size, CRC32), because a stamp-only shortcut missed changes
  (user: bundles must scan all relevant files). A
  touched but unchanged bundle reports up to date and records the new stamp.
- Caveat: a file deleted by hand that is not a sentinel is not noticed until the
  stamp changes. Clock accuracy on the device is irrelevant, only server values
  are compared.
- Interrupting an update leaves a mix of old and new files; the next check fixes it.
- Not verified on the device: speed of the CRC pass over a 93 MB bundle on the
  R36S, and Wi-Fi behaviour during a 30 MB first install.

## Games are their own packages (2026-10-13)
- The SGBA emulator package holds no real games (only its demo, input lab and controller setup). Each game is a package of its own: a launcher plus its own folder. Parry Perry is `parry-perry-port.zip` from the BibooSGBA `latest` release: `Parry Perry.sh` plus ONE compiled file, `parry-perry-game/parry-perry.sgba`, run on the separately installed emulator. (The first version unpacked the whole project into `parry-perry/`; the owner wanted the single `.sgba`, so that loose folder is retired after the new package installs.)
- The only Parry Perry URL GitROMs uses is `.../BibooSGBA/releases/download/latest/parry-perry-port.zip`. The release still carries redundant leftovers (`parry-perry.sgba`, `parry-perry-arkos-aarch64.zip`, `parry-perry-native-arkos-aarch64.zip`). They are never downloaded or listed by GitROMs, and the built-in list treats the old `.sgba` URL as obsolete: a card's old components.conf line for it is dropped on load.
- After a verified install, GitROMs removes the old copies listed in a component's last field (files or folders under the ports folder; `sgba/games/parry-perry.sgba`, `sgba/games/parry-perry` and the loose `parry-perry` here).
- Cause of the original "device has an old build" report: the updater wrote `sgba/games/parry-perry.sgba`, but the old standalone port ran the loose folder `sgba/games/parry-perry/`. Two install paths existed.

## The emulator is the native C runtime (2026-10-14)
- Owner order: speed above everything, Python may go. The SGBA emulator package (`SGBA-arkos-aarch64.zip`, about 1.5 MB) is now the native C runtime: `SGBA.sh` plus `sgba/sgba`, an SDL2 fallback library, `controls.json`, and the demo and input lab (browser games, because the C runtime does not run Python games). Python is gone from the device.
- GitROMs removes the old Python emulator's files (`sgba/python`, `sgba/site`, the `sgba/*.py` files, `sgba/sgba_js_shim.js`, `sgba/cache/sgba-package-cache`) after the new package is verified. A `controls.json` you changed is kept.
- Games still install as their own package (launcher + one `.sgba`) and start the emulator with `SGBA.sh path/to/game.sgba`. Parry Perry and Slingshot Orbits need no change.
- Unverified on the R36S: the native runtime has run only on a PC and under qemu-aarch64. If it fails to start, `sgba/sgba_launch.log` and `sgba/sgba.log` say why.

## Report upload (2026-10-14, version 3.8)
- The SGBA emulator writes diagnostics to `sgba/reports/` (see the SGBA repo notes): `sessions.log` with frame statistics per game session, plus `crash-*`, `unclean-*`, `errors-*` and `failed-*` text files.
- GitROMs got an upload side. The root list has `[Send reports]`. It posts each report file, plus one generated status report (tails of `sessions.log`, `sgba_launch.log`, `sgba.log`, `gitroms.log` and `updates.txt`), as a new issue with `POST /repos/<repo>/issues`. A sent report file moves to `sgba/reports/sent/`. The first failure stops the run with a readable message (401 token rejected, 403 missing Issues permission, 404 repo not found).
- Destination and token live in `gitroms/data/upload.conf` (`repo=`, `token=`). The file is created with an empty token on first use and is not in the update package, so updates never touch it. A token cannot be typed on the handheld, so it is pasted into that file from a computer. No token is stored in any repo. The token reaches curl through a private temp config file, not the command line, and is not logged.
- Default repo is `xenodorian/GitROMs`, which is public: reports (device name, logs, game names) are visible to anyone. Point `repo=` at a private repo to avoid that.
- Test: `tests/reports_test.sh` against `tests/mock_reports.py`. Not tried against real GitHub or on the device.

## Commit-dated file check no longer loops (2026-10-15, version 3.9)
A single-file item dated by its last commit (the Pokemon ROM) compares a stamp with the one recorded at install. When the GitHub date lookup failed (rate limit, no route), the stamp fell back to the file's Last-Modified or ETag header, a different kind of value from the recorded commit date, so the item showed as "changed" on every check, and installing it recorded the header kind, which then mismatched the next successful lookup. Now: a stamp of one kind and a record of the other kind cannot be compared, so an installed file of exactly the server's size stands as up to date and the record is rewritten in the current kind. The stamp, record and any lookup failure are written to gitroms.log. Cause is inferred from the code and the reports (the recorded date matched the repo's last commit); the device log line will confirm it.

## GitROMs crash reports and update clean-up (2026-10-15, version 4.0)
Why: the updater died at start on the device with no trace, because gitroms.log was emptied on every start and nothing recorded a crash.
- Reports: a crash handler (SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE, on its own stack) writes gitroms-crash-<time>.txt into sgba/reports, where [Send reports] already posts from. A marker file (data/.running, with the current state such as "updating <name>") turns a kill or power loss into gitroms-unclean-<time>.txt at the next start. Titles use the kind "gitroms-crash" and "gitroms-unclean".
- No backtrace(): it needs libgcc_s, which does not link against the glibc 2.31 sysroot for armhf. The report has pc, lr and a frame-pointer chain (aarch64, x86_64); armhf has pc and lr only. Offsets = address minus the "program mapping" start; symbolize with an unstripped build of the source named by "source:" (a hash of native/*.c and *.h, printed by tools/build_native.sh).
- Logs: gitroms.log is rotated to gitroms.prev.log instead of truncated; launch.log to launch.prev.log.
- Clean-up at start (nothing is running yet): tmp files, *.gitroms-part under the ports folder (4 levels) and <roms>/gba (1 level), old reports beyond 8 of each kind. Writable check and free space are logged and shown on the Updates screen when bad.
- Not done: staging a bundle in a separate folder and swapping it in. A half-updated bundle already heals on the next check because every file is compared by size and CRC.

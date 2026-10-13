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

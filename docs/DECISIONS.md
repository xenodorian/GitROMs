# GitROMs 2.0 decisions and evidence

## Why earlier versions did not launch (inferred, not confirmed on device)

Evidence from the repos:

1. `GitROMs/GitROMs.sh` sat inside a `GitROMs/` subfolder. ArkOS lists scripts
   that sit directly in `ports/`; it does not scan subfolders for launchers.
   (Based on the PortMaster layout described in xenodorian/GitGet
   `docs/DECISIONS.md`: launcher `Name.sh` beside a same-purpose folder.)
2. The launcher runs `main.py`, but `main.py` is not in the repo history
   (commits 1ca9f42 and 3758518 add only `GitROMs.sh`, `config.py`,
   `github_client.py`, docs). The app could never have started from this repo.
3. It depended on system Python plus pygame. xenodorian/GitGet has no device
   report showing either is present on the user's ArkOS image.
4. The one launch that opened a browser is not explained by anything in these
   repos. Unverified.

## Choice: LOVE, not SDL2 C

- The CryMon LOVE port already used a PortMaster launcher structure; the
  CryMon SDL2 port needed an aarch64 cross build plus bundled libs.
- HTTPS is the hard part in C. Here it is delegated to the system `curl`,
  which ArkOS ships with CA certificates. LOVE 11 has no HTTPS of its own.
- Lua code is plain text: no compiler, no per-device binary, so the zip
  contents are exactly what was tested on the PC.

## Network design

Each request is a background `curl` process; the UI polls a result file, so it
never freezes. Cancel kills the curl. Timeouts: 15 s connect, 40 s API, 1 h
downloads, 2 retries. Errors map to plain messages (Wi-Fi, rate limit, date).

## Verified on a PC

Launcher flow, two-page repo listing, folder navigation, extension filtering
(dotfiles, README, images hidden), download to `.gitroms-part` then rename,
numbered copy on name conflict, history file, error screens.
See `tests/check_package.py` and `tests/smoke_test.sh`.

## NOT verified (needs the R36S)

- That the LOVE 11.5 runtime exists on the device. It is normally provided by
  PortMaster as `libs/love_11.5.squashfs`. This sandbox could not reach the
  PortMaster runtime host, so the squashfs name, its internal path to the
  `love` binary, and the `port.json` `"runtime"` value are from memory of the
  PortMaster convention. The launcher tries several locations and logs what it
  finds in `gitroms/data/launch.log`.
- Real GitHub API calls (blocked in this sandbox; the mock mirrors the
  documented response shape).
- Controller mapping through `gptokeyb`, 640x480 rendering, Wi-Fi and the
  device clock (a wrong date breaks HTTPS).
- Whether the roms partition is the parent of `ports/` (assumed; overridable
  with `GITROMS_ROMS`).

# GitROMs

Controller-driven GitHub ROM browser for the R36S (ArkOS). A native SDL2
program: no web browser, no login, no LOVE or Python runtime to install.
It downloads files into the matching `roms/<system>/` folders.

**Download:** [dist/GitROMs.zip](https://github.com/xenodorian/GitROMs/raw/claude/amazing-allen-ua012v/dist/GitROMs.zip)
(checksum: `dist/GitROMs.zip.sha256`)

Extract it into the **ports** folder on the roms partition so you get
`ports/GitROMs.sh` and `ports/gitroms/`, then open Ports > GitROMs.
Details: [port/gitroms/README.md](port/gitroms/README.md).

## Status

Built and tested on a PC only (x86_64 build, headless SDL, mock GitHub API;
the aarch64/armhf builds were load-tested under qemu with a glibc 2.31
sysroot). Not yet run on a real R36S. See [docs/DECISIONS.md](docs/DECISIONS.md).

## Repo layout

- `native/gitroms.c` the whole program (SDL2 declared by hand, like the CryMon port)
- `port/` package contents: launcher, gptokeyb map, config, docs
- `tools/build_native.sh` cross-builds aarch64, armhf and x86_64 into `build/`
- `tools/fetch_sysroot.sh` downloads Ubuntu 20.04 glibc 2.31 so binaries run on older ArkOS
- `tools/build_zip.py` builds `dist/GitROMs.zip`
- `tests/` `check_package.py` (static), `smoke_test.sh` (headless run), `mock_github.py`
- `port-love/` the LOVE version. Superseded: it needs a LOVE runtime the device lacked.
- `GitROMs/` the first Python/pygame attempt. Incomplete (`main.py` never committed).

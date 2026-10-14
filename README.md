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
First use: GitROMs creates `gitroms/data/upload.conf`. On a computer, set `repo=` and paste a fine-grained GitHub token (Issues: read and write on that repo) after `token=`, then press SELECT on the screen to reload. The default repo is public, so anything sent there is public.

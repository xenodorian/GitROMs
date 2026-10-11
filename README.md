# GitROMs

Controller-driven GitHub ROM browser for the R36S (ArkOS). It is a dedicated
LOVE (Lua) app. It is not a web browser, needs no login, and downloads files
into the matching `roms/<system>/` folders.

**Download:** [dist/GitROMs.zip](dist/GitROMs.zip) (checksum: `dist/GitROMs.zip.sha256`)

Extract it into the **ports** folder on the roms partition so you get
`ports/GitROMs.sh` and `ports/gitroms/`, then open Ports > GitROMs.
Full details: [port/gitroms/README.md](port/gitroms/README.md).

## Status

Built and tested on a PC only (headless LOVE 11.5 against a mock GitHub API).
Not yet run on a real R36S. See [docs/DECISIONS.md](docs/DECISIONS.md) for what
is verified and what is not.

## Repo layout

- `port/` the package contents (launcher plus `gitroms/` folder)
- `tools/build_zip.py` builds `dist/GitROMs.zip`
- `tests/` `check_package.py` (static), `smoke_test.sh` (headless run), `mock_github.py`
- `GitROMs/` the earlier Python/pygame attempt. It is incomplete (`main.py` was
  never committed) and is superseded by `port/`.

#!/bin/bash
# Report upload: incident files and a status report are posted as issues with the token from upload.conf, sent files move to
# sent/, bad tokens and missing repos give readable errors, the token never appears on a command line or in the log. PC only.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/gitroms.x86_64"
[ -x "$BIN" ] || { echo "build first: tools/build_native.sh"; exit 2; }
T="$(mktemp -d)"
PORT=8783
python3 "$ROOT/tests/mock_reports.py" $PORT >/dev/null 2>&1 &
MOCK=$!
trap 'kill $MOCK 2>/dev/null; rm -rf "${T:?}"' EXIT
sleep 1
export NO_PROXY=127.0.0.1 no_proxy=127.0.0.1 GITROMS_NODEFAULTS=1 GITROMS_API="http://127.0.0.1:$PORT" GITROMS_WINDOWED=1 SDL_VIDEODRIVER=dummy
PORTS="$T/ports"; H="$PORTS/gitroms/data"; R="$PORTS/sgba/reports"
mkdir -p "$H" "$R" "$T/roms"
fail=0
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
posts() { curl -s --noproxy '*' "http://127.0.0.1:$PORT/__posts"; }
app() {
  rm -f "$H/gitroms.log"
  GITROMS_HOME="$H" GITROMS_PORTS="$PORTS" GITROMS_ROMS="$T/roms" GITROMS_TEST="$1" timeout 60 "$BIN" >/dev/null 2>&1
  LOG="$H/gitroms.log"
}
OPEN="idle;act=confirm;act=confirm"       # placeholder, replaced below
printf 'SGBA report: crash\nbuild: abc\nsignal: 11 "quoted" \\ back\nline two\n' > "$R/crash-20261014-100000.txt"
printf 'SGBA report: unclean\nbuild: abc\n' > "$R/unclean-20261014-090000.txt"
printf '2026-10-14 10:00:00 game=x secs=10 fps=59.9\n' > "$R/sessions.log"
printf 'sgba launch log line\n' > "$PORTS/sgba/sgba_launch.log"; printf 'sgba log line\n' > "$PORTS/sgba/sgba.log"
# the root list needs the GitHub user listing; with the API pointed at the mock it returns 404 -> error screen, so open the
# reports screen through the test hook instead
OPEN="idle;rpopen"
curl -s --noproxy '*' "http://127.0.0.1:$PORT/__reset" >/dev/null

# 1. no upload.conf yet: a template is created, nothing is sent
app "$OPEN;act=confirm;wait=1;logrp"
chk "template upload.conf is created" "[ -f \"$H/upload.conf\" ] && grep -q '^token=' \"$H/upload.conf\""
chk "no token: nothing posted, screen shows 2 waiting reports" "[ \"\$(posts)\" = '[]' ] && grep -q 'rp phase=0 n=2 sent=0 token=0' \"\$LOG\""

# 2. a good token: 2 incident reports plus the status report are posted
printf 'repo=owner/repo\ntoken=secret123\n' > "$H/upload.conf"
app "$OPEN;act=confirm;idle;logrp"
chk "3 issues posted" "[ \"\$(posts | python3 -c 'import sys,json;print(len(json.load(sys.stdin)))')\" = 3 ]"
chk "posted to the configured repo with the app user agent" "posts | python3 -c 'import sys,json;d=json.load(sys.stdin);raise SystemExit(0 if all(p[\"path\"]==\"/repos/owner/repo/issues\" and p[\"ua\"].startswith(\"GitROMs\") for p in d) else 1)'"
chk "crash body arrived intact (quotes, backslash, newlines)" "posts | python3 -c 'import sys,json;d=json.load(sys.stdin);b=[p for p in d if p[\"title\"].startswith(\"[crash]\")][0][\"body\"];raise SystemExit(0 if b.startswith(\"Event time: \") and \"SGBA report: crash\\nbuild: abc\\nsignal: 11 \\\"quoted\\\" \\\\ back\\nline two\" in b else 1)'"
chk "status report carries the launch log, sgba.log, sessions and gitroms.log" "posts | python3 -c 'import sys,json;d=json.load(sys.stdin);b=[p for p in d if p[\"title\"].startswith(\"[status]\")][0][\"body\"];raise SystemExit(0 if all(k in b for k in (\"sgba launch log line\",\"sgba log line\",\"fps=59.9\",\"GitROMs 4.4 start\",\"version: 4.4\")) else 1)'"
chk "sent files moved into sent/, sessions.log stays" "[ ! -e \"$R/crash-20261014-100000.txt\" ] && [ -f \"$R/sent/crash-20261014-100000.txt\" ] && [ -f \"$R/sessions.log\" ]"
chk "done message says Sent 3 reports" "grep -q 'rp phase=2 n=0 sent=3 .*msg=Sent 3 reports' \"\$LOG\""
chk "token is not written to the log" "! grep -q secret123 \"\$LOG\""
chk "curl config with the token is removed afterwards" "[ ! -e \"$H/tmp/rp.cfg\" ]"

# 3. wrong token: readable message, files kept
printf 'SGBA report: crash\nbuild: abc\n' > "$R/crash-20261014-110000.txt"
printf 'repo=owner/repo\ntoken=bad\n' > "$H/upload.conf"
curl -s --noproxy '*' "http://127.0.0.1:$PORT/__reset" >/dev/null
app "$OPEN;act=confirm;idle;logrp"
chk "bad token: message names the token, nothing posted, file kept" "grep -q 'msg=GitHub rejected the token (401)' \"\$LOG\" && [ \"\$(posts)\" = '[]' ] && [ -f \"$R/crash-20261014-110000.txt\" ]"

# 4. missing repo
printf 'repo=nope/nope\ntoken=secret123\n' > "$H/upload.conf"
app "$OPEN;act=confirm;idle;logrp"
chk "missing repo: 404 message" "grep -q 'msg=Repository not found' \"\$LOG\" && [ -f \"$R/crash-20261014-110000.txt\" ]"

# 7. reports carry the time of the incident, and ones older than max_age_days are set aside, not posted
printf 'SGBA report: crash\nbuild: old\n' > "$R/crash-1700000000.txt"                      # November 2023
printf 'SGBA report: crash\nbuild: recent\n' > "$R/crash-$(date +%s).txt"
rm -f "$R"/unclean-* "$R"/sent/* 2>/dev/null
curl -s --noproxy '*' "http://127.0.0.1:$PORT/__reset" >/dev/null
printf 'repo=owner/repo\ntoken=secret123\nmax_age_days=7\n' > "$H/upload.conf"
app "$OPEN;act=confirm;idle;logrp"
chk "an old report is set aside in reports/old and not posted" "[ -e \"$R/old/crash-1700000000.txt\" ] && [ ! -e \"$R/crash-1700000000.txt\" ] && ! posts | grep -q 'build: old'"
chk "a recent report is posted with its event time in the body and the title" "posts | python3 -c 'import sys,json;d=json.load(sys.stdin);r=[p for p in d if \"build: recent\" in p[\"body\"]];raise SystemExit(0 if r and r[0][\"body\"].startswith(\"Event time: 20\") and r[0][\"title\"].startswith(\"[crash]\") else 1)'"
exit $fail

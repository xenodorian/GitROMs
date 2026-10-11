#!/usr/bin/env python3
"""PC-side static checks for dist/GitROMs.zip. Not a substitute for a device test."""
import hashlib, os, re, subprocess, sys, tempfile, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZIP = os.path.join(ROOT, "dist", "GitROMs.zip")
fails = []


def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg)
    if not cond:
        fails.append(msg)


with zipfile.ZipFile(ZIP) as z:
    check(z.testzip() is None, "zip integrity")
    names = z.namelist()
    check("GitROMs.sh" in names, "launcher is at zip root (lands directly in ports/)")
    check(all(n == "GitROMs.sh" or n.startswith("gitroms/") for n in names), "only GitROMs.sh and gitroms/ are packaged")
    for need in ["gitroms/game/main.lua", "gitroms/game/conf.lua", "gitroms/gitroms.gptk", "gitroms/port.json", "gitroms/README.md"]:
        check(need in names, f"contains {need}")
    check(not any(".." in n or n.startswith("/") for n in names), "no unsafe paths")
    check(not any(n.endswith((".pyc", ".log", ".part")) or "/data/" in n for n in names), "no build output, logs or runtime data")
    sha = open(ZIP + ".sha256").read().split()[0]
    check(sha == hashlib.sha256(open(ZIP, "rb").read()).hexdigest(), "checksum file matches")
    mode = (z.getinfo("GitROMs.sh").external_attr >> 16) & 0o777
    check(mode == 0o755, "launcher marked executable in zip")
    allbytes = b""
    for n in names:
        d = z.read(n)
        allbytes += d
        if n.endswith((".sh", ".lua", ".gptk", ".json", ".md", ".txt")):
            check(b"\r" not in d, f"LF line endings: {n}")
    low = allbytes.lower()
    for bad in [b"chromium", b"firefox", b"webkit", b"github_pat_", b"ghp_", b"authorization: bearer"]:
        check(bad not in low, f"package does not contain {bad.decode()!r}")
    tmp = tempfile.mkdtemp(prefix="gitroms-check-")
    z.extractall(tmp)

sh = os.path.join(tmp, "GitROMs.sh")
check(subprocess.run(["bash", "-n", sh]).returncode == 0, "launcher passes bash -n")
if subprocess.run(["which", "luajit"], capture_output=True).returncode == 0:
    for base, _, fs in os.walk(os.path.join(tmp, "gitroms")):
        for f in fs:
            if f.endswith(".lua"):
                p = os.path.join(base, f)
                r = subprocess.run(["luajit", "-bl", p], capture_output=True)
                check(r.returncode == 0, f"lua syntax: {os.path.relpath(p, tmp)}")
else:
    print("SKIP lua syntax (luajit not installed)")

print("\nRESULT:", "FAILED" if fails else "all static checks passed")
print("Note: these are PC-only checks. Launch, display, input and Wi-Fi on a real R36S are NOT covered.")
sys.exit(1 if fails else 0)

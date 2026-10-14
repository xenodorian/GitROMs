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


def have(cmd):
    return subprocess.run(["which", cmd], capture_output=True).returncode == 0


with zipfile.ZipFile(ZIP) as z:
    check(z.testzip() is None, "zip integrity")
    names = z.namelist()
    check("GitROMs.sh" in names, "launcher is at zip root (lands directly in ports/)")
    check(all(n == "GitROMs.sh" or n.startswith("gitroms/") for n in names), "only GitROMs.sh and gitroms/ are packaged")
    for need in ["gitroms/gitroms.aarch64", "gitroms/gitroms.armhf", "gitroms/gitroms.gptk",
                 "gitroms/gitroms.conf", "gitroms/components.conf", "gitroms/collect.sh", "gitroms/port.json", "gitroms/README.md"]:
        check(need in names, f"contains {need}")
    check(not any(".." in n or n.startswith("/") for n in names), "no unsafe paths")
    check(not any(n.endswith((".pyc", ".log", ".part", ".lua")) or "/data/" in n for n in names), "no build output, logs, runtime data or LOVE files")
    sha = open(ZIP + ".sha256").read().split()[0]
    check(sha == hashlib.sha256(open(ZIP, "rb").read()).hexdigest(), "checksum file matches")
    for n in ["GitROMs.sh", "gitroms/gitroms.aarch64", "gitroms/gitroms.armhf"]:
        check(((z.getinfo(n).external_attr >> 16) & 0o777) == 0o755, f"{n} marked executable")
    allbytes = b""
    for n in names:
        d = z.read(n)
        allbytes += d
        if n.endswith((".sh", ".gptk", ".json", ".md", ".txt", ".conf")):
            check(b"\r" not in d, f"LF line endings: {n}")
    low = allbytes.lower()
    for bad in [b"chromium", b"firefox", b"webkit", b"github_pat_", b"ghp_"]:
        check(bad not in low, f"package does not contain {bad.decode()!r}")
    check(b"authorization: bearer %s" in low and low.count(b"authorization: bearer") <= 2, "the only auth header is built from the user's upload.conf token")
    tmp = tempfile.mkdtemp(prefix="gitroms-check-")
    z.extractall(tmp)

check(subprocess.run(["bash", "-n", os.path.join(tmp, "GitROMs.sh")]).returncode == 0, "launcher passes bash -n")

for arch, objdump, machine in [("aarch64", "aarch64-linux-gnu-objdump", "aarch64"), ("armhf", "arm-linux-gnueabihf-objdump", "arm")]:
    path = os.path.join(tmp, "gitroms", f"gitroms.{arch}")
    head = open(path, "rb").read(20)
    em = int.from_bytes(head[18:20], "little")
    check(head[:4] == b"\x7fELF" and em == {"aarch64": 183, "arm": 40}[machine], f"{arch} binary is a real ELF for {machine}")
    if have(objdump):
        out = subprocess.run([objdump, "-T", path], capture_output=True, text=True).stdout
        vers = sorted({tuple(map(int, v.split("."))) for v in re.findall(r"GLIBC_([0-9.]+)", out)})
        check(bool(vers) and max(vers) <= (2, 17), f"{arch} needs glibc <= 2.17 (found {['.'.join(map(str, v)) for v in vers]})")
        needed = subprocess.run([objdump.replace("objdump", "readelf"), "-d", path], capture_output=True, text=True).stdout
        libs = re.findall(r"Shared library: \[(.*?)\]", needed)
        check(sorted(libs) == ["libSDL2-2.0.so.0", "libc.so.6"], f"{arch} links only libc and libSDL2 ({libs})")
    else:
        print(f"SKIP glibc check for {arch} ({objdump} not installed)")

print("\nRESULT:", "FAILED" if fails else "all static checks passed")
print("Note: PC-only checks. Launch, display, input and Wi-Fi on a real R36S are NOT covered.")
sys.exit(1 if fails else 0)

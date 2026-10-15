#!/usr/bin/env python3
"""Build dist/GitROMs.zip from port/ plus the handheld binaries in build/.
Run tools/build_native.sh first. Extract the zip into the ports folder."""
import hashlib, os, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "port")
DIST = os.path.join(ROOT, "dist")
BUILD = os.path.join(ROOT, "build")
BINARIES = ["aarch64", "armhf"]
OUT = os.path.join(DIST, "GitROMs.zip")
SKIP_DIRS = {"data", "__pycache__"}
FIXED_TIME = (2026, 1, 1, 0, 0, 0)  # reproducible output


def collect():
    files = []
    for base, dirs, names in os.walk(SRC):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
        for n in sorted(names):
            full = os.path.join(base, n)
            files.append((os.path.relpath(full, SRC).replace(os.sep, "/"), full))
    return sorted(files)


def main():
    files = [f for f in collect() if f[0] != "gitroms/MANIFEST.txt"]
    for arch in BINARIES:
        src = os.path.join(BUILD, f"gitroms.{arch}")
        if not os.path.exists(src):
            sys.exit(f"missing {src}; run tools/build_native.sh")
        files.append((f"gitroms/gitroms.{arch}", src))
    files.sort()
    manifest = "\n".join(rel for rel, _ in files) + "\ngitroms/MANIFEST.txt\n"
    os.makedirs(DIST, exist_ok=True)
    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for rel, full in files + [("gitroms/MANIFEST.txt", None)]:
            data = manifest.encode() if full is None else open(full, "rb").read()
            if rel.endswith((".sh", ".lua", ".gptk", ".json", ".md", ".txt")):
                data = data.replace(b"\r\n", b"\n")
            info = zipfile.ZipInfo(rel, FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            exe = rel.endswith(".sh") or "/gitroms." in rel and rel.rsplit(".", 1)[-1] in BINARIES
            info.external_attr = (0o755 if exe else 0o644) << 16
            z.writestr(info, data)
    digest = hashlib.sha256(open(OUT, "rb").read()).hexdigest()
    with open(OUT + ".sha256", "w") as f:
        f.write(f"{digest}  GitROMs.zip\n")
    for arch in BINARIES:                    # the same program files on their own: GitROMs.sh repairs a damaged copy from these
        data = open(os.path.join(BUILD, f"gitroms.{arch}"), "rb").read()
        with open(os.path.join(DIST, f"gitroms.{arch}"), "wb") as f:
            f.write(data)
        with open(os.path.join(DIST, f"gitroms.{arch}.sha256"), "w") as f:
            f.write(f"{hashlib.sha256(data).hexdigest()}  gitroms.{arch}\n")
    print(f"wrote {OUT} ({os.path.getsize(OUT)} bytes) sha256 {digest}")


if __name__ == "__main__":
    sys.exit(main())

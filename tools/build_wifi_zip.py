#!/usr/bin/env python3
"""Build dist/WiFiReport.zip. Run tools/build_native.sh first."""
import hashlib, os, sys, zipfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "wifi-report")
files = [("WiFiReport.sh", os.path.join(SRC, "WiFiReport.sh"), 0o755),
         ("wifireport/collect.sh", os.path.join(SRC, "wifireport/collect.sh"), 0o755),
         ("wifireport/wifireport.gptk", os.path.join(SRC, "wifireport/wifireport.gptk"), 0o644),
         ("wifireport/README.txt", os.path.join(SRC, "wifireport/README.txt"), 0o644)]
for arch in ("aarch64", "armhf"):
    p = os.path.join(ROOT, "build", f"wifireport.{arch}")
    if not os.path.exists(p):
        sys.exit(f"missing {p}; run tools/build_native.sh")
    files.append((f"wifireport/wifireport.{arch}", p, 0o755))
out = os.path.join(ROOT, "dist", "WiFiReport.zip")
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for rel, path, mode in sorted(files):
        data = open(path, "rb").read()
        if rel.endswith((".sh", ".gptk", ".txt")):
            data = data.replace(b"\r\n", b"\n")
        i = zipfile.ZipInfo(rel, (2026, 1, 1, 0, 0, 0))
        i.compress_type = zipfile.ZIP_DEFLATED
        i.external_attr = mode << 16
        z.writestr(i, data)
h = hashlib.sha256(open(out, "rb").read()).hexdigest()
open(out + ".sha256", "w").write(f"{h}  WiFiReport.zip\n")
print(out, h)

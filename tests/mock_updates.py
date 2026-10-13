#!/usr/bin/env python3
"""Fake release host for the update tests: serves a zip bundle and a single
zip-based file with HTTP Range support, counts what it serves, and can mutate,
corrupt or add path-traversal entries on demand.
Usage: mock_updates.py PORT [--norange]"""
import io, json, os, random, sys, urllib.parse, zipfile
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8780
NORANGE = "--norange" in sys.argv
rnd = random.Random(7)

def text(n, tag):
    return (f"# file {n} {tag}\n" + "".join(f"line {i} {rnd.random():.6f}\n" for i in range(80))).encode()

FILES = {"README.txt": b"readme v1\n", "SGBA.sh": b"#!/bin/sh\necho v1\n",
         "sgba/python/bin/python3": os.urandom(2_500_000),
         "sgba/games/demo.sgba": b"demo v1", "sgba/data/conf.json": b'{"v":1}'}
for i in range(120):
    FILES[f"sgba/lib/mod{i:03d}.py"] = text(i, "v1")
EXEC = {"SGBA.sh", "sgba/python/bin/python3"}
GAME = {"manifest.json": b'{"v":1}', "main.py": text("game", "v1"), "assets/blob.bin": os.urandom(400_000)}
EXTRA = {}          # extra raw entries (e.g. path traversal)
CORRUPT = set()     # entry names whose compressed bytes are damaged in the served copy
STATS = {"requests": 0, "bytes": 0, "ranged": 0, "paths": [], "by": {}}

def build(files, execs=()):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in sorted(files.items()):
            zi = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = (0o755 if name in execs else 0o644) << 16
            z.writestr(zi, data)
        for name, data in EXTRA.items():
            zi = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            z.writestr(zi, data)
    return buf.getvalue()

def served_bundle():
    data = bytearray(build(FILES, EXEC))
    if CORRUPT:
        z = zipfile.ZipFile(io.BytesIO(bytes(data)))
        for name in CORRUPT:
            zi = z.getinfo(name)
            start = zi.header_offset + 30 + len(zi.filename.encode()) + len(zi.extra)
            data[start + zi.compress_size // 2] ^= 0xFF
    return bytes(data)

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def reply(self, code, body=b"", extra=None):
        self.send_response(code)
        for k, v in (extra or {}).items(): self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD": self.wfile.write(body)
    def do_GET(self):
        u = urllib.parse.urlparse(self.path); q = urllib.parse.parse_qs(u.query)
        if u.path == "/__stats": return self.reply(200, json.dumps(STATS).encode())
        if u.path == "/__reset":
            STATS.update(requests=0, bytes=0, ranged=0, paths=[], by={}); return self.reply(200, b"ok")
        if u.path == "/__set":      # change a bundle file
            FILES[q["name"][0]] = q["text"][0].encode(); return self.reply(200, b"ok")
        if u.path == "/__add":
            FILES[q["name"][0]] = q["text"][0].encode(); return self.reply(200, b"ok")
        if u.path == "/__game":
            GAME["main.py"] = q["text"][0].encode(); return self.reply(200, b"ok")
        if u.path == "/__corrupt":
            CORRUPT.add(q["name"][0]); return self.reply(200, b"ok")
        if u.path == "/__clean":
            CORRUPT.clear(); EXTRA.clear(); return self.reply(200, b"ok")
        if u.path == "/__evil":
            EXTRA["../evil.txt"] = b"pwned"; return self.reply(200, b"ok")
        if u.path == "/bundle.zip": data = served_bundle()
        elif u.path == "/game.sgba": data = build(GAME)
        else: return self.reply(404, b"nope")
        STATS["requests"] += 1
        rng = self.headers.get("Range")
        if rng and not NORANGE:
            spec = rng.split("=", 1)[1]
            a, _, b = spec.partition("-")
            if a == "":                      # suffix ranges: unsupported, like GitHub release assets
                return self.reply(501, b"Unsupported client range")
            a = int(a); b = int(b) if b else len(data) - 1; b = min(b, len(data) - 1)
            part = data[a:b + 1]
            STATS["ranged"] += 1; STATS["bytes"] += len(part); STATS["paths"].append([u.path, a, b])
            STATS["by"][u.path] = STATS["by"].get(u.path, 0) + len(part)
            return self.reply(206, part, {"Content-Range": f"bytes {a}-{b}/{len(data)}", "Accept-Ranges": "bytes"})
        STATS["bytes"] += len(data); STATS["paths"].append([u.path, 0, len(data) - 1])
        STATS["by"][u.path] = STATS["by"].get(u.path, 0) + len(data)
        return self.reply(200, data)
    do_HEAD = do_GET

if __name__ == "__main__":
    HTTPServer(("127.0.0.1", PORT), H).serve_forever()

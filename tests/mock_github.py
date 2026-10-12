#!/usr/bin/env python3
"""Tiny fake of the GitHub REST API pieces GitROMs uses (for PC tests)."""
import hashlib, json, os, sys, urllib.parse
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
BASE = f"http://127.0.0.1:{PORT}"
BLOB = os.urandom(1_500_000)
VER = 0  # bumped via GET /__bump to simulate new commits and a new release build

REPOS = [{"name": f"filler{i}", "description": None, "default_branch": "main",
          "owner": {"login": "xenodorian"}} for i in range(100)]
REPOS[0] = {"name": "BibooSGBA", "description": "real release fixture", "default_branch": "main",
            "owner": {"login": "xenodorian"}}  # first in the list; the rest are fillers
REPOS += [
    {"name": "GBA-Roms", "description": "My homebrew é roms", "default_branch": "main", "owner": {"login": "xenodorian"}},
    {"name": "Empty", "description": "nothing", "default_branch": "main", "owner": {"login": "xenodorian"}},
]
TREE = {
    "GBA-Roms": {
        "": [("dir", "hacks", 0), ("file", "game.gba", None), ("file", "README.md", 10),
             ("file", ".hidden.gba", 5), ("file", "Big Name (USA) [!].sgba", 700_000)],
        "hacks": [("file", "x.zip", 1234), ("file", "photo.png", 5)],
    },
    "Empty": {"": []},
    "BibooSGBA": {"": []},
}

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, body, ctype="application/json"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)
        p = urllib.parse.unquote(u.path)
        if p == "/users/xenodorian/repos":
            page = int(q.get("page", ["1"])[0])
            chunk = REPOS[(page - 1) * 100: page * 100]
            return self.send(200, json.dumps(chunk).encode())
        if p == "/__bump":
            global VER
            VER += 1
            return self.send(200, str(VER).encode(), "text/plain")
        if p.startswith("/repos/") and p.endswith("/releases/latest"):
            fx = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures", f"release_{p.split('/')[3]}.json")
            if os.path.exists(fx):
                return self.send(200, open(fx, "rb").read())
            if p.split("/")[3] != "GBA-Roms":
                return self.send(404, b'{"message":"Not Found"}')
            size = 20000 + VER
            rel = {"tag_name": "latest", "assets": [
                {"name": "SGBA-arkos-aarch64.zip", "size": size, "id": 1000 + VER,
                 "updated_at": f"2026-10-1{VER}T00:00:00Z", "browser_download_url": f"{BASE}/dl/SGBA-arkos-aarch64.zip?size={size}"},
                {"name": "build.log", "size": 5, "id": 7, "updated_at": "x", "browser_download_url": f"{BASE}/dl/build.log?size=5"}]}
            return self.send(200, json.dumps(rel).encode())
        if p.startswith("/repos/") and "/contents" in p:
            parts = p.split("/")
            repo = parts[3]
            sub = p.split("/contents", 1)[1].strip("/")
            if repo == "Ratelimited":
                return self.send(403, b'{"message":"rate limit"}')
            tree = TREE.get(repo)
            if tree is None or sub not in tree:
                return self.send(404, b'{"message":"Not Found"}')
            out = []
            for kind, name, size in tree[sub]:
                path = (sub + "/" + name).strip("/")
                if name == "game.gba":
                    size = len(BLOB) + 100 * VER
                e = {"name": name, "path": path, "type": kind, "size": size,
                     "sha": hashlib.sha1(f"{path}{VER if name == 'game.gba' else 0}".encode()).hexdigest()}
                if kind == "file":
                    e["download_url"] = f"{BASE}/raw/{urllib.parse.quote(path)}?size={size}"
                out.append(e)
            return self.send(200, json.dumps(out).encode())
        if p.startswith("/raw/") or p.startswith("/dl/"):
            size = int(q.get("size", ["10"])[0])
            data = (f"VER{VER}".encode() + BLOB * 2)[:size]
            return self.send(200, data, "application/octet-stream")
        self.send(404, b"{}")

if __name__ == "__main__":
    HTTPServer(("127.0.0.1", PORT), H).serve_forever()

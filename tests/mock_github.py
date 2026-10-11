#!/usr/bin/env python3
"""Tiny fake of the GitHub REST API pieces GitROMs uses (for PC tests)."""
import json, os, sys, urllib.parse
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
BASE = f"http://127.0.0.1:{PORT}"
BLOB = os.urandom(1_500_000)

REPOS = [{"name": f"filler{i}", "description": None, "default_branch": "main",
          "owner": {"login": "xenodorian"}} for i in range(100)]
REPOS += [
    {"name": "GBA-Roms", "description": "My homebrew é roms", "default_branch": "main", "owner": {"login": "xenodorian"}},
    {"name": "Empty", "description": "nothing", "default_branch": "main", "owner": {"login": "xenodorian"}},
]
TREE = {
    "GBA-Roms": {
        "": [("dir", "hacks", 0), ("file", "game.gba", len(BLOB)), ("file", "README.md", 10),
             ("file", ".hidden.gba", 5), ("file", "Big Name (USA) [!].sgba", 700_000)],
        "hacks": [("file", "x.zip", 1234), ("file", "photo.png", 5)],
    },
    "Empty": {"": []},
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
                e = {"name": name, "path": path, "type": kind, "size": size}
                if kind == "file":
                    e["download_url"] = f"{BASE}/raw/{urllib.parse.quote(path)}?size={size}"
                out.append(e)
            return self.send(200, json.dumps(out).encode())
        if p.startswith("/raw/"):
            size = int(q.get("size", ["10"])[0])
            return self.send(200, (BLOB * 2)[:size] if size else b"", "application/octet-stream")
        self.send(404, b"{}")

if __name__ == "__main__":
    HTTPServer(("127.0.0.1", PORT), H).serve_forever()

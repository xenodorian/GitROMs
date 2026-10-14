"""Fake GitHub issues API for the report upload test. POST /repos/OWNER/REPO/issues records the request and returns 201.
GET /__posts returns what was received. The token 'bad' is rejected with 401; the repo 'nope/nope' gives 404."""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

POSTS = []


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def reply(self, code, obj):
        data = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/__posts":
            return self.reply(200, POSTS)
        if self.path == "/__reset":
            POSTS.clear()
            return self.reply(200, {})
        self.reply(404, {})

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n)
        auth = self.headers.get("Authorization", "")
        if self.path == "/repos/nope/nope/issues":
            return self.reply(404, {"message": "Not Found"})
        if auth != "Bearer secret123":
            return self.reply(401, {"message": "Bad credentials"})
        try:
            obj = json.loads(body)
        except ValueError:
            return self.reply(400, {"message": "bad json"})
        POSTS.append({"path": self.path, "title": obj.get("title"), "body": obj.get("body"), "ua": self.headers.get("User-Agent")})
        self.reply(201, {"html_url": "https://github.example/issues/%d" % len(POSTS)})


HTTPServer(("127.0.0.1", int(sys.argv[1])), H).serve_forever()

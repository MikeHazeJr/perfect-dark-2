#!/usr/bin/env python3
"""Minimal kanban HTTP server. Serves index.html and provides /api/state read/write."""

import json
import os
import pathlib
import sys
import tempfile
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = 7531
BASE = pathlib.Path(__file__).parent
STATE_PATH = BASE / "state.json"
INDEX_PATH = BASE / "index.html"


def read_state():
    return STATE_PATH.read_text(encoding="utf-8")


def write_state(body: bytes):
    data = json.loads(body)  # validate JSON first
    serialized = json.dumps(data, indent=2, ensure_ascii=False)
    tmp = STATE_PATH.with_suffix(".json.tmp")
    tmp.write_text(serialized, encoding="utf-8")
    os.replace(tmp, STATE_PATH)
    return serialized


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(f"[kanban] {self.address_string()} - {fmt % args}")

    def send_cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_cors()
        self.end_headers()

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            content = INDEX_PATH.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(content)))
            self.send_cors()
            self.end_headers()
            self.wfile.write(content)

        elif self.path == "/api/state":
            content = read_state().encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(content)))
            self.send_cors()
            self.end_headers()
            self.wfile.write(content)

        else:
            self.send_error(404)

    def do_POST(self):
        if self.path == "/api/state":
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length)
            try:
                written = write_state(body)
                print(f"[kanban] state.json written ({len(written)} bytes)")
                self.send_response(200)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_cors()
                self.end_headers()
                self.wfile.write(b'{"ok":true}')
            except Exception as exc:
                print(f"[kanban] write error: {exc}", file=sys.stderr)
                self.send_response(400)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.end_headers()
                self.wfile.write(json.dumps({"error": str(exc)}).encode())
        else:
            self.send_error(404)


if __name__ == "__main__":
    server = HTTPServer(("localhost", PORT), Handler)
    print(f"[kanban] listening on http://localhost:{PORT}/")
    print(f"[kanban] state file: {STATE_PATH}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[kanban] stopped.")

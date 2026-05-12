#!/usr/bin/env python3
"""Kanban + Parked-Threads + Bug-Tracker HTTP server.

Serves the dev-window UI at / and provides these API endpoints:

  /api/state              GET  POST            kanban state.json
  /api/cards/:id          PATCH                kanban card partial update (flag + flagged_at, etc.)
  /api/parked             GET  POST            parked.json (tools/kanban/parked.json)
  /api/bugs               GET  POST            bug state (tools/bugs/state.json)
  /api/briefing           GET                  daily-flow briefing (tools/kanban/daily-briefing.json)

  /api/park               POST                 park a kanban card by id + resume condition
  /api/unpark             POST                 restore a parked entry to kanban
  /api/parked/complete    POST                 archive a parked entry without restoring
  /api/parked/delete      POST                 hard-delete a parked entry
  /api/evaluate           POST                 run resume + staleness evaluator (writes parked.json)
  /api/cascade            POST                 cascade after a card is moved to done

  /heartbeat              POST                 page liveness ping (every ~5s while a tab is open)
  /shutdown-now           POST                 best-effort beacon when the last tab is closing

All writes are atomic (temp + rename). All endpoints support OPTIONS for CORS preflight.

Lifecycle: the server is ephemeral. The Dev Window v2 "Open Kanban" button
spawns it on demand; tabs heartbeat every ~5s; if no heartbeat arrives for
KANBAN_IDLE_TIMEOUT_S seconds (default 15) the watcher shuts the server down.
Startup waits an extra STARTUP_GRACE_S (default 30) before the timeout starts
counting down, so a slow browser launch is not a problem. /shutdown-now is
a cooperative hint: it backdates the heartbeat clock to fire in
SHUTDOWN_NOW_GRACE_S (default 6) seconds; if any other tab heartbeats inside
that window the clock resets and the server stays up.
"""

import json
import os
import pathlib
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = 7531
BASE = pathlib.Path(__file__).parent
REPO_ROOT = BASE.parent.parent

STATE_PATH = BASE / "state.json"
PARKED_PATH = BASE / "parked.json"
BUGS_PATH = REPO_ROOT / "tools" / "bugs" / "state.json"
BRIEFING_PATH = BASE / "daily-briefing.json"
INDEX_PATH = BASE / "index.html"

IDLE_TIMEOUT_S = max(1, int(os.environ.get("KANBAN_IDLE_TIMEOUT_S", "15")))
STARTUP_GRACE_S = max(0, int(os.environ.get("KANBAN_STARTUP_GRACE_S", "30")))
SHUTDOWN_NOW_GRACE_S = max(1, int(os.environ.get("KANBAN_SHUTDOWN_GRACE_S", "6")))
WATCHER_INTERVAL_S = 2.0

sys.path.insert(0, str(REPO_ROOT / "tools"))
import parked_evaluator as pe

_state_lock = threading.Lock()
_last_heartbeat_at = time.time() + STARTUP_GRACE_S
_server = None
_shutdown_fired = False


def heartbeat_now():
    global _last_heartbeat_at
    with _state_lock:
        _last_heartbeat_at = time.time()


def heartbeat_soft_shutdown():
    """Backdate heartbeat so the idle watcher fires in SHUTDOWN_NOW_GRACE_S
    seconds unless another tab heartbeats before then (multi-tab safety)."""
    global _last_heartbeat_at
    with _state_lock:
        target = time.time() - IDLE_TIMEOUT_S + SHUTDOWN_NOW_GRACE_S
        if target < _last_heartbeat_at:
            _last_heartbeat_at = target


def request_shutdown(reason):
    global _shutdown_fired
    with _state_lock:
        if _shutdown_fired:
            return
        _shutdown_fired = True
    print(f"[kanban] shutting down: {reason}")
    srv = _server
    if srv is None:
        return

    def _do():
        time.sleep(0.05)
        try:
            srv.shutdown()
        except Exception as exc:
            print(f"[kanban] shutdown error: {exc}", file=sys.stderr)

    threading.Thread(target=_do, daemon=True).start()


def heartbeat_watcher():
    while True:
        time.sleep(WATCHER_INTERVAL_S)
        with _state_lock:
            idle = time.time() - _last_heartbeat_at
            fired = _shutdown_fired
        if fired:
            return
        if idle > IDLE_TIMEOUT_S:
            request_shutdown(f"idle {idle:.1f}s > timeout {IDLE_TIMEOUT_S}s")
            return


def read_text(path: pathlib.Path) -> str:
    if not path.exists():
        return "{}"
    return path.read_text(encoding="utf-8")


def write_json_atomic(path: pathlib.Path, body: bytes) -> str:
    data = json.loads(body)
    serialized = json.dumps(data, indent=2, ensure_ascii=False)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(serialized, encoding="utf-8")
    os.replace(tmp, path)
    return serialized


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        msg = fmt % args
        if "/heartbeat" in msg:
            return  # heartbeat fires every 5s per tab; do not flood stdout
        print(f"[kanban] {self.address_string()} - {msg}")

    def send_cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, PATCH, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def reply_json(self, status, payload):
        body = json.dumps(payload).encode("utf-8") if not isinstance(payload, (bytes, bytearray)) else bytes(payload)
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_cors()
        self.end_headers()
        self.wfile.write(body)

    def reply_text(self, status, content_bytes, content_type="text/plain; charset=utf-8"):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(content_bytes)))
        self.send_cors()
        self.end_headers()
        self.wfile.write(content_bytes)

    def read_body(self):
        length = int(self.headers.get("Content-Length", 0))
        if length == 0:
            return b""
        return self.rfile.read(length)

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_cors()
        self.end_headers()

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            content = INDEX_PATH.read_bytes()
            self.reply_text(200, content, content_type="text/html; charset=utf-8")
            return

        if self.path == "/api/state":
            self.reply_text(200, read_text(STATE_PATH).encode("utf-8"), content_type="application/json; charset=utf-8")
            return

        if self.path == "/api/parked":
            self.reply_text(200, read_text(PARKED_PATH).encode("utf-8"), content_type="application/json; charset=utf-8")
            return

        if self.path == "/api/bugs":
            self.reply_text(200, read_text(BUGS_PATH).encode("utf-8"), content_type="application/json; charset=utf-8")
            return

        if self.path == "/api/briefing":
            self.reply_text(200, read_text(BRIEFING_PATH).encode("utf-8"), content_type="application/json; charset=utf-8")
            return

        self.send_error(404)

    def do_POST(self):
        try:
            if self.path == "/heartbeat":
                heartbeat_now()
                # drain body if any (sendBeacon may send a tiny payload)
                self.read_body()
                self.reply_json(200, {"ok": True})
                return

            if self.path == "/shutdown-now":
                # cooperative: backdate the heartbeat clock so the watcher
                # fires soon unless another tab keeps the server alive.
                self.read_body()
                heartbeat_soft_shutdown()
                self.reply_json(200, {"ok": True})
                return

            if self.path == "/api/state":
                written = write_json_atomic(STATE_PATH, self.read_body())
                print(f"[kanban] state.json written ({len(written)} bytes)")
                self.reply_json(200, {"ok": True})
                return

            if self.path == "/api/parked":
                written = write_json_atomic(PARKED_PATH, self.read_body())
                print(f"[kanban] parked.json written ({len(written)} bytes)")
                self.reply_json(200, {"ok": True})
                return

            if self.path == "/api/bugs":
                BUGS_PATH.parent.mkdir(parents=True, exist_ok=True)
                written = write_json_atomic(BUGS_PATH, self.read_body())
                print(f"[kanban] bugs/state.json written ({len(written)} bytes)")
                self.reply_json(200, {"ok": True})
                return

            if self.path == "/api/park":
                payload = json.loads(self.read_body() or b"{}")
                card_id = payload.get("card_id")
                rc = payload.get("resume_condition") or {"type": "date", "targets": None, "semantics": "all-of"}
                note = payload.get("note", "")
                if not card_id:
                    self.reply_json(400, {"error": "card_id required"})
                    return
                parked = pe.load_json(PARKED_PATH)
                kanban = pe.load_json(STATE_PATH)
                entry = pe.park_card(parked, kanban, card_id, rc, snapshot_extra=note)
                pe.save_json_atomic(PARKED_PATH, parked)
                pe.save_json_atomic(STATE_PATH, kanban)
                self.reply_json(200, {"ok": True, "parked_id": entry["id"]})
                return

            if self.path == "/api/unpark":
                payload = json.loads(self.read_body() or b"{}")
                pid = payload.get("parked_id")
                if not pid:
                    self.reply_json(400, {"error": "parked_id required"})
                    return
                parked = pe.load_json(PARKED_PATH)
                kanban = pe.load_json(STATE_PATH)
                restored = pe.unpark_entry(parked, kanban, pid)
                pe.save_json_atomic(PARKED_PATH, parked)
                pe.save_json_atomic(STATE_PATH, kanban)
                self.reply_json(200, {"ok": True, "card_id": restored.get("id")})
                return

            if self.path == "/api/parked/complete":
                payload = json.loads(self.read_body() or b"{}")
                pid = payload.get("parked_id")
                reason = payload.get("reason", "completed")
                if not pid:
                    self.reply_json(400, {"error": "parked_id required"})
                    return
                parked = pe.load_json(PARKED_PATH)
                pe.mark_parked_complete(parked, pid, reason=reason)
                pe.save_json_atomic(PARKED_PATH, parked)
                self.reply_json(200, {"ok": True})
                return

            if self.path == "/api/parked/delete":
                payload = json.loads(self.read_body() or b"{}")
                pid = payload.get("parked_id")
                if not pid:
                    self.reply_json(400, {"error": "parked_id required"})
                    return
                parked = pe.load_json(PARKED_PATH)
                parked["parked"] = [e for e in parked.get("parked", []) if e.get("id") != pid]
                pe.save_json_atomic(PARKED_PATH, parked)
                self.reply_json(200, {"ok": True})
                return

            if self.path == "/api/parked/update":
                payload = json.loads(self.read_body() or b"{}")
                pid = payload.get("parked_id")
                patch = payload.get("patch") or {}
                if not pid:
                    self.reply_json(400, {"error": "parked_id required"})
                    return
                parked = pe.load_json(PARKED_PATH)
                target = None
                for entry in parked.get("parked", []):
                    if entry.get("id") == pid:
                        target = entry
                        break
                if target is None:
                    self.reply_json(404, {"error": "parked entry not found"})
                    return
                for k, v in patch.items():
                    target[k] = v
                pe.save_json_atomic(PARKED_PATH, parked)
                self.reply_json(200, {"ok": True})
                return

            if self.path == "/api/bugs/update":
                payload = json.loads(self.read_body() or b"{}")
                bid = payload.get("bug_id")
                patch = payload.get("patch") or {}
                if not bid:
                    self.reply_json(400, {"error": "bug_id required"})
                    return
                bugs = pe.load_json(BUGS_PATH)
                target = None
                for bug in bugs.get("bugs", []):
                    if bug.get("id") == bid:
                        target = bug
                        break
                if target is None:
                    self.reply_json(404, {"error": "bug not found"})
                    return
                for k, v in patch.items():
                    target[k] = v
                pe.save_json_atomic(BUGS_PATH, bugs)
                self.reply_json(200, {"ok": True})
                return

            if self.path == "/api/bugs/add":
                payload = json.loads(self.read_body() or b"{}")
                new_bug = payload.get("bug") or {}
                if not new_bug.get("id") or not new_bug.get("title"):
                    self.reply_json(400, {"error": "bug.id and bug.title required"})
                    return
                bugs = pe.load_json(BUGS_PATH)
                for existing in bugs.get("bugs", []):
                    if existing.get("id") == new_bug["id"]:
                        self.reply_json(409, {"error": f"bug {new_bug['id']} already exists"})
                        return
                new_bug.setdefault("severity", "minor")
                new_bug.setdefault("status", "open")
                new_bug.setdefault("repro", "")
                new_bug.setdefault("root_cause_file_lines", [])
                new_bug.setdefault("linked_test", None)
                new_bug.setdefault("fix_commit", None)
                new_bug.setdefault("fixed_date", None)
                new_bug.setdefault("related_bugs", [])
                new_bug.setdefault("filed_date", __import__("datetime").date.today().isoformat())
                bugs.setdefault("bugs", []).append(new_bug)
                pe.save_json_atomic(BUGS_PATH, bugs)
                self.reply_json(200, {"ok": True})
                return

            if self.path == "/api/evaluate":
                parked = pe.load_json(PARKED_PATH)
                kanban = pe.load_json(STATE_PATH)
                flipped = pe.evaluate_resume_conditions(parked, kanban)
                newly_stale = pe.evaluate_staleness(parked)
                pe.save_json_atomic(PARKED_PATH, parked)
                self.reply_json(200, {
                    "ok": True,
                    "ready_flipped": [{"id": e["id"], "title": e.get("title", "")} for e in flipped],
                    "newly_stale": [{"id": e["id"], "title": e.get("title", "")} for e in newly_stale],
                })
                return

            if self.path == "/api/cascade":
                payload = json.loads(self.read_body() or b"{}")
                card_done = payload.get("card_done")
                if not card_done:
                    self.reply_json(400, {"error": "card_done required"})
                    return
                parked = pe.load_json(PARKED_PATH)
                kanban = pe.load_json(STATE_PATH)
                result = pe.cascade_card_done(parked, kanban, card_done)
                pe.save_json_atomic(PARKED_PATH, parked)
                self.reply_json(200, {
                    "ok": True,
                    "matched": [{"id": e["id"]} for e in result["matched"]],
                    "flipped": [{"id": e["id"], "title": e.get("title", "")} for e in result["flipped"]],
                })
                return

            self.send_error(404)
        except Exception as exc:
            print(f"[kanban] POST error on {self.path}: {exc}", file=sys.stderr)
            self.reply_json(400, {"error": str(exc)})

    def do_PATCH(self):
        if self.path.startswith("/api/cards/"):
            card_id = self.path[len("/api/cards/"):]
            try:
                patch = json.loads(self.read_body() or b"{}")
                data = json.loads(read_text(STATE_PATH))
                found = False
                for card in data.get("cards", []):
                    if card.get("id") == card_id:
                        for k, v in patch.items():
                            card[k] = v
                        found = True
                        break
                if not found:
                    self.reply_json(404, {"error": "card not found"})
                    return
                write_json_atomic(STATE_PATH, json.dumps(data).encode("utf-8"))
                print(f"[kanban] PATCH card {card_id}")
                self.reply_json(200, {"ok": True})
            except Exception as exc:
                print(f"[kanban] patch error: {exc}", file=sys.stderr)
                self.reply_json(400, {"error": str(exc)})
            return

        self.send_error(404)


if __name__ == "__main__":
    server = HTTPServer(("localhost", PORT), Handler)
    _server = server
    threading.Thread(target=heartbeat_watcher, daemon=True).start()
    print(f"[kanban] listening on http://localhost:{PORT}/")
    print(f"[kanban] idle timeout: {IDLE_TIMEOUT_S}s (override via KANBAN_IDLE_TIMEOUT_S)")
    print(f"[kanban] startup grace: {STARTUP_GRACE_S}s, shutdown-now grace: {SHUTDOWN_NOW_GRACE_S}s")
    print(f"[kanban] state file: {STATE_PATH}")
    print(f"[kanban] parked file: {PARKED_PATH}")
    print(f"[kanban] bugs file: {BUGS_PATH}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[kanban] stopped (Ctrl-C).")
    finally:
        try:
            server.server_close()
        except Exception:
            pass
    print("[kanban] exited.")

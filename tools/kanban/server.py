#!/usr/bin/env python3
"""Kanban + Parked-Threads + Bug-Tracker HTTP server.

Serves the dev-window UI at / and provides these API endpoints:

  /api/state              GET  POST            kanban state.json
  /api/cards/:id          PATCH                kanban card partial update (flag + flagged_at, etc.)
  /api/parked             GET  POST            parked.json (tools/kanban/parked.json)
  /api/bugs               GET  POST            bug state (tools/bugs/state.json)
  /api/bugs/delete        POST                 hard-delete a bug from tools/bugs/state.json
  /api/briefing           GET                  daily-flow briefing (tools/kanban/daily-briefing.json)
  /api/memory-review      GET  POST            dynamic memory review rows + review markup
  /api/memory-review/apply POST                apply deterministic review markup to memories.md
  /api/memory-review/:id  PATCH                update one memory review item status/note

  /api/park               POST                 park a kanban card by id + resume condition
  /api/unpark             POST                 restore a parked entry to kanban
  /api/parked/complete    POST                 archive a parked entry without restoring
  /api/parked/delete      POST                 hard-delete a parked entry
  /api/evaluate           POST                 run resume + staleness evaluator (writes parked.json)
  /api/cascade            POST                 cascade after a card is moved to done

  /api/cards/:id/questions                          POST   add a new open question to a card
  /api/cards/:id/questions/:qid/answer              POST   Mike answers (choice + custom_text)
  /api/cards/:id/questions/:qid/interpret           POST   orchestrator writes interpretation
  /api/cards/:id/questions/:qid/confirm-interpretation POST Mike confirms or refines (refine spawns follow-up)
  /api/open-questions                               GET    list every unresolved question
  /api/decision-requests                            GET    list every question + Mike response
  /api/cards/:id/blocked-status                     GET    is this card blocked on open questions

  /api/cards/:id/mark-pending-completion            POST   session/orchestrator marks card ready for review
  /api/cards/:id/confirm-completion                 POST   Mike confirms (moves to done)
  /api/cards/:id/reject-completion                  POST   Mike rejects (clears flag, stays in column)
  /api/pending-completions                          GET    list every card with pending_completion non-null

  /api/cards/:id/sessions                           GET    list card-scoped Codex sessions
  /api/cards/:id/sessions/start                     POST   start one headless Codex session for the card
  /api/sessions                                      GET    list every Codex session
  /api/sessions/:id                                  GET    read one Codex session with formatted events
  /api/sessions/start                                POST   start an ad-hoc Codex session
  /api/sessions/:id/message                          POST   send a follow-up turn to a Codex session
  /api/sessions/:id/queue/:message_id/steer          POST   make a queued prompt the next session turn

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

Remote mode: set KANBAN_REMOTE_TOKEN to require a token via ?token=...,
X-PD2-Kanban-Token, Authorization: Bearer, or the pd2kb_token cookie. The
remote starter uses this with a localhost-only Cloudflare tunnel.
"""

import hashlib
import hmac
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import threading
import time
import urllib.parse
import uuid
from http.server import BaseHTTPRequestHandler, HTTPServer

HOST = os.environ.get("KANBAN_HOST", "localhost")
PORT = int(os.environ.get("KANBAN_PORT", "7531"))
BASE = pathlib.Path(__file__).parent
REPO_ROOT = BASE.parent.parent

STATE_PATH = BASE / "state.json"
PARKED_PATH = BASE / "parked.json"
BUGS_PATH = REPO_ROOT / "tools" / "bugs" / "state.json"
BRIEFING_PATH = BASE / "daily-briefing.json"
MEMORY_REVIEW_PATH = BASE / "memory-review.json"
TRACKED_MEMORY_PATH = BASE / "memories.md"
CODEX_MEMORY_PATH = pathlib.Path(os.environ.get(
    "KANBAN_MEMORY_SOURCE",
    str(pathlib.Path.home() / ".codex" / "memories" / "MEMORY.md"),
))
INDEX_PATH = BASE / "index.html"
SESSION_BASE = REPO_ROOT / ".claude" / "scratch" / "kanban-sessions"
SESSION_INDEX_PATH = SESSION_BASE / "index.json"

IDLE_TIMEOUT_S = max(0, int(os.environ.get("KANBAN_IDLE_TIMEOUT_S", "15")))
STARTUP_GRACE_S = max(0, int(os.environ.get("KANBAN_STARTUP_GRACE_S", "30")))
SHUTDOWN_NOW_GRACE_S = max(1, int(os.environ.get("KANBAN_SHUTDOWN_GRACE_S", "6")))
WATCHER_INTERVAL_S = 2.0
REMOTE_TOKEN = os.environ.get("KANBAN_REMOTE_TOKEN", "").strip()
AUTH_COOKIE_NAME = "pd2kb_token"

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
    if IDLE_TIMEOUT_S <= 0:
        return
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
    if IDLE_TIMEOUT_S <= 0:
        return
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


def _request_path(raw_path: str) -> str:
    return urllib.parse.urlsplit(raw_path).path or "/"


def _request_query(raw_path: str):
    return urllib.parse.parse_qs(urllib.parse.urlsplit(raw_path).query, keep_blank_values=True)


def _cookie_value(cookie_header: str, name: str):
    if not cookie_header:
        return None
    for part in cookie_header.split(";"):
        if "=" not in part:
            continue
        k, v = part.split("=", 1)
        if k.strip() == name:
            return urllib.parse.unquote(v.strip())
    return None


def _token_matches(value) -> bool:
    if not REMOTE_TOKEN or not value:
        return False
    return hmac.compare_digest(str(value), REMOTE_TOKEN)


def _default_memory_review():
    return {
        "schema_version": 2,
        "semantic_version": "2.0.0",
        "source_path": str(_memory_source_path()),
        "source_generated_from": "Task Group sections in live memories.md",
        "generated_at": None,
        "review_statuses": ["unreviewed", "keep", "adjust", "remove"],
        "review_items": [],
    }


def _memory_source_path():
    for candidate in (CODEX_MEMORY_PATH, TRACKED_MEMORY_PATH):
        if candidate.exists():
            return candidate
    return TRACKED_MEMORY_PATH


def _memory_review_title_id(title):
    slug = re.sub(r"[^a-z0-9]+", "-", (title or "").lower()).strip("-")
    slug = slug[:48].strip("-") or "memory"
    digest = hashlib.sha1((title or slug).encode("utf-8")).hexdigest()[:10]
    return f"mem-{slug}-{digest}"


def _memory_text_sha(text):
    return hashlib.sha1(text.encode("utf-8")).hexdigest()


def _load_memory_review_markup():
    if not MEMORY_REVIEW_PATH.exists():
        return _default_memory_review()
    data = json.loads(read_text(MEMORY_REVIEW_PATH))
    data.setdefault("review_statuses", ["unreviewed", "keep", "adjust", "remove"])
    data.setdefault("review_items", [])
    return data


def _review_overlay_maps(data):
    by_id = {}
    by_title = {}
    for item in data.get("review_items", []):
        if item.get("id"):
            by_id[item["id"]] = item
        if item.get("title"):
            by_title[item["title"]] = item
    return by_id, by_title


def _parse_memory_review_items(source_path):
    if not source_path.exists():
        return []
    text = source_path.read_text(encoding="utf-8-sig")
    lines = text.splitlines()
    starts = []
    for idx, line in enumerate(lines):
        if re.match(r"^#{1,2}\s+Task Group:\s+", line):
            starts.append(idx)
    items = []
    for pos, start in enumerate(starts):
        end = starts[pos + 1] if pos + 1 < len(starts) else len(lines)
        while end > start and not lines[end - 1].strip():
            end -= 1
        block_lines = lines[start:end]
        if not block_lines:
            continue
        title = re.sub(r"^#{1,2}\s+Task Group:\s*", "", block_lines[0]).strip()
        source_text = "\n".join(block_lines).rstrip() + "\n"
        source_hash = _memory_text_sha(source_text)
        scope = ""
        keywords = []
        in_keywords = False
        for raw in block_lines[1:]:
            line = raw.strip()
            if line.startswith("scope:"):
                scope = line[len("scope:"):].strip()
            if line.lower() == "### keywords":
                in_keywords = True
                continue
            if in_keywords:
                if line.startswith("### ") or line.startswith("## ") or line.startswith("# "):
                    in_keywords = False
                    continue
                if line.startswith("- "):
                    keywords.extend([p.strip(" `") for p in line[2:].split(",") if p.strip()])
                elif not line:
                    in_keywords = False
        items.append({
            "id": _memory_review_title_id(title),
            "title": title,
            "source_path": str(source_path),
            "line_start": start + 1,
            "line_end": end,
            "status": "unreviewed",
            "adjustment_note": "",
            "source_text": source_text,
            "source_hash": source_hash,
            "scope": scope,
            "keywords": keywords,
            "review_applied": False,
            "rebuild_ready": True,
        })
    return items


def _load_memory_review():
    markup = _load_memory_review_markup()
    source_path = _memory_source_path()
    items = _parse_memory_review_items(source_path)
    by_id, by_title = _review_overlay_maps(markup)
    for item in items:
        overlay = by_id.get(item["id"]) or by_title.get(item["title"]) or {}
        for key in ("status", "adjustment_note", "reviewed_at", "updated_at", "review_applied", "review_applied_at"):
            if key in overlay:
                item[key] = overlay.get(key)
        item["source_changed"] = bool(overlay.get("source_hash") and overlay.get("source_hash") != item.get("source_hash"))
    data = {
        "schema_version": 2,
        "semantic_version": "2.0.0",
        "source_path": str(source_path),
        "source_generated_from": "Parsed live # Task Group sections from memories.md",
        "tracked_mirror_path": str(TRACKED_MEMORY_PATH),
        "markup_path": str(MEMORY_REVIEW_PATH),
        "generated_at": _utcnow_iso(),
        "review_statuses": markup.get("review_statuses", ["unreviewed", "keep", "adjust", "remove"]),
        "review_items": items,
    }
    return data


def _memory_review_markup_payload(data):
    return {
        "schema_version": 2,
        "semantic_version": "2.0.0",
        "source_path": data.get("source_path") or str(_memory_source_path()),
        "source_generated_from": "Review markup overlay for live memories.md",
        "generated_at": _utcnow_iso(),
        "review_statuses": data.get("review_statuses", ["unreviewed", "keep", "adjust", "remove"]),
        "review_items": [
            {
                "id": item.get("id"),
                "title": item.get("title", ""),
                "source_hash": item.get("source_hash"),
                "status": item.get("status") or "unreviewed",
                "adjustment_note": item.get("adjustment_note") or "",
                "reviewed_at": item.get("reviewed_at"),
                "updated_at": item.get("updated_at"),
                "review_applied": bool(item.get("review_applied")),
                "review_applied_at": item.get("review_applied_at"),
            }
            for item in data.get("review_items", [])
            if (item.get("status") and item.get("status") != "unreviewed")
            or item.get("adjustment_note")
            or item.get("review_applied")
        ],
    }


def _save_memory_review_atomic(data):
    serialized = json.dumps(_memory_review_markup_payload(data), indent=2, ensure_ascii=True)
    tmp = MEMORY_REVIEW_PATH.with_suffix(MEMORY_REVIEW_PATH.suffix + ".tmp")
    tmp.write_text(serialized, encoding="utf-8")
    os.replace(tmp, MEMORY_REVIEW_PATH)
    return serialized


def _memory_review_summary(data):
    counts = {status: 0 for status in data.get("review_statuses", [])}
    for item in data.get("review_items", []):
        status = item.get("status") or "unreviewed"
        counts[status] = counts.get(status, 0) + 1
    return {
        "total": len(data.get("review_items", [])),
        "counts": counts,
    }


def _find_memory_review_item(data, item_id):
    for item in data.get("review_items", []):
        if item.get("id") == item_id:
            return item
    return None


def _memory_adjustment_replacement(note):
    text = (note or "").strip()
    if re.match(r"^#{1,2}\s+Task Group:\s+", text):
        return text.rstrip() + "\n"
    fenced = re.search(r"```(?:memory|markdown|md)?\s*\n(#{1,2}\s+Task Group:.*?\n)```", text, re.S)
    if fenced:
        return fenced.group(1).rstrip() + "\n"
    return None


def _write_memory_source_text(source_path, text):
    source_path.parent.mkdir(parents=True, exist_ok=True)
    with source_path.open("w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    if source_path.resolve() != TRACKED_MEMORY_PATH.resolve():
        TRACKED_MEMORY_PATH.parent.mkdir(parents=True, exist_ok=True)
        with TRACKED_MEMORY_PATH.open("w", encoding="utf-8", newline="\n") as f:
            f.write(text)


def _apply_memory_review_markup():
    data = _load_memory_review()
    source_path = pathlib.Path(data["source_path"])
    if not source_path.exists():
        raise FileNotFoundError(f"memory source not found: {source_path}")
    text = source_path.read_text(encoding="utf-8-sig")
    lines = text.splitlines(keepends=True)
    applied = []
    skipped = []
    changed = False
    candidates = [
        item for item in data.get("review_items", [])
        if item.get("status") in ("remove", "adjust") and not item.get("review_applied")
    ]
    for item in sorted(candidates, key=lambda x: x.get("line_start") or 0, reverse=True):
        start = max(0, int(item.get("line_start") or 1) - 1)
        end = max(start, int(item.get("line_end") or start))
        status = item.get("status")
        if status == "remove":
            del lines[start:end]
            applied.append({"id": item.get("id"), "title": item.get("title"), "action": "remove"})
            item["review_applied"] = True
            item["review_applied_at"] = _utcnow_iso()
            changed = True
        elif status == "adjust":
            replacement = _memory_adjustment_replacement(item.get("adjustment_note"))
            if replacement is None:
                skipped.append({
                    "id": item.get("id"),
                    "title": item.get("title"),
                    "reason": "adjustment note is not a full replacement block starting with '# Task Group:'",
                })
                continue
            lines[start:end] = [replacement]
            applied.append({"id": item.get("id"), "title": item.get("title"), "action": "replace"})
            item["review_applied"] = True
            item["review_applied_at"] = _utcnow_iso()
            changed = True
    if changed:
        new_text = "".join(lines)
        if new_text and not new_text.endswith("\n"):
            new_text += "\n"
        _write_memory_source_text(source_path, new_text)
    _save_memory_review_atomic(data)
    return {
        "ok": True,
        "changed": changed,
        "applied": applied,
        "skipped": skipped,
        "source_path": str(source_path),
        "tracked_mirror_path": str(TRACKED_MEMORY_PATH),
    }


# ---------- decision-request mechanism helpers (c121) ----------

def _save_state_atomic(data):
    serialized = json.dumps(data, indent=2, ensure_ascii=False)
    tmp = STATE_PATH.with_suffix(STATE_PATH.suffix + ".tmp")
    tmp.write_text(serialized, encoding="utf-8")
    os.replace(tmp, STATE_PATH)


def _load_state():
    return json.loads(read_text(STATE_PATH))


def _find_card(data, card_id):
    for card in data.get("cards", []):
        if card.get("id") == card_id:
            return card
    return None


def _next_question_id(data):
    """Monotonic q-NNN across the whole state file."""
    max_n = 0
    for card in data.get("cards", []):
        for q in card.get("open_questions", []) or []:
            qid = q.get("id", "")
            if qid.startswith("q-"):
                try:
                    n = int(qid[2:])
                except ValueError:
                    continue
                if n > max_n:
                    max_n = n
    return f"q-{max_n + 1:03d}"


def _utcnow_iso():
    return __import__("datetime").datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")


def _question_is_resolved(q):
    if not q.get("answered_date"):
        return False
    if q.get("answer_type") == "choice":
        return True
    if q.get("answer_type") == "custom":
        return bool(q.get("interpretation_confirmed"))
    return False


def _question_status(q):
    if _question_is_resolved(q):
        return "resolved"
    if q.get("answer_type") == "custom":
        if q.get("interpretation"):
            return "needs_confirmation"
        return "needs_interpretation"
    return "awaiting_answer"


def _question_selected_choice(q):
    answer = q.get("answer")
    if q.get("answer_type") != "choice" or answer is None:
        return None
    for ch in q.get("choices", []) or []:
        if ch.get("id") == answer:
            return ch
    return None


def _question_entry(card, q):
    status = _question_status(q)
    selected_choice = _question_selected_choice(q)
    return {
        "card_id": card.get("id"),
        "card_title": card.get("title", ""),
        "card_column": card.get("column", ""),
        "card_pillar": card.get("pillar", ""),
        "question_id": q.get("id"),
        "question": q.get("question", ""),
        "asked_by": q.get("asked_by", ""),
        "asked_date": q.get("asked_date", ""),
        "choices": q.get("choices", []),
        "allow_custom": q.get("allow_custom", True),
        "answer": q.get("answer"),
        "answer_type": q.get("answer_type"),
        "answered_date": q.get("answered_date"),
        "custom_text": q.get("custom_text"),
        "interpretation": q.get("interpretation"),
        "interpretation_confirmed": bool(q.get("interpretation_confirmed")),
        "selected_choice": selected_choice,
        "resolved": _question_is_resolved(q),
        "status": status,
        "needs_interpretation": status == "needs_interpretation",
        "needs_confirmation": status == "needs_confirmation",
        "follow_up_question_ids": q.get("follow_up_question_ids", []),
    }


def _card_blocked_q_ids(card):
    return [q.get("id") for q in (card.get("open_questions") or []) if not _question_is_resolved(q)]


def _find_question(card, q_id):
    for q in card.get("open_questions") or []:
        if q.get("id") == q_id:
            return q
    return None


# ---------- pending-completion mechanism helpers (c123) ----------

def _card_max_order(data, column):
    return max(
        (c.get("order", 0) for c in data.get("cards", []) if c.get("column") == column),
        default=0,
    )


def _append_note(card, line):
    existing = (card.get("notes") or "").rstrip()
    stamped = f"[{_utcnow_iso()}] {line}"
    card["notes"] = f"{existing}\n{stamped}".strip() if existing else stamped


# ---------- card-scoped Codex session helpers ----------

def _safe_slug(value, fallback="session"):
    slug = re.sub(r"[^A-Za-z0-9_.-]+", "-", str(value or "")).strip("-._")
    return slug[:72] or fallback


def _decode_url_segment(value):
    return urllib.parse.unquote(value or "")


def _load_session_index():
    if not SESSION_INDEX_PATH.exists():
        return {"sessions": []}
    try:
        data = json.loads(SESSION_INDEX_PATH.read_text(encoding="utf-8"))
    except Exception:
        data = {}
    if not isinstance(data.get("sessions"), list):
        data["sessions"] = []
    return data


def _save_session_index(data):
    SESSION_BASE.mkdir(parents=True, exist_ok=True)
    tmp = SESSION_INDEX_PATH.with_suffix(SESSION_INDEX_PATH.suffix + ".tmp")
    tmp.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
    os.replace(tmp, SESSION_INDEX_PATH)


def _tail_text(path, max_bytes=12000):
    path = pathlib.Path(path)
    if not path.exists():
        return ""
    try:
        size = path.stat().st_size
        with path.open("rb") as f:
            if size > max_bytes:
                f.seek(size - max_bytes)
            data = f.read()
        return data.decode("utf-8", errors="replace")
    except Exception as exc:
        return f"[unable to read {path.name}: {exc}]"


def _read_jsonl_events(path, max_bytes=240000, max_events=240):
    path = pathlib.Path(path)
    if not path.exists():
        return []
    try:
        size = path.stat().st_size
        with path.open("rb") as f:
            if size > max_bytes:
                f.seek(size - max_bytes)
            data = f.read()
        text = data.decode("utf-8", errors="replace")
        lines = text.splitlines()
        if size > max_bytes and lines:
            lines = lines[1:]
        events = []
        for line in lines:
            line = line.strip()
            if not line:
                continue
            try:
                events.append(json.loads(line))
            except Exception:
                events.append({"type": "raw", "text": line})
        return events[-max_events:]
    except Exception as exc:
        return [{"type": "read_error", "text": f"unable to read {path.name}: {exc}"}]


def _extract_thread_id(path):
    path = pathlib.Path(path)
    if not path.exists():
        return ""
    try:
        with path.open("r", encoding="utf-8", errors="replace") as f:
            for _ in range(400):
                line = f.readline()
                if not line:
                    break
                try:
                    event = json.loads(line)
                except Exception:
                    continue
                if event.get("type") == "thread.started" and event.get("thread_id"):
                    return event.get("thread_id")
    except Exception:
        return ""
    return ""


def _pid_is_running(pid):
    try:
        pid = int(pid)
    except Exception:
        return False
    if pid <= 0:
        return False
    if os.name == "nt":
        try:
            import ctypes
            from ctypes import wintypes
            PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
            STILL_ACTIVE = 259
            handle = ctypes.windll.kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
            if not handle:
                return False
            code = wintypes.DWORD()
            try:
                if not ctypes.windll.kernel32.GetExitCodeProcess(handle, ctypes.byref(code)):
                    return False
                return code.value == STILL_ACTIVE
            finally:
                ctypes.windll.kernel32.CloseHandle(handle)
        except Exception:
            return False
    try:
        os.kill(pid, 0)
        return True
    except PermissionError:
        return True
    except OSError:
        return False


def _new_session_message_id(prefix="msg"):
    stamp = __import__("datetime").datetime.utcnow().strftime("%Y%m%d-%H%M%S")
    return f"{prefix}-{stamp}-{uuid.uuid4().hex[:6]}"


def _append_session_event(entry, event):
    stdout_path = pathlib.Path(entry.get("stdout_path", ""))
    if not stdout_path:
        return
    try:
        stdout_path.parent.mkdir(parents=True, exist_ok=True)
        with stdout_path.open("a", encoding="utf-8") as stdout_f:
            stdout_f.write(json.dumps(event, ensure_ascii=False) + "\n")
    except Exception:
        pass


def _refresh_session_statuses(data, drain_queues=True):
    changed = False
    for entry in data.get("sessions", []):
        if not entry.get("thread_id"):
            thread_id = _extract_thread_id(entry.get("stdout_path", ""))
            if thread_id:
                entry["thread_id"] = thread_id
                changed = True
        turn_running = entry.get("status") == "running" and _pid_is_running(entry.get("pid"))
        if not turn_running and entry.get("status") == "running":
            entry["status"] = "finished"
            entry["finished_at"] = entry.get("finished_at") or _utcnow_iso()
            changed = True
        if drain_queues and not turn_running and entry.get("queued_messages"):
            launched, err = _drain_session_queue(entry)
            if launched or err:
                changed = True
    return changed


def _session_public(entry, include_logs=True):
    public = dict(entry)
    public.pop("prompt_path", None)
    public["queued_count"] = len(public.get("queued_messages") or [])
    public["paths"] = {
        "session_dir": entry.get("session_dir", ""),
        "last_message": entry.get("last_message_path", ""),
        "stdout": entry.get("stdout_path", ""),
        "stderr": entry.get("stderr_path", ""),
    }
    if include_logs:
        public["last_message"] = _tail_text(entry.get("last_message_path", ""), max_bytes=10000)
        public["stdout_tail"] = _tail_text(entry.get("stdout_path", ""), max_bytes=10000)
        public["stderr_tail"] = _tail_text(entry.get("stderr_path", ""), max_bytes=10000)
        public["events"] = _read_jsonl_events(entry.get("stdout_path", ""))
    return public


def _all_session_entries(include_logs=False):
    data = _load_session_index()
    if _refresh_session_statuses(data):
        _save_session_index(data)
    entries = list(data.get("sessions", []))
    entries.sort(key=lambda e: e.get("started_at") or "", reverse=True)
    return data, [_session_public(e, include_logs=include_logs) for e in entries]


def _find_session_entry(data, session_id):
    for entry in data.get("sessions", []):
        if entry.get("session_id") == session_id:
            return entry
    return None


def _session_entry_by_id(session_id, drain_queues=True):
    data = _load_session_index()
    changed = _refresh_session_statuses(data, drain_queues=drain_queues)
    entry = _find_session_entry(data, session_id)
    if changed:
        _save_session_index(data)
    return data, entry


def _card_session_entries(card_id):
    data = _load_session_index()
    if _refresh_session_statuses(data):
        _save_session_index(data)
    entries = [e for e in data.get("sessions", []) if e.get("card_id") == card_id]
    entries.sort(key=lambda e: e.get("started_at") or "", reverse=True)
    return data, entries


def _find_codex_exe():
    env_path = os.environ.get("KANBAN_CODEX_EXE", "").strip()
    candidates = [env_path, shutil.which("codex"), shutil.which("codex.exe")]
    if os.name == "nt":
        base = pathlib.Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "WindowsApps"
        try:
            for app_dir in sorted(base.glob("OpenAI.Codex_*_x64__*"), reverse=True):
                candidates.append(str(app_dir / "app" / "resources" / "codex.exe"))
                candidates.append(str(app_dir / "app" / "resources" / "codex"))
        except Exception:
            pass
    for candidate in candidates:
        if not candidate:
            continue
        p = pathlib.Path(candidate)
        if p.exists() or shutil.which(str(candidate)):
            return str(candidate)
    return None


def _session_card_summary(card):
    return {
        "id": card.get("id"),
        "title": card.get("title", ""),
        "pillar": card.get("pillar", ""),
        "column": card.get("column", ""),
        "priority": card.get("priority"),
        "flag": card.get("flag"),
        "order": card.get("order"),
    }


def _build_card_session_prompt(kanban, card, session_id, request_note, running_entries):
    active_cards = [
        _session_card_summary(c)
        for c in kanban.get("cards", [])
        if c.get("column") == "active" and c.get("id") != card.get("id")
    ]
    running_cards = [
        {
            "session_id": e.get("session_id"),
            "card_id": e.get("card_id"),
            "card_title": e.get("card_title"),
            "started_at": e.get("started_at"),
            "status": e.get("status"),
        }
        for e in running_entries
        if e.get("status") == "running" and e.get("card_id") != card.get("id")
    ]

    lines = [
        "You are a headless Codex coding session launched from the PD2 mobile Kanban board.",
        "",
        f"Session id: {session_id}",
        f"Workspace: {REPO_ROOT}",
        "",
        "Target card:",
        json.dumps(card, indent=2, ensure_ascii=False),
        "",
        "Other cards currently in the Active column:",
        json.dumps(active_cards, indent=2, ensure_ascii=False),
        "",
        "Other card sessions currently running at launch time:",
        json.dumps(running_cards, indent=2, ensure_ascii=False),
        "",
        "Instructions:",
        "- Work on the target card only unless the repository context makes a narrow support edit necessary.",
        "- Before editing, read the project context files required by AGENTS.md and respect existing uncommitted changes.",
        "- Avoid touching files that are clearly owned by another running card session unless necessary for correctness.",
        "- Keep context, Kanban, and UNRELEASED.md updated when the work changes project state.",
        "- Verify with the narrowest practical parser, test, or build check before reporting completion.",
        "- End with a concise status report and include any manual retest Mike still needs to do.",
    ]
    if request_note:
        lines.extend(["", "Launch note from Mike:", str(request_note).strip()])
    return "\n".join(lines).strip() + "\n"


def _running_session_summaries(running_entries, exclude_session_id=None, exclude_card_id=None):
    summaries = []
    for entry in running_entries:
        if entry.get("status") != "running":
            continue
        if exclude_session_id and entry.get("session_id") == exclude_session_id:
            continue
        if exclude_card_id and entry.get("card_id") == exclude_card_id:
            continue
        summaries.append({
            "session_id": entry.get("session_id"),
            "scope": entry.get("scope") or ("card" if entry.get("card_id") else "general"),
            "card_id": entry.get("card_id"),
            "card_title": entry.get("card_title"),
            "title": entry.get("title") or entry.get("card_title"),
            "started_at": entry.get("started_at"),
            "status": entry.get("status"),
        })
    return summaries


def _build_general_session_prompt(kanban, session_id, title, request_note, running_entries):
    active_cards = [
        _session_card_summary(c)
        for c in kanban.get("cards", [])
        if c.get("column") == "active"
    ]
    lines = [
        "You are a headless Codex coding session launched from the PD2 mobile Kanban board.",
        "",
        f"Session id: {session_id}",
        f"Workspace: {REPO_ROOT}",
        "",
        "This session is not tied to a Kanban card yet.",
        f"Task title: {title}",
        "",
        "Task/request from Mike:",
        str(request_note or title).strip(),
        "",
        "Cards currently in the Active column:",
        json.dumps(active_cards, indent=2, ensure_ascii=False),
        "",
        "Other sessions currently running at launch time:",
        json.dumps(_running_session_summaries(running_entries, exclude_session_id=session_id), indent=2, ensure_ascii=False),
        "",
        "Instructions:",
        "- First decide whether this task should create or update a Kanban card. If it changes project state, update the board/context/release notes as appropriate.",
        "- Before editing, read the project context files required by AGENTS.md and respect existing uncommitted changes.",
        "- Avoid touching files that are clearly owned by another running session unless necessary for correctness.",
        "- Verify with the narrowest practical parser, test, or build check before reporting completion.",
        "- End with a concise status report and include any manual retest Mike still needs to do.",
    ]
    return "\n".join(lines).strip() + "\n"


def _launch_codex_exec(entry, prompt, prompt_path, stdout_path, stderr_path, last_message_path, mode="start"):
    codex = _find_codex_exe()
    if not codex:
        return None, "codex executable not found. Set KANBAN_CODEX_EXE or install Codex CLI."

    prompt_path.write_text(prompt, encoding="utf-8")
    if mode == "resume":
        thread_id = entry.get("thread_id") or _extract_thread_id(entry.get("stdout_path", ""))
        if not thread_id:
            return None, "Codex thread id is not available yet. Wait for the session to emit thread.started, then retry."
        cmd = [
            codex,
            "exec",
            "resume",
            "--json",
            "--output-last-message",
            str(last_message_path),
            thread_id,
            "-",
        ]
    else:
        cmd = [
            codex,
            "exec",
            "--json",
            "--sandbox",
            "workspace-write",
            "--cd",
            str(REPO_ROOT),
            "--output-last-message",
            str(last_message_path),
            "-",
        ]
    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    popen_kwargs = {"creationflags": creationflags} if creationflags else {}
    try:
        with prompt_path.open("rb") as stdin_f, stdout_path.open("ab") as stdout_f, stderr_path.open("ab") as stderr_f:
            proc = subprocess.Popen(
                cmd,
                cwd=str(REPO_ROOT),
                stdin=stdin_f,
                stdout=stdout_f,
                stderr=stderr_f,
                **popen_kwargs,
            )
        return proc, None
    except Exception as exc:
        return None, str(exc)


def _create_session_entry(index, session_id, scope, title, prompt, card=None):
    session_dir = SESSION_BASE / session_id
    session_dir.mkdir(parents=True, exist_ok=True)
    prompt_path = session_dir / "prompt.md"
    stdout_path = session_dir / "stdout.jsonl"
    stderr_path = session_dir / "stderr.log"
    last_message_path = session_dir / "last-message.md"

    entry = {
        "session_id": session_id,
        "scope": scope,
        "card_id": card.get("id") if card else None,
        "card_title": card.get("title", "") if card else "",
        "title": title,
        "status": "starting",
        "pid": 0,
        "thread_id": "",
        "started_at": _utcnow_iso(),
        "finished_at": None,
        "last_user_message_at": None,
        "session_dir": str(session_dir),
        "prompt_path": str(prompt_path),
        "stdout_path": str(stdout_path),
        "stderr_path": str(stderr_path),
        "last_message_path": str(last_message_path),
        "messages": [],
        "command": "codex exec --json --sandbox workspace-write",
    }
    proc, err = _launch_codex_exec(entry, prompt, prompt_path, stdout_path, stderr_path, last_message_path, mode="start")
    if err:
        return None, err
    entry["status"] = "running"
    entry["pid"] = proc.pid
    index.setdefault("sessions", []).append(entry)
    return entry, None


def _start_card_session(card_id, payload):
    payload = payload or {}
    kanban = _load_state()
    card = _find_card(kanban, card_id)
    if card is None:
        return 404, {"error": f"card {card_id} not found"}

    index = _load_session_index()
    changed = _refresh_session_statuses(index)
    for entry in index.get("sessions", []):
        if entry.get("card_id") == card_id and entry.get("status") == "running":
            if changed:
                _save_session_index(index)
            entries = [e for e in index.get("sessions", []) if e.get("card_id") == card_id]
            entries.sort(key=lambda e: e.get("started_at") or "", reverse=True)
            return 200, {
                "ok": True,
                "already_running": True,
                "session": _session_public(entry),
                "sessions": [_session_public(e) for e in entries],
            }

    stamp = __import__("datetime").datetime.utcnow().strftime("%Y%m%d-%H%M%S")
    session_id = f"{_safe_slug(card_id)}-{stamp}-{uuid.uuid4().hex[:6]}"
    prompt = _build_card_session_prompt(
        kanban,
        card,
        session_id,
        payload.get("note", ""),
        index.get("sessions", []),
    )
    entry, err = _create_session_entry(index, session_id, "card", card.get("title", ""), prompt, card=card)
    if err:
        if changed:
            _save_session_index(index)
        return 503, {"error": err}
    _save_session_index(index)
    entries = [e for e in index.get("sessions", []) if e.get("card_id") == card_id]
    entries.sort(key=lambda e: e.get("started_at") or "", reverse=True)
    return 200, {
        "ok": True,
        "already_running": False,
        "session": _session_public(entry),
        "sessions": [_session_public(e) for e in entries],
    }


def _start_general_session(payload):
    payload = payload or {}
    title = (payload.get("title") or "").strip()
    note = (payload.get("note") or payload.get("message") or "").strip()
    if not title and note:
        title = re.sub(r"\s+", " ", note)[:80].strip()
    if not title:
        return 400, {"error": "title or note required"}
    kanban = _load_state()
    index = _load_session_index()
    changed = _refresh_session_statuses(index)
    stamp = __import__("datetime").datetime.utcnow().strftime("%Y%m%d-%H%M%S")
    session_id = f"adhoc-{stamp}-{uuid.uuid4().hex[:6]}"
    prompt = _build_general_session_prompt(kanban, session_id, title, note or title, index.get("sessions", []))
    entry, err = _create_session_entry(index, session_id, "general", title, prompt, card=None)
    if err:
        if changed:
            _save_session_index(index)
        return 503, {"error": err}
    _save_session_index(index)
    return 200, {
        "ok": True,
        "session": _session_public(entry),
    }


def _format_choice_answer_message(answer):
    question = (answer.get("question") or answer.get("prompt") or "").strip()
    question_id = (answer.get("question_id") or answer.get("id") or "").strip()
    header = (answer.get("header") or "").strip()
    selected = (answer.get("selected_label") or answer.get("selected_value") or answer.get("label") or answer.get("value") or "").strip()
    description = (answer.get("selected_description") or answer.get("description") or "").strip()
    freeform = (answer.get("freeform") or answer.get("text") or "").strip()
    lines = []
    if header:
        lines.append(f"Question group: {header}")
    if question_id:
        lines.append(f"Question id: {question_id}")
    if question:
        lines.append(f"Question: {question}")
    if selected:
        lines.append(f"Selected answer: {selected}")
    if description:
        lines.append(f"Selected answer detail: {description}")
    if freeform and freeform != selected:
        lines.append(f"Additional response: {freeform}")
    return "\n".join(lines).strip()


def _build_session_message_prompt(record):
    text = (record.get("text") or "").strip()
    if record.get("kind") == "choice_answer":
        intro = "Mike answered a choice question from the mobile Kanban session interface."
        closing = "Use the selected answer for the pending question and continue the same session."
    elif record.get("steered_at"):
        intro = "Mike selected this queued prompt to steer the next turn from the mobile Kanban session interface."
        closing = "Use this selected queued prompt as the next steering instruction for the same session."
    elif record.get("queued_at"):
        intro = "Mike sent this queued follow-up from the mobile Kanban session interface."
        closing = "Continue the same session using this queued prompt. Keep the response concise unless the task requires implementation details."
    else:
        intro = "Mike sent this follow-up from the mobile Kanban session interface."
        closing = "Continue the same session. Keep the response concise unless the task requires implementation details."
    return "\n".join([
        intro,
        "",
        text,
        "",
        closing,
    ]).strip() + "\n"


def _message_record_from_payload(message_id, message, answer=None, status="queued"):
    record = {
        "id": message_id,
        "role": "user",
        "kind": "choice_answer" if answer else "message",
        "status": status,
        "text": message,
        "created_at": _utcnow_iso(),
    }
    if status == "queued":
        record["queued_at"] = record["created_at"]
    if answer:
        record["answer"] = answer
    return record


def _launch_session_message(entry, record):
    session_id = entry.get("session_id", "")
    session_dir = pathlib.Path(entry.get("session_dir", SESSION_BASE / session_id))
    messages_dir = session_dir / "messages"
    messages_dir.mkdir(parents=True, exist_ok=True)
    prompt_path = messages_dir / f"{record.get('id')}.md"
    stdout_path = pathlib.Path(entry.get("stdout_path", session_dir / "stdout.jsonl"))
    stderr_path = pathlib.Path(entry.get("stderr_path", session_dir / "stderr.log"))
    last_message_path = pathlib.Path(entry.get("last_message_path", session_dir / "last-message.md"))
    prompt = _build_session_message_prompt(record)
    proc, err = _launch_codex_exec(entry, prompt, prompt_path, stdout_path, stderr_path, last_message_path, mode="resume")
    if err:
        return None, err

    sent_record = dict(record)
    sent_record["status"] = "sent"
    sent_record["sent_at"] = _utcnow_iso()
    sent_record.pop("queued_at", None)
    event = {
        "type": "mobile.choice_answer" if sent_record.get("kind") == "choice_answer" else "mobile.user_message",
        "message_id": sent_record.get("id"),
        "created_at": sent_record["sent_at"],
        "text": sent_record.get("text", ""),
    }
    if record.get("queued_at"):
        event["from_queue"] = True
        event["queued_at"] = record.get("queued_at")
    if sent_record.get("answer"):
        event["answer"] = sent_record.get("answer")
    _append_session_event(entry, event)

    entry.setdefault("messages", []).append(sent_record)
    entry["status"] = "running"
    entry["pid"] = proc.pid
    entry["finished_at"] = None
    entry["last_user_message_at"] = sent_record["sent_at"]
    entry["command"] = "codex exec resume --json"
    entry["queue_error"] = ""
    return proc, None


def _queue_session_message(entry, record):
    record = dict(record)
    record["status"] = "queued"
    record["queued_at"] = record.get("queued_at") or _utcnow_iso()
    entry.setdefault("queued_messages", []).append(record)
    event = {
        "type": "mobile.queued_message",
        "message_id": record.get("id"),
        "created_at": record["queued_at"],
        "text": record.get("text", ""),
        "kind": record.get("kind", "message"),
        "queue_position": len(entry.get("queued_messages") or []),
    }
    if record.get("answer"):
        event["answer"] = record.get("answer")
    _append_session_event(entry, event)
    return record


def _drain_session_queue(entry):
    if entry.get("status") == "running" and _pid_is_running(entry.get("pid")):
        return False, None
    queue = entry.get("queued_messages") or []
    if not queue:
        return False, None
    record = dict(queue[0])
    proc, err = _launch_session_message(entry, record)
    if err:
        entry["queue_error"] = err
        return False, err
    entry["queued_messages"] = queue[1:]
    return True, None


def _message_session(session_id, payload):
    payload = payload or {}
    answer = payload.get("answer") if isinstance(payload.get("answer"), dict) else None
    message = (payload.get("message") or "").strip()
    if answer:
        message = _format_choice_answer_message(answer)
    if not message:
        return 400, {"error": "message or answer required"}
    data, entry = _session_entry_by_id(session_id)
    if entry is None:
        return 404, {"error": f"session {session_id} not found"}

    message_id = _new_session_message_id()
    record = _message_record_from_payload(message_id, message, answer=answer, status="sent")
    turn_running = entry.get("status") == "running" and _pid_is_running(entry.get("pid"))
    if turn_running or entry.get("queued_messages"):
        queued = _queue_session_message(entry, record)
        _save_session_index(data)
        return 200, {
            "ok": True,
            "queued": True,
            "queued_message": queued,
            "session": _session_public(entry),
        }

    proc, err = _launch_session_message(entry, record)
    if err:
        _save_session_index(data)
        status = 409 if "thread id" in err.lower() else 503
        return status, {"error": err}

    _save_session_index(data)
    return 200, {
        "ok": True,
        "queued": False,
        "session": _session_public(entry),
    }


def _steer_session_queue(session_id, message_id):
    data, entry = _session_entry_by_id(session_id, drain_queues=False)
    if entry is None:
        return 404, {"error": f"session {session_id} not found"}
    queue = entry.get("queued_messages") or []
    found_index = -1
    for i, item in enumerate(queue):
        if item.get("id") == message_id:
            found_index = i
            break
    if found_index < 0:
        return 404, {"error": f"queued message {message_id} not found"}

    selected = dict(queue.pop(found_index))
    selected["steered_at"] = _utcnow_iso()
    selected["status"] = "queued"
    queue.insert(0, selected)
    entry["queued_messages"] = queue
    _append_session_event(entry, {
        "type": "mobile.queue_steer",
        "message_id": selected.get("id"),
        "created_at": selected["steered_at"],
        "text": selected.get("text", ""),
    })

    turn_running = entry.get("status") == "running" and _pid_is_running(entry.get("pid"))
    launched = False
    err = None
    if not turn_running:
        launched, err = _drain_session_queue(entry)
    _save_session_index(data)
    if err:
        return 503, {
            "error": err,
            "session": _session_public(entry),
        }
    return 200, {
        "ok": True,
        "steered": True,
        "launched": launched,
        "session": _session_public(entry),
    }


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        msg = fmt % args
        if "/heartbeat" in msg:
            return  # heartbeat fires every 5s per tab; do not flood stdout
        print(f"[kanban] {self.address_string()} - {msg}")

    def send_cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, PATCH, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-PD2-Kanban-Token")

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

    def request_token(self):
        header_token = self.headers.get("X-PD2-Kanban-Token", "").strip()
        if header_token:
            return header_token
        auth = self.headers.get("Authorization", "").strip()
        if auth.lower().startswith("bearer "):
            return auth[7:].strip()
        return _cookie_value(self.headers.get("Cookie", ""), AUTH_COOKIE_NAME)

    def accept_query_token(self, raw_path, clean_path):
        query = _request_query(raw_path)
        token = (query.get("token") or [None])[0]
        if not _token_matches(token):
            return False
        quoted = urllib.parse.quote(token, safe="")
        self.send_response(302)
        self.send_header("Location", clean_path or "/")
        self.send_header(
            "Set-Cookie",
            f"{AUTH_COOKIE_NAME}={quoted}; Path=/; HttpOnly; SameSite=Lax; Max-Age=43200",
        )
        self.send_cors()
        self.end_headers()
        return True

    def require_auth(self):
        raw_path = self.path
        clean_path = _request_path(raw_path)
        if not REMOTE_TOKEN:
            self.path = clean_path
            return True
        if self.accept_query_token(raw_path, clean_path):
            return False
        self.path = clean_path
        if _token_matches(self.request_token()):
            return True
        if clean_path.startswith("/api/"):
            self.reply_json(401, {"error": "kanban remote token required"})
        else:
            body = (
                "PD2 Kanban remote token required.\n"
                "Open the remote URL generated by devtools/start-kanban-remote.ps1.\n"
            ).encode("utf-8")
            self.reply_text(401, body)
        return False

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_cors()
        self.end_headers()

    def do_GET(self):
        if not self.require_auth():
            return
        path = _request_path(self.path)

        if path in ("/", "/index.html"):
            content = INDEX_PATH.read_bytes()
            self.reply_text(200, content, content_type="text/html; charset=utf-8")
            return

        if path == "/api/state":
            self.reply_text(200, read_text(STATE_PATH).encode("utf-8"), content_type="application/json; charset=utf-8")
            return

        if path == "/api/parked":
            self.reply_text(200, read_text(PARKED_PATH).encode("utf-8"), content_type="application/json; charset=utf-8")
            return

        if path == "/api/bugs":
            self.reply_text(200, read_text(BUGS_PATH).encode("utf-8"), content_type="application/json; charset=utf-8")
            return

        if path == "/api/briefing":
            self.reply_text(200, read_text(BRIEFING_PATH).encode("utf-8"), content_type="application/json; charset=utf-8")
            return

        if path == "/api/memory-review":
            data = _load_memory_review()
            data["summary"] = _memory_review_summary(data)
            self.reply_json(200, data)
            return

        if path == "/api/sessions":
            _, sessions = _all_session_entries(include_logs=False)
            self.reply_json(200, {
                "ok": True,
                "sessions": sessions,
            })
            return

        if path.startswith("/api/sessions/"):
            session_id = _decode_url_segment(path[len("/api/sessions/"):])
            _, entry = _session_entry_by_id(session_id)
            if entry is None:
                self.reply_json(404, {"error": f"session {session_id} not found"})
                return
            self.reply_json(200, {
                "ok": True,
                "session": _session_public(entry),
            })
            return

        if path.startswith("/api/cards/") and path.endswith("/sessions"):
            card_id = _decode_url_segment(path[len("/api/cards/"):-len("/sessions")])
            _, entries = _card_session_entries(card_id)
            self.reply_json(200, {
                "ok": True,
                "card_id": card_id,
                "sessions": [_session_public(e) for e in entries],
            })
            return

        if path == "/api/open-questions":
            data = _load_state()
            questions = []
            blocked_cards = 0
            for card in data.get("cards", []):
                unresolved = [q for q in (card.get("open_questions") or []) if not _question_is_resolved(q)]
                if not unresolved:
                    continue
                blocked_cards += 1
                for q in unresolved:
                    questions.append(_question_entry(card, q))
            self.reply_json(200, {
                "ok": True,
                "questions": questions,
                "blocked_card_count": blocked_cards,
                "open_question_count": len(questions),
            })
            return

        if path == "/api/decision-requests":
            data = _load_state()
            requests = []
            for card in data.get("cards", []):
                for q in (card.get("open_questions") or []):
                    requests.append(_question_entry(card, q))
            status_rank = {
                "needs_confirmation": 0,
                "needs_interpretation": 1,
                "awaiting_answer": 2,
                "resolved": 3,
            }
            requests.sort(key=lambda e: e.get("answered_date") or e.get("asked_date") or "", reverse=True)
            requests.sort(key=lambda e: status_rank.get(e.get("status"), 9))
            unresolved = [r for r in requests if not r.get("resolved")]
            answered = [r for r in requests if r.get("answered_date")]
            self.reply_json(200, {
                "ok": True,
                "requests": requests,
                "request_count": len(requests),
                "unresolved_question_count": len(unresolved),
                "answered_question_count": len(answered),
                "needs_interpretation_count": len([r for r in requests if r.get("needs_interpretation")]),
                "needs_confirmation_count": len([r for r in requests if r.get("needs_confirmation")]),
            })
            return

        if path.startswith("/api/cards/") and path.endswith("/blocked-status"):
            card_id = path[len("/api/cards/"):-len("/blocked-status")]
            data = _load_state()
            card = _find_card(data, card_id)
            if card is None:
                self.reply_json(404, {"error": f"card {card_id} not found"})
                return
            blocked_ids = _card_blocked_q_ids(card)
            self.reply_json(200, {
                "ok": True,
                "card_id": card_id,
                "blocked": len(blocked_ids) > 0,
                "blocked_question_ids": blocked_ids,
                "unresolved_count": len(blocked_ids),
            })
            return

        if path == "/api/pending-completions":
            data = _load_state()
            pending = []
            for card in data.get("cards", []):
                pc = card.get("pending_completion")
                if not pc:
                    continue
                pending.append({
                    "card_id": card.get("id"),
                    "card_title": card.get("title", ""),
                    "card_column": card.get("column", ""),
                    "card_pillar": card.get("pillar", ""),
                    "marked_at": pc.get("marked_at"),
                    "marked_by": pc.get("marked_by"),
                    "summary": pc.get("summary", ""),
                    "verify_notes": pc.get("verify_notes", ""),
                    "evidence_refs": pc.get("evidence_refs", []),
                    "expected_artifacts": pc.get("expected_artifacts", []),
                })
            pending.sort(key=lambda e: e.get("marked_at") or "")
            self.reply_json(200, {
                "ok": True,
                "pending": pending,
                "pending_count": len(pending),
            })
            return

        self.send_error(404)

    def do_POST(self):
        try:
            if not self.require_auth():
                return
            path = _request_path(self.path)

            if path == "/heartbeat":
                heartbeat_now()
                # drain body if any (sendBeacon may send a tiny payload)
                self.read_body()
                self.reply_json(200, {"ok": True})
                return

            if path == "/shutdown-now":
                # cooperative: backdate the heartbeat clock so the watcher
                # fires soon unless another tab keeps the server alive.
                self.read_body()
                heartbeat_soft_shutdown()
                self.reply_json(200, {"ok": True})
                return

            if path == "/api/state":
                written = write_json_atomic(STATE_PATH, self.read_body())
                print(f"[kanban] state.json written ({len(written)} bytes)")
                self.reply_json(200, {"ok": True})
                return

            if path == "/api/parked":
                written = write_json_atomic(PARKED_PATH, self.read_body())
                print(f"[kanban] parked.json written ({len(written)} bytes)")
                self.reply_json(200, {"ok": True})
                return

            if path == "/api/bugs":
                BUGS_PATH.parent.mkdir(parents=True, exist_ok=True)
                written = write_json_atomic(BUGS_PATH, self.read_body())
                print(f"[kanban] bugs/state.json written ({len(written)} bytes)")
                self.reply_json(200, {"ok": True})
                return

            if path == "/api/memory-review":
                data = json.loads(self.read_body() or b"{}")
                written = _save_memory_review_atomic(data)
                print(f"[kanban] memory-review markup written ({len(written)} bytes)")
                self.reply_json(200, {"ok": True, "summary": _memory_review_summary(data)})
                return

            if path == "/api/memory-review/apply":
                self.read_body()
                result = _apply_memory_review_markup()
                print(f"[kanban] memory review apply: {len(result.get('applied', []))} applied, {len(result.get('skipped', []))} skipped")
                self.reply_json(200, result)
                return

            if path == "/api/park":
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

            if path == "/api/unpark":
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

            if self.path == "/api/bugs/delete":
                payload = json.loads(self.read_body() or b"{}")
                bid = payload.get("bug_id")
                if not bid:
                    self.reply_json(400, {"error": "bug_id required"})
                    return
                bugs = pe.load_json(BUGS_PATH)
                before = len(bugs.get("bugs", []))
                bugs["bugs"] = [bug for bug in bugs.get("bugs", []) if bug.get("id") != bid]
                if len(bugs["bugs"]) == before:
                    self.reply_json(404, {"error": "bug not found"})
                    return
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

            if path == "/api/sessions/start":
                payload = json.loads(self.read_body() or b"{}")
                status, result = _start_general_session(payload)
                if status == 200:
                    print(f"[kanban] adhoc session start session={result.get('session', {}).get('session_id')}")
                self.reply_json(status, result)
                return

            if path.startswith("/api/sessions/") and "/queue/" in path and path.endswith("/steer"):
                prefix = "/api/sessions/"
                rest = path[len(prefix):]
                session_part, queue_part = rest.split("/queue/", 1)
                message_part = queue_part[:-len("/steer")]
                session_id = _decode_url_segment(session_part)
                message_id = _decode_url_segment(message_part)
                status, result = _steer_session_queue(session_id, message_id)
                if status == 200:
                    print(f"[kanban] session queue steer session={session_id} message={message_id} launched={result.get('launched')}")
                self.reply_json(status, result)
                return

            if path.startswith("/api/sessions/") and path.endswith("/message"):
                session_id = _decode_url_segment(path[len("/api/sessions/"):-len("/message")])
                payload = json.loads(self.read_body() or b"{}")
                status, result = _message_session(session_id, payload)
                if status == 200:
                    print(f"[kanban] session message session={session_id} queued={result.get('queued')}")
                self.reply_json(status, result)
                return

            if path.startswith("/api/cards/") and path.endswith("/sessions/start"):
                card_id = _decode_url_segment(path[len("/api/cards/"):-len("/sessions/start")])
                payload = json.loads(self.read_body() or b"{}")
                status, result = _start_card_session(card_id, payload)
                if status == 200:
                    print(f"[kanban] card session start card={card_id} session={result.get('session', {}).get('session_id')} already_running={result.get('already_running')}")
                self.reply_json(status, result)
                return

            # ---------- decision-request mechanism (c121) ----------

            if self.path.startswith("/api/cards/") and self.path.endswith("/questions"):
                # POST a new question to a card.
                card_id = self.path[len("/api/cards/"):-len("/questions")]
                payload = json.loads(self.read_body() or b"{}")
                question = (payload.get("question") or "").strip()
                if not question:
                    self.reply_json(400, {"error": "question text required"})
                    return
                asked_by = (payload.get("asked_by") or "unknown").strip()
                choices_raw = payload.get("choices") or []
                if not isinstance(choices_raw, list):
                    self.reply_json(400, {"error": "choices must be an array"})
                    return
                # validate choices
                used_choice_ids = set()
                choices = []
                for ch in choices_raw:
                    cid = (ch.get("id") or "").strip()
                    if not cid:
                        self.reply_json(400, {"error": "every choice needs an id"})
                        return
                    if cid in used_choice_ids:
                        self.reply_json(400, {"error": f"duplicate choice id {cid}"})
                        return
                    used_choice_ids.add(cid)
                    choices.append({
                        "id": cid,
                        "label": ch.get("label", ""),
                        "rationale": ch.get("rationale", ""),
                        "implication": ch.get("implication", ""),
                    })

                data = _load_state()
                card = _find_card(data, card_id)
                if card is None:
                    self.reply_json(404, {"error": f"card {card_id} not found"})
                    return

                qid = _next_question_id(data)
                new_q = {
                    "id": qid,
                    "question": question,
                    "asked_by": asked_by,
                    "asked_date": _utcnow_iso(),
                    "choices": choices,
                    "allow_custom": True,
                    "answer": None,
                    "answer_type": None,
                    "answered_date": None,
                    "custom_text": None,
                    "interpretation": None,
                    "interpretation_confirmed": False,
                    "follow_up_question_ids": [],
                }
                card.setdefault("open_questions", []).append(new_q)
                card["updated"] = _utcnow_iso()
                _save_state_atomic(data)
                print(f"[kanban] question {qid} added to card {card_id}")
                self.reply_json(200, {"ok": True, "question_id": qid})
                return

            # /api/cards/<id>/questions/<qid>/answer
            if self.path.startswith("/api/cards/") and self.path.endswith("/answer"):
                rest = self.path[len("/api/cards/"):-len("/answer")]
                parts = rest.split("/")
                if len(parts) != 3 or parts[1] != "questions":
                    self.send_error(404)
                    return
                card_id, q_id = parts[0], parts[2]
                payload = json.loads(self.read_body() or b"{}")
                choice_id = payload.get("choice_id")
                custom_text = payload.get("custom_text")
                if choice_id is None and (custom_text is None or not str(custom_text).strip()):
                    self.reply_json(400, {"error": "must include choice_id or non-empty custom_text"})
                    return

                data = _load_state()
                card = _find_card(data, card_id)
                if card is None:
                    self.reply_json(404, {"error": f"card {card_id} not found"})
                    return
                q = _find_question(card, q_id)
                if q is None:
                    self.reply_json(404, {"error": f"question {q_id} not found on card {card_id}"})
                    return

                # choice wins if both supplied; custom_text recorded as annotation
                if choice_id is not None:
                    valid = [c["id"] for c in (q.get("choices") or [])]
                    if choice_id not in valid:
                        self.reply_json(400, {"error": f"choice_id {choice_id} not in {valid}"})
                        return
                    q["answer"] = choice_id
                    q["answer_type"] = "choice"
                    q["interpretation_confirmed"] = True
                    if custom_text is not None:
                        q["custom_text"] = str(custom_text).strip() or None
                else:
                    q["answer"] = str(custom_text).strip()
                    q["answer_type"] = "custom"
                    q["custom_text"] = str(custom_text).strip()
                    q["interpretation_confirmed"] = False

                q["answered_date"] = _utcnow_iso()
                card["updated"] = q["answered_date"]
                _save_state_atomic(data)
                resolved = _question_is_resolved(q)
                blocked_left = len(_card_blocked_q_ids(card))
                print(f"[kanban] question {q_id} on {card_id} answered ({q['answer_type']}); card blocked_left={blocked_left}")
                self.reply_json(200, {
                    "ok": True,
                    "resolved": resolved,
                    "card_blocked_left": blocked_left,
                })
                return

            # /api/cards/<id>/questions/<qid>/interpret
            if self.path.startswith("/api/cards/") and self.path.endswith("/interpret"):
                rest = self.path[len("/api/cards/"):-len("/interpret")]
                parts = rest.split("/")
                if len(parts) != 3 or parts[1] != "questions":
                    self.send_error(404)
                    return
                card_id, q_id = parts[0], parts[2]
                payload = json.loads(self.read_body() or b"{}")
                interpretation = (payload.get("interpretation") or "").strip()
                if not interpretation:
                    self.reply_json(400, {"error": "interpretation text required"})
                    return

                data = _load_state()
                card = _find_card(data, card_id)
                if card is None:
                    self.reply_json(404, {"error": f"card {card_id} not found"})
                    return
                q = _find_question(card, q_id)
                if q is None:
                    self.reply_json(404, {"error": f"question {q_id} not found on card {card_id}"})
                    return
                if q.get("answer_type") != "custom":
                    self.reply_json(400, {"error": "interpretation only valid for custom-answer questions"})
                    return

                q["interpretation"] = interpretation
                q["interpretation_confirmed"] = False
                card["updated"] = _utcnow_iso()
                _save_state_atomic(data)
                print(f"[kanban] question {q_id} on {card_id} interpretation set")
                self.reply_json(200, {"ok": True})
                return

            # /api/cards/<id>/questions/<qid>/confirm-interpretation
            if self.path.startswith("/api/cards/") and self.path.endswith("/confirm-interpretation"):
                rest = self.path[len("/api/cards/"):-len("/confirm-interpretation")]
                parts = rest.split("/")
                if len(parts) != 3 or parts[1] != "questions":
                    self.send_error(404)
                    return
                card_id, q_id = parts[0], parts[2]
                payload = json.loads(self.read_body() or b"{}")
                confirmed = bool(payload.get("confirmed"))

                data = _load_state()
                card = _find_card(data, card_id)
                if card is None:
                    self.reply_json(404, {"error": f"card {card_id} not found"})
                    return
                q = _find_question(card, q_id)
                if q is None:
                    self.reply_json(404, {"error": f"question {q_id} not found on card {card_id}"})
                    return
                if q.get("answer_type") != "custom":
                    self.reply_json(400, {"error": "confirm-interpretation only valid for custom-answer questions"})
                    return
                if not q.get("interpretation"):
                    self.reply_json(400, {"error": "no interpretation has been written for this question yet"})
                    return

                follow_up_id = None
                if confirmed:
                    q["interpretation_confirmed"] = True
                else:
                    follow_up = payload.get("follow_up") or {}
                    fu_question = (follow_up.get("question") or "").strip()
                    if not fu_question:
                        self.reply_json(400, {"error": "refine path requires follow_up.question"})
                        return
                    fu_choices_raw = follow_up.get("choices") or []
                    if not isinstance(fu_choices_raw, list):
                        self.reply_json(400, {"error": "follow_up.choices must be an array"})
                        return
                    used_choice_ids = set()
                    fu_choices = []
                    for ch in fu_choices_raw:
                        cid = (ch.get("id") or "").strip()
                        if not cid or cid in used_choice_ids:
                            self.reply_json(400, {"error": "follow_up choice ids must be unique and non-empty"})
                            return
                        used_choice_ids.add(cid)
                        fu_choices.append({
                            "id": cid,
                            "label": ch.get("label", ""),
                            "rationale": ch.get("rationale", ""),
                            "implication": ch.get("implication", ""),
                        })
                    follow_up_id = _next_question_id(data)
                    follow_q = {
                        "id": follow_up_id,
                        "question": fu_question,
                        "asked_by": q.get("asked_by", "follow-up"),
                        "asked_date": _utcnow_iso(),
                        "choices": fu_choices,
                        "allow_custom": True,
                        "answer": None,
                        "answer_type": None,
                        "answered_date": None,
                        "custom_text": None,
                        "interpretation": None,
                        "interpretation_confirmed": False,
                        "follow_up_question_ids": [],
                    }
                    card.setdefault("open_questions", []).append(follow_q)
                    q.setdefault("follow_up_question_ids", []).append(follow_up_id)
                    # q remains with interpretation_confirmed=false; the new follow-up is the active blocker
                card["updated"] = _utcnow_iso()
                _save_state_atomic(data)
                blocked_left = len(_card_blocked_q_ids(card))
                print(f"[kanban] question {q_id} confirmed={confirmed} follow_up={follow_up_id}; card blocked_left={blocked_left}")
                self.reply_json(200, {
                    "ok": True,
                    "follow_up_id": follow_up_id,
                    "card_blocked_left": blocked_left,
                })
                return

            # ---------- pending-completion mechanism (c123) ----------

            if self.path.startswith("/api/cards/") and self.path.endswith("/mark-pending-completion"):
                card_id = self.path[len("/api/cards/"):-len("/mark-pending-completion")]
                payload = json.loads(self.read_body() or b"{}")
                marked_by = (payload.get("marked_by") or "").strip()
                summary = (payload.get("summary") or "").strip()
                evidence_refs = payload.get("evidence_refs") or []
                if not marked_by:
                    self.reply_json(400, {"error": "marked_by required"})
                    return
                if not summary:
                    self.reply_json(400, {"error": "summary required (describe what was completed)"})
                    return
                if not isinstance(evidence_refs, list):
                    self.reply_json(400, {"error": "evidence_refs must be an array of strings"})
                    return
                evidence_refs = [str(x).strip() for x in evidence_refs if str(x).strip()]

                data = _load_state()
                card = _find_card(data, card_id)
                if card is None:
                    self.reply_json(404, {"error": f"card {card_id} not found"})
                    return
                if card.get("column") == "done":
                    self.reply_json(409, {"error": "card is already in done column"})
                    return

                pc = {
                    "marked_at": _utcnow_iso(),
                    "marked_by": marked_by,
                    "summary": summary,
                    "evidence_refs": evidence_refs,
                }
                card["pending_completion"] = pc
                card["updated"] = pc["marked_at"]
                _save_state_atomic(data)
                print(f"[kanban] card {card_id} marked pending-completion by {marked_by}")
                self.reply_json(200, {"ok": True, "card_id": card_id, "marked_at": pc["marked_at"]})
                return

            if self.path.startswith("/api/cards/") and self.path.endswith("/confirm-completion"):
                card_id = self.path[len("/api/cards/"):-len("/confirm-completion")]
                self.read_body()
                data = _load_state()
                card = _find_card(data, card_id)
                if card is None:
                    self.reply_json(404, {"error": f"card {card_id} not found"})
                    return
                if not card.get("pending_completion"):
                    self.reply_json(409, {"error": "card has no pending_completion to confirm"})
                    return

                prev_column = card.get("column")
                card["column"] = "done"
                card["order"] = _card_max_order(data, "done") + 1000
                card["pending_completion"] = None
                card["updated"] = _utcnow_iso()
                _save_state_atomic(data)
                print(f"[kanban] card {card_id} completion confirmed; moved {prev_column} -> done")
                self.reply_json(200, {
                    "ok": True,
                    "card_id": card_id,
                    "from_column": prev_column,
                    "to_column": "done",
                })
                return

            if self.path.startswith("/api/cards/") and self.path.endswith("/reject-completion"):
                card_id = self.path[len("/api/cards/"):-len("/reject-completion")]
                payload = json.loads(self.read_body() or b"{}")
                rejection_note = (payload.get("rejection_note") or "").strip()
                data = _load_state()
                card = _find_card(data, card_id)
                if card is None:
                    self.reply_json(404, {"error": f"card {card_id} not found"})
                    return
                if not card.get("pending_completion"):
                    self.reply_json(409, {"error": "card has no pending_completion to reject"})
                    return

                pc = card["pending_completion"]
                card["pending_completion"] = None
                if rejection_note:
                    _append_note(card, f"completion-rejected (was marked by {pc.get('marked_by','?')}): {rejection_note}")
                card["updated"] = _utcnow_iso()
                _save_state_atomic(data)
                print(f"[kanban] card {card_id} completion rejected; stays in {card.get('column')}")
                self.reply_json(200, {
                    "ok": True,
                    "card_id": card_id,
                    "column": card.get("column"),
                    "note_appended": bool(rejection_note),
                })
                return

            self.send_error(404)
        except Exception as exc:
            print(f"[kanban] POST error on {self.path}: {exc}", file=sys.stderr)
            self.reply_json(400, {"error": str(exc)})

    def do_PATCH(self):
        if not self.require_auth():
            return

        if self.path.startswith("/api/memory-review/"):
            item_id = self.path[len("/api/memory-review/"):]
            try:
                patch = json.loads(self.read_body() or b"{}")
                data = _load_memory_review()
                item = _find_memory_review_item(data, item_id)
                if item is None:
                    self.reply_json(404, {"error": "memory review item not found"})
                    return
                allowed_statuses = set(data.get("review_statuses", ["unreviewed", "keep", "adjust", "remove"]))
                if "status" in patch:
                    status = patch.get("status")
                    if status not in allowed_statuses:
                        self.reply_json(400, {"error": f"invalid status {status!r}"})
                        return
                    item["status"] = status
                    item["reviewed_at"] = None if status == "unreviewed" else _utcnow_iso()
                if "adjustment_note" in patch:
                    item["adjustment_note"] = patch.get("adjustment_note") or ""
                item["updated_at"] = _utcnow_iso()
                _save_memory_review_atomic(data)
                print(f"[kanban] memory review {item_id} patched")
                self.reply_json(200, {"ok": True, "item": item, "summary": _memory_review_summary(data)})
            except Exception as exc:
                print(f"[kanban] memory-review patch error: {exc}", file=sys.stderr)
                self.reply_json(400, {"error": str(exc)})
            return

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
    server = HTTPServer((HOST, PORT), Handler)
    _server = server
    if IDLE_TIMEOUT_S > 0:
        threading.Thread(target=heartbeat_watcher, daemon=True).start()
    print(f"[kanban] listening on http://{HOST}:{PORT}/")
    if REMOTE_TOKEN:
        print("[kanban] remote token auth: enabled")
    else:
        print("[kanban] remote token auth: disabled (local/dev-window mode)")
    if IDLE_TIMEOUT_S > 0:
        print(f"[kanban] idle timeout: {IDLE_TIMEOUT_S}s (override via KANBAN_IDLE_TIMEOUT_S)")
    else:
        print("[kanban] idle timeout: disabled")
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

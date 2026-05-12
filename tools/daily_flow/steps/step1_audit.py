"""
Step 1 - Previous-day audit.

Inputs:
 - git log over the 24 h window (ET, ending start-of-day today)
 - context/session-log.md entries dated within window
 - kanban snapshot diff vs yesterday's persisted snapshot
 - latest smoke-verify results-*.json files
 - yesterday's daily log (if it exists) for cross-reference

Output: tools/daily_flow/state/audit-YYYY-MM-DD.json

The audit blob is the single source of truth for all downstream steps. Steps 2-7
read it instead of re-running git or re-parsing the session log.
"""

from __future__ import annotations

import datetime as dt
import json
import pathlib
import re
from typing import Any

from ..lib import fsutil, gitutil, timefmt

LOG_PREFIX = "DAILY-FLOW.AUDIT"

SESSION_HEADER_RE = re.compile(
    r"^##\s+Session\s*\(`(?P<slug>[^`]+)`\)\s*-\s*(?P<date>\d{4}-\d{2}-\d{2})\s*-\s*(?P<title>.+?)\s*$"
)


def _session_log_path() -> pathlib.Path:
    return fsutil.repo_root() / "context" / "session-log.md"


def _kanban_path() -> pathlib.Path:
    return fsutil.repo_root() / "tools" / "kanban" / "state.json"


def _smoke_runs_dir() -> pathlib.Path:
    return fsutil.repo_root() / ".claude" / "smoke-verify-runs"


def _snapshot_path(date: str) -> pathlib.Path:
    return fsutil.state_dir() / f"kanban-snapshot-{date}.json"


def _audit_path(date: str) -> pathlib.Path:
    return fsutil.state_dir() / f"audit-{date}.json"


def _parse_session_log(window_start: dt.date, window_end: dt.date) -> list[dict[str, str]]:
    path = _session_log_path()
    if not path.exists():
        return []
    out: list[dict[str, str]] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = SESSION_HEADER_RE.match(line)
        if not m:
            continue
        try:
            d = dt.date.fromisoformat(m.group("date"))
        except ValueError:
            continue
        if window_start <= d < window_end:
            out.append({"slug": m.group("slug"), "date": m.group("date"), "title": m.group("title").strip()})
    return out


def _kanban_transitions(snapshot_yesterday: dict[str, Any], current: dict[str, Any]) -> list[dict[str, str]]:
    """Returns list of {id, title, from, to} for cards that changed column.

    If snapshot_yesterday is missing (no prior snapshot), return empty list.
    Cards present in the current state but absent from a prior snapshot are NOT
    treated as transitions on first-run; that prevents the first daily log from
    listing every existing card as a fresh transition.
    """
    if not snapshot_yesterday or not snapshot_yesterday.get("cards"):
        return []
    prev_cards = {c.get("id"): c for c in snapshot_yesterday.get("cards", [])}
    transitions: list[dict[str, str]] = []
    for card in current.get("cards", []):
        cid = card.get("id")
        prev = prev_cards.get(cid)
        if prev is None:
            continue
        if prev.get("column") != card.get("column"):
            transitions.append({
                "id": cid,
                "title": card.get("title", ""),
                "from": prev.get("column", ""),
                "to": card.get("column", ""),
            })
    return transitions


def _smoke_results(window_start: dt.datetime, window_end: dt.datetime) -> list[dict[str, Any]]:
    """Returns list of {result_file, results: [...]} for runs whose mtime falls in window."""
    runs_dir = _smoke_runs_dir()
    if not runs_dir.exists():
        return []
    out: list[dict[str, Any]] = []
    for jf in sorted(runs_dir.glob("results-*.json")):
        try:
            mtime = dt.datetime.fromtimestamp(jf.stat().st_mtime, tz=timefmt.ET)
        except OSError:
            continue
        if not (window_start <= mtime < window_end):
            continue
        try:
            data = json.loads(jf.read_text(encoding="utf-8", errors="replace"))
        except (json.JSONDecodeError, OSError):
            continue
        out.append({"result_file": fsutil.posix_rel(jf), "results": data})
    return out


def _yesterday_log_text(date: str) -> str | None:
    path = fsutil.repo_root() / "context" / "daily-logs" / f"{date}.md"
    if not path.exists():
        return None
    return path.read_text(encoding="utf-8", errors="replace")


def run_audit(today: dt.date | None = None, *, force: bool = False) -> dict[str, Any]:
    today = today or timefmt.today_et()
    today_iso = today.isoformat()
    yesterday_iso = (today - dt.timedelta(days=1)).isoformat()

    audit_file = _audit_path(today_iso)
    if audit_file.exists() and not force:
        cached = fsutil.load_json(audit_file)
        if cached and cached.get("for_date") == today_iso:
            return cached

    window_start_iso, window_end_iso = timefmt.iso_window_24h(today)
    window_start_dt = dt.datetime.fromisoformat(window_start_iso)
    window_end_dt = dt.datetime.fromisoformat(window_end_iso)

    commits = gitutil.log_between(window_start_iso, window_end_iso, branch="dev")
    sessions = _parse_session_log(window_start_dt.date(), window_end_dt.date())
    current_kanban = fsutil.load_json(_kanban_path(), default={})
    snapshot_yesterday = fsutil.load_json(_snapshot_path(yesterday_iso), default={})
    transitions = _kanban_transitions(snapshot_yesterday, current_kanban)
    smoke = _smoke_results(window_start_dt, window_end_dt)
    prev_log_text = _yesterday_log_text(yesterday_iso)

    audit_blob: dict[str, Any] = {
        "schema_version": 1,
        "semantic_version": "0.1.0",
        "for_date": today_iso,
        "window": {"start": window_start_iso, "end": window_end_iso},
        "head_sha": gitutil.head_sha(),
        "commits": commits,
        "sessions": sessions,
        "kanban_transitions": transitions,
        "smoke_results": smoke,
        "prev_log_present": prev_log_text is not None,
        "prev_log_path": f"context/daily-logs/{yesterday_iso}.md" if prev_log_text else None,
    }
    audit_blob["audit_hash"] = fsutil.audit_hash({
        "head_sha": audit_blob["head_sha"],
        "commit_count": len(commits),
        "session_count": len(sessions),
        "transition_count": len(transitions),
        "smoke_run_count": len(smoke),
    })

    fsutil.save_json_atomic(audit_file, audit_blob)
    fsutil.save_json_atomic(_snapshot_path(today_iso), current_kanban)
    return audit_blob

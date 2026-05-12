"""
Step 7 - Day-prep briefing payload.

Writes tools/kanban/daily-briefing.json with the headline + ready threads + blocker bugs +
recommended focus list. The kanban browser reads this on load and surfaces a top banner.
"""

from __future__ import annotations

import datetime as dt
import pathlib
from typing import Any

from ..lib import fsutil, timefmt

LOG_PREFIX = "DAILY-FLOW.BRIEFING"

BRIEFING_PATH = pathlib.Path("tools") / "kanban" / "daily-briefing.json"


def _briefing_abs() -> pathlib.Path:
    return fsutil.repo_root() / BRIEFING_PATH


def _headlines(audit: dict[str, Any], state_sync: dict[str, Any]) -> list[str]:
    out: list[str] = []
    commit_n = len(audit.get("commits", []))
    session_n = len(audit.get("sessions", []))
    if commit_n or session_n:
        out.append(f"{commit_n} commits and {session_n} sessions in the last 24 h")
    flipped = state_sync.get("cards_flipped", [])
    if flipped:
        ids = ", ".join(f.get("id", "?") for f in flipped[:5])
        out.append(f"Cards advanced to done: {ids}{' ...' if len(flipped) > 5 else ''}")
    bugs_flipped = state_sync.get("bugs_flipped", [])
    if bugs_flipped:
        ids = ", ".join(b.get("id", "?") for b in bugs_flipped[:5])
        out.append(f"Bugs verified fixed: {ids}")
    if not out:
        out.append("No new commits, sessions, or state transitions overnight.")
    return out[:3]


def _blocker_bugs(priority_sort: dict[str, Any]) -> list[dict[str, Any]]:
    return [
        {"id": b.get("id"), "title": b.get("title"), "severity": b.get("severity")}
        for b in priority_sort.get("bugs_ranked", [])
        if (b.get("severity") or "").lower() in ("blocker", "critical")
    ]


def run_briefing(
    *,
    audit: dict[str, Any],
    state_sync: dict[str, Any],
    priority_sort: dict[str, Any],
    daily_log_result: dict[str, Any],
    today: dt.date | None = None,
    partial: bool = False,
    failure_point: str | None = None,
    compaction_quality_review: bool = False,
) -> dict[str, Any]:
    today = today or timefmt.today_et()

    parked = fsutil.load_json(fsutil.repo_root() / "tools" / "kanban" / "parked.json", default={})
    ready = [p for p in parked.get("parked", []) if p.get("ready_to_resume")]

    briefing = {
        "schema_version": 1,
        "semantic_version": "0.1.0",
        "x_extensibility_rule": (
            "Additive-only. Unknown fields tolerated. Reserved namespace: x_*. "
            "Bump schema_version only on a breaking change."
        ),
        "generated_at": timefmt.format_iso_et(),
        "generated_display": timefmt.format_display(),
        "for_date": today.isoformat(),
        "partial": partial,
        "headlines": _headlines(audit, state_sync),
        "ready_to_resume": {
            "count": len(ready),
            "items": [{"id": p.get("id"), "title": p.get("title")} for p in ready],
        },
        "blocker_bugs": {
            "count": len(_blocker_bugs(priority_sort)),
            "items": _blocker_bugs(priority_sort),
        },
        "focus": priority_sort.get("focus_shortlist", []),
        "daily_log_path": daily_log_result.get("log_path"),
        "decisions_allocated": daily_log_result.get("decisions_allocated", []),
        "x_failure_point": failure_point,
        "x_compaction_quality_review": compaction_quality_review,
    }

    fsutil.save_json_atomic(_briefing_abs(), briefing)
    return briefing

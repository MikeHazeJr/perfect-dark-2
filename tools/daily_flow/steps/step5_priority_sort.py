"""
Step 5 - Priority sort.

Produces a focus shortlist (top 3-5) by ranking:
 - Kanban cards in active or backlog
 - Open bugs (not fixed)
 - Parked threads with ready_to_resume = true

Cards rank by (flag desc, pillar weight desc, created asc).
Bugs rank by (severity desc, filed_date asc).
Parked-ready rank by (parked_date asc).

The shortlist mixes all three streams; the caller (Step 6) chooses how
many of each kind go into the daily log's `## Today Focus`.
"""

from __future__ import annotations

import pathlib
from typing import Any

from ..lib import fsutil, priority

LOG_PREFIX = "DAILY-FLOW.PRIORITY_SORT"


def _kanban_path() -> pathlib.Path:
    return fsutil.repo_root() / "tools" / "kanban" / "state.json"


def _bugs_path() -> pathlib.Path:
    return fsutil.repo_root() / "tools" / "bugs" / "state.json"


def _parked_path() -> pathlib.Path:
    return fsutil.repo_root() / "tools" / "kanban" / "parked.json"


def _filter_cards(kanban: dict[str, Any]) -> list[dict[str, Any]]:
    return [c for c in kanban.get("cards", []) if c.get("column") in ("active", "backlog")]


def _filter_open_bugs(bugs: dict[str, Any]) -> list[dict[str, Any]]:
    return [b for b in bugs.get("bugs", []) if b.get("status") not in ("fixed",)]


def _filter_parked_ready(parked: dict[str, Any]) -> list[dict[str, Any]]:
    return [p for p in parked.get("parked", []) if p.get("ready_to_resume")]


def run_priority_sort(top_n: int = 5) -> dict[str, Any]:
    kanban = fsutil.load_json(_kanban_path(), default={})
    bugs = fsutil.load_json(_bugs_path(), default={})
    parked = fsutil.load_json(_parked_path(), default={})

    cards = sorted(_filter_cards(kanban), key=priority.card_sort_key)
    open_bugs = sorted(_filter_open_bugs(bugs), key=priority.bug_sort_key)
    parked_ready = sorted(_filter_parked_ready(parked), key=priority.parked_sort_key)

    result = {
        "cards_ranked": [
            {"id": c.get("id"), "title": c.get("title"), "pillar": c.get("pillar"), "flag": c.get("flag"), "column": c.get("column")}
            for c in cards
        ],
        "bugs_ranked": [
            {"id": b.get("id"), "title": b.get("title"), "severity": b.get("severity"), "status": b.get("status")}
            for b in open_bugs
        ],
        "parked_ready": [
            {"id": p.get("id"), "title": p.get("title"), "pillar": p.get("pillar"), "parked_date": p.get("parked_date")}
            for p in parked_ready
        ],
        "focus_shortlist": _build_shortlist(cards, open_bugs, parked_ready, top_n=top_n),
    }
    return result


def _build_shortlist(
    cards: list[dict[str, Any]],
    bugs: list[dict[str, Any]],
    parked_ready: list[dict[str, Any]],
    *,
    top_n: int,
) -> list[dict[str, Any]]:
    """Heuristic mix: at most 1 ready parked, at most 1 high-severity bug, rest cards.

    The intent is to keep critical-path cards visible while not dropping a clearly
    blocker bug or a thread that just came off the parked list. Tunable later.
    """
    shortlist: list[dict[str, Any]] = []

    high_bug = next((b for b in bugs if (b.get("severity") or "").lower() in ("blocker", "critical")), None)
    if high_bug:
        shortlist.append({
            "kind": "bug",
            "id": high_bug.get("id"),
            "title": high_bug.get("title"),
            "rationale": f"open {high_bug.get('severity')} severity, filed {high_bug.get('filed_date')}",
        })

    if parked_ready:
        p = parked_ready[0]
        shortlist.append({
            "kind": "parked",
            "id": p.get("id"),
            "title": p.get("title"),
            "rationale": f"resume condition met, parked {p.get('parked_date')}",
        })

    for c in cards:
        if len(shortlist) >= top_n:
            break
        flag = c.get("flag") or ""
        pillar = c.get("pillar", "")
        rationale = f"{flag + ' ' if flag else ''}{pillar} card, column {c.get('column')}"
        shortlist.append({
            "kind": "card",
            "id": c.get("id"),
            "title": c.get("title"),
            "rationale": rationale.strip(),
        })

    return shortlist[:top_n]

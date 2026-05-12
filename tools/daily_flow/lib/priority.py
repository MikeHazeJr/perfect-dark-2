"""
Priority sort comparators for the daily-flow orchestrator.

Pillar weights reflect Mike's current emphasis (Engine + Catalog + Connectivity
load-bearing for v0.1.x; Tooling lower because it is meta-infrastructure).
Adjust here when the project's center of gravity shifts.
"""

from __future__ import annotations

import datetime as dt
from typing import Any

LOG_PREFIX = "DAILY-FLOW.PRIORITY"

PILLAR_WEIGHT: dict[str, int] = {
    "engine": 100,
    "catalog": 95,
    "networking": 90,
    "input": 85,
    "ui": 80,
    "mod-infrastructure": 75,
    "campaign": 70,
    "tests": 65,
    "tooling": 60,
    "social": 55,
    "vehicles": 50,
    "benchmarking": 45,
}

FLAG_WEIGHT: dict[str | None, int] = {
    "P0": 1000,
    "P1": 800,
    "P2": 600,
    "P3": 400,
    None: 0,
}

SEVERITY_WEIGHT: dict[str, int] = {
    "blocker": 100,
    "critical": 90,
    "major": 70,
    "minor": 40,
    "trivial": 20,
}


def _parse_date(s: str | None) -> dt.date:
    if not s:
        return dt.date(1970, 1, 1)
    try:
        return dt.date.fromisoformat(s[:10])
    except (TypeError, ValueError):
        return dt.date(1970, 1, 1)


def card_sort_key(card: dict[str, Any]) -> tuple[int, int, dt.date]:
    """Higher score first; created date asc as tiebreaker."""
    flag = card.get("flag")
    pillar = (card.get("pillar") or "").lower()
    flag_w = FLAG_WEIGHT.get(flag, 0)
    pillar_w = PILLAR_WEIGHT.get(pillar, 30)
    created = _parse_date(card.get("created"))
    return (-flag_w, -pillar_w, created)


def bug_sort_key(bug: dict[str, Any]) -> tuple[int, dt.date]:
    sev = (bug.get("severity") or "minor").lower()
    sev_w = SEVERITY_WEIGHT.get(sev, 30)
    filed = _parse_date(bug.get("filed_date"))
    return (-sev_w, filed)


def parked_sort_key(entry: dict[str, Any]) -> tuple[dt.date, str]:
    return (_parse_date(entry.get("parked_date")), entry.get("id", ""))

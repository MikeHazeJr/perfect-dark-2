"""
Decision-ID registry.

A decision is a notable, durable architectural call that survives daily -> weekly -> monthly
rollups verbatim. The registry threads a monotonically-increasing id (dec-NNN) across all
three log tiers so the same decision is identifiable in any of them.

Sessions append proposed decisions to their scratch files (.claude/scratch/<slug>.md) under
the marker `### dec-PROPOSED: <title>` and the orchestrator allocates a real id on the
next 6 AM run.
"""

from __future__ import annotations

import pathlib
import re
from typing import Any

from . import fsutil, timefmt

LOG_PREFIX = "DAILY-FLOW.DECISIONS"

REGISTRY_PATH = fsutil.REPO_ROOT / "tools" / "daily_flow" / "decisions.json"

DEFAULT_REGISTRY: dict[str, Any] = {
    "schema_version": 1,
    "semantic_version": "0.1.0",
    "x_extensibility_rule": (
        "Additive-only. Reserved namespace: x_*. Decision IDs are monotonic and never reused. "
        "Bump schema_version only on a breaking change; bump semantic_version on additive change."
    ),
    "next_id": 1,
    "decisions": [],
}

PROPOSED_PATTERN = re.compile(r"^### dec-PROPOSED:\s*(?P<title>.+)$", re.MULTILINE)


def load_registry() -> dict[str, Any]:
    reg = fsutil.load_json(REGISTRY_PATH, default=DEFAULT_REGISTRY.copy())
    for key, value in DEFAULT_REGISTRY.items():
        reg.setdefault(key, value if not isinstance(value, list) else [])
    return reg


def save_registry(reg: dict[str, Any]) -> None:
    fsutil.save_json_atomic(REGISTRY_PATH, reg)


def allocate_id(reg: dict[str, Any]) -> str:
    n = int(reg.get("next_id", 1))
    reg["next_id"] = n + 1
    return f"dec-{n:03d}"


def register_decision(
    reg: dict[str, Any],
    *,
    title: str,
    decided: str,
    why: str,
    alternatives: list[str] | None = None,
    not_done: list[str] | None = None,
    implications: str = "",
    daily_log_path: str | None = None,
) -> dict[str, Any]:
    new_id = allocate_id(reg)
    entry = {
        "id": new_id,
        "date": timefmt.today_et().isoformat(),
        "title": title,
        "decided": decided,
        "why": why,
        "alternatives": alternatives or [],
        "not_done": not_done or [],
        "implications": implications,
        "daily_log": daily_log_path,
        "weekly_log": None,
        "monthly_log": None,
        "x_meta": {},
    }
    reg.setdefault("decisions", []).append(entry)
    return entry


def scan_scratch_for_proposed(scratch_dir: pathlib.Path) -> list[dict[str, Any]]:
    """Walk .claude/scratch/*.md for `### dec-PROPOSED: <title>` markers.

    Each found marker, along with the text up to the next `### ` or end of file,
    is parsed into a proposed-decision record. The body uses light "Key: value"
    lines (Decided / Why / Alternatives / Not done / Implications).
    """
    proposals: list[dict[str, Any]] = []
    if not scratch_dir.exists():
        return proposals
    for md in sorted(scratch_dir.glob("*.md")):
        text = md.read_text(encoding="utf-8", errors="replace")
        for match in PROPOSED_PATTERN.finditer(text):
            title = match.group("title").strip()
            start = match.end()
            next_header = text.find("\n### ", start)
            body = text[start:next_header if next_header != -1 else len(text)].strip()
            proposals.append({
                "source": fsutil.posix_rel(md),
                "title": title,
                "decided": _grab(body, "Decided"),
                "why": _grab(body, "Why"),
                "alternatives": _grab_list(body, "Alternatives"),
                "not_done": _grab_list(body, "Not done"),
                "implications": _grab(body, "Implications"),
            })
    return proposals


def _grab(body: str, key: str) -> str:
    for line in body.splitlines():
        ln = line.strip()
        if ln.lower().startswith(f"- {key.lower()}:") or ln.lower().startswith(f"{key.lower()}:"):
            return ln.split(":", 1)[1].strip()
    return ""


def _grab_list(body: str, key: str) -> list[str]:
    raw = _grab(body, key)
    if not raw:
        return []
    return [piece.strip() for piece in raw.split(",") if piece.strip()]


def update_log_pointer(reg: dict[str, Any], decision_id: str, *, weekly_log: str | None = None, monthly_log: str | None = None) -> None:
    for entry in reg.get("decisions", []):
        if entry.get("id") == decision_id:
            if weekly_log is not None:
                entry["weekly_log"] = weekly_log
            if monthly_log is not None:
                entry["monthly_log"] = monthly_log
            return

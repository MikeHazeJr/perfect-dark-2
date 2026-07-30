"""Read-only Workbench briefing mode for the legacy daily-flow entrypoint.

Durable Workbench mutation remains API-only. This module replaces the old
Kanban-mutating daily pipeline with a compact current-truth export that the
scheduled entrypoint can produce without creating a second task store.
"""

from __future__ import annotations

import json
from collections import Counter
from pathlib import Path
from typing import Any

from .lib import fsutil, timefmt


def roadmap_path() -> Path:
    return fsutil.repo_root() / "Tools" / "Workbench" / "data" / "roadmap.json"


def available() -> bool:
    return roadmap_path().exists()


def _jsonl(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    rows: list[dict[str, Any]] = []
    for line_number, line in enumerate(
        path.read_text(encoding="utf-8-sig").splitlines(), start=1
    ):
        if not line.strip():
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError as exc:
            rows.append(
                {"malformed": True, "line": line_number, "error": str(exc)}
            )
    return rows


def _fold_notes(events: list[dict[str, Any]]) -> list[dict[str, Any]]:
    notes: dict[str, dict[str, Any]] = {}
    for event in events:
        note_id = str(event.get("id", ""))
        if event.get("event") == "note_add" and note_id:
            notes[note_id] = {
                "id": note_id,
                "target": event.get("target"),
                "author": event.get("author"),
                "text": event.get("text"),
                "state": "new",
                "ts": event.get("ts"),
            }
        elif event.get("event") == "note_state" and note_id in notes:
            notes[note_id]["state"] = event.get("state")
            notes[note_id]["updated"] = event.get("ts")
    return list(notes.values())


def run() -> dict[str, Any]:
    root = fsutil.repo_root()
    data_dir = root / "Tools" / "Workbench" / "data"
    roadmap = json.loads(roadmap_path().read_text(encoding="utf-8-sig"))
    items = roadmap.get("items", [])
    if not isinstance(items, list):
        raise ValueError("Workbench roadmap requires items[]")
    notes = _fold_notes(_jsonl(data_dir / "notes.jsonl"))
    changelog = _jsonl(data_dir / "changelog.jsonl")

    decisions = [
        {
            "id": item.get("id"),
            "title": item.get("title"),
            "detail": item.get("detail"),
            "options": item.get("options", []),
        }
        for item in items
        if item.get("type") == "decision" and item.get("status") == "open"
    ]
    open_notes = [
        note
        for note in notes
        if note.get("state")
        in {"new", "needs_clarification", "waiting_user_decision"}
    ]
    active_truth = [
        {
            "id": item.get("id"),
            "type": item.get("type"),
            "area": item.get("area"),
            "status": item.get("status"),
            "phase": item.get("phase"),
            "owner": item.get("owner"),
            "title": item.get("title"),
        }
        for item in items
        if item.get("status") in {"missing", "stub", "partial", "blocked", "open"}
        and item.get("type") != "area"
    ]
    active_truth.sort(
        key=lambda item: (
            item.get("phase") if isinstance(item.get("phase"), int) else 999,
            str(item.get("area", "")),
            str(item.get("id", "")),
        )
    )

    briefing = {
        "schemaVersion": 1,
        "kind": "pd2-workbench-daily-briefing",
        "generated": timefmt.format_iso_et(),
        "roadmapUpdated": roadmap.get("updated"),
        "counts": {
            "items": len(items),
            "notes": len(notes),
            "openNotes": len(open_notes),
            "openDecisions": len(decisions),
            "byType": dict(Counter(str(item.get("type", "")) for item in items)),
            "byStatus": dict(Counter(str(item.get("status", "")) for item in items)),
        },
        "openNotes": open_notes,
        "openDecisions": decisions,
        "activeTruth": active_truth,
        "recentActivity": list(reversed(changelog[-25:])),
        "policy": (
            "Read-only export. All durable roadmap and note mutations must use "
            "the Workbench server API."
        ),
    }
    output = root / "Tools" / "Workbench" / "exports" / "daily-briefing.json"
    fsutil.save_json_atomic(output, briefing)
    return {"briefing": output.relative_to(root).as_posix(), **briefing["counts"]}

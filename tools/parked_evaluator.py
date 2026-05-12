#!/usr/bin/env python3
"""
Parked-thread evaluator + bug-state helpers.

Callable from CLI (headless) AND as a Python library (from daily-flow orchestrator).

CLI subcommands:
  update-ready     evaluate every parked entry against current kanban + date, write ready_to_resume flags
  update-stale     scan parked entries, write stale flags where last_checked + 14 days < today
  cascade          --card-done <card-id>: find dependents, decrement deps, mark ready where satisfied
  list             print parked + bug summary (human-readable)
  bug-status       --bug <id> --to <status>: flip a bug's status, write back

State files (resolved relative to repo root):
  tools/kanban/parked.json
  tools/kanban/state.json
  tools/bugs/state.json

Schema extensibility: additive-only, unknown fields tolerated, reserved namespace x_*, semantic_version field,
frozen field semantics. Loader uses .get() with defaults so missing fields do not raise.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import pathlib
import sys
from typing import Any

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
PARKED_PATH = REPO_ROOT / "tools" / "kanban" / "parked.json"
KANBAN_PATH = REPO_ROOT / "tools" / "kanban" / "state.json"
BUGS_PATH = REPO_ROOT / "tools" / "bugs" / "state.json"

STALE_DAYS = 14
DEFAULT_RESUME_DELTA_DAYS = 30
BUG_STATUS_VALUES = ("open", "triaged", "fix-pending-verification", "fixed")


def _today_str() -> str:
    return dt.date.today().isoformat()


def _parse_iso_date(s: str) -> dt.date:
    return dt.date.fromisoformat(s)


def load_json(path: pathlib.Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def save_json_atomic(path: pathlib.Path, data: dict[str, Any]) -> None:
    """Atomic JSON write (tmp file + rename). Matches kanban/server.py write semantics."""
    serialized = json.dumps(data, indent=2, ensure_ascii=False) + "\n"
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(serialized, encoding="utf-8")
    os.replace(tmp, path)


def _kanban_card_status(kanban: dict[str, Any], card_id: str) -> str | None:
    """Return the column ('done', 'active', 'backlog', 'blocked') of a card, or None if missing."""
    for card in kanban.get("cards", []):
        if card.get("id") == card_id:
            return card.get("column")
    return None


def _kanban_card_done(kanban: dict[str, Any], card_id: str) -> bool:
    return _kanban_card_status(kanban, card_id) == "done"


def _eval_resume_condition(entry: dict[str, Any], kanban: dict[str, Any], today: dt.date) -> bool:
    """Pure evaluator: returns True if entry should flip ready_to_resume."""
    rc = entry.get("resume_condition") or {}
    rc_type = rc.get("type", "manual")
    targets = rc.get("targets")
    semantics = rc.get("semantics", "all-of")

    if rc_type == "manual":
        return False

    if rc_type == "date":
        if not targets or not isinstance(targets, str):
            return False
        try:
            target_date = _parse_iso_date(targets)
        except ValueError:
            return False
        return today >= target_date

    if rc_type == "dependency":
        if not targets or not isinstance(targets, list) or len(targets) == 0:
            return False
        results = [_kanban_card_done(kanban, cid) for cid in targets]
        if semantics == "all-of":
            return all(results)
        if semantics == "any-of":
            return any(results)
        return all(results)

    return False


def evaluate_resume_conditions(parked: dict[str, Any], kanban: dict[str, Any], today: dt.date | None = None) -> list[dict[str, Any]]:
    """Flip ready_to_resume for every parked entry. Returns the list of entries whose flag flipped true this pass."""
    if today is None:
        today = dt.date.today()
    flipped: list[dict[str, Any]] = []
    for entry in parked.get("parked", []):
        prev = bool(entry.get("ready_to_resume", False))
        now_ready = _eval_resume_condition(entry, kanban, today)
        entry["ready_to_resume"] = now_ready
        entry["last_checked"] = today.isoformat()
        if now_ready and not prev:
            flipped.append(entry)
    return flipped


def evaluate_staleness(parked: dict[str, Any], today: dt.date | None = None) -> list[dict[str, Any]]:
    """Flip stale for parked entries whose last_checked + 14 days < today."""
    if today is None:
        today = dt.date.today()
    threshold = today - dt.timedelta(days=STALE_DAYS)
    newly_stale: list[dict[str, Any]] = []
    for entry in parked.get("parked", []):
        last = entry.get("last_checked") or entry.get("parked_date")
        if not last:
            entry["stale"] = True
            newly_stale.append(entry)
            continue
        try:
            last_date = _parse_iso_date(last)
        except ValueError:
            entry["stale"] = True
            newly_stale.append(entry)
            continue
        was_stale = bool(entry.get("stale", False))
        is_stale = last_date < threshold
        entry["stale"] = is_stale
        if is_stale and not was_stale:
            newly_stale.append(entry)
    return newly_stale


def cascade_card_done(parked: dict[str, Any], kanban: dict[str, Any], card_id: str, today: dt.date | None = None) -> dict[str, Any]:
    """When a kanban card is moved to done, find dependents and re-evaluate.

    Returns: {'matched': [entries that depend on card_id], 'flipped': [entries whose ready flipped this pass]}.
    Does NOT mutate the kanban file; this is purely a parked-side cascade.
    """
    if today is None:
        today = dt.date.today()
    matched: list[dict[str, Any]] = []
    flipped: list[dict[str, Any]] = []
    for entry in parked.get("parked", []):
        rc = entry.get("resume_condition") or {}
        if rc.get("type") != "dependency":
            continue
        targets = rc.get("targets") or []
        if not isinstance(targets, list):
            continue
        if card_id not in targets:
            continue
        matched.append(entry)
        prev_ready = bool(entry.get("ready_to_resume", False))
        now_ready = _eval_resume_condition(entry, kanban, today)
        entry["ready_to_resume"] = now_ready
        entry["last_checked"] = today.isoformat()
        if now_ready and not prev_ready:
            flipped.append(entry)
    return {"matched": matched, "flipped": flipped}


def park_card(parked: dict[str, Any], kanban: dict[str, Any], card_id: str, resume_condition: dict[str, Any], snapshot_extra: str = "") -> dict[str, Any]:
    """Move a kanban card into the parked list. Captures full state snapshot. Returns the new parked entry."""
    target_card = None
    for card in kanban.get("cards", []):
        if card.get("id") == card_id:
            target_card = card
            break
    if target_card is None:
        raise ValueError(f"card {card_id} not found in kanban state")

    snapshot_lines = [
        f"Title: {target_card.get('title','')}",
        f"Pillar: {target_card.get('pillar','')}",
        f"Column: {target_card.get('column','')}",
        f"Priority: {target_card.get('priority')}",
    ]
    if target_card.get("description"):
        snapshot_lines.append(f"Description: {target_card['description']}")
    if target_card.get("notes"):
        snapshot_lines.append(f"Notes: {target_card['notes']}")
    sts = target_card.get("subtasks") or []
    if sts:
        snapshot_lines.append("Subtasks:")
        for st in sts:
            snapshot_lines.append(f"  - [{st.get('status','?')}] {st.get('title','')}")
    if snapshot_extra:
        snapshot_lines.append(f"Park note: {snapshot_extra}")

    new_id = _next_parked_id(parked)
    today = _today_str()
    entry = {
        "id": new_id,
        "title": target_card.get("title", ""),
        "pillar": target_card.get("pillar", ""),
        "parked_date": today,
        "parked_from_session": "manual-park",
        "last_state_snapshot": "\n".join(snapshot_lines),
        "resume_condition": resume_condition,
        "context_refs": [],
        "kanban_card_origin": card_id,
        "last_checked": today,
        "tags": [],
        "ready_to_resume": False,
        "stale": False,
        "x_archived_card_payload": target_card,
    }
    parked.setdefault("parked", []).append(entry)

    kanban["cards"] = [c for c in kanban.get("cards", []) if c.get("id") != card_id]
    return entry


def unpark_entry(parked: dict[str, Any], kanban: dict[str, Any], parked_id: str) -> dict[str, Any]:
    """Restore a parked entry as a kanban card. Returns the restored card."""
    target = None
    for entry in parked.get("parked", []):
        if entry.get("id") == parked_id:
            target = entry
            break
    if target is None:
        raise ValueError(f"parked entry {parked_id} not found")

    archived_payload = target.get("x_archived_card_payload")
    if archived_payload:
        restored = dict(archived_payload)
        restored["column"] = "active"
    else:
        restored = {
            "id": target.get("kanban_card_origin") or f"c{int(dt.datetime.now().timestamp())%10000:04d}",
            "title": target.get("title", ""),
            "pillar": target.get("pillar", ""),
            "column": "active",
            "order": 1000,
            "priority": 3,
            "description": target.get("last_state_snapshot", ""),
            "notes": "",
            "subtasks": [],
            "flag": None,
            "flagged_at": None,
            "created": target.get("parked_date") or _today_str(),
            "updated": _today_str(),
        }
    restored["updated"] = _today_str()

    existing_ids = {c.get("id") for c in kanban.get("cards", [])}
    if restored["id"] in existing_ids:
        restored["id"] = f"{restored['id']}-r{int(dt.datetime.now().timestamp())%10000:04d}"
    kanban.setdefault("cards", []).append(restored)

    parked["parked"] = [e for e in parked.get("parked", []) if e.get("id") != parked_id]
    parked.setdefault("archived", []).append({
        "id": target.get("id"),
        "title": target.get("title"),
        "archived_date": _today_str(),
        "archived_reason": "unparked",
        "original": target,
    })
    return restored


def mark_parked_complete(parked: dict[str, Any], parked_id: str, reason: str = "completed") -> dict[str, Any]:
    """Archive a parked entry without restoring it to kanban (work was done elsewhere or no longer needed)."""
    target = None
    for entry in parked.get("parked", []):
        if entry.get("id") == parked_id:
            target = entry
            break
    if target is None:
        raise ValueError(f"parked entry {parked_id} not found")
    parked["parked"] = [e for e in parked.get("parked", []) if e.get("id") != parked_id]
    parked.setdefault("archived", []).append({
        "id": target.get("id"),
        "title": target.get("title"),
        "archived_date": _today_str(),
        "archived_reason": reason,
        "original": target,
    })
    return target


def _next_parked_id(parked: dict[str, Any]) -> str:
    max_n = 0
    for entry in parked.get("parked", []) + parked.get("archived", []):
        pid = entry.get("id", "") if isinstance(entry, dict) else ""
        if pid.startswith("pt-"):
            try:
                n = int(pid[3:])
                if n > max_n:
                    max_n = n
            except ValueError:
                pass
    return f"pt-{max_n + 1:03d}"


def set_bug_status(bugs: dict[str, Any], bug_id: str, new_status: str, fix_commit: str | None = None) -> dict[str, Any]:
    if new_status not in BUG_STATUS_VALUES:
        raise ValueError(f"bug status must be one of {BUG_STATUS_VALUES}, got {new_status!r}")
    for bug in bugs.get("bugs", []):
        if bug.get("id") == bug_id:
            bug["status"] = new_status
            if new_status == "fixed" and not bug.get("fixed_date"):
                bug["fixed_date"] = _today_str()
            if fix_commit:
                bug["fix_commit"] = fix_commit
            return bug
    raise ValueError(f"bug {bug_id} not found")


def _cmd_update_ready(args: argparse.Namespace) -> int:
    parked = load_json(PARKED_PATH)
    kanban = load_json(KANBAN_PATH)
    flipped = evaluate_resume_conditions(parked, kanban)
    save_json_atomic(PARKED_PATH, parked)
    print(f"update-ready: {len(parked.get('parked', []))} parked entries scanned, {len(flipped)} newly ready")
    for entry in flipped:
        print(f"  READY: {entry['id']} ({entry['title']})")
    return 0


def _cmd_update_stale(args: argparse.Namespace) -> int:
    parked = load_json(PARKED_PATH)
    newly = evaluate_staleness(parked)
    save_json_atomic(PARKED_PATH, parked)
    total_stale = sum(1 for e in parked.get("parked", []) if e.get("stale"))
    print(f"update-stale: {len(parked.get('parked', []))} entries scanned, {total_stale} stale ({len(newly)} newly flagged)")
    for entry in newly:
        print(f"  STALE: {entry['id']} ({entry['title']})")
    return 0


def _cmd_cascade(args: argparse.Namespace) -> int:
    parked = load_json(PARKED_PATH)
    kanban = load_json(KANBAN_PATH)
    result = cascade_card_done(parked, kanban, args.card_done)
    save_json_atomic(PARKED_PATH, parked)
    print(f"cascade: card-done={args.card_done} matched={len(result['matched'])} flipped={len(result['flipped'])}")
    for entry in result["flipped"]:
        print(f"  READY: {entry['id']} ({entry['title']})")
    return 0


def _cmd_list(args: argparse.Namespace) -> int:
    parked = load_json(PARKED_PATH)
    bugs = load_json(BUGS_PATH)
    items = parked.get("parked", [])
    ready = [e for e in items if e.get("ready_to_resume")]
    stale = [e for e in items if e.get("stale")]
    print(f"PARKED: {len(items)} total, {len(ready)} ready, {len(stale)} stale")
    for entry in items:
        flags = []
        if entry.get("ready_to_resume"):
            flags.append("READY")
        if entry.get("stale"):
            flags.append("STALE")
        flag_str = (" [" + ", ".join(flags) + "]") if flags else ""
        rc = entry.get("resume_condition") or {}
        rc_desc = f"{rc.get('type','?')}:{rc.get('targets')}"
        print(f"  {entry['id']:<8}  {entry.get('pillar',''):<18}  {entry.get('title','')[:60]:<60}  ({rc_desc}){flag_str}")
    open_bugs = [b for b in bugs.get("bugs", []) if b.get("status") != "fixed"]
    print()
    print(f"BUGS: {len(bugs.get('bugs', []))} total, {len(open_bugs)} non-fixed")
    for bug in bugs.get("bugs", []):
        print(f"  {bug['id']:<8}  {bug.get('severity','?'):<8}  {bug.get('status','?'):<28}  {bug.get('title','')[:60]}")
    return 0


def _cmd_bug_status(args: argparse.Namespace) -> int:
    bugs = load_json(BUGS_PATH)
    bug = set_bug_status(bugs, args.bug, args.to, fix_commit=args.fix_commit)
    save_json_atomic(BUGS_PATH, bugs)
    print(f"bug-status: {bug['id']} -> {bug['status']}" + (f" (fix_commit={bug.get('fix_commit')})" if bug.get('fix_commit') else ""))
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Parked-thread evaluator + bug-state helpers")
    sub = parser.add_subparsers(dest="cmd", required=True)

    sp_ready = sub.add_parser("update-ready", help="evaluate resume conditions and write ready_to_resume flags")
    sp_ready.set_defaults(func=_cmd_update_ready)

    sp_stale = sub.add_parser("update-stale", help="flip stale flags where last_checked + 14d < today")
    sp_stale.set_defaults(func=_cmd_update_stale)

    sp_cascade = sub.add_parser("cascade", help="cascade after a kanban card is marked done")
    sp_cascade.add_argument("--card-done", required=True, help="kanban card id that just completed")
    sp_cascade.set_defaults(func=_cmd_cascade)

    sp_list = sub.add_parser("list", help="print parked + bug summary")
    sp_list.set_defaults(func=_cmd_list)

    sp_bug = sub.add_parser("bug-status", help="flip a bug status (open|triaged|fix-pending-verification|fixed)")
    sp_bug.add_argument("--bug", required=True, help="bug id, e.g. B-323")
    sp_bug.add_argument("--to", required=True, choices=BUG_STATUS_VALUES, help="new status")
    sp_bug.add_argument("--fix-commit", default=None, help="commit SHA or worktree name when flipping to fixed")
    sp_bug.set_defaults(func=_cmd_bug_status)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())

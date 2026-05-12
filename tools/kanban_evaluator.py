#!/usr/bin/env python3
"""Kanban decision-request evaluator.

Callable from the daily-flow orchestrator (and any other automation) AND as a
Python library import.

CLI subcommands:
  check-active-blocks       list cards in 'active' (or priority-1 'backlog') that
                            are blocked on open questions; orchestrator skips
                            spawning work on these cards
  interpret-pending         list custom-answer questions whose interpretation
                            has not been written yet; orchestrator's Claude pass
                            reads, then POSTs the interpretation back via the API
  cascade-on-answer         --question-id <q-NNN>: when a question resolves, run
                            the parked-thread cascade (same as
                            parked_evaluator.cascade) in case any parked threads
                            were waiting on this card
  list-pending-completions  list every card with pending_completion non-null;
                            orchestrator surfaces these in step 6 daily log
                            under Awaiting Your Confirmation (c123)

State file (resolved relative to repo root):
  tools/kanban/state.json

Schema extensibility: additive-only, unknown fields tolerated, reserved namespace
x_*, schema_version + semantic_version on the state file root. Loader uses .get()
with defaults so missing fields do not raise.

Companion design doc: context/designs/decision-request-mechanism.md
Companion module: tools/parked_evaluator.py (re-used for the cascade-on-answer
parked side).
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from typing import Any

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
KANBAN_PATH = REPO_ROOT / "tools" / "kanban" / "state.json"


def load_json(path: pathlib.Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def _question_is_resolved(q: dict[str, Any]) -> bool:
    if not q.get("answered_date"):
        return False
    at = q.get("answer_type")
    if at == "choice":
        return True
    if at == "custom":
        return bool(q.get("interpretation_confirmed"))
    return False


def _card_blocked_q_ids(card: dict[str, Any]) -> list[str]:
    return [
        q.get("id", "")
        for q in (card.get("open_questions") or [])
        if not _question_is_resolved(q)
    ]


def _find_card(data: dict[str, Any], card_id: str) -> dict[str, Any] | None:
    for card in data.get("cards", []):
        if card.get("id") == card_id:
            return card
    return None


def _find_question_on_card(card: dict[str, Any], q_id: str) -> dict[str, Any] | None:
    for q in card.get("open_questions") or []:
        if q.get("id") == q_id:
            return q
    return None


# ---------- public library API ----------

def list_blocked_cards(
    kanban: dict[str, Any],
    columns: tuple[str, ...] = ("active",),
    upcoming_priority_threshold: int = 1,
) -> list[dict[str, Any]]:
    """Cards blocked on open questions that the orchestrator should NOT spawn work on.

    Definition:
    - column in `columns` (default: just 'active'), OR
    - column == 'backlog' AND priority is a number <= upcoming_priority_threshold.

    A card is blocked when it has at least one unresolved open_question.
    Returns a list of {card_id, title, column, pillar, q_ids, count}.
    """
    blocked: list[dict[str, Any]] = []
    for card in kanban.get("cards", []):
        col = card.get("column")
        priority = card.get("priority")
        in_active = col in columns
        upcoming = (
            col == "backlog"
            and isinstance(priority, int)
            and priority <= upcoming_priority_threshold
        )
        if not (in_active or upcoming):
            continue
        q_ids = _card_blocked_q_ids(card)
        if not q_ids:
            continue
        blocked.append({
            "card_id": card.get("id"),
            "title": card.get("title", ""),
            "column": col,
            "pillar": card.get("pillar", ""),
            "priority": priority,
            "q_ids": q_ids,
            "count": len(q_ids),
        })
    return blocked


def list_pending_completions(kanban: dict[str, Any]) -> list[dict[str, Any]]:
    """Cards whose pending_completion field is non-null (c123).

    These cards are awaiting Mike's Confirm or Reject from the kanban browser.
    Orchestrator surfaces this list in the daily log under Awaiting Your
    Confirmation; sessions read it to avoid double-marking a card already
    flagged ready.
    """
    pending: list[dict[str, Any]] = []
    for card in kanban.get("cards", []):
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
            "evidence_refs": pc.get("evidence_refs", []),
        })
    pending.sort(key=lambda e: e.get("marked_at") or "")
    return pending


def list_pending_interpretations(kanban: dict[str, Any]) -> list[dict[str, Any]]:
    """Custom-answer questions whose interpretation has not been written yet."""
    pending: list[dict[str, Any]] = []
    for card in kanban.get("cards", []):
        for q in card.get("open_questions") or []:
            if q.get("answer_type") != "custom":
                continue
            if q.get("interpretation"):
                continue
            if not q.get("custom_text"):
                continue
            pending.append({
                "card_id": card.get("id"),
                "card_title": card.get("title", ""),
                "card_pillar": card.get("pillar", ""),
                "question_id": q.get("id"),
                "question": q.get("question", ""),
                "custom_text": q.get("custom_text", ""),
                "answered_date": q.get("answered_date"),
                "asked_by": q.get("asked_by", ""),
            })
    return pending


def find_question(kanban: dict[str, Any], q_id: str) -> tuple[dict[str, Any] | None, dict[str, Any] | None]:
    """Locate a question across all cards. Returns (card, question) or (None, None)."""
    for card in kanban.get("cards", []):
        q = _find_question_on_card(card, q_id)
        if q is not None:
            return card, q
    return None, None


# ---------- CLI subcommands ----------

def _cmd_check_active_blocks(args: argparse.Namespace) -> int:
    kanban = load_json(KANBAN_PATH)
    if not kanban:
        print(json.dumps({"blocked": [], "total_blocked_cards": 0, "total_open_questions": 0}))
        return 0
    blocked = list_blocked_cards(kanban)
    total_qs = sum(b["count"] for b in blocked)
    payload = {
        "blocked": blocked,
        "total_blocked_cards": len(blocked),
        "total_open_questions": total_qs,
    }
    print(json.dumps(payload, indent=2))
    return 0


def _cmd_interpret_pending(args: argparse.Namespace) -> int:
    kanban = load_json(KANBAN_PATH)
    if not kanban:
        print(json.dumps({"pending": []}))
        return 0
    pending = list_pending_interpretations(kanban)
    print(json.dumps({"pending": pending}, indent=2))
    return 0


def _cmd_list_pending_completions(args: argparse.Namespace) -> int:
    kanban = load_json(KANBAN_PATH)
    if not kanban:
        print(json.dumps({"pending": [], "pending_count": 0}))
        return 0
    pending = list_pending_completions(kanban)
    print(json.dumps({"pending": pending, "pending_count": len(pending)}, indent=2))
    return 0


def _cmd_cascade_on_answer(args: argparse.Namespace) -> int:
    kanban = load_json(KANBAN_PATH)
    if not kanban:
        print(json.dumps({"error": "no kanban state"}))
        return 2
    card, q = find_question(kanban, args.question_id)
    if card is None or q is None:
        print(json.dumps({"error": f"question {args.question_id} not found"}))
        return 2

    card_id = card.get("id")
    card_unblocked = len(_card_blocked_q_ids(card)) == 0
    result: dict[str, Any] = {
        "question_id": args.question_id,
        "card_id": card_id,
        "card_unblocked": card_unblocked,
        "parked_cascade": None,
    }

    if card_unblocked:
        # Only run the parked-thread cascade once a card is fully unblocked.
        try:
            sys.path.insert(0, str(REPO_ROOT / "tools"))
            import parked_evaluator as pe  # type: ignore
            parked = pe.load_json(pe.PARKED_PATH)
            cascade_result = pe.cascade_card_done(parked, kanban, card_id)
            pe.save_json_atomic(pe.PARKED_PATH, parked)
            result["parked_cascade"] = {
                "matched": [{"id": e.get("id"), "title": e.get("title", "")} for e in cascade_result["matched"]],
                "flipped": [{"id": e.get("id"), "title": e.get("title", "")} for e in cascade_result["flipped"]],
            }
        except Exception as exc:
            result["parked_cascade"] = {"error": str(exc)}

    print(json.dumps(result, indent=2))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Kanban decision-request evaluator (c121).")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p1 = sub.add_parser("check-active-blocks", help="list active/upcoming cards blocked on open questions")
    p1.set_defaults(func=_cmd_check_active_blocks)

    p2 = sub.add_parser("interpret-pending", help="list custom-answer questions awaiting orchestrator interpretation")
    p2.set_defaults(func=_cmd_interpret_pending)

    p3 = sub.add_parser("cascade-on-answer", help="run parked-thread cascade when a question resolves a card")
    p3.add_argument("--question-id", required=True, help="q-NNN id")
    p3.set_defaults(func=_cmd_cascade_on_answer)

    p4 = sub.add_parser("list-pending-completions", help="list cards marked ready for completion review (c123)")
    p4.set_defaults(func=_cmd_list_pending_completions)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())

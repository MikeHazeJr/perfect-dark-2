#!/usr/bin/env python3
"""
Daily-flow orchestrator - the Python entrypoint for the seven-step pipeline.

Invocation:
  python -m tools.daily_flow.orchestrator [--force] [--no-merge] [--no-push] [--catchup-only]

The package uses underscore-named directory (tools/daily_flow) for valid Python
imports; the canonical product name is still "daily-flow" in user-facing surfaces.

This is the mechanical layer. It is safe to run on a clean tree without a
Claude session. The Claude orchestrator-prompt.md drives this script and
contributes judgment for: (a) dirty-tree merge resolution, (b) weekly
narrative prose, (c) monthly narrative prose.

Exit codes:
  0  success (full or no-op)
  1  partial run (failure recorded in state/, daily log marked partial)
  2  unrecoverable error (state file corrupted, refused to write)
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import sys
import traceback
from typing import Any

from .lib import fsutil, gitutil, timefmt
from .steps import (
    step1_audit,
    step2_state_sync,
    step3_cascade,
    step4_merge_consolidate,
    step5_priority_sort,
    step6_daily_log,
    step7_briefing,
)

LOG_PREFIX = "DAILY-FLOW.ORCHESTRATOR"


def _last_run_path() -> pathlib.Path:
    return fsutil.state_dir() / "last-run.json"


def _record_last_run(today_iso: str, status: str, failure_point: str | None = None) -> None:
    payload = {
        "schema_version": 1,
        "date": today_iso,
        "completed_at": timefmt.format_iso_et(),
        "status": status,
        "failure_point": failure_point,
    }
    fsutil.save_json_atomic(_last_run_path(), payload)


def _detect_missed_days(today: dt.date) -> list[dt.date]:
    """Returns the list of calendar days that have no daily log AND fall between the last run
    and today (exclusive on today). Oldest-first."""
    last = fsutil.load_json(_last_run_path(), default={})
    last_date_iso = last.get("date")
    if last_date_iso:
        try:
            last_date = dt.date.fromisoformat(last_date_iso)
        except ValueError:
            last_date = today - dt.timedelta(days=1)
    else:
        last_date = today - dt.timedelta(days=1)

    missing: list[dt.date] = []
    cur = last_date + dt.timedelta(days=1)
    while cur < today:
        log = fsutil.repo_root() / "context" / "daily-logs" / f"{cur.isoformat()}.md"
        if not log.exists():
            missing.append(cur)
        cur += dt.timedelta(days=1)
    return missing


def _print(prefix: str, payload: Any) -> None:
    print(f"{prefix} {json.dumps(payload, ensure_ascii=False)}", flush=True)


def run_pipeline(
    *,
    today: dt.date | None = None,
    force: bool = False,
    do_merge: bool = True,
    do_push: bool = True,
) -> dict[str, Any]:
    today = today or timefmt.today_et()
    today_iso = today.isoformat()
    print(f"{LOG_PREFIX}.START date={today_iso} head={gitutil.head_sha()}", flush=True)

    partial = False
    failure_point: str | None = None

    audit: dict[str, Any] = {}
    state_sync: dict[str, Any] = {}
    cascade: dict[str, Any] = {}
    merge_consolidate: dict[str, Any] = {"merged": [], "skipped": [], "candidates": []}
    sort_result: dict[str, Any] = {}
    log_result: dict[str, Any] = {}
    briefing: dict[str, Any] = {}

    try:
        audit = step1_audit.run_audit(today, force=force)
        _print(f"{LOG_PREFIX}.STEP1.OK", {"commits": len(audit.get("commits", [])), "sessions": len(audit.get("sessions", []))})
    except Exception as exc:
        partial = True
        failure_point = "step1_audit"
        _print(f"{LOG_PREFIX}.STEP1.FAIL", {"error": str(exc), "traceback": traceback.format_exc()})

    if not partial:
        try:
            state_sync = step2_state_sync.run_state_sync(audit, today)
            _print(f"{LOG_PREFIX}.STEP2.OK", {"cards_flipped": len(state_sync.get("cards_flipped", [])), "bugs_flipped": len(state_sync.get("bugs_flipped", []))})
        except Exception as exc:
            partial = True
            failure_point = "step2_state_sync"
            _print(f"{LOG_PREFIX}.STEP2.FAIL", {"error": str(exc), "traceback": traceback.format_exc()})

    if not partial:
        try:
            cascade = step3_cascade.run_cascade(state_sync, today)
            _print(f"{LOG_PREFIX}.STEP3.OK", {"events": len(cascade.get("cascade_events", []))})
        except Exception as exc:
            partial = True
            failure_point = "step3_cascade"
            _print(f"{LOG_PREFIX}.STEP3.FAIL", {"error": str(exc), "traceback": traceback.format_exc()})

    if not partial and do_merge:
        try:
            merge_consolidate = step4_merge_consolidate.run_auto_consolidate(push=do_push, today=today)
            _print(f"{LOG_PREFIX}.STEP4.OK", {
                "merged": len(merge_consolidate.get("merged", [])),
                "skipped": len(merge_consolidate.get("skipped", [])),
                "dirty_tree": merge_consolidate.get("skipped_due_to_dirty_tree", False),
            })
        except Exception as exc:
            partial = True
            failure_point = "step4_merge_consolidate"
            _print(f"{LOG_PREFIX}.STEP4.FAIL", {"error": str(exc), "traceback": traceback.format_exc()})

    if not partial:
        try:
            sort_result = step5_priority_sort.run_priority_sort()
            _print(f"{LOG_PREFIX}.STEP5.OK", {"focus_count": len(sort_result.get("focus_shortlist", []))})
        except Exception as exc:
            partial = True
            failure_point = "step5_priority_sort"
            _print(f"{LOG_PREFIX}.STEP5.FAIL", {"error": str(exc), "traceback": traceback.format_exc()})

    try:
        log_result = step6_daily_log.run_daily_log(
            audit=audit,
            state_sync=state_sync,
            cascade=cascade,
            merge_consolidate=merge_consolidate,
            priority_sort=sort_result,
            today=today,
            partial=partial,
            failure_point=failure_point,
        )
        _print(f"{LOG_PREFIX}.STEP6.OK", {"log": log_result.get("log_path"), "decisions": log_result.get("decisions_allocated", [])})
    except Exception as exc:
        partial = True
        failure_point = failure_point or "step6_daily_log"
        _print(f"{LOG_PREFIX}.STEP6.FAIL", {"error": str(exc), "traceback": traceback.format_exc()})

    try:
        briefing = step7_briefing.run_briefing(
            audit=audit,
            state_sync=state_sync,
            priority_sort=sort_result,
            daily_log_result=log_result,
            today=today,
            partial=partial,
            failure_point=failure_point,
        )
        _print(f"{LOG_PREFIX}.STEP7.OK", {"briefing": "tools/kanban/daily-briefing.json", "headlines": len(briefing.get("headlines", []))})
    except Exception as exc:
        partial = True
        failure_point = failure_point or "step7_briefing"
        _print(f"{LOG_PREFIX}.STEP7.FAIL", {"error": str(exc), "traceback": traceback.format_exc()})

    _record_last_run(today_iso, status=("partial" if partial else "ok"), failure_point=failure_point)

    return {
        "for_date": today_iso,
        "partial": partial,
        "failure_point": failure_point,
        "audit": audit,
        "state_sync": state_sync,
        "cascade": cascade,
        "merge_consolidate": merge_consolidate,
        "priority_sort": sort_result,
        "daily_log": log_result,
        "briefing": briefing,
    }


def run_catchup(today: dt.date) -> list[dict[str, Any]]:
    missing = _detect_missed_days(today)
    catchup_results: list[dict[str, Any]] = []
    for day in missing:
        print(f"{LOG_PREFIX}.CATCHUP date={day.isoformat()}", flush=True)
        result = run_pipeline(today=day, force=True, do_merge=False, do_push=False)
        catchup_results.append({"date": day.isoformat(), "partial": result.get("partial")})
    return catchup_results


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Daily-flow orchestrator pipeline.")
    parser.add_argument("--force", action="store_true", help="Force re-run even if today's audit cache exists.")
    parser.add_argument("--no-merge", action="store_true", help="Skip Step 4 (merge consolidation).")
    parser.add_argument("--no-push", action="store_true", help="Skip the post-merge push.")
    parser.add_argument("--catchup-only", action="store_true", help="Run catch-up for missed days but skip today.")
    parser.add_argument("--for-date", default=None, help="Override today (YYYY-MM-DD). Used in catch-up internals.")
    args = parser.parse_args(argv)

    today = timefmt.today_et() if args.for_date is None else dt.date.fromisoformat(args.for_date)

    catchup = run_catchup(today)
    if catchup:
        _print(f"{LOG_PREFIX}.CATCHUP.COMPLETE", {"days": [c["date"] for c in catchup]})

    if args.catchup_only:
        return 0

    result = run_pipeline(today=today, force=args.force, do_merge=not args.no_merge, do_push=not args.no_push)
    return 1 if result.get("partial") else 0


if __name__ == "__main__":
    sys.exit(main())

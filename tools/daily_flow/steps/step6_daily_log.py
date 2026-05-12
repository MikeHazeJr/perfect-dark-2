"""
Step 6 - Daily log generation.

Assembles the daily log markdown from the prior step outputs and writes it to
context/daily-logs/YYYY-MM-DD.md. The decisions section is template-enforced:
the header is always present even when empty, so downstream rollups have a
consistent shape to compact from.

Sessions append proposed decisions to .claude/scratch/<slug>.md under the
marker `### dec-PROPOSED: <title>`. We scan for those, allocate real
dec-NNN ids from the registry, and emit them into today's log.
"""

from __future__ import annotations

import datetime as dt
import pathlib
from typing import Any

from ..lib import decisions, fsutil, templates, timefmt

LOG_PREFIX = "DAILY-FLOW.DAILY_LOG"


def _log_path(date_iso: str) -> pathlib.Path:
    return fsutil.repo_root() / "context" / "daily-logs" / f"{date_iso}.md"


def _scratch_dir() -> pathlib.Path:
    return fsutil.repo_root() / ".claude" / "scratch"


def _render_shipped(audit: dict[str, Any]) -> str:
    commits = audit.get("commits", [])
    sessions = audit.get("sessions", [])
    transitions = [t for t in audit.get("kanban_transitions", []) if t.get("to") == "done"]

    if not commits and not sessions and not transitions:
        return "(none)"

    lines: list[str] = []
    if commits:
        lines.append("Commits on dev:")
        for c in commits[:30]:
            sha = (c.get("sha") or "")[:8]
            lines.append(f"- `{sha}` {c.get('subject', '').strip()}")
        if len(commits) > 30:
            lines.append(f"- ... and {len(commits) - 30} more")
    if sessions:
        lines.append("")
        lines.append("Sessions in window:")
        for s in sessions[:30]:
            lines.append(f"- `{s.get('slug', '')}` -- {s.get('title', '')}")
    if transitions:
        lines.append("")
        lines.append("Kanban cards moved to done:")
        for t in transitions:
            lines.append(f"- `{t.get('id', '')}` {t.get('title', '')} ({t.get('from', '')} -> done)")
    return "\n".join(lines)


def _render_bugs(audit: dict[str, Any], state_sync: dict[str, Any]) -> str:
    bugs_flipped = state_sync.get("bugs_flipped", [])
    smoke = audit.get("smoke_results", [])
    regressions: list[str] = []
    for run in smoke:
        for r in run.get("results", []):
            if not r.get("Passed"):
                rid = r.get("BugId") or r.get("Name", "")
                regressions.append(f"- `{rid}` failed (assertions {r.get('AssertionsMet', 0)}/{r.get('AssertionsTotal', 0)})")

    parts: list[str] = []
    parts.append(f"- Filed: (orchestrator does not file bugs; see [bugs.md](../bugs.md))")
    if bugs_flipped:
        rs = [f"`{b.get('id')}` (was {b.get('from')})" for b in bugs_flipped]
        parts.append(f"- Resolved: {', '.join(rs)}")
    else:
        parts.append("- Resolved: none")
    if regressions:
        parts.append("- Regressions caught:")
        parts.extend(regressions)
    else:
        parts.append("- Regressions caught: none")
    return "\n".join(parts)


def _render_decisions(decisions_today: list[dict[str, Any]]) -> str:
    if not decisions_today:
        return "(none)"
    return "\n".join(templates.render_decision_block(d) for d in decisions_today)


def _render_parked(cascade: dict[str, Any], audit_date_iso: str) -> str:
    parked = fsutil.load_json(fsutil.repo_root() / "tools" / "kanban" / "parked.json", default={})
    parked_entries = parked.get("parked", [])

    newly_parked = [p for p in parked_entries if p.get("parked_date") == audit_date_iso]
    newly_ready = [p for p in parked_entries if p.get("ready_to_resume") and p.get("last_checked") == audit_date_iso]
    stale = [p for p in parked_entries if p.get("stale")]

    cascade_events = cascade.get("cascade_events", [])

    parts: list[str] = []
    parts.append(f"- Newly parked: {', '.join(p.get('id') for p in newly_parked) if newly_parked else 'none'}")
    parts.append(f"- Newly ready: {', '.join(p.get('id') for p in newly_ready) if newly_ready else 'none'}")
    if cascade_events:
        ev = ", ".join(f"{e.get('card')}->parked-evaluator" for e in cascade_events)
        parts.append(f"- Cascade events: {ev}")
    else:
        parts.append("- Cascade events: none")
    parts.append(f"- Stale needing triage: {', '.join(p.get('id') for p in stale) if stale else 'none'}")
    return "\n".join(parts)


def _render_awaiting_confirmation(today_iso: str) -> str:
    """Section c123: cards with pending_completion non-null.

    Surfaces every card marked ready for review by sessions/orchestrator in the
    prior 24h or earlier (until Mike confirms or rejects). Empty by design when
    nothing is queued -- header preserved so the shape is consistent.
    """
    state_path = fsutil.repo_root() / "tools" / "kanban" / "state.json"
    state = fsutil.load_json(state_path, default={})
    if not state:
        return "(none)"
    try:
        import sys as _sys
        _sys.path.insert(0, str(fsutil.repo_root() / "tools"))
        import kanban_evaluator as ke  # type: ignore
        pending = ke.list_pending_completions(state)
    except Exception as exc:
        return f"(evaluator error: {exc})"
    if not pending:
        return "(none)"

    lines: list[str] = []
    cutoff = today_iso  # used only for the "marked today" callout
    for entry in pending:
        cid = entry.get("card_id", "?")
        title = entry.get("title") or entry.get("card_title", "")
        marked_at = entry.get("marked_at") or ""
        marked_by = entry.get("marked_by") or "unknown"
        column = entry.get("card_column") or "?"
        summary = entry.get("summary") or "(no summary)"
        evidence = entry.get("evidence_refs") or []
        marked_today = " (marked today)" if marked_at.startswith(cutoff) else ""
        lines.append(f"### `{cid}` {title} -- in `{column}`{marked_today}")
        lines.append(f"- Marked by: {marked_by} at {marked_at}")
        lines.append(f"- Summary: {summary}")
        if evidence:
            ev_str = ", ".join(f"`{e}`" for e in evidence)
            lines.append(f"- Evidence: {ev_str}")
        lines.append(f"- Review at: http://localhost:7531/  (Ready button in header, then Confirm or Reject)")
        lines.append("")
    return "\n".join(lines).rstrip()


def _render_focus(priority_sort: dict[str, Any]) -> str:
    shortlist = priority_sort.get("focus_shortlist", [])
    if not shortlist:
        return "(none)"
    lines: list[str] = []
    for i, item in enumerate(shortlist, 1):
        kind = item.get("kind", "?")
        ident = item.get("id", "")
        title = item.get("title", "")
        rationale = item.get("rationale", "")
        lines.append(f"{i}. [{kind}] `{ident}` -- {title}. {rationale}")
    return "\n".join(lines)


def _allocate_decision_ids(today_iso: str, log_path: pathlib.Path) -> list[dict[str, Any]]:
    """Scan scratch files for dec-PROPOSED entries, allocate ids, return rendered decisions."""
    reg = decisions.load_registry()
    proposals = decisions.scan_scratch_for_proposed(_scratch_dir())
    if not proposals:
        return []
    allocated: list[dict[str, Any]] = []
    for p in proposals:
        entry = decisions.register_decision(
            reg,
            title=p.get("title", "(untitled)"),
            decided=p.get("decided", "") or "(unspecified)",
            why=p.get("why", "") or "(unspecified)",
            alternatives=p.get("alternatives") or [],
            not_done=p.get("not_done") or [],
            implications=p.get("implications", "") or "(unspecified)",
            daily_log_path=fsutil.posix_rel(log_path),
        )
        allocated.append(entry)
    if allocated:
        decisions.save_registry(reg)
    return allocated


def run_daily_log(
    *,
    audit: dict[str, Any],
    state_sync: dict[str, Any],
    cascade: dict[str, Any],
    merge_consolidate: dict[str, Any],
    priority_sort: dict[str, Any],
    today: dt.date | None = None,
    partial: bool = False,
    failure_point: str | None = None,
) -> dict[str, Any]:
    today = today or timefmt.today_et()
    today_iso = today.isoformat()
    log_path = _log_path(today_iso)

    decisions_today = _allocate_decision_ids(today_iso, log_path)

    sections = {
        "Yesterday Shipped": _render_shipped(audit),
        "Bugs": _render_bugs(audit, state_sync),
        "Decisions": _render_decisions(decisions_today),
        "Awaiting Your Confirmation": _render_awaiting_confirmation(today_iso),
        "Parked Threads": _render_parked(cascade, today_iso),
        "Today Focus": _render_focus(priority_sort),
    }

    if merge_consolidate.get("skipped"):
        sections["Yesterday Shipped"] += "\n\nMerge consolidation notes:\n"
        for s in merge_consolidate.get("skipped", []):
            sections["Yesterday Shipped"] += f"- skipped {s.get('branch')}: {s.get('reason')}\n"
    if merge_consolidate.get("merged"):
        sections["Yesterday Shipped"] += "\n\nBranches merged this morning:\n"
        for m in merge_consolidate.get("merged", []):
            sections["Yesterday Shipped"] += f"- {m.get('branch')} (push_ok={m.get('push_ok')}, deleted={m.get('branch_deleted')})\n"

    if partial and failure_point:
        sections["Today Focus"] = (
            f"PARTIAL RUN: failure at {failure_point}. The orchestrator should be re-run "
            f"after the failure is resolved. Pre-failure data preserved in tools/daily_flow/state/."
            f"\n\n" + sections["Today Focus"]
        )

    window = audit.get("window", {"start": "", "end": ""})
    body = templates.render_daily(
        date=today_iso,
        ts_display=timefmt.format_display(),
        window_start=window.get("start", ""),
        window_end=window.get("end", ""),
        sections=sections,
        partial=partial,
    )
    fsutil.assert_no_em_dash(body, where=f"daily-log {today_iso}")
    fsutil.write_text_atomic(log_path, body)

    return {
        "for_date": today_iso,
        "log_path": fsutil.posix_rel(log_path),
        "decisions_allocated": [d.get("id") for d in decisions_today],
        "partial": partial,
        "failure_point": failure_point,
    }

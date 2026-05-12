"""
Monthly rollup - run on the first Monday of a new calendar month.

Same two-phase split as weekly: prepare_monthly_draft() gathers raw materials
from the prior month's weekly logs; finalize_monthly() merges narrative with
verbatim Major Decisions into the template, runs quality threshold checks,
and deletes the source weeklies on pass.
"""

from __future__ import annotations

import datetime as dt
import pathlib
import re
from typing import Any

from ..lib import decisions, fsutil, templates, timefmt

LOG_PREFIX = "DAILY-FLOW.ROLLUP_MONTHLY"

MONTHLY_LINE_FLOOR = 120
MONTHLY_LINE_CEILING = 1200

SECTION_RE = re.compile(r"^##\s+(?P<name>.+?)\s*$")


def _weekly_log_path(week_label: str) -> pathlib.Path:
    return fsutil.repo_root() / "context" / "weekly-logs" / f"{week_label}.md"


def _monthly_log_path(month_label: str) -> pathlib.Path:
    return fsutil.repo_root() / "context" / "monthly-logs" / f"{month_label}.md"


def _extract_sections(md_text: str) -> dict[str, str]:
    out: dict[str, str] = {}
    current = None
    buf: list[str] = []
    for line in md_text.splitlines():
        m = SECTION_RE.match(line)
        if m:
            if current is not None:
                out[current] = "\n".join(buf).strip()
            current = m.group("name").strip()
            buf = []
        else:
            if current is not None:
                buf.append(line)
    if current is not None:
        out[current] = "\n".join(buf).strip()
    return out


def _weeklies_in_prior_month(today: dt.date) -> list[str]:
    """Returns ISO week labels (YYYY-WNN) whose Monday falls in the prior month."""
    first_of_this_month = today.replace(day=1)
    prior_last = first_of_this_month - dt.timedelta(days=1)
    cur = prior_last.replace(day=1)
    seen: set[str] = set()
    while cur <= prior_last:
        iso = cur.isocalendar()
        seen.add(f"{iso.year}-W{iso.week:02d}")
        cur += dt.timedelta(days=1)
    weekly_dir = fsutil.repo_root() / "context" / "weekly-logs"
    if not weekly_dir.exists():
        return []
    return sorted([s for s in seen if (weekly_dir / f"{s}.md").exists()])


def prepare_monthly_draft(today: dt.date | None = None) -> dict[str, Any]:
    today = today or timefmt.today_et()
    month_label = timefmt.month_label(today)
    week_labels = _weeklies_in_prior_month(today)

    decisions_verbatim: list[str] = []
    shipped_compiled: list[str] = []
    bugs_compiled: list[str] = []
    direction_compiled: list[str] = []
    stalls_compiled: list[str] = []
    not_taken_compiled: list[str] = []
    notable_compiled: list[str] = []

    for wl in week_labels:
        text = _weekly_log_path(wl).read_text(encoding="utf-8", errors="replace")
        secs = _extract_sections(text)
        dec_body = secs.get("Decisions This Week", "")
        if dec_body and dec_body.strip().lower() not in ("(none)", "none"):
            decisions_verbatim.append(f"<!-- from {wl} -->\n{dec_body.strip()}")
        sh = secs.get("Shipped", "")
        if sh and sh.strip().lower() not in ("(none)", "none"):
            shipped_compiled.append(f"### {wl}\n{sh.strip()}")
        bg = secs.get("Bugs", "")
        if bg and bg.strip().lower() not in ("(none)", "none"):
            bugs_compiled.append(f"### {wl}\n{bg.strip()}")
        sb = secs.get("Stalls and Blockers", "")
        if sb and sb.strip().lower() not in ("(none)", "none"):
            stalls_compiled.append(f"### {wl}\n{sb.strip()}")
        pnt = secs.get("Path Not Taken", "")
        if pnt and pnt.strip().lower() not in ("(none)", "none"):
            not_taken_compiled.append(f"### {wl}\n{pnt.strip()}")

    return {
        "for_month": month_label,
        "weekly_labels": week_labels,
        "decisions_verbatim": decisions_verbatim,
        "shipped_compiled": "\n\n".join(shipped_compiled) if shipped_compiled else "(no shipped activity recorded)",
        "bugs_compiled": "\n\n".join(bugs_compiled) if bugs_compiled else "(no bug activity recorded)",
        "stalls_compiled": "\n\n".join(stalls_compiled) if stalls_compiled else "(no stalls recorded)",
        "path_not_taken_compiled": "\n\n".join(not_taken_compiled) if not_taken_compiled else "(none)",
        "notable_compiled": "\n\n".join(notable_compiled) if notable_compiled else "(none)",
    }


def finalize_monthly(
    draft: dict[str, Any],
    *,
    narrative: dict[str, str],
    today: dt.date | None = None,
    delete_weeklies_if_quality_ok: bool = True,
) -> dict[str, Any]:
    today = today or timefmt.today_et()
    month_label = draft.get("for_month", timefmt.month_label(today))

    sections = {
        "Headline Arc": narrative.get("Headline Arc", "(orchestrator did not supply a Headline Arc)"),
        "Pillar Movements": narrative.get("Pillar Movements", "(none)"),
        "Major Decisions": "\n\n".join(draft.get("decisions_verbatim", [])) or "(none)",
        "Bugs Resolved": narrative.get("Bugs Resolved", draft.get("bugs_compiled", "(none)")),
        "Direction Changes": narrative.get("Direction Changes", "(none)"),
        "Notable Sessions": narrative.get("Notable Sessions", "(none)"),
    }

    body = templates.render_monthly(
        month_label=month_label,
        ts_display=timefmt.format_display(),
        sections=sections,
    )
    fsutil.assert_no_em_dash(body, where=f"monthly-log {month_label}")

    line_count = len(body.splitlines())
    quality_review_required = line_count < MONTHLY_LINE_FLOOR or line_count > MONTHLY_LINE_CEILING

    monthly_path = _monthly_log_path(month_label)
    fsutil.write_text_atomic(monthly_path, body)

    deleted: list[str] = []
    if delete_weeklies_if_quality_ok and not quality_review_required:
        for wl in draft.get("weekly_labels", []):
            path = _weekly_log_path(wl)
            if path.exists():
                path.unlink()
                deleted.append(fsutil.posix_rel(path))

    _stamp_monthly_into_decisions(monthly_path, draft.get("decisions_verbatim", []))

    return {
        "month_label": month_label,
        "monthly_log_path": fsutil.posix_rel(monthly_path),
        "line_count": line_count,
        "compaction_quality_review": quality_review_required,
        "weeklies_deleted": deleted,
        "weeklies_retained_for_review": [
            f"context/weekly-logs/{wl}.md" for wl in draft.get("weekly_labels", [])
            if quality_review_required or not delete_weeklies_if_quality_ok
        ],
    }


def _stamp_monthly_into_decisions(monthly_path: pathlib.Path, decision_blocks: list[str]) -> None:
    reg = decisions.load_registry()
    changed = False
    monthly_rel = fsutil.posix_rel(monthly_path)
    pattern = re.compile(r"Decision id:\s*(dec-\d+)")
    for block in decision_blocks:
        for m in pattern.finditer(block):
            did = m.group(1)
            for entry in reg.get("decisions", []):
                if entry.get("id") == did and entry.get("monthly_log") != monthly_rel:
                    entry["monthly_log"] = monthly_rel
                    changed = True
    if changed:
        decisions.save_registry(reg)

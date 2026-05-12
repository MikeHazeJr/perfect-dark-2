"""
Weekly rollup - run on Mondays.

Two-phase by design:

 1. prepare_weekly_draft() - mechanical: reads the prior Mon-Sun daily logs,
    extracts each section verbatim, gathers decision blocks, counts bug
    activity, returns a structured draft dict.

 2. finalize_weekly(narrative_sections) - mechanical: takes the orchestrator
    session's prose for narrative-only sections (Headline, Stalls, Path Not
    Taken) and merges them with the verbatim decision blocks + shipped lists
    into the template. Writes context/weekly-logs/YYYY-WNN.md. Runs the
    compaction-quality threshold check. If pass, deletes the seven daily
    files in the same commit.

The split is intentional: narrative writing is judgment work, mechanical
preservation of decisions is not.
"""

from __future__ import annotations

import datetime as dt
import pathlib
import re
from typing import Any

from ..lib import decisions, fsutil, templates, timefmt

LOG_PREFIX = "DAILY-FLOW.ROLLUP_WEEKLY"

WEEKLY_LINE_FLOOR = 80
WEEKLY_LINE_CEILING = 600

SECTION_RE = re.compile(r"^##\s+(?P<name>.+?)\s*$")
DECISION_RE = re.compile(r"^### (?P<title>.+?)\s*$\n((?:- .+\n)+)", re.MULTILINE)


def _daily_log_path(date_iso: str) -> pathlib.Path:
    return fsutil.repo_root() / "context" / "daily-logs" / f"{date_iso}.md"


def _weekly_log_path(week_label: str) -> pathlib.Path:
    return fsutil.repo_root() / "context" / "weekly-logs" / f"{week_label}.md"


def _extract_sections(md_text: str) -> dict[str, str]:
    """Returns {section_name: body_text} parsing ## headers."""
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


def _extract_decision_blocks(daily_md: str) -> list[str]:
    """Extracts each `### <title>` decision block (followed by `- Decision id:` lines) verbatim."""
    sections = _extract_sections(daily_md)
    body = sections.get("Decisions", "")
    if not body or body.strip().lower() in ("(none)", "none"):
        return []
    blocks: list[str] = []
    for chunk in re.split(r"\n(?=### )", body):
        if "Decision id:" in chunk:
            blocks.append(chunk.strip())
    return blocks


def prepare_weekly_draft(today: dt.date | None = None) -> dict[str, Any]:
    today = today or timefmt.today_et()
    mon, sun = timefmt.week_window(today)
    week_label = timefmt.iso_week_label(sun)

    days_present: list[str] = []
    days_missing: list[str] = []
    day_sections: dict[str, dict[str, str]] = {}
    all_decisions: list[str] = []

    cur = mon
    while cur <= sun:
        iso = cur.isoformat()
        path = _daily_log_path(iso)
        if path.exists():
            text = path.read_text(encoding="utf-8", errors="replace")
            day_sections[iso] = _extract_sections(text)
            all_decisions.extend(_extract_decision_blocks(text))
            days_present.append(iso)
        else:
            days_missing.append(iso)
        cur += dt.timedelta(days=1)

    shipped_lines: list[str] = []
    bugs_lines: list[str] = []
    parked_lines: list[str] = []
    focus_seen: list[str] = []
    for iso in days_present:
        secs = day_sections[iso]
        sh = secs.get("Yesterday Shipped", "")
        if sh and sh.strip().lower() not in ("(none)", "none"):
            shipped_lines.append(f"### {iso}\n{sh.strip()}")
        bg = secs.get("Bugs", "")
        if bg and bg.strip().lower() not in ("(none)", "none"):
            bugs_lines.append(f"### {iso}\n{bg.strip()}")
        pk = secs.get("Parked Threads", "")
        if pk and pk.strip().lower() not in ("(none)", "none"):
            parked_lines.append(f"### {iso}\n{pk.strip()}")
        fs = secs.get("Today Focus", "")
        if fs:
            focus_seen.append(f"### {iso} focus\n{fs.strip()}")

    return {
        "for_week": week_label,
        "mon": mon.isoformat(),
        "sun": sun.isoformat(),
        "days_present": days_present,
        "days_missing": days_missing,
        "decisions_verbatim": all_decisions,
        "shipped_compiled": "\n\n".join(shipped_lines) if shipped_lines else "(no shipped activity recorded)",
        "bugs_compiled": "\n\n".join(bugs_lines) if bugs_lines else "(no bug activity recorded)",
        "parked_compiled": "\n\n".join(parked_lines) if parked_lines else "(no parked activity recorded)",
        "focus_compiled": "\n\n".join(focus_seen) if focus_seen else "(no focus history)",
    }


def finalize_weekly(
    draft: dict[str, Any],
    *,
    narrative: dict[str, str],
    today: dt.date | None = None,
    delete_dailies_if_quality_ok: bool = True,
) -> dict[str, Any]:
    """Merge narrative + verbatim decisions into the weekly template.

    `narrative` must supply: Headline, Stalls and Blockers, Path Not Taken.
    Shipped, Bugs, Decisions This Week, Parked Activity come from `draft`.
    """
    today = today or timefmt.today_et()
    week_label = draft.get("for_week", timefmt.iso_week_label(today))

    sections = {
        "Headline": narrative.get("Headline", "(orchestrator did not supply a Headline)"),
        "Shipped": draft.get("shipped_compiled", "(none)"),
        "Bugs": draft.get("bugs_compiled", "(none)"),
        "Decisions This Week": "\n\n".join(draft.get("decisions_verbatim", [])) or "(none)",
        "Parked Activity": draft.get("parked_compiled", "(none)"),
        "Stalls and Blockers": narrative.get("Stalls and Blockers", "(none)"),
        "Path Not Taken": narrative.get("Path Not Taken", "(none)"),
    }

    body = templates.render_weekly(
        week_label=week_label,
        ts_display=timefmt.format_display(),
        window_start=draft.get("mon", ""),
        window_end=draft.get("sun", ""),
        sections=sections,
    )
    fsutil.assert_no_em_dash(body, where=f"weekly-log {week_label}")

    line_count = len(body.splitlines())
    quality_review_required = line_count < WEEKLY_LINE_FLOOR or line_count > WEEKLY_LINE_CEILING

    weekly_path = _weekly_log_path(week_label)
    fsutil.write_text_atomic(weekly_path, body)

    deleted: list[str] = []
    if delete_dailies_if_quality_ok and not quality_review_required:
        for iso in draft.get("days_present", []):
            path = _daily_log_path(iso)
            if path.exists():
                path.unlink()
                deleted.append(fsutil.posix_rel(path))

    _update_decisions_registry_to_weekly(weekly_path, draft.get("decisions_verbatim", []))

    return {
        "week_label": week_label,
        "weekly_log_path": fsutil.posix_rel(weekly_path),
        "line_count": line_count,
        "compaction_quality_review": quality_review_required,
        "dailies_deleted": deleted,
        "dailies_retained_for_review": [
            f"context/daily-logs/{d}.md" for d in draft.get("days_present", [])
            if quality_review_required or not delete_dailies_if_quality_ok
        ],
    }


def _update_decisions_registry_to_weekly(weekly_path: pathlib.Path, decision_blocks: list[str]) -> None:
    """For each decision block, look up its id and stamp the weekly_log path."""
    reg = decisions.load_registry()
    changed = False
    weekly_rel = fsutil.posix_rel(weekly_path)
    for block in decision_blocks:
        m = re.search(r"Decision id:\s*(dec-\d+)", block)
        if not m:
            continue
        did = m.group(1)
        for entry in reg.get("decisions", []):
            if entry.get("id") == did and entry.get("weekly_log") != weekly_rel:
                entry["weekly_log"] = weekly_rel
                changed = True
    if changed:
        decisions.save_registry(reg)

"""
Markdown templates for daily, weekly, and monthly logs.

All templates honor:
 - No em-dashes (U+2014). Use double-hyphens or commas instead.
 - Sentinel line at the end (truncation guard per context/procedures.md).
 - The `## Decisions` header is preserved even when there are no decisions
   (template enforced; downstream rollups expect the header).
"""

from __future__ import annotations

from typing import Any

LOG_PREFIX = "DAILY-FLOW.TEMPLATES"

DAILY_HEADER = """# Daily Summary {date}

> Orchestrator run {ts}. Window: {window_start} to {window_end}.{partial_marker}

"""

DAILY_SECTIONS_ORDER = [
    "Yesterday Shipped",
    "Bugs",
    "Decisions",
    "Awaiting Your Confirmation",
    "Parked Threads",
    "Today Focus",
]

WEEKLY_SECTIONS_ORDER = [
    "Headline",
    "Shipped",
    "Bugs",
    "Decisions This Week",
    "Parked Activity",
    "Stalls and Blockers",
    "Path Not Taken",
]

MONTHLY_SECTIONS_ORDER = [
    "Headline Arc",
    "Pillar Movements",
    "Major Decisions",
    "Bugs Resolved",
    "Direction Changes",
    "Notable Sessions",
]


def render_daily(
    *,
    date: str,
    ts_display: str,
    window_start: str,
    window_end: str,
    sections: dict[str, str],
    partial: bool = False,
) -> str:
    partial_marker = "  PARTIAL RUN -- see Failure Notes below." if partial else ""
    out = DAILY_HEADER.format(
        date=date,
        ts=ts_display,
        window_start=window_start,
        window_end=window_end,
        partial_marker=partial_marker,
    )
    for name in DAILY_SECTIONS_ORDER:
        body = sections.get(name, "(none)").strip() or "(none)"
        out += f"## {name}\n\n{body}\n\n"
    out += f"---\nSENTINEL: daily-flow {date} complete.\n"
    return out


def render_weekly(
    *,
    week_label: str,
    ts_display: str,
    window_start: str,
    window_end: str,
    sections: dict[str, str],
) -> str:
    out = f"# Week {week_label}\n\n> Generated {ts_display}. Window: {window_start} to {window_end}.\n\n"
    for name in WEEKLY_SECTIONS_ORDER:
        body = sections.get(name, "(none)").strip() or "(none)"
        out += f"## {name}\n\n{body}\n\n"
    out += f"---\nSENTINEL: weekly-flow {week_label} complete.\n"
    return out


def render_monthly(
    *,
    month_label: str,
    ts_display: str,
    sections: dict[str, str],
) -> str:
    out = f"# Month {month_label}\n\n> Generated {ts_display}.\n\n"
    for name in MONTHLY_SECTIONS_ORDER:
        body = sections.get(name, "(none)").strip() or "(none)"
        out += f"## {name}\n\n{body}\n\n"
    out += f"---\nSENTINEL: monthly-flow {month_label} complete.\n"
    return out


def render_decision_block(decision: dict[str, Any]) -> str:
    """Format a single decision block per the v0.5 spec template."""
    alts = decision.get("alternatives") or []
    skips = decision.get("not_done") or []
    return (
        f"### {decision.get('title', '(untitled)')}\n"
        f"- Decision id: {decision.get('id', 'dec-???')}\n"
        f"- Decided: {decision.get('decided', '(unspecified)')}\n"
        f"- Why: {decision.get('why', '(unspecified)')}\n"
        f"- Alternatives considered: {', '.join(alts) if alts else '(none)'}\n"
        f"- What we did NOT do: {', '.join(skips) if skips else '(none)'}\n"
        f"- Implications: {decision.get('implications', '(unspecified)')}\n"
    )

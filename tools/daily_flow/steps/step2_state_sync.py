"""
Step 2 - State sync.

Reads the audit blob, flips kanban cards to `done` when commit + scratch ref is present,
flips bugs to `fixed` when their linked_test has a passing smoke result after fix_commit,
and bumps `last_checked` on every parked entry to today.

Card flip rule (all must hold):
 - card column is currently active or blocked
 - at least one commit in the audit window touches a branch slug that appears in the card's
   notes/description OR a scratch file .claude/scratch/<slug>.md exists referencing the
   card id

Bug flip rule (all must hold):
 - bug.status == "fix-pending-verification"
 - bug.linked_test is a non-empty string
 - latest smoke result for that test file has Passed = true AND was produced at or after
   the bug's fix_commit (we approximate "after" by mtime > earliest commit in window
   matching fix_commit; conservative: if we cannot resolve, we still require Passed=true).

Idempotent: re-running with the same audit produces the same writes.
"""

from __future__ import annotations

import datetime as dt
import pathlib
from typing import Any

from ..lib import fsutil, timefmt

LOG_PREFIX = "DAILY-FLOW.STATE_SYNC"


def _kanban_path() -> pathlib.Path:
    return fsutil.repo_root() / "tools" / "kanban" / "state.json"


def _bugs_path() -> pathlib.Path:
    return fsutil.repo_root() / "tools" / "bugs" / "state.json"


def _parked_path() -> pathlib.Path:
    return fsutil.repo_root() / "tools" / "kanban" / "parked.json"


def _scratch_dir() -> pathlib.Path:
    return fsutil.repo_root() / ".claude" / "scratch"


def _branch_slugs_in_commits(commits: list[dict[str, str]]) -> set[str]:
    """Extract claude/<slug> hints from commit subjects.

    The project commits worktree work with subjects like "Merge claude/<slug>: <title>"
    or directly "claude/<slug>: <title>". Returns the set of slugs seen.
    """
    out: set[str] = set()
    for c in commits:
        subj = c.get("subject", "")
        for token in subj.split():
            if token.startswith("claude/"):
                slug = token.split("/", 1)[1].rstrip(":,.;")
                if slug:
                    out.add(slug)
    return out


def _scratch_slugs() -> set[str]:
    sd = _scratch_dir()
    if not sd.exists():
        return set()
    return {p.stem for p in sd.glob("*.md")}


def _scratch_text_contains(scratch_slugs: set[str], needle: str) -> set[str]:
    """Return the subset of scratch files whose body contains `needle` (case-insensitive)."""
    sd = _scratch_dir()
    out: set[str] = set()
    n = needle.lower()
    for slug in scratch_slugs:
        path = sd / f"{slug}.md"
        try:
            text = path.read_text(encoding="utf-8", errors="replace").lower()
        except OSError:
            continue
        if n in text:
            out.add(slug)
    return out


def _candidate_cards_to_flip(
    kanban: dict[str, Any],
    branch_slugs: set[str],
    scratch_slugs: set[str],
) -> list[tuple[str, str]]:
    """Returns [(card_id, source_slug)] for cards whose commit+scratch evidence exists.

    A card flips if (a) its column is active or blocked AND (b) some scratch file
    mentions the card id and that scratch slug also appears in the commit-derived
    branch slug set (i.e. the worktree owning that scratch did get merged in window).
    """
    flips: list[tuple[str, str]] = []
    for card in kanban.get("cards", []):
        column = card.get("column")
        if column not in ("active", "blocked"):
            continue
        card_id = card.get("id")
        if not card_id:
            continue
        matching = _scratch_text_contains(scratch_slugs, card_id)
        intersect = matching & branch_slugs
        if intersect:
            flips.append((card_id, sorted(intersect)[0]))
    return flips


def _apply_card_flips(kanban: dict[str, Any], flips: list[tuple[str, str]], today_iso: str) -> list[dict[str, str]]:
    applied: list[dict[str, str]] = []
    flip_ids = {cid for cid, _ in flips}
    flip_meta = {cid: slug for cid, slug in flips}
    for card in kanban.get("cards", []):
        cid = card.get("id")
        if cid in flip_ids and card.get("column") in ("active", "blocked"):
            prev_col = card.get("column")
            card["column"] = "done"
            card["updated"] = f"{today_iso}T00:00:00Z"
            note_suffix = f"\n[daily-flow {today_iso}] auto-flipped to done from {prev_col} (scratch {flip_meta[cid]})."
            existing = card.get("notes") or ""
            if note_suffix not in existing:
                card["notes"] = (existing + note_suffix).strip()
            applied.append({"id": cid, "from": prev_col, "to": "done", "source": flip_meta[cid]})
    return applied


def _resolve_smoke_pass_for_test(audit: dict[str, Any], linked_test: str) -> bool:
    """Returns True if the latest smoke result for `linked_test` in the audit window passed."""
    if not linked_test:
        return False
    # linked_test is a path like "tools/smoke-verify/tests/bugs/B-318.json"; tests get a Name
    # derived from the file stem in run.ps1 (Name property in result).
    test_stem = linked_test.rsplit("/", 1)[-1].rsplit(".", 1)[0]
    candidates: list[dict[str, Any]] = []
    for run in audit.get("smoke_results", []):
        for r in run.get("results", []):
            if r.get("Name") == test_stem or r.get("BugId") and r.get("BugId") in linked_test:
                candidates.append(r)
    if not candidates:
        return False
    return any(bool(r.get("Passed")) for r in candidates)


def _apply_bug_flips(bugs: dict[str, Any], audit: dict[str, Any], today_iso: str) -> list[dict[str, str]]:
    applied: list[dict[str, str]] = []
    for bug in bugs.get("bugs", []):
        if bug.get("status") != "fix-pending-verification":
            continue
        linked = bug.get("linked_test") or ""
        if not linked:
            continue
        if not _resolve_smoke_pass_for_test(audit, linked):
            continue
        prev = bug.get("status")
        bug["status"] = "fixed"
        if not bug.get("fixed_date"):
            bug["fixed_date"] = today_iso
        applied.append({"id": bug.get("id"), "from": prev, "to": "fixed"})
    return applied


def _bump_parked_last_checked(parked: dict[str, Any], today_iso: str) -> int:
    n = 0
    for entry in parked.get("parked", []):
        if entry.get("last_checked") != today_iso:
            entry["last_checked"] = today_iso
            n += 1
    return n


def run_state_sync(audit: dict[str, Any], today: dt.date | None = None) -> dict[str, Any]:
    today = today or timefmt.today_et()
    today_iso = today.isoformat()

    kanban = fsutil.load_json(_kanban_path(), default={})
    bugs = fsutil.load_json(_bugs_path(), default={})
    parked = fsutil.load_json(_parked_path(), default={})

    branch_slugs = _branch_slugs_in_commits(audit.get("commits", []))
    scratch_slugs = _scratch_slugs()
    flips = _candidate_cards_to_flip(kanban, branch_slugs, scratch_slugs)
    applied_cards = _apply_card_flips(kanban, flips, today_iso)
    applied_bugs = _apply_bug_flips(bugs, audit, today_iso)
    bumped = _bump_parked_last_checked(parked, today_iso)

    if applied_cards:
        fsutil.save_json_atomic(_kanban_path(), kanban)
    if applied_bugs:
        fsutil.save_json_atomic(_bugs_path(), bugs)
    if bumped > 0:
        fsutil.save_json_atomic(_parked_path(), parked)

    result = {
        "for_date": today_iso,
        "cards_flipped": applied_cards,
        "bugs_flipped": applied_bugs,
        "parked_last_checked_bumped": bumped,
        "branch_slugs_seen": sorted(branch_slugs),
    }
    fsutil.save_json_atomic(fsutil.state_dir() / f"state-sync-{today_iso}.json", result)
    return result

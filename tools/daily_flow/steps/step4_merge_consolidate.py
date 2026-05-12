"""
Step 4 - Pending-merge consolidation.

Enumerates claude/* branches ahead of dev and merges them sequentially in
committer-date order with line-count verification.

Two distinct entry points so the orchestrator (a Claude session) can split
mechanical work from judgment:

 - enumerate_pending(): pure read, returns the dry-run candidate list
 - merge_one(branch): does one merge end-to-end with snapshot verify + push

Dirty-tree handling is delegated to the caller: this module exposes
commit_dirty_wip() and stash_dirty() but does not pick between them. The
caller (Claude) reads `git status --porcelain` + `git diff --stat` and
chooses based on whether the diff looks coherent.

A `--auto` orchestrator path is provided for clean-tree runs that need
no judgment: it walks the candidate list, skips dirty-tree branches with
a clear surface-to-summary record, and merges everything else FF.
"""

from __future__ import annotations

import datetime as dt
import json
import pathlib
import subprocess
from typing import Any

from ..lib import fsutil, gitutil, timefmt

LOG_PREFIX = "DAILY-FLOW.MERGE_CONSOLIDATE"


def _result_path(today_iso: str) -> pathlib.Path:
    return fsutil.state_dir() / f"merge-consolidate-{today_iso}.json"


def enumerate_pending(base_branch: str = "dev") -> list[dict[str, Any]]:
    """Returns claude/* branches ahead of base_branch in oldest-committerdate-first order.

    Each entry: {name, committerdate, conflicts, conflict_files, worktree_locked}.
    """
    branches_meta = gitutil.claude_branches()
    ahead = set(gitutil.branches_ahead_of(base_branch))
    candidates: list[dict[str, Any]] = []
    for b in branches_meta:
        if b["name"] not in ahead:
            continue
        has_conflicts, conflicts = gitutil.merge_tree_check(b["name"], base=base_branch)
        candidates.append({
            "name": b["name"],
            "committerdate": b["committerdate"],
            "conflicts": has_conflicts,
            "conflict_markers": conflicts[:20],
            "worktree_locked": gitutil.worktree_locked(b["name"]),
        })
    candidates.sort(key=lambda x: x["committerdate"])
    return candidates


def precheck_dirty_tree() -> dict[str, Any]:
    """Inspect dev working tree before merges. Returns a dict the Claude orchestrator can judge."""
    porcelain = gitutil.status_porcelain()
    if not porcelain:
        return {"clean": True, "porcelain": [], "diff_stat": []}
    return {
        "clean": False,
        "porcelain": porcelain,
        "diff_stat": gitutil.diff_stat("HEAD"),
    }


def _line_counts(file_paths: list[str]) -> dict[str, int]:
    out: dict[str, int] = {}
    for rel in file_paths:
        path = fsutil.repo_root() / rel
        if not path.exists():
            continue
        try:
            with path.open("rb") as fh:
                out[rel] = sum(1 for _ in fh)
        except OSError:
            continue
    return out


def _changed_files_in_merge(branch: str, base: str = "HEAD") -> list[str]:
    proc = subprocess.run(
        ["git", "diff", "--name-only", f"{base}...{branch}"],
        cwd=fsutil.repo_root(),
        capture_output=True,
        text=True,
        check=False,
        encoding="utf-8",
        errors="replace",
    )
    if proc.returncode != 0:
        return []
    return [ln.strip() for ln in proc.stdout.splitlines() if ln.strip()]


def merge_one(branch: str, *, base_branch: str = "dev", push: bool = True) -> dict[str, Any]:
    """Merge a single branch with pre/post snapshot verification.

    Steps: (1) line-count snapshot, (2) merge --no-ff, (3) line-count verify,
    (4) push (optional), (5) delete branch on success.
    """
    pre_files = _changed_files_in_merge(branch, base=base_branch)
    pre_counts = _line_counts(pre_files)
    pre_head = gitutil.head_sha()

    short_title = f"daily-flow merge {branch}"
    ok, output = gitutil.merge_no_ff(branch, f"Merge {branch}: auto-merge by daily-flow orchestrator")
    if not ok:
        return {
            "branch": branch,
            "merged": False,
            "error": "merge_failed",
            "stderr": output,
            "pre_head": pre_head,
            "post_head": gitutil.head_sha(),
        }

    post_counts = _line_counts(pre_files)
    shrunk: list[dict[str, Any]] = []
    for f, pre in pre_counts.items():
        post = post_counts.get(f, 0)
        if post < pre and (pre - post) > 5:
            shrunk.append({"file": f, "pre": pre, "post": post})
    if shrunk:
        return {
            "branch": branch,
            "merged": True,
            "verification_failed": True,
            "shrunk_files": shrunk,
            "note": "halted: line-count shrink. Inspect before pushing.",
            "pre_head": pre_head,
            "post_head": gitutil.head_sha(),
        }

    push_ok = True
    push_output = ""
    if push:
        push_ok, push_output = gitutil.push("origin", base_branch)
        if not push_ok:
            push_ok, push_output = gitutil.push("origin", base_branch)

    branch_deleted = False
    if push_ok or not push:
        branch_deleted = gitutil.delete_branch(branch)

    return {
        "branch": branch,
        "merged": True,
        "verification_failed": False,
        "push_ok": push_ok,
        "branch_deleted": branch_deleted,
        "pre_head": pre_head,
        "post_head": gitutil.head_sha(),
        "files_touched": pre_files,
        "x_push_output_tail": push_output[-400:] if push_output else "",
    }


def run_auto_consolidate(*, base_branch: str = "dev", push: bool = True, today: dt.date | None = None) -> dict[str, Any]:
    """Clean-tree mechanical pass. Skips dirty-tree, conflicting, or locked branches."""
    today = today or timefmt.today_et()
    today_iso = today.isoformat()

    dirty = precheck_dirty_tree()
    if not dirty["clean"]:
        result: dict[str, Any] = {
            "for_date": today_iso,
            "skipped_due_to_dirty_tree": True,
            "porcelain": dirty["porcelain"],
            "diff_stat": dirty["diff_stat"],
            "merged": [],
            "skipped": [],
            "candidates": enumerate_pending(base_branch),
        }
        fsutil.save_json_atomic(_result_path(today_iso), result)
        return result

    candidates = enumerate_pending(base_branch)
    merged: list[dict[str, Any]] = []
    skipped: list[dict[str, Any]] = []
    for cand in candidates:
        if cand["worktree_locked"]:
            skipped.append({"branch": cand["name"], "reason": "worktree_locked"})
            continue
        if cand["conflicts"]:
            skipped.append({"branch": cand["name"], "reason": "conflicts", "markers": cand["conflict_markers"]})
            continue
        outcome = merge_one(cand["name"], base_branch=base_branch, push=push)
        if outcome.get("merged") and not outcome.get("verification_failed"):
            merged.append(outcome)
        else:
            skipped.append({"branch": cand["name"], "reason": "merge_failed_or_verify", "details": outcome})
            break

    result = {
        "for_date": today_iso,
        "skipped_due_to_dirty_tree": False,
        "merged": merged,
        "skipped": skipped,
        "candidates": candidates,
    }
    fsutil.save_json_atomic(_result_path(today_iso), result)
    return result

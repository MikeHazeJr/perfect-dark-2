"""
Git subprocess wrappers used by the daily-flow orchestrator.

These are thin shells around `git` so callers can mock them in tests and
the orchestrator can log every command.
"""

from __future__ import annotations

import subprocess
from typing import Sequence

from . import fsutil

LOG_PREFIX = "DAILY-FLOW.GITUTIL"


def _run(args: Sequence[str], check: bool = True) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(
        list(args),
        cwd=fsutil.repo_root(),
        capture_output=True,
        text=True,
        check=False,
        encoding="utf-8",
        errors="replace",
    )
    if check and proc.returncode != 0:
        raise RuntimeError(
            f"{LOG_PREFIX}: command {' '.join(args)} failed rc={proc.returncode}\n"
            f"stdout={proc.stdout}\nstderr={proc.stderr}"
        )
    return proc


def head_sha() -> str:
    return _run(["git", "rev-parse", "HEAD"]).stdout.strip()


def current_branch() -> str:
    return _run(["git", "rev-parse", "--abbrev-ref", "HEAD"]).stdout.strip()


def log_since(since: str, branch: str = "dev") -> list[dict[str, str]]:
    """Returns a list of {sha, author, date, subject} for commits in <branch> within the window."""
    fmt = "%H%x09%an%x09%aI%x09%s"
    proc = _run(["git", "log", f"--since={since}", f"--pretty=format:{fmt}", branch], check=False)
    if proc.returncode != 0:
        return []
    entries: list[dict[str, str]] = []
    for line in proc.stdout.splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) < 4:
            continue
        entries.append({"sha": parts[0], "author": parts[1], "date": parts[2], "subject": parts[3]})
    return entries


def log_between(start_iso: str, end_iso: str, branch: str = "dev") -> list[dict[str, str]]:
    fmt = "%H%x09%an%x09%aI%x09%s"
    proc = _run(
        ["git", "log", f"--since={start_iso}", f"--until={end_iso}", f"--pretty=format:{fmt}", branch],
        check=False,
    )
    if proc.returncode != 0:
        return []
    entries: list[dict[str, str]] = []
    for line in proc.stdout.splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) < 4:
            continue
        entries.append({"sha": parts[0], "author": parts[1], "date": parts[2], "subject": parts[3]})
    return entries


def status_porcelain() -> list[str]:
    return [line for line in _run(["git", "status", "--porcelain"]).stdout.splitlines() if line.strip()]


def is_clean() -> bool:
    return len(status_porcelain()) == 0


def diff_stat(ref: str = "HEAD") -> list[str]:
    proc = _run(["git", "diff", "--stat", ref], check=False)
    return proc.stdout.splitlines() if proc.returncode == 0 else []


def claude_branches() -> list[dict[str, str]]:
    """Returns claude/* branches with their committerdate (ISO 8601 strict)."""
    proc = _run(
        [
            "git",
            "for-each-ref",
            "--format=%(refname:short)|%(committerdate:iso8601-strict)",
            "refs/heads/claude/",
        ],
        check=False,
    )
    if proc.returncode != 0:
        return []
    out: list[dict[str, str]] = []
    for line in proc.stdout.splitlines():
        if "|" not in line:
            continue
        name, date = line.split("|", 1)
        out.append({"name": name.strip(), "committerdate": date.strip()})
    return out


def merge_tree_check(branch: str, base: str = "HEAD") -> tuple[bool, list[str]]:
    """Returns (has_conflicts, conflicting_files). Uses `git merge-tree --name-only` form."""
    proc = _run(["git", "merge-tree", "--no-messages", base, branch], check=False)
    if proc.returncode != 0:
        return True, [f"merge-tree exit {proc.returncode}"]
    conflicts: list[str] = []
    for line in proc.stdout.splitlines():
        if line.startswith("<<<<<<<") or line.startswith("=======") or line.startswith(">>>>>>>"):
            conflicts.append(line)
    return (len(conflicts) > 0), conflicts


def branches_ahead_of(base: str = "dev") -> list[str]:
    """List of claude/* branches that have commits not in <base>."""
    proc = _run(["git", "branch", "--list", "--no-merged", base, "claude/*"], check=False)
    if proc.returncode != 0:
        return []
    return [line.strip().lstrip("* ").strip() for line in proc.stdout.splitlines() if line.strip()]


def worktree_locked(branch: str) -> bool:
    """Returns True if the branch is tied to an active worktree (other than the main one)."""
    proc = _run(["git", "worktree", "list", "--porcelain"], check=False)
    if proc.returncode != 0:
        return False
    blocks = proc.stdout.split("\n\n")
    for block in blocks:
        if f"branch refs/heads/{branch}" in block:
            return True
    return False


def commit_all_wip(message: str) -> str:
    _run(["git", "add", "-A"])
    _run(["git", "commit", "-m", message], check=False)
    return head_sha()


def stash_push(paths: list[str], message: str) -> bool:
    """Stash explicit paths only. Never bare stash. Returns True on success."""
    if not paths:
        return False
    args = ["git", "stash", "push", "-m", message, "--"] + paths
    proc = _run(args, check=False)
    return proc.returncode == 0


def merge_no_ff(branch: str, message: str) -> tuple[bool, str]:
    proc = _run(["git", "merge", "--no-ff", "-m", message, branch], check=False)
    return (proc.returncode == 0), proc.stdout + proc.stderr


def push(remote: str = "origin", branch: str = "dev") -> tuple[bool, str]:
    proc = _run(["git", "push", remote, branch], check=False)
    return (proc.returncode == 0), proc.stdout + proc.stderr


def delete_branch(name: str) -> bool:
    proc = _run(["git", "branch", "-D", name], check=False)
    return proc.returncode == 0

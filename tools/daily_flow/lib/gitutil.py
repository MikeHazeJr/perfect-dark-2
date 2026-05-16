"""
Git subprocess wrappers used by the daily-flow orchestrator.

These are thin shells around `git` so callers can mock them in tests and
the orchestrator can log every command.
"""

from __future__ import annotations

import os
import subprocess
import time
from typing import Sequence

from . import fsutil

LOG_PREFIX = "DAILY-FLOW.GITUTIL"

# Lock-file hygiene tuning. Locks older than STALE_LOCK_AGE_SECONDS are
# considered orphaned from a prior crashed git process and safe to clear.
# Daily-flow orchestrator runs are typically sub-minute; 5 min is a generous
# floor that still releases the repo within one cron interval (24h).
STALE_LOCK_AGE_SECONDS = 300
# Fresh-lock wait window before giving up (c130 defense #1, branch B).
FRESH_LOCK_WAIT_SECONDS = 10
FRESH_LOCK_POLL_INTERVAL = 1.0
# Lock files git creates at the top level of .git/. Subdirectory locks
# (e.g. .git/refs/heads/<branch>.lock) are also possible but rarer; the
# common case for daily-flow lockouts is HEAD.lock or index.lock left
# behind when a top-level ref update or staging op crashed.
TRACKED_LOCK_NAMES = ("HEAD.lock", "index.lock")


def _git_dir() -> "fsutil.pathlib.Path":  # type: ignore[name-defined]
    return fsutil.repo_root() / ".git"


def _live_git_processes() -> int:
    """Best-effort count of running git.exe processes on Windows.

    Returns -1 if the probe fails (so callers don't get a misleading 0
    on a system where tasklist is unavailable, e.g. non-Windows).
    """
    try:
        proc = subprocess.run(
            ["tasklist", "/FI", "IMAGENAME eq git.exe", "/NH"],
            capture_output=True,
            text=True,
            check=False,
            encoding="utf-8",
            errors="replace",
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError):
        return -1
    if proc.returncode != 0:
        return -1
    # tasklist returns "INFO: No tasks..." when nothing matches; otherwise
    # one line per process. Count lines that look like a process row.
    count = 0
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.lower().startswith("info:"):
            continue
        if "git.exe" in line.lower():
            count += 1
    return count


def cleanup_stale_locks(
    *,
    stale_after_seconds: int = STALE_LOCK_AGE_SECONDS,
    wait_fresh_seconds: int = FRESH_LOCK_WAIT_SECONDS,
) -> dict[str, object]:
    """Best-effort sweep of stale .git/*.lock files left by crashed git ops.

    Defense in depth for c130: the daily-flow orchestrator runs unattended;
    if a prior run crashed mid-commit, the next run inherits a stale lock
    that blocks every git operation in the repo until cleared manually.

    Safety rules:
      1. Only sweep locks listed in TRACKED_LOCK_NAMES (top-level .git/).
      2. Locks with mtime <= stale_after_seconds ago are considered "fresh"
         and likely held by a live git process; we poll briefly, then if
         still present, return a "deferred" status WITHOUT deleting. The
         caller can elect to skip the run rather than corrupt a concurrent
         git op. SIGKILL crashes leave a lock with a recent mtime, but the
         next orchestrator fire (next morning at the latest) will see it
         as stale and sweep it.
      3. On Windows, optionally short-circuit "fresh + live git.exe" as a
         belt-and-braces guard (mtime check alone is usually sufficient).
      4. Errors from os.unlink are swallowed (best-effort).

    Returns a status dict for logging:
      {
        "cleared": [list of lock paths cleared],
        "deferred_fresh": [list of fresh locks we did NOT touch],
        "missing": [list of locks that were never present],
        "live_git_processes": int (or -1 if probe failed),
      }
    """
    git_dir = _git_dir()
    cleared: list[str] = []
    deferred: list[str] = []
    missing: list[str] = []
    now = time.time()

    for name in TRACKED_LOCK_NAMES:
        lock_path = git_dir / name
        if not lock_path.exists():
            missing.append(str(lock_path))
            continue
        try:
            mtime = lock_path.stat().st_mtime
        except OSError:
            # Race: lock disappeared between exists() and stat(). Treat as missing.
            missing.append(str(lock_path))
            continue
        age = now - mtime
        if age >= stale_after_seconds:
            try:
                os.unlink(lock_path)
                cleared.append(str(lock_path))
            except OSError:
                # Another process may have grabbed it between stat() and unlink();
                # leave it for the next sweep.
                deferred.append(str(lock_path))
            continue
        # Fresh lock: wait briefly in case the holder is finishing up.
        waited = 0.0
        while waited < wait_fresh_seconds:
            time.sleep(FRESH_LOCK_POLL_INTERVAL)
            waited += FRESH_LOCK_POLL_INTERVAL
            if not lock_path.exists():
                missing.append(str(lock_path))
                break
        else:
            # Still present after the wait window. Do NOT delete -- a fresh
            # lock that survives the wait is most likely held by a live git
            # process, and deleting it would corrupt their operation.
            deferred.append(str(lock_path))

    return {
        "cleared": cleared,
        "deferred_fresh": deferred,
        "missing": missing,
        "live_git_processes": _live_git_processes(),
    }


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

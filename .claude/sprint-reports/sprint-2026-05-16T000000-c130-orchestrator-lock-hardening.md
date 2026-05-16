# Sprint Report c130: Daily-Flow Orchestrator Stale HEAD.lock Hardening

- Date: 2026-05-16
- Card: c130 (Tooling pillar)
- Commit: `5b960a54`
- Branch: `dev` (main checkout, worktrees disabled)

## Problem

The daily-flow cron at 06:00 ET crashed on 2026-05-13 and left
`.git/HEAD.lock` behind. The lock was still present at 09:40 (~3.5 hours
stale), blocking every git operation in the repo until Mike manually ran
`rm .git/HEAD.lock`. The orchestrator runs unattended; without self-healing,
each crash kicks off a multi-hour window where the entire repo is read-only
for git purposes until a human intervenes.

Surfaced by 2026-05-13 super-audit (MS-1). Recurs on every crash that aborts
mid-ref-update or mid-stage.

## Solution: Three defenses in depth

All three call the same helper (`gitutil.cleanup_stale_locks`) so the
behavior is uniform and the code is in one place.

### Defense 1: Pre-flight stale-lock sweep

`orchestrator.main()` calls `_run_lock_cleanup(reason="preflight")` before
any git operation runs (before `gitutil.head_sha()`, before `run_catchup`,
before the dedup-guard load). If the sweep clears anything, the action is
logged via `DAILY-FLOW.ORCHESTRATOR.LOCK_CLEANUP`.

### Defense 2: try/finally around the pipeline

Pipeline body is wrapped in `try: ... finally: _run_lock_cleanup(reason="finally")`.
Any exception path in `run_catchup` or `run_pipeline` (including `KeyboardInterrupt`
and bare `SystemExit`) hits the cleanup before propagating. Exit code is
preserved.

### Defense 3: atexit + signal handlers

`_install_cleanup_hooks()` registers:
- `atexit.register(_run_lock_cleanup, reason="atexit")` for normal interpreter
  shutdown.
- `signal.SIGTERM` -> `_signal_cleanup` for graceful scheduled-task termination.
- `signal.SIGBREAK` -> `_signal_cleanup` on Windows for Ctrl-Break / console-close.

SIGKILL (`taskkill /f`) cannot be intercepted; that case is covered by the
next morning's preflight sweep clearing the now-stale lock.

### Bonus: `--skip-on-fresh-lock` flag

If preflight detects a fresh lock (which it leaves alone for concurrency
safety), the operator can opt in to graceful skip via this flag. The cron
will retry on its next scheduled fire when the lock has either resolved or
become stale.

## Cleanup helper signature

```python
# tools/daily_flow/lib/gitutil.py
def cleanup_stale_locks(
    *,
    stale_after_seconds: int = STALE_LOCK_AGE_SECONDS,  # 300s = 5 min
    wait_fresh_seconds: int = FRESH_LOCK_WAIT_SECONDS,  # 10s
) -> dict[str, object]:
    """Returns:
      cleared: list[str]          # paths deleted
      deferred_fresh: list[str]   # paths left alone (mtime within threshold)
      missing: list[str]          # paths that were not present
      live_git_processes: int     # tasklist probe, -1 if probe failed
    """
```

Tracked lock names: `HEAD.lock`, `index.lock` (top-level `.git/` only).
Subdirectory locks (`refs/heads/*.lock`) are rarer in our flow and would
require recursive enumeration; the design doc notes Phase 2 may extend this
if needed.

## Concurrency safety reasoning

The core invariant: a lock file held by a live git process MUST NOT be
deleted, or that process's operation will corrupt.

Three layers of protection:

1. **Mtime threshold (primary).** Locks younger than `STALE_LOCK_AGE_SECONDS`
   (default 300s) are never deleted. A live git op typically holds a lock
   for milliseconds to seconds, well under the threshold.

2. **Fresh-lock wait (secondary).** A fresh lock might be in the middle of
   a brief op; we poll every 1s for up to 10s. If the lock clears during the
   wait, normal flow resumes. If it persists, we defer (do not delete).

3. **Live git.exe process probe (tertiary, informational).** A Windows-only
   `tasklist /FI "IMAGENAME eq git.exe"` count is included in the log output
   so post-hoc analysis can confirm whether a fresh lock was really held by
   a live process. In environments where tasklist is unavailable (non-Windows,
   or sandboxed shells like the one in this dispatch session), the probe
   returns -1 and the rest of the safety logic still holds.

The mtime threshold alone is sufficient for correctness; the probe is
diagnostic.

**Edge case: SIGKILL leaves a fresh lock.** Best case scenario, the next
day's preflight sweep sees it as stale (mtime now > 5 min) and clears it.
Worst case (the very next cron fires within 5 min), `--skip-on-fresh-lock`
allows graceful skip and the run after that succeeds. Either way, no
manual intervention required after the first 24h.

## Verification

Two scenarios, both green:

### Scenario A: Stale lock (crash recovery path)

```
Created .git/HEAD.lock with mtime 1h ago
cleanup_stale_locks() ->
  cleared: ['.../HEAD.lock']
  deferred_fresh: []
  missing: ['.../index.lock']
PASS: stale lock deleted, normal flow resumes
```

### Scenario B: Fresh lock (concurrent git protection)

```
Created .git/HEAD.lock with mtime = now
cleanup_stale_locks(wait_fresh_seconds=2) ->
  cleared: []
  deferred_fresh: ['.../HEAD.lock']
PASS: fresh lock preserved; orchestrator with --skip-on-fresh-lock returned 0
```

Also verified `main(['--skip-on-fresh-lock', '--catchup-only'])` with a
fresh lock present: returned 0, emitted `LOCK_FRESH.SKIP` log line, did
NOT touch the lock.

## File changes

- `tools/daily_flow/lib/gitutil.py` (+135 lines): new `cleanup_stale_locks`
  helper, `_live_git_processes` probe, `_git_dir` helper, tuning constants.
- `tools/daily_flow/orchestrator.py` (+95 lines, -7 lines): atexit/signal
  hooks, `_run_lock_cleanup` wrapper, three insertion points in `main()`.
- `tools/kanban/state.json`: c130 flipped to `pending_completion` (column
  remains `backlog` per dispatch authority convention; coordinator promotes
  to `done`).

## Insertion sites in orchestrator.py

| Site | Location | Trigger |
|------|----------|---------|
| 1. Preflight sweep | `main()` line 244 (after `_install_cleanup_hooks()`) | Start of every run |
| 2. try/finally | `main()` lines 260-271 | Wraps `run_catchup` + `run_pipeline` |
| 3. atexit + signal | `main()` line 243 (`_install_cleanup_hooks()`) | Process termination paths |

## Hard rules respected

- No source-code edits outside `tools/daily_flow/` and `tools/kanban/state.json`.
- Mtime threshold ensures the fix never deletes a lock held by a live git
  process.
- No worktrees used (disabled).
- No sub-agents.
- Build verify NOT required (Python-only change).
- No bare `git stash`; `git add` was used with explicit paths.

## Next steps

- Coordinator can promote c130 from `pending_completion` to `done` after
  observing the next morning's cron fires cleanly.
- Optional Phase 2 enhancement: extend `TRACKED_LOCK_NAMES` to scan
  `refs/heads/*.lock` recursively if a real-world incident shows the
  current top-level coverage is insufficient.

---
SENTINEL: c130 orchestrator-lock-hardening sprint complete.

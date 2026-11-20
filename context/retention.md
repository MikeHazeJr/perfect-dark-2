# Retention Rules

## Purpose

Prevent the live `context/` tree from re-accreting cruft over time. A doc that captured value at a moment in time has a finite useful window. After that window, it lives in `_old/` for forensic value, not in the live tree.

## Generated workspace and smoke evidence (T-TOOLING-005, 2026-09-30)

- `.claude/` is still used by Codex build and smoke tooling. Its name does not
  establish obsolescence. Public typed archives may contain edited game-facing
  source; they are not disposable engine caches.
- Age-based cleanup must not automatically erase logs, result receipts,
  screenshots, modified fixtures, unique frozen binaries or source baselines.
  `devtools/clean-claude-workspace.ps1` now makes automatic invocations a cheap
  no-op; its explicit `-DryRun` is unreviewed inventory only.
- Use `devtools/smoke-storage-manifest.py` for the approved historical pool.
  It hashes every proposed duplicate, names its retained copy, preserves unique
  content and current output, and separately identifies compiler products.
  It has no deletion operation. Compressed exact file manifests stay under
  `.claude/cleanup-manifests/` and must themselves be preserved.
- Mike's current deletion boundary is canonical
  `C:/Users/mikeh/Perfect-Dark-2/perfect_dark-mike/.claude/` only. Reject reparse
  traversal, tracked files and any target outside that root. Future permanent
  removal requires a reviewed manifest and explicit authorization. For the
  September 30 historical pools only, Mike explicitly stopped exhaustive hash
  verification and authorized removal with reduced assurance and possible later
  repair. Preserve both partial hash attempts and every identified retained
  witness. Recheck containment, metadata and active use; skip locked/current or
  uncertain targets. Use individual files, never recursive whole directories.
- Mike authorized prospective controls (T-TOOLING-006). Implemented in
  `tools/smoke-verify/storage.py`, `lib/Storage-Harness.ps1`, both install wrappers
  and `run.ps1`: immutable compressed content-addressed reconstruction blobs,
  private ordinary files for mutation, complete seed/change/delete recipes,
  exact binary/definition/save/cache evidence, verdicts, logs and screenshots.
  Shared mode leases one working copy exclusively, verifies every prior byte and
  identity, then resets it completely; closed unchanged private files may be
  reused. Failed/pinned shared copies move into managed private retention.
- `tools/smoke-verify/storage-policy.json` defaults: five full installs globally
  within 8 GiB (active/pinned/restored included), 8 GiB compact archive, 200 GiB
  run/install free floor plus 2 GiB extra growth and pending 2 GiB run / 4 GiB
  build growth. Builds default to no fixed floor (`min_build_free_gib: 0`).
  All values are explicit configurable preflight budgets. Prefer the
  latest successful full copy; older successes then oldest failures yield space.
  Every completed run keeps reconstructable content even after full-copy pruning.
  Pins, active/incomplete work, edited fixtures, locked/reparse/hardlinked paths,
  corrupt/incomplete receipts and exhausted budgets refuse further copies.
- Mike removed the fixed build reserve on November 19, 2026 (T-TOOLING-015).
  The default build preflight now needs 6 GiB (4 pending + 2 extra), not 206.
  The October 3 D-011 200 GiB floor remains for non-build operations. Explicit
  custom policies with `min_free_gib` but no `min_build_free_gib` retain their
  old build floor; set the latter explicitly to choose a separate build floor.
  Location policies cannot override the checkout's build floor. Archive caps,
  queues, Astral budgets, cleanup permission and global machine rules are unchanged.
- This policy applies only to newly registered `.claude/smoke-storage/` work.
  It never imports/prunes historical evidence or deletes archive blobs/receipts.
  Explicit `-Install` is a read-only template copied privately. Generic preflight
  and non-deleting dry-run inventory are exposed; restoration creates a new pinned
  budgeted copy. The smoke optional build and direct `devtools/build-session.ps1` starts are
  checked. Standalone package/extraction tools need preflight adoption. This is not
  an OS quota: unexpected native growth is preserved and blocks later work rather
  than erasing evidence. Interrupted active work requires deliberate review.
- Validation: 14 tiny isolated Python cases and 11 PowerShell integration checks
  passed; no game/build launched or large existing fixture copied. See
  `audits/2026/smoke-storage-prevention-2026-09-30.md` for exact measurements and
  receipts. Historical cleanup is complete and was not repeated.

This file is the convention that prevents the next 88K-line cleanup pass.

---

## Per-surface windows

| Surface | Active window | Where it goes |
|---------|---------------|---------------|
| `audits/` | 14 days from authorship, OR while actively cited from `tasks.md` or an active design | `_old/audits/<year>/` |
| `daily-logs/` | 30 days | `_old/daily-logs/<year>/` |
| `designs/` | While not yet shipped, OR while in-flight | `_old/designs-shipped/<pillar>/` |
| `session-log.md` | Last ~100 sessions in active rolling window | `_old/session-log/sN-sM/` |
| `tasks.md` | Active milestones and live dependencies only; completed slices delete | `session-log.md`; use `_old/tasks/<year>/` only for a dated major-consolidation snapshot |
| `scratch/` (if reintroduced) | 14 days | `_old/scratch/<date>/` |
| `pillars/*.md` | Live always; updated with code changes | n/a |
| `constraints.md` | Live always | n/a |
| `procedures.md`, `working-preferences.md`, `roadmap.md` | Live always | n/a |
| `bugs.md` | Current one-off defects, routed regression clusters, and explicit deferred bugs only | `_old/bugs/<year>/` for dated full-ledger snapshots |
| `systemic-bugs.md` | Live architectural pattern catalog | n/a |

---

## Triggers

- **Design ships.** Within 7 days of the design's primary scope shipping, the design doc moves to `_old/designs-shipped/<pillar>/`. The pillar doc that absorbs the live invariant is updated in the same commit.
- **Audit ages out.** 14 days after authorship, an audit moves to `_old/audits/<year>/` UNLESS it is referenced from `tasks.md` or an active design. A grep for the audit's filename across active context determines this.
- **Daily-log ages out.** 30 days after authorship, a daily-log moves to `_old/daily-logs/<year>/`.
- **Scratch ages out.** 14 days after authorship, a scratch file moves to `_old/scratch/<date>/`.
- **Session log saturates.** When the active rolling window passes ~120 sessions, the oldest ~50 are cut to a new tier in `_old/session-log/`.

---

## Pillar doc maintenance

- **Update with the code.** A code change that affects a pillar updates the pillar doc in the same commit. Pillar docs do not get separate maintenance passes; they are part of the change.
- **No status drift.** A pillar doc that says "Status: Implemented" must reflect the live code. If a pillar doc accumulates stale claims, that is a violation of this rule, not a normal state to be cleaned up later.
- **File:line citations** are required for any factual claim that might drift. Citations are the audit primitive that lets a future reader verify against current code in seconds.

---

## Scratch convention

`scratch/` is reserved for ephemeral working files used during an investigation. Anything older than 14 days that has not been promoted to `audits/`, `bugs.md`, `session-log.md`, or a pillar doc moves to `_old/scratch/<date>/` automatically.

If `scratch/` does not exist, do not create it casually. Most ephemera belongs in the session-log entry as inline narrative, not as a separate file.

---

## Daily-log convention

Write a `daily-logs/<YYYY-MM-DD>.md` only on a heavy day:

- 3 or more worktree sessions, OR
- A major feature line shipping, OR
- A multi-pillar pass that crosses subsystems

Routine days do not get a daily-log. The session-log.md chronological entries cover them.

---

## ADR convention (retired 2026-04-30)

The `ADR-NNN-...md` filename pattern at root level is retired. Architectural decisions are captured in:

1. The relevant pillar doc (`pillars/*.md`) for live invariants.
2. `constraints.md` for cross-cutting invariants.
3. A design doc in `designs/` while the decision is in flight.
4. The session-log entry that landed the decision, for traceability.

No new `ADR-NNN-...md` files. The historical ADR-001/002/003/004 live in `_old/`.

---

## Discipline

- **AI sessions doing context updates check retention windows** when they touch nearby content, and move at-window files as part of the same commit.
- **Mike runs a quick retention pass monthly** OR when context feels heavy. The expected size is bounded: pillar docs total ~3000 lines, root files total ~2500 lines, designs/audits/daily-logs combined under ~5000 lines. If the tree exceeds ~12000 active lines (excluding `_old/`), retention is overdue.
- **Adding a new top-level file requires justification.** New pillar = new architectural pillar. New root file = new cross-cutting invariant. Anything else fits in an existing pillar or design.

---

## Exemptions

A file that would otherwise age out can be retained if:

- It is the canonical reference for an active design (must be linked from at least one active design doc OR `tasks.md`).
- It carries a unique decision-rationale that has not been propagated elsewhere AND removing it would lose information not recoverable from `_old/`.
- Mike explicitly marks it `<!-- retention: keep -->` at the top of the file.

The third option is the override; the first two are the normal cases.

# Retention Rules

## Purpose

Prevent the live `context/` tree from re-accreting cruft over time. A doc that captured value at a moment in time has a finite useful window. After that window, it lives in `_old/` for forensic value, not in the live tree.

This file is the convention that prevents the next 88K-line cleanup pass.

---

## Per-surface windows

| Surface | Active window | Where it goes |
|---------|---------------|---------------|
| `audits/` | 14 days from authorship, OR while actively cited from `tasks.md` or an active design | `_old/audits/<year>/` |
| `daily-logs/` | 30 days | `_old/daily-logs/<year>/` |
| `designs/` | While not yet shipped, OR while in-flight | `_old/designs-shipped/<pillar>/` |
| `session-log.md` | Last ~100 sessions in active rolling window | `_old/session-log/sN-sM/` |
| `tasks.md` | Active lanes only; completed slices delete | session-log.md retains the narrative |
| `scratch/` (if reintroduced) | 14 days | `_old/scratch/<date>/` |
| `pillars/*.md` | Live always; updated with code changes | n/a |
| `constraints.md` | Live always | n/a |
| `procedures.md`, `working-preferences.md`, `roadmap.md` | Live always | n/a |
| `bugs.md`, `systemic-bugs.md` | Live always | n/a |

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

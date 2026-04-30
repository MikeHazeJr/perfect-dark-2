---
name: context-session-start
description: >-
  Bootstraps a new Cursor session on this repo using context/ - reads README,
  working-preferences, constraints, procedures, tasks, and session-log in order,
  then summarizes state and next steps. Use when the user starts a new chat,
  says cold start, pick up where we left off, catch up, get up to speed, or
  session bootstrap.
---

# Context session start (Perfect Dark 2)

## When to use

Apply at the **start of a session** before writing code or picking tasks. If the user attached this skill explicitly, **read the listed files** (use the Read tool); do not skip them in favor of guessing from old chat.

## Is this necessary in Cursor?

**No — not strictly.** The same routine can live in:

- **Cursor Rules** (`.cursor/rules` or `AGENTS.md`): injected often; good for a *short* “read these files first.”
- **This skill**: optional; user attaches it or the description matches; good for the *full* checklist and briefing without bloating every message.

Use this skill when you want a **deliberate cold start** or the user asks to **sync with context/**. Rules + skill can coexist: rules remind; skill structures the ritual.

## Read order (per `context/README.md` cold-start checklist)

Paths are relative to the repo root (`context/`).

1. **[context/README.md](context/README.md)** - cold-start orientation, navigation hub, pillar table.
2. **[context/working-preferences.md](context/working-preferences.md)** - collaboration mode, tone, decision authority.
3. **[context/constraints.md](context/constraints.md)** - active + removed invariants.
4. **[context/procedures.md](context/procedures.md)** - build verify, git safety, worktree rules, truncation discipline.
5. **[context/tasks.md](context/tasks.md)** - razor-thin punch list, current critical path lanes.
6. **[context/session-log.md](context/session-log.md)** - top entries (rolling window, last ~110 sessions). Older tiers live in `_old/session-log/`.
7. **[context/retention.md](context/retention.md)** - the rules that keep the live tree from re-accreting cruft.

If the task is pillar-specific, then load **[context/pillars/<pillar>.md](context/pillars/)** for that pillar (one of: catalog, input, menus, modding, connectivity, save-wire-format, server, build-dev-tooling, tests, rendering, physics-collision).

If the task is design-driven, load the relevant doc from **[context/designs/<pillar>/](context/designs/)** (sub-bucketed by pillar).

Historical / archived material lives at **`_old/`** at repo root - do not pull facts from there without verifying against current code or the live `context/` tree.

## After reading — required briefing

Reply with a **short** briefing (plain prose, not a giant table):

- **Where things stand** — active tracks, anything “awaiting build test.”
- **What’s next** — from tasks-current + latest session-log “what’s next” / handoff.
- **Blockers or risks** — open bugs, protocol/build notes, worktree warnings if relevant.
- **Confirm** — ask one targeted question if direction is ambiguous; otherwise state assumed focus.

Do **not** claim “context loaded” without having read at least items 1-5 (through `tasks.md`) for substantive work.

## Relationship to other skills

- **context-manager** (`.claude/skills/context-manager/`): meta-rules for maintaining `context/` (update-after-implement, splits, build verify). Session-start **loads** context; context-manager **governs** how it is updated during work.

## Anti-patterns

- Reading `tasks.md` before `constraints.md` / `procedures.md` when doing a full bootstrap (violates the README cold-start checklist order).
- Treating archived session files (in `_old/session-log/`) as current without checking the active `session-log.md` header for the rolling window.
- Starting implementation before reading `constraints.md` for this codebase.
- Pulling facts from `_old/` (the archived prior context tree) without verifying against current code or the live `context/` tree.

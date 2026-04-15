---
name: context-session-start
description: >-
  Bootstraps a new Cursor session on this repo using context/ — reads QUICKSTART,
  README, tasks, session-log, constraints, and CRITICAL-PROCEDURES in order, then
  summarizes state and next steps. Use when the user starts a new chat, says cold
  start, pick up where we left off, catch up, get up to speed, or session bootstrap.
---

# Context session start (Perfect Dark 2)

## When to use

Apply at the **start of a session** before writing code or picking tasks. If the user attached this skill explicitly, **read the listed files** (use the Read tool); do not skip them in favor of guessing from old chat.

## Is this necessary in Cursor?

**No — not strictly.** The same routine can live in:

- **Cursor Rules** (`.cursor/rules` or `AGENTS.md`): injected often; good for a *short* “read these files first.”
- **This skill**: optional; user attaches it or the description matches; good for the *full* checklist and briefing without bloating every message.

Use this skill when you want a **deliberate cold start** or the user asks to **sync with context/**. Rules + skill can coexist: rules remind; skill structures the ritual.

## Read order (follow QUICKSTART session protocol)

Paths are relative to the repo root (`context/`). **QUICKSTART §3** specifies constraints → session-log → tasks after orientation; keep that order here. ([INDEX.md](context/INDEX.md) uses a slightly different table for quick hub scanning — both reach the same files.)

1. **[context/QUICKSTART.md](context/QUICKSTART.md)** — project identity, session protocol, abridged constraints.
2. **[context/README.md](context/README.md)** — hub, file index, session history table (latest rows).
3. **[context/INDEX.md](context/INDEX.md)** — where older session tiers live (`_archive/`); skim if already familiar.
4. **[context/constraints.md](context/constraints.md)** — rules that must hold for code changes.
5. **[context/session-log.md](context/session-log.md)** — top entries (recent sessions); deeper history only if needed (INDEX → archives).
6. **[context/tasks-current.md](context/tasks-current.md)** — active punch list and next-up.
7. **[context/CRITICAL-PROCEDURES.md](context/CRITICAL-PROCEDURES.md)** — build verification, git, worktrees, file integrity.

If the task is domain-specific (e.g. networking, UI), also read the **plan or domain files** README points to.

## After reading — required briefing

Reply with a **short** briefing (plain prose, not a giant table):

- **Where things stand** — active tracks, anything “awaiting build test.”
- **What’s next** — from tasks-current + latest session-log “what’s next” / handoff.
- **Blockers or risks** — open bugs, protocol/build notes, worktree warnings if relevant.
- **Confirm** — ask one targeted question if direction is ambiguous; otherwise state assumed focus.

Do **not** claim “context loaded” without having read at least items 1–6 (through `tasks-current.md`) for substantive work.

## Relationship to other skills

- **context-manager** (`.claude/skills/context-manager/`): meta-rules for maintaining `context/` (update-after-implement, splits, build verify). Session-start **loads** context; context-manager **governs** how it is updated during work.

## Anti-patterns

- Reading `tasks-current.md` before `constraints.md` / `session-log.md` when doing a full bootstrap (violates QUICKSTART §3).
- Treating archived session files as current without checking the active `session-log.md` header for the rolling window.
- Starting implementation before reading `constraints.md` for this codebase.

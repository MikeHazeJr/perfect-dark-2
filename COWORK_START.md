# Perfect Dark Mike — Cold Start Recovery

*If you're a new AI session and don't know where to begin, this is your bootstrap.*
*Last updated: 2026-03-25 (Session 48)*

---

## What This Is

PC port of Perfect Dark (N64, Rare 2000). C11 game code + C++ port layer. CMake + MSYS2/MinGW on Windows. Mike is the sole developer — he builds and tests. AI writes code, never compiles.

GitHub: https://github.com/MikeHazeJr/perfect-dark-2

---

## Session Start — Read These Files In Order

**All paths relative to the project root (`perfect_dark-mike/`).**

| Step | File | What It Tells You |
|------|------|-------------------|
| 1 | `CLAUDE.md` | Stack, critical rules, architecture, standing orders for the context system |
| 2 | `context/README.md` | Master index — links every context file with "when to read" guidance |
| 3 | `context/constraints.md` | Active constraints (must respect) and removed constraints (don't work around these) |
| 4 | `context/session-log.md` | Last 2-3 sessions — what was done, decisions, next steps |
| 5 | `context/tasks-current.md` | Active punch list — current work, open bugs, prioritized backlog |

**After reading all five:** summarize to Mike where things stand across all active fronts, then ask what he wants to focus on. Don't assume the last task is the next task.

**Domain files** (collision.md, networking.md, imgui.md, etc.) — load **only** when the current task requires them. The index in `context/README.md` tells you which file covers which system.

---

## Context Files Also Live at Project Parent Level

Some context files are mirrored one directory up (in the `Perfect-Dark-2/` folder alongside `perfect_dark-mike/`):

- `../README.md` — duplicate of `context/README.md` (master index)
- `../constraints.md` — duplicate of `context/constraints.md`
- `../session-log.md` — duplicate of `context/session-log.md`
- `../tasks-current.md` — duplicate of `context/tasks-current.md`

If the `context/` folder versions are stale, check the parent-level copies — those may be more current. The authoritative versions are whichever were updated most recently.

---

## Critical Rules (Quick Reference)

- **PC-only target** — no N64 constraints apply. Prefer correctness over micro-optimization.
- **`bool` is `s32`** — defined in `types.h` / `data.h`. Never include `<stdbool.h>` in game code.
- **C11 game code / C++ port code** — no C++ in `src/game/` or `src/lib/`.
- **Name-based asset resolution only** — all lookups through Asset Catalog. No numeric ROM addresses.
- **Protocol v21** — ENet, 60Hz tick, unreliable position + reliable state.
- **Update context files as you go** — a code change without a context update is incomplete.
- **Constraint check before every significant change** — if the complexity comes from a removed constraint, stop and propose the simpler approach.
- **Rabbit hole protocol** — if mid-task you're going deeper than expected, stop, explain, present options, let Mike decide.

---

## If Context Files Are Missing or Corrupted

The git repo has the full history. Run `git log --oneline -20` to see recent commits and `git show HEAD:context/README.md` (etc.) to recover any file from the last good state.

---

*This file is a bootstrap pointer, not a source of truth. The source of truth is the context system itself — start with CLAUDE.md and follow the protocol.*

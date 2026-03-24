# Perfect Dark 2 — PC Port

## Project
Merged PC port combining AllInOneMods (GEX, Kakariko, Goldfinger 64, Dark Noon) + netplay (ENet).
PC only — x86_64 via MSYS2/MinGW + CMake. SDL2 + OpenGL rendering.

## Stack
- **Language**: C (game code), C++ (port/fast3d renderer, ImGui backend)
- **Build**: CMake with `file(GLOB_RECURSE)` auto-discovery. PowerShell build-gui.ps1 for Windows.
- **Rendering**: fast3d GBI translator (N64 display lists → OpenGL), Dear ImGui v1.91.8 overlay
- **Networking**: ENet (UDP), server-authoritative with client prediction
- **Platform**: Windows x86_64 (MinGW), no N64 constraints apply

## Repository
GitHub: https://github.com/MikeHazeJr/perfect-dark-2

---

## STANDING ORDERS — Context System

**This section is mandatory. Every AI session, every tool, every agent must follow these rules.**

### 1. Context Is Code — Treat It That Way

Context files in `context/` are project infrastructure, not optional documentation. Updating them is **equal priority** to writing code. A code change without a corresponding context update is an incomplete change.

### 2. Session Start — Always Read First, Then Confirm

Before writing any code or making any changes:
1. Read `context/README.md` (master index — links everything)
2. Read `context/constraints.md` (what you must respect, what's been removed)
3. Read `context/session-log.md` (last 2–3 sessions — what was done, what's next)
4. Read `context/tasks-current.md` (active punch list — what needs doing)
5. Summarize to the user: where we are, what's next, any blockers
6. **Present all active work fronts** — don't assume the last task is the next task
7. Confirm direction before starting work

**Only load domain files** (collision.md, networking.md, etc.) **when the current task requires them.** Don't waste context loading everything.

### 3. Constraint Check — Before Every Significant Change

Before implementing anything complex, check `context/constraints.md`:
- **Active Constraints**: Things we must still respect (save format, protocol version, array limits)
- **Removed Constraints**: Things we've abandoned. If the task's complexity comes from a removed constraint, **stop and propose the simpler approach**.
- **Index Domain Warning**: Three index spaces that must not be confused (stage table, solo stage, stagenum)

### 4. Update As You Go — Not In a Batch

- Decision made → update constraints.md or relevant domain file **immediately**
- Bug found → add to `context/bugs.md` **immediately**
- Bug reveals a pattern → add to `context/systemic-bugs.md`
- Task completed → update `context/tasks-current.md` **immediately**
- Phase status changed → update `context/infrastructure.md`
- Constraint removed → add to Removed section of constraints.md with date and rationale

**Do not defer context updates to the end of the session.** If the conversation is cleared mid-task, the next session must be able to pick up from the context files alone.

### 5. Session End — Save State

When the user wraps up or a major task completes:
1. Update `context/session-log.md` with: focus, what was done, decisions, next steps
2. Update `context/tasks-current.md` with current status and any new blockers
3. Update any domain files that were touched
4. Brief summary to the user of what was recorded

### 6. Bug Discipline

- **One-off bugs** → `context/bugs.md` (ID, severity, root cause, fix, session)
- **Systemic patterns** (classes of bugs) → `context/systemic-bugs.md` (with search commands and audit checklists)
- After fixing a bug, **always do a propagation check**: does this same problem exist anywhere else? Fix the class, not the instance.

### 7. Pre-Task Sanity Check

Before starting significant work, mentally run through:
1. **Constraint check**: Does this assume a constraint that's been removed?
2. **Root cause check**: Am I fixing a symptom or the underlying problem?
3. **Scope check**: Is this the simplest approach for modern hardware?
4. **Cascade check**: Will this conflict with things already modernized?
5. **Effort check**: Is this proportional to its importance?

### 8. Rabbit Hole Protocol

If mid-task you realize you're going deeper than expected — **stop, don't push through.** Explain what's happening, present options (refactor vs. partial modernize vs. patch), recommend one, let Mike decide.

### 9. Proactive Context Saves

If the conversation is getting long, suggest saving state:
> "We've covered a lot of ground. Want me to save current state and start fresh?"

The goal: if context is cleared right now, the next session picks up in under a minute.

---

## Critical Rules — Code

- **PC-only target**: No N64 or Switch support. Modern hardware, no legacy constraints.
- **New code types**: Standard C types fine (`bool`, `int`, `float`, `uint8_t`, `<stdbool.h>`).
- **Legacy types**: `s32/u8/f32` from `PR/ultratypes.h` — typedefs, mix safely. Modernize organically.
- **No platform guards**: Zero `PLATFORM_N64` remain. All new code unconditional.
- **Memory**: `mempAlloc(size, MEMPOOL_STAGE)` for stage-lifetime. `IS4MB()` is compile-time `0`.
- **Modern HW**: Prefer correctness over micro-optimization. Legacy workarounds exist because of N64 limits.
- **Dead code removed**: N64 assembly, ultra/os, ultra/libc all removed.
- **AI builds via `build-headless.ps1`**: Game director tests in-game via playtest dashboard.

## Architecture
- `src/` — Original decompiled game code (C). `src/game/`, `src/lib/`, `src/include/`
- `port/` — PC port additions (C/C++). `port/fast3d/`, `port/src/`, `port/include/`
- `include/PR/` — N64 SDK headers (ultratypes.h, gbi.h)
- `context/` — **Project context encyclopedia.** Start with `context/README.md`.

## Key Subsystems
- `src/game/` — Game logic: player movement, props, menus, multiplayer
- `src/lib/` — Engine libraries: collision, capsule physics, model loading
- `port/fast3d/` — Rendering: GBI translator, ImGui backend, PD-authentic styling
- `port/src/net/` — Networking: ENet integration, message handlers

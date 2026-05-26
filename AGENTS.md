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

## Build environment — every session, before any build

> **Do not rediscover TEMP or PATH. Do not invent alternatives.**

From bash:
```bash
source devtools/build-env.sh && ninja -C Build pd pd-tests
```

From PowerShell:
```powershell
.\devtools\build-headless.ps1   # self-configures env
```

`build-env.sh` sets `TEMP`, `TMP`, and prepends `/c/msys64/mingw64/bin` to `PATH`.
`build-headless.ps1` dot-sources `devtools/_build-env-prelude.ps1` (idempotent).

### Parallel test builds

If more than one AI/code session may build at the same time, **do not use shared `Build/`**.
Use an isolated session build directory instead:

```powershell
.\devtools\build-session.ps1 -Session <short-session-id> -Target all
```

Outputs go to `.claude/session-builds/<short-session-id>/`. Reuse the same
`-Session` value for incremental rebuilds in one session; use a different value
for simultaneous sessions. The wrapper is queued by default, so watch its
queue status/ETA while waiting. Do not pass `-NoQueue` unless Mike explicitly
asks. Clean up after the session:

```powershell
.\devtools\build-session.ps1 -Remove -Session <short-session-id>
```

Queued builds have a 60-second active-build watchdog by default. If a build
times out, treat exit code `124` as a hung-build failure, record it in context,
and clean up the session directory with the same `-Remove -Session <id>`
command. Use `-BuildTimeoutSeconds <seconds>` only for a specific slow clean
build; use `0` only when Mike explicitly asks to disable the watchdog. New
wrapper starts capture live stdout/stderr in
`.claude/session-builds/<id>/_build-session.out.log` and
`_build-session.err.log`; use `.\devtools\build-session.ps1 -Tail -Session <id>`
or `-Tail -Follow` for the active build.

---

## STANDING ORDERS — Context System

**This section is mandatory. Every AI session, every tool, every agent must follow these rules.**

### 1. Context Is Code -- Treat It That Way

Context files in `context/` are project infrastructure, not optional documentation. Updating them is **equal priority** to writing code. A code change without a corresponding context update is an incomplete change.

**Canonical location**: `perfect_dark-mike/context/`. Copies of key files also exist at the parent level (`Perfect-Dark-2/`) for Cowork session access. When updating, always write to `context/` first, then sync the parent copy. If they ever diverge, `context/` wins.

### 2. Session Start — Always Read First, Then Confirm

Before writing any code or making any changes:
1. Read `context/README.md` (cold-start orientation, navigation hub)
2. Read `context/working-preferences.md` (collaboration mode)
3. Read `context/constraints.md` (active + removed invariants)
4. Read `context/procedures.md` (build verify, git safety, worktree, truncation discipline)
5. Read `context/tasks.md` (active punch list)
6. Read `context/session-log.md` (last 2-3 sessions)
7. Summarize to the user: where we are, what's next, any blockers
8. **Present all active work fronts** - don't assume the last task is the next task
9. Confirm direction before starting work

**Only load pillar docs** (`context/pillars/<pillar>.md`) **when the current task requires them.** Don't waste context loading everything. The 11 pillars: catalog, input, menus, modding, connectivity, save-wire-format, server, build-dev-tooling, tests, rendering, physics-collision.

### 3. Constraint Check — Before Every Significant Change

Before implementing anything complex, check `context/constraints.md`:
- **Active Constraints**: Things we must still respect (save format, protocol version, array limits)
- **Removed Constraints**: Things we've abandoned. If the task's complexity comes from a removed constraint, **stop and propose the simpler approach**.
- **Index Domain Warning**: Three index spaces that must not be confused (stage table, solo stage, stagenum)

### 4. Update As You Go — Not In a Batch

- Decision made -> update `context/constraints.md` or relevant pillar doc **immediately**
- Bug found -> add to `context/bugs.md` **immediately**
- Bug reveals a pattern -> add to `context/systemic-bugs.md`
- Task completed -> update `context/tasks.md` **immediately**
- Pillar status changed -> update `context/pillars/<pillar>.md` in the same commit as the code change
- Constraint removed -> add to Removed section of constraints.md with date and rationale

**Do not defer context updates to the end of the session.** If the conversation is cleared mid-task, the next session must be able to pick up from the context files alone.

### 5. Session End — Save State

When the user wraps up or a major task completes:
1. Update `context/session-log.md` with: focus, what was done, decisions, next steps
2. Update `context/tasks.md` with current status and any new blockers
3. Update any pillar doc whose live state shifted
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

### 9. Git Safety — Worktree Operations

These rules prevent the class of bugs where git operations silently discard or truncate in-flight work.

**Commit-first discipline.** As soon as a session has a meaningful unit of work, commit it (WIP message fine) before running ANY git operation that touches the working tree — stash, rebase, reset, checkout, merge. Uncommitted work is unprotected work.

**No bare `git stash`.** Never run `git stash` or `git stash push` without explicit paths. If separating work, use `git stash push -- <path> <path>` with explicit targets only. A bare stash grabs everything in the working tree — including build-verify noise and unrelated changes — and the pop can silently corrupt or conflict.

**Pre-op snapshot for destructive operations.** Before any `git rebase`, `git reset`, or `git merge`:
1. Record current HEAD SHA: `git rev-parse HEAD`
2. Record line counts of all changed files: `git diff --stat`
3. After the operation, verify against the snapshot — any file that shrank unexpectedly = halt and report to the user before continuing.

The optional helper `devtools/git-snapshot.sh` automates this (see `devtools/README-git-snapshot.md`).

**No `git reset --hard` without human instruction.** Never use `git reset --hard` in a session unless the user explicitly instructs it. Use `git reset --soft` or `git reset --mixed` if you need to unstage.

**Post-merge verification.** After any merge, re-verify line counts of all changed files vs. their worktree-pre-merge state. If any file shrank or disappeared that shouldn't have, halt and report before continuing. This applies to worktree merges, branch merges, and rebase completions.

### 10. Proactive Context Saves

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

## Asset Pipeline c3842 Codex Hook

Every Codex session that touches asset archives, extraction, catalog/provider loading, runtime asset adapters, Modding Hub asset save/import flows, distribution manifests, or typed `.pdxxx` examples must enforce this preflight:

1. Read the active constraint **Public asset source is the game-facing source** in `context/constraints.md`.
2. Treat every public typed-archive payload as directly user-editable source that the game client consumes natively through catalog/provider loading.
3. Treat renderer, GPU, collision, audio-codec, animation, graph-runtime, room/portal, and other engine-ready products as source-hashed rebuildable cache only.
4. Reject designs that add parallel authored runtime files, opaque `.bin` payloads, raw preprocessed dumps, numeric/legacy asset references in public payloads, or hand-maintained duplicate source/runtime representations.
5. When changing any asset family, add or update focused tests proving the public source file feeds runtime use, not only that it exists or can be previewed.
6. Run `python tools/asset_native_source_guard.py` before reporting completion. For commits, the tracked pre-commit hook runs the same guard with staged-file checks.

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

## Server and hosting

Build targets include **`pd`** (game client), **`pd-tests`**, and **`pd-updater`**. The standalone **`pd-server` / `PerfectDarkServer.exe` target is removed/deprecated**; listen mode runs the server inside the game client (`g_NetDedicated == 0`) and is the shipping path. Connect codes hide raw IPs in UI per `context/constraints.md`.

Threat model and operational notes: **`context/designs/connectivity/hosting-modes-listen-vs-dedicated.md`**. Live server pillar: **`context/pillars/server.md`** (folds in the prior `context/server-architecture.md`).

# Perfect Dark 2 - PC Port

## Project
Merged PC port combining AllInOneMods (GEX, Kakariko, Goldfinger 64, Dark Noon) + netplay (ENet).
PC only - x86_64 via MSYS2/MinGW + CMake. SDL2 + OpenGL rendering.

## Stack
- **Language**: C (game code), C++ (port/fast3d renderer, ImGui backend)
- **Build**: CMake with `file(GLOB_RECURSE)` auto-discovery. PowerShell build-gui.ps1 for Windows.
- **Rendering**: fast3d GBI translator (N64 display lists → OpenGL), Dear ImGui v1.91.8 overlay
- **Networking**: ENet (UDP), server-authoritative with client prediction
- **Platform**: Windows x86_64 (MinGW), no N64 constraints apply

## Repository
GitHub: https://github.com/MikeHazeJr/perfect-dark-2

## Codex live-session mode

Codex worktrees are disabled by user choice as of 2026-08-27. Codex sessions
work directly in the canonical checkout and coordinate exact Workbench item and
file ownership before editing. Only one live session may stage, commit, or push
at a time; other sessions must source-freeze and wait for that Git lane. Builds
remain isolated and queued through `build-session.ps1`. The worktree merge
rules below apply only if Mike explicitly re-enables worktrees.

## Startup

Canonical checkout: `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike`. Start with the scoped checklist in `context/README.md`, current user instructions, and Workbench ownership/notes for the assigned lane. Dated context snapshots and parent-level mirrors are historical convenience copies; this checkout's `context/` and canonical Workbench take precedence. An already assigned task needs no second direction confirmation.

## Build environment - before a build

> **Do not rediscover TEMP or PATH. Do not invent alternatives.**

AI verification uses the queued isolated wrapper below. It sets the canonical environment through `devtools/_build-env-prelude.ps1`; `devtools/build-env.sh` supplies the same TEMP/TMP/PATH invariants for Bash. See `context/procedures.md` for operator build entry points. Do not use shared `Build/` for concurrent verification.

### Isolated queued verification builds

If more than one AI/code session may build at the same time, **do not use shared `Build/`**.
Use an isolated session build directory for AI verification. Choose the affected target(s); `all` remains appropriate when client and tests both need rebuilding:

```powershell
.\devtools\build-session.ps1 -Session <short-session-id> -Target <client|tests|all>
```

Outputs go to `.claude/session-builds/<short-session-id>/`. Reuse the same
`-Session` value for incremental rebuilds in one session; use a different value
for simultaneous sessions. The wrapper is queued by default, so watch its
queue status/ETA while waiting. Do not pass `-NoQueue` unless Mike explicitly
asks. Clean up after the session:

```powershell
.\devtools\build-session.ps1 -Remove -Session <short-session-id>
```

Queued builds have a 120-second no-output watchdog by default, not a 120-second total build limit. If a build
times out, treat exit code `124` as a hung-build failure, record it in context,
and preserve useful failure logs/evidence before cleaning up the owned session
directory with the same `-Remove -Session <id>` command. Use `-BuildTimeoutSeconds <seconds>` only for a specific slow clean
build; use `0` only when Mike explicitly asks to disable the watchdog. New
wrapper starts capture live stdout/stderr in
`.claude/session-builds/<id>/_build-session.out.log` and
`_build-session.err.log`; use `.\devtools\build-session.ps1 -Tail -Session <id>`
or `-Tail -Follow` for the active build.

---

## STANDING ORDERS - Workbench and Coordination

The repo-local Workbench replaces Kanban as the durable project source of
truth. The coordination hub is separate operational state for active sessions
and exclusive-resource queues.

Canonical tooling:

- Workbench: `Tools/Workbench/`
- Durable data: `Tools/Workbench/data/roadmap.json`, `notes.jsonl`,
  `changelog.jsonl`
- Coordination: `Tools/CodexCoordination/CodexCoordination.ps1`
- Coordination guide: `docs/CODEX_COORDINATION.md`

Every agent session must:

1. Read `Tools/Workbench/README.md` and `Tools/Workbench/SCHEMAS.md`.
   Codex sessions must keep the repository lifecycle hooks in
   `.codex/hooks.json` enabled and trusted. The hooks register the real Codex
   session, inject live Workbench context, and block normal file-edit tools
   until that coordination session owns an active Workbench item and has
   processed targeted new notes. Review changed hooks with `/hooks`; never
   bypass hook trust or disable the hooks to evade this workflow.
2. Before any Workbench API mutation, inspect `GET /api/meta` and verify
   `canonical=true`, `isolated=false`, and that `projectRoot`/`dataDir` point at
   the canonical checkout. The default server refuses linked-worktree startup;
   isolated tests require an explicit nondefault port and data directory.
3. Reuse the exact coordination session id injected by the trusted lifecycle
   hook; do not register a second identity. Without a hook-provided identity,
   run coordination `status`, then `register` with a stable session id, goal,
   current task, plan, and ETA.
4. Read the Workbench roadmap and fold notes affecting its lane.
5. Process every `new` note affecting its lane before implementation:
   `acknowledged`, then `incorporated`, `rejected_with_reason`,
   `needs_clarification`, `waiting_user_decision`, or `superseded`.
6. Record ownership and dependencies before editing shared surfaces. Respect
   other active owners; use coordination chat for short operational conflicts.
7. Before builds, tests, game runs, smoke/multiplayer runs, extraction runs,
   captures, editors, or deployments, enter the appropriate coordination FIFO,
   wait until the item is next, run `start`, and run `finish` immediately after.
   The existing `build-session.ps1` queue remains an additional build safety
   layer and must not be bypassed.
   Hold the `game-runtime` slot only for a runnable game-test batch, from
   launch through testing and normal process teardown. Prepare binaries,
   storage, fixtures, and commands before taking the slot. Do not hold it
   for builds, staging, extraction, offline analysis, or other waiting.
   Let active tests finish uninterrupted. Release your slot promptly once
   the test processes exit; if another batch is not ready, requeue through
   the normal FIFO only when it is ready.
8. Update Workbench evidence and status as truth changes. `implemented` means
   connected to the production path and requires evidence plus model
   attribution. `validated` requires durable passing evidence; validation and
   performance items additionally require a passing verdict and artifacts.
9. Questions requiring a user choice become decision items with two to four
   concise options and trade-offs. Do not hide user decisions in free-text
   notes.
10. Use Workbench notes for durable handoffs whenever ownership changes or work
   pauses. Use coordination chat only for short live-session messages.
11. Never hand-count, reuse, rename, or delete Workbench IDs. Ask the server for
    the next ID or let `POST /api/roadmap/item` assign it.

All Workbench views use the same underlying records. Never create a parallel
per-view task list or revive the retired Kanban as live state.

---

## Context and safety

`context/README.md` owns startup/navigation and `context/procedures.md` owns operational rules. Follow their applicable sections instead of repeating a full context recovery on every turn. Load pillar/domain documents only for the assigned task.

- Canonical context is this checkout's `context/`. Parent copies never win by timestamp. Refresh a convenience mirror only for an identified consumer/handoff, after canonical context is updated; mark its source/date.
- Keep resumable state current as decisions, constraints, bugs, ownership, and evidence change. Workbench is the live task/decision/feedback authority; `context/tasks.md` is its concise narrative summary. Update relevant pillars and the session log at meaningful checkpoints, including before compaction; ordinary checkpointing needs no extra approval.
- Check active and removed constraints before significant changes. Do not retain workarounds for removed constraints. Preserve the index-domain distinctions and public-source asset contract below.
- Fix root causes and check propagation of a discovered bug class. Record one-off bugs in `context/bugs.md` and systemic patterns in `context/systemic-bugs.md`.
- If a task grows into an architectural/product-scope decision, explain the expansion and options and let Mike decide. Tactical choices within assigned work remain delegated.
- Follow `context/procedures.md` Git safety: protect owned work before worktree-changing operations, use explicit stash paths, snapshot HEAD/diffs/line counts before destructive operations, preserve user/peer edits, and verify integration. No `git reset --hard` without explicit human instruction. Never delete a live Git lock or reset to repair a lock.
- Keep the Git lane serialized. Worktrees remain disabled unless Mike explicitly re-enables them; do not overwrite the canonical checkout by copying a worktree over it.
- Verify file integrity and the changed unit proportionately. Preserve necessary builds, focused/full tests, ordinary runtime proof, asset guards, historical receipts, queues, and retention rules. Documentation-only changes need static/diff checks rather than a game build.

---

## Critical Rules - Code

- **PC-only target**: No N64 or Switch support. Modern hardware, no legacy constraints.
- **New code types**: Standard C types fine (`int`, `float`, `uint8_t`). `bool` is the project's `s32` macro (types.h); never include `<stdbool.h>`, and spell `s32` in declarations that C and C++ both see (SP-75, `context/constraints.md`).
- **Legacy types**: `s32/u8/f32` from `PR/ultratypes.h` - typedefs, mix safely. Modernize organically.
- **No platform guards**: Zero `PLATFORM_N64` remain. All new code unconditional.
- **Memory**: `mempAlloc(size, MEMPOOL_STAGE)` for stage-lifetime. `IS4MB()` is compile-time `0`.
- **Modern HW**: Prefer correctness over micro-optimization. Legacy workarounds exist because of N64 limits.
- **Dead code removed**: N64 assembly, ultra/os, ultra/libc all removed.
- **AI verification via isolated queued `build-session.ps1`**: Use affected targets and required tests/runtime proof. Game-director playtests remain a separate acceptance gate.

## Asset Pipeline c3842 Codex Hook

Every Codex session that touches asset archives, extraction, catalog/provider loading, runtime asset adapters, Modding Hub asset save/import flows, distribution manifests, or typed `.pdxxx` examples must enforce this preflight:

1. Read the active constraint **Public asset source is the game-facing source** in `context/constraints.md`.
2. Treat every public typed-archive payload as directly user-editable source that the game client consumes natively through catalog/provider loading.
3. Treat renderer, GPU, collision, audio-codec, animation, graph-runtime, room/portal, and other engine-ready products as source-hashed rebuildable cache only.
4. Reject designs that add parallel authored runtime files, opaque `.bin` payloads, raw preprocessed dumps, numeric/legacy asset references in public payloads, or hand-maintained duplicate source/runtime representations.
5. When changing any asset family, add or update focused tests proving the public source file feeds runtime use, not only that it exists or can be previewed.
6. Run `python tools/asset_native_source_guard.py` before reporting completion. For commits, the tracked pre-commit hook runs the same guard with staged-file checks.
7. Treat any runtime ROM/RomProvider fallback after extraction as an asset-chain failure owned by `c3844`, not an acceptable fallback path.

## Architecture
- `src/` - Original decompiled game code (C). `src/game/`, `src/lib/`, `src/include/`
- `port/` - PC port additions (C/C++). `port/fast3d/`, `port/src/`, `port/include/`
- `include/PR/` - N64 SDK headers (ultratypes.h, gbi.h)
- `context/` - **Project context encyclopedia.** Start with `context/README.md`.

## Key Subsystems
- `src/game/` - Game logic: player movement, props, menus, multiplayer
- `src/lib/` - Engine libraries: collision, capsule physics, model loading
- `port/fast3d/` - Rendering: GBI translator, ImGui backend, PD-authentic styling
- `port/src/net/` - Networking: ENet integration, message handlers

## Server and hosting

Build targets include **`pd`** (game client), **`pd-tests`**, and **`pd-updater`**. The standalone **`pd-server` / `PerfectDarkServer.exe` target is removed/deprecated**; listen mode runs the server inside the game client (`g_NetDedicated == 0`) and is the shipping path. Connect codes hide raw IPs in UI per `context/constraints.md`.

Threat model and operational notes: **`context/designs/connectivity/hosting-modes-listen-vs-dedicated.md`**. Live server pillar: **`context/pillars/server.md`** (folds in the prior `context/server-architecture.md`).

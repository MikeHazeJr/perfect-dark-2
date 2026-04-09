# PD2 PROJECT CONTEXT BRIEFING
**Generated:** 2026-04-06T09:00 (automated)
**Branch:** `dev` @ `0a519ae` (Add daily summary for 2026-04-05)
**Build Health:** UNABLE TO VERIFY — MSYS2 build environment only accessible on host machine; sandbox cannot invoke `C:/msys64/usr/bin/bash.exe`. **Mike must verify build before any new code sessions begin.**

---

## Git State

- **Uncommitted changes:** YES — `context/tasks-lobby-unification.md` (modified, not staged). Context file only, no code impact.
- **Unpushed commits:** 0 (branch up to date with `origin/dev`)
- **Active worktrees:** 1 — `claude/gracious-wright` at `238edb0` (marked **prunable**). No commits ahead of `dev`. Safe to clean up.
- **Stale local branches:** 22 `claude/*` branches exist locally. All have 0 commits ahead of `dev`. These are leftover from prior Cowork sessions and can be pruned.
- **Git index note:** `error: improper chunk offset(s) 993c and a3e0` appears on every git command. This is a commit-graph cache corruption — harmless but should be fixed with `git commit-graph write --reachable` when convenient.

---

## Active Workstreams

### 1. Catalog ID Migration — Primary Workstream
- **Goal:** Eliminate all integer asset identity from the codebase. Every asset reference becomes a catalog ID string. Zero-conversion mandate.
- **Task IDs:** Catalog Migration Phases 0–6 (bodies/heads DONE, S155). Weapons (~660 refs), stages (~80 refs), models (~83 refs), textures, sounds, animations, game modes, lang banks, props, HUD still ahead.
- **Status:** IN PROGRESS — bodies/heads complete through Phase 6. Remaining asset types not started.
- **Key files:** `port/src/net/netmsg.c`, `port/include/net/net.h`, `src/game/mplayer/*.c`, `port/fast3d/pdgui_menu_room.cpp`, `src/game/chr.c`, `src/game/player.c`, `port/src/modmgr.c`, `src/game/setup.c`, `src/include/matchsetup.h`, `plan-catalog-id-migration.md`
- **Last known state:** Phases 0–6 committed for bodies/heads. Version v0.0.45. Protocol v31. Build verification needed.
- **Dependencies:** None blocking. This is the top-priority workstream by game director decision (D-1).

### 2. B-112: Chr Pointer Corruption (31-bot matches)
- **Goal:** Find and fix root cause of `chr->hidden` access violation in `chraTick` during 31-bot matches.
- **Task IDs:** B-112
- **Status:** BLOCKED — defense-in-depth guards applied (chrBruise, chrDamage, model->definition check, VEH + 8MB stack), but root cause is unknown. Awaiting next crash log from playtest.
- **Key files:** `src/game/chraction.c`, `src/game/chr.c`, `port/src/crash.c`, `port/src/system.c`
- **Last known state:** Multiple guard layers in place. No crash reproduction pathway identified yet.
- **Dependencies:** Requires playtest with 31 bots to trigger crash and capture VEH log.

### 3. D5 — Full Menu System Replacement
- **Goal:** Replace all legacy C menu screens with ImGui equivalents. Infrastructure-first approach.
- **Task IDs:** D5.0 through D5.8
- **Status:** PARTIAL — D5.0a (tech spike) DONE, D5.1 (input boundary) DONE, D5.4 (MP scoreboard/endscreen) PARTIAL, D5.5 (Combat Sim polish) PARTIAL, D5.8 (OG removal) PARTIAL (matchsetup.cpp retired). D5.0, D5.2, D5.3, D5.6, D5.7 still PLANNED.
- **Key files:** `port/fast3d/pdgui_menu_*.cpp`, `port/fast3d/pdgui_theme.*`, `port/fast3d/pdgui_backend.cpp`, `port/fast3d/pdgui_hotswap.cpp`, `port/src/pdmain.c`
- **Last known state:** Next planned sub-phase is D5.0 (Menu Visual Layer — pdgui_theme module, OG ROM textures via catalog). Not started.
- **Dependencies:** D5.0 unblocks all subsequent D5 sub-phases. Catalog migration may touch some of the same UI files.

### 4. Open Playtest Issues
- **Goal:** Resolve remaining playtest regressions before next public build.
- **Task IDs:** B-115 (post-game mouse), prop sync (event-driven), B-91 (mission objectives), B-93 (pause menu), B-96 (difficulty flow), B-98 (pause menu OG fallback)
- **Status:** OPEN — multiple HIGH-severity bugs in solo mission flow (B-91, B-93, B-96, B-98) and one MED in MP (B-115).
- **Key files:** `port/fast3d/pdgui_menu_pausemenu.cpp`, `port/fast3d/pdgui_menu_solomission.cpp`, `port/fast3d/pdgui_menu_endscreen.cpp`, `port/src/net/netmsg.c`
- **Dependencies:** B-91/B-93/B-96/B-98 are all D5 scope (pause menu + mission select redesign). B-115 is standalone.

### 5. Security / Hardening Tier 2
- **Goal:** Fix pre-release security issues.
- **Task IDs:** B-78 (chat rate limit), B-79 (mod chunk ordering), B-80 (archive_bytes validation), B-81 (JSON recursion)
- **Status:** OPEN — not started. All MED severity.
- **Key files:** `port/src/net/netmsg.c`, `port/src/net/netdistrib.c`, `port/src/savefile.c`
- **Dependencies:** None. Can be parallelized safely.

### 6. D13 — Update System
- **Goal:** Self-updating client via GitHub Releases API.
- **Task IDs:** D13
- **Status:** Code written, needs libcurl MSYS2 install + compile test + first GitHub release.
- **Key files:** `port/src/updater.c`, `CMakeLists.txt`
- **Dependencies:** libcurl must be installed in MSYS2 environment. Mike needs to do first compile test.

---

## Collision Map

### HIGH RISK: `port/src/net/netmsg.c`
- **Conflicting workstreams:** Catalog ID Migration (weapons phase), B-78 (chat rate limit), B-72 (lobby state), B-84 (dead variable)
- **Collision type:** Edit conflict — multiple tasks modify different parts of the same ~5000-line file
- **Mitigation:** SERIALIZE all netmsg.c tasks. Assign to the same session. Never run two netmsg.c tasks in parallel.

### HIGH RISK: `port/src/net/netdistrib.c`
- **Conflicting workstreams:** B-79 (chunk ordering), B-80 (archive_bytes), B-86 (enet_peer_send)
- **Collision type:** Edit conflict — three bugs in the same file
- **Mitigation:** Bundle all three into a single session. Small fixes, low risk of logical conflict.

### MEDIUM RISK: `CMakeLists.txt`
- **Conflicting workstreams:** D13 (libcurl dependency), any build system changes
- **Collision type:** Build system fragility — this file has been corrupted before (B-114)
- **Mitigation:** Only ONE session touches CMakeLists.txt at a time. Use Edit tool ONLY, never Write. Verify file size after any edit.

### MEDIUM RISK: `port/fast3d/pdgui_menu_room.cpp`
- **Conflicting workstreams:** Catalog ID Migration (UI shadow structs), D5.5 (Combat Sim polish), D5.7 (Online Lobby polish)
- **Collision type:** Edit conflict + logical dependency (catalog ID fields in UI structs)
- **Mitigation:** Complete catalog migration phases for this file before starting D5 UI work on it.

### MEDIUM RISK: `port/fast3d/pdgui_menu_solomission.cpp`
- **Conflicting workstreams:** B-90, B-91, B-96, B-97, D5.2 (Mission Select Redesign)
- **Collision type:** All are solo mission UI bugs + the D5.2 redesign
- **Mitigation:** Defer individual bug fixes — D5.2 will likely rewrite this screen entirely. Fix bugs as part of the redesign.

### MEDIUM RISK: `port/fast3d/pdgui_menu_pausemenu.cpp`
- **Conflicting workstreams:** B-93, B-94, B-98, D5.3 (Pause Menu)
- **Collision type:** Same as above — multiple bugs + planned rewrite
- **Mitigation:** Defer bug fixes to D5.3 implementation.

### LOW RISK: `src/game/chr.c` / `src/game/chraction.c`
- **Conflicting workstreams:** B-112 (root cause investigation), Catalog ID Migration (if chr struct fields change)
- **Collision type:** Logical dependency — catalog migration may change struct fields that B-112 investigation reads
- **Mitigation:** B-112 investigation is read-only (diagnostic). Safe to run in parallel with catalog migration if the investigator is aware of struct changes.

### LOW RISK: Context files (`context/*.md`)
- **Conflicting workstreams:** Every session updates context
- **Collision type:** Text merge conflict
- **Mitigation:** Each session updates its own sections. Use Edit tool with precise old_string matches.

---

## Recommended Session Plan

### Safe to Parallelize (touch different files):
- **Security hardening (B-78, B-79, B-80, B-81, B-86)** — `netmsg.c` (B-78 only), `netdistrib.c`, `savefile.c`
  - ⚠️ B-78 touches netmsg.c → CANNOT run in parallel with catalog migration weapons phase
  - B-79 + B-80 + B-86 (all netdistrib.c) → bundle into one session
  - B-81 (savefile.c) → safe to run independently

### MUST Be Serialized:
1. **Catalog ID Migration weapons phase** → touches `netmsg.c`, `setup.c`, `modmgr.c`, many game files
2. **Any other netmsg.c work** (B-78, B-72, B-84) → after catalog migration completes its netmsg.c changes
3. **D5 UI work on room.cpp** → after catalog migration UI shadow struct changes are stable

### Recommended Execution Order:
1. **BUILD VERIFICATION FIRST** — Mike must confirm client + server compile clean on host before any new code work
2. **Catalog ID Migration: Weapons** — highest priority, touches the most files, should go first while the codebase is stable
3. **B-79 + B-80 + B-86 bundle** (netdistrib.c) — safe in parallel with #2
4. **B-81** (savefile.c JSON recursion) — safe in parallel with #2 and #3
5. **B-78** (chat rate limit in netmsg.c) — after #2 completes
6. **D5 sub-phases** — after catalog migration stabilizes

### Require Human Review Before Delegating:
- **B-112 root cause** — investigation only, not a code task until a crash log provides new evidence
- **D13 build test** — requires Mike to install libcurl in MSYS2 first
- **Any NET_PROTOCOL_VER bump** — currently 31, must be coordinated across all sessions

---

## Critical Build/Project Notes

Every code session MUST follow these rules:

1. **Use ONLY the Edit tool on existing files, NEVER Write** — Write tool has caused file truncation before (B-114 incident). The sole exception is creating brand-new files.
2. **Build command**: `TEMP=/tmp` with MSYS2 shell — `C:/msys64/usr/bin/bash.exe -lc "cd /c/Users/mikeh/Perfect-Dark-2/perfect_dark-mike && TEMP=/tmp cmake --build build/client"` (and `build/server`).
3. **types.h defines `#define bool s32`** — use `s32`, not `_Bool`, in legacy game code (`src/game/`). Port code (`port/`) can use standard C++ `bool`.
4. **NET_PROTOCOL_VER is currently 31** — any wire format change requires a bump. Coordinate across sessions.
5. **Do not use worktrees** — changes must land directly in the main working copy on `dev`.
6. **Run headless build check before reporting any task as complete** — both client and server.
7. **Every code session must merge its changes into the main working copy, build-verify BOTH client and server, and confirm the merge is clean before reporting done.** No handing off unmerged work.
8. **Context updates are mandatory** — update `tasks-current.md`, `session-log.md`, and any affected domain files before session end.
9. **MATCH_MAX_SLOTS = 40** (canonical, from `matchsetup.h`). Do not redefine locally.
10. **Asset identity is name-based only** — all lookups through Asset Catalog. No numeric ROM addresses or table indices for identity.

---

## Open Questions / Blockers

1. **Build health unknown** — cannot verify from sandbox. Mike must confirm client + server compile clean before dispatching code sessions. Last known good: v0.0.45 (S155 commits).
2. **B-112 root cause** — no new crash logs available. Cannot make progress without a 31-bot playtest crash that captures VEH output.
3. **Git commit-graph corruption** — `error: improper chunk offset(s)` on every git command. Not harmful but should be fixed: `git commit-graph write --reachable`.
4. **Stale worktree** — `claude/gracious-wright` is prunable. Clean up with `git worktree remove`.
5. **22 stale claude/* branches** — all merged into dev. Can be batch-deleted: `git branch -d claude/adoring-mayer claude/affectionate-fermi ...`
6. **Uncommitted context change** — `context/tasks-lobby-unification.md` is modified but unstaged. Should be committed or discarded.
7. **D13 blocked on libcurl** — Mike needs to install libcurl in MSYS2 before update system can be compile-tested.
8. **Catalog migration scope** — weapons phase (~660 refs) is the next major effort. Game director should confirm whether to proceed or address any D5/playtest items first.

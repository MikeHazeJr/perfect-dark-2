# PD2 PROJECT CONTEXT BRIEFING

**Generated:** 2026-04-11 07:05 EDT
**Branch:** `dev` @ `f7f5d26d` (S199: context updates — updater parse diagnosis, D13 instrumented)
**Build Health:** UNVERIFIED THIS RUN — sandbox lacks the Windows MSYS2 toolchain (`C:/msys64/usr/bin/bash.exe`) that the task spec calls for. Last build on record (S199, ~2 hours ago) was clean: PerfectDark.exe 48,919,825 bytes + PerfectDarkServer.exe 22,788,944 bytes, both exit 0. Any session that lands code MUST run the headless build itself before declaring done.

---

## Git State

- **Uncommitted changes:** YES (3 files, doc-only, no source code)
  - `context/README.md` — refreshes the "Last updated" line and adds S196–S199 to recent-sessions table
  - `context/roadmap.md` — bumps Build to v0.0.75, Sessions to 199+, marks D13 as in-progress
  - `context/tasks-current.md` — updates B-129 to "FIXED (S190 + S198)" and adds B-130/B-131 to the open-bug section
  - These are all consistent S199 housekeeping edits — staging + commit them as `S199: context updates` is safe and unblocking. They do NOT touch any code that another session would compile.
- **Unpushed commits on `dev`:** 5
  - `f7f5d26d` S199 context updates
  - `da99c172` Build v0.0.75 auto-commit
  - `4581074c` Merge worktree claude/agitated-wozniak: updater parse instrumentation
  - `654ac54b` fix(updater): instrument parse failures + per_page 30→100
  - `fd40c31a` Build v0.0.75 auto-commit
  - All represent S199 work; nothing here is mid-flight.
- **Stashes:** 3 ancient WIP stashes at `e1ba2344` / `195052ba` / `b597b2b2` (all from the v0.0.51–v0.0.56 era). These have aged out — Mike should review and drop in a quiet moment. Sessions should NOT pop them blindly.
- **Active worktrees:** 6, all marked **prunable** by git, all on the host filesystem at `C:/Users/mikeh/Perfect-Dark-2/perfect_dark-mike/.claude/worktrees/`:

| Worktree | Branch HEAD | Status vs `dev` | Action |
|---|---|---|---|
| agitated-mclaren | `18fab903` | 0 commits ahead | safe to prune |
| agitated-wozniak | `654ac54b` | 0 commits ahead (already merged in S199) | safe to prune |
| busy-morse | `36fe8f12` | 0 commits ahead | safe to prune |
| jovial-almeida | `94db4f5c` | 0 commits ahead | safe to prune |
| magical-ardinghelli | `94db4f5c` | 0 commits ahead | safe to prune |
| nervous-germain | `f3ead408` | 4 commits ahead BUT identical patches already on dev (`a2653d95`, `c81f555f`, `dfb3d043`, `3fc345bf`) | safe to prune; cherry-pick already done in S198 |

All worktrees are prunable and contain no unmerged work. Recommend `git worktree prune` housekeeping at start of next maintenance session.

---

## Active Workstreams

### 1. D5 Phase 3 — Menu Screen Replacement (Batches 3–11)
- **Status:** IN PROGRESS. Batches 0–2 DONE (S192–S194). 9 batches remain (~10–14 sessions).
- **Task IDs:** D5 P3
- **Key files:** `port/fast3d/pdgui_menu_*.cpp` (esp. `pdgui_menu_solomission.cpp`, `pdgui_menu_mainmenu.cpp`), `pdgui_layout.{h,cpp}`, `pdgui_backend.cpp`, `pdgui_scaling.h`
- **Last known state:** Foundation primitives (action bar, scrim, model preview, 1080p baseline) are landed. Co-op / Counter-Op flow (Batch 2) shipped at +701 LOC in `pdgui_menu_solomission.cpp`. Plan in `context/designs/menu-replacement-plan.md`.
- **Dependencies:** none structural; each batch is independent at the dialog level but most live in shared menu source files.

### 2. D5 Phase 4 — Theme System
- **Status:** IN PROGRESS (S196). Visible chrome rendering — pending in-game verification by Mike.
- **Key files:** `port/src/modmgr.c`, `port/fast3d/pdgui_nineslice.{h,cpp}`, `pdgui_menu_settings_video.cpp`, `pdgui_menu_theme_editor.cpp`, `mods/base-game/ui-chrome/`, `pdguiDrawPdDialog` (render branch)
- **Last known state:** Template-mod plumbing landed; nineslice pipeline lifted; Settings → Video → UI Chrome Style toggle wired. Deferred follow-ups: `modmgrSaveAs`, Modding Hub template grouping, base-ui extractor migration.
- **Dependencies:** B-130 instrumentation (S198) lives in `pdgui_menu_theme_editor.cpp` — same file. Coordinate any theme-editor edits with B-130 diagnosis.

### 3. B-112 — Chr pointer corruption in 31-bot matches
- **Status:** INVESTIGATING (S191). Awaiting next 31-bot crash log.
- **Key files:** `src/game/chraction.c`, `src/game/chr.c`, `port/src/crash.c` (SIGABRT handler)
- **Last known state:** Entry guard at top of `chraTick`, `g_ChrLastTickedIndex` slot tracker, SIGABRT handler reads index. No code changes pending — purely diagnostic posture.
- **Dependencies:** Mike must run a 31-bot match and capture the crash log before any further fix can land.

### 4. B-126 — Silent crash ~8 min into MP
- **Status:** INVESTIGATING (S191). Awaiting next repro.
- **Key files:** `src/game/lv.c` (heartbeat), `port/src/crash.c`, `port/src/net/net.c` (`netHeartbeatLog`)
- **Last known state:** Heartbeat 60→30s, full NET.WATCHDOG per-peer dump, SIGABRT handler logs chr index. No pending code changes.
- **Dependencies:** Same as B-112 — needs a fresh log from playtest.

### 5. B-129 — Mission-end AV crash + agent save path
- **Status:** FIXED (S190 + S198). Awaiting visual confirmation from Mike.
- **Key files:** `port/src/main.c:167`, `port/src/server_main.c:226`, `src/game/endscreen.c`, `port/fast3d/pdgui_menu_warning.cpp`
- **Last known state:** `saveInit()` wired into both startups (`3fc345bf`); endscreen no longer calls `filemgrSaveOrLoad`. Saves now land in `AppData/Roaming/perfectdark/`.
- **Dependencies:** Mike's verification of Mission 1 → Mission 2 advance.

### 6. B-130 — Theme editor close-paths ineffective
- **Status:** OPEN (S198). Lifecycle instrumentation deployed; do NOT fix blind.
- **Key files:** `port/fast3d/pdgui_menu_theme_editor.cpp`
- **Last known state:** `sysLogPrintf` at all 4 exit sites (Begin collapsed, Close button, X button, click-outside InvisibleButton). Working hypothesis: z-order vs Settings menu.
- **Dependencies:** Awaiting next playtest log.

### 7. B-131 — Post-restart Start button double-fire
- **Status:** OPEN (S198). Deferred to a follow-up session.
- **Key files:** `port/src/inputctx.c`, `src/game/bondmove.c`
- **Last known state:** Hypothesis = residual `menuinputs.start` consume flag OR lingering deferred-pop inputctx entry across `mainChangeToStage(0x30)`. No code pending.
- **Dependencies:** None blocking; can be picked up any time.

### 8. D13 — Update System parse diagnosis
- **Status:** IN PROGRESS (S199). Instrumentation shipped, awaiting next failed-check log.
- **Key files:** `port/src/updater.c`
- **Last known state:** `per_page` 30→100, HTTP code logging on non-200, raw response preview on parse failure, token-type log on non-array root. Likely root cause: GitHub rate-limit 403.
- **Dependencies:** None blocking. Next step is observation, not code.

### 9. Pre-v0.1.0 verification pass (input/sky/save bugs from S189–S198)
- **Status:** PLANNED — needs Mike at the keyboard, not a code session.
- **Verification items:** A=jump/B=crouch/Y=use, R3 unbound, LSTICK click sprints, rebind UI MP slots 1–3 unbound, CrouchMode=2 toggle, Mission 1 save persists, sky tearing gone, Skedar pink-sky check.

### 10. Should-Have backlog (M3 lobby polish, B-78, B-81, prop sync event-driven, B-118)
- **Status:** PLANNED. Each is a discrete contained change in a single subsystem.

---

## Collision Map

The active workstreams are mostly diagnostic (B-112, B-126, B-130, D13) and won't generate edits, so the collision risk surface is small. Real risks below:

### HIGH risk

1. **`port/fast3d/pdgui_menu_theme_editor.cpp`**
   - **Conflicts:** D5 Phase 4 (Theme System) ↔ B-130 (close-path bug)
   - **Type:** Edit conflict + logical coupling. B-130 instrumentation is the diagnostic hook for the very window the theme system renders. If a Phase-4 session refactors the theme editor before B-130 logs are read, it can erase the diagnostic before Mike captures a repro.
   - **Mitigation:** Defer all D5 Phase-4 follow-ups touching `pdgui_menu_theme_editor.cpp` until B-130 is closed. Other Phase-4 work (modmgr / nineslice / settings dropdown) is fine.

2. **`port/fast3d/pdgui_menu_solomission.cpp`**
   - **Conflicts:** D5 Phase 3 Batches 3–11 will likely add MP / Combat-Sim / Challenge dialogs into this same file (it just grew +701 lines in Batch 2 to 3,351 lines)
   - **Type:** Edit conflict — large file, single shared compilation unit, multiple parallel sessions touching it would clobber each other.
   - **Mitigation:** SERIALIZE all D5 P3 batches that touch `pdgui_menu_solomission.cpp`. Never assign two of them to parallel sessions. Have each session pull a fresh dev tip and rebase before edits.

3. **`context/tasks-current.md`, `context/bugs.md`, `context/session-log.md`, `context/README.md`, `context/roadmap.md`**
   - **Conflicts:** Every session updates these.
   - **Type:** Edit conflict (this has caused real problems in the past, including file truncation).
   - **Mitigation:** SERIALIZE context-file writes. Prefer Edit (never Write) on these per CRITICAL-PROCEDURES.md. Each session should commit context updates as the *last* step, then push immediately so the next session pulls a clean tip.

### MEDIUM risk

4. **`port/src/net/net.c` and `netmsg.c`**
   - **Conflicts:** B-126 instrumentation (NET.WATCHDOG) is live. Any future M3 lobby polish, B-78 chat rate-limiting, or prop-sync event-driven work touches the same compilation units.
   - **Type:** Edit conflict + protocol-version coupling. NET_PROTOCOL_VER is currently **v32**; any wire change requires bump-and-coordination.
   - **Mitigation:** No two net-touching tasks in parallel. Any session changing wire format must (a) bump NET_PROTOCOL_VER, (b) verify both client and server build, (c) call out the bump in commit message.

5. **`src/game/bondmove.c`**
   - **Conflicts:** B-131 (Start double-fire investigation) and S189/S190 input fixes verification. Recently heavily edited; sensitive to merge order.
   - **Type:** Edit conflict + brittle hand-merged regions.
   - **Mitigation:** Treat B-131 as exclusive owner of bondmove.c until closed. Use Edit only, never Write.

6. **`port/src/inputctx.c`**
   - **Conflicts:** B-131 (Start double-fire) and B-122/B-124 input-context system. Stable today but B-131 may need new logic here.
   - **Mitigation:** Same — single session at a time.

### LOW risk

7. **`CMakeLists.txt`**
   - **Conflicts:** Build-system edits from any source-adding session.
   - **History:** B-114 was a 30 MB CMakeLists.txt corruption from a tool encoding bug. Use Edit only, NEVER Write.
   - **Mitigation:** Single session per CMakeLists edit. Inspect diff before commit.

8. **`port/src/savefile.c`**
   - **Conflicts:** B-81 (JSON recursion guard) is a clean isolated change but lives in the same file the S198 fix exposed via `saveInit()`. Unlikely to collide unless another save-related task is opened in parallel.
   - **Mitigation:** Schedule B-81 in its own session.

---

## Recommended Session Plan

**Immediately commit-and-push** the doc-only `dev` working-copy diffs (`context/README.md`, `roadmap.md`, `tasks-current.md`) so the next session starts on a clean tip. Single session, ~1 minute.

**Safe to run in parallel** (different files, no overlap):
- D5 P3 Batch 3 dialog set (in `pdgui_menu_*.cpp` files OTHER than `solomission.cpp` and `theme_editor.cpp`) — pick a batch from the menu-replacement-plan that touches a different unit
- B-81 JSON recursion guard (`savefile.c`)
- B-78 chat rate limiting (`netmsg.c`) — but NOT alongside any other net.c/netmsg.c task

**Must serialize** (same files, real collision risk):
- All D5 P3 batches touching `pdgui_menu_solomission.cpp` — one at a time
- Any context/ file writes — one at a time, commit + push between
- All net.c / netmsg.c work — one at a time, bump NET_PROTOCOL_VER if wire changes

**Specific execution order (dependencies):**
- D5 P3 Batches 3 → 4 → … 11 should follow plan order (each batch builds on the prior batch's primitives)
- B-130 fix MUST wait for next playtest log — do not assign as a code session yet
- B-112 / B-126 fixes MUST wait for next crash log — do not assign as code sessions yet
- D13 next step is observation, not code — do not assign as a code session yet

**Should NOT be delegated without human review:**
- Any change to NET_PROTOCOL_VER
- Any edit to `crash.c` / VEH / SIGABRT machinery (one wrong move and B-126 instrumentation goes silent)
- Any popping of the 3 stale stashes
- Any deletion of the 6 prunable worktrees if Mike has manual edits in them outside git's view (low-risk but worth confirming)
- B-129 closure (still pending Mike's visual confirmation; do not declare verified)

---

## Critical Build/Project Notes (every session must read)

- **Use ONLY the `Edit` tool on existing files. NEVER `Write`.** Truncation has happened before — `_dev-window.ps1` (S189), CMakeLists.txt (B-114). Truncation kills the project.
- **Build command:** `C:/msys64/usr/bin/bash.exe -lc "cd /c/Users/mikeh/Perfect-Dark-2/perfect_dark-mike && TEMP=/tmp cmake --build build/client 2>&1 | tail -20"`. Server: same with `build/server`. The `TEMP=/tmp` is required — MSYS2 dies on Windows-style temp paths.
- **`types.h` defines `#define bool s32`.** Use `s32`, NEVER `_Bool`. Mixing them causes silent struct-layout drift (the original B-119 / shadow-struct class of bug).
- **NET_PROTOCOL_VER is currently v32.** Coordinate any bump. Both client and server must build green and the bump must be called out in the commit message.
- **Do NOT use worktrees.** Every code session must land in the main working copy. The 6 currently-existing worktrees are all prunable leftovers from prior sessions and contain no unmerged work.
- **Run the headless build BEFORE declaring any task complete.** Build BOTH client and server. Verify both exit 0.
- **EVERY code session must merge its changes into the main working copy, build-verify BOTH client and server, and confirm the merge is clean before reporting done. No handing off unmerged work.**
- **Em-dashes get truncated by the encoding bug.** Use ASCII hyphens `-` in any file the build pipeline touches (PowerShell scripts, CMake files, C source comments).
- **Context files are append-mostly.** Updating tasks-current.md / bugs.md / session-log.md / README.md / roadmap.md is one of the easiest ways to lose work to a parallel-session collision. Always Edit, never Write; commit + push immediately.

---

## Open Questions / Blockers

1. **Build verification was skipped this run.** The task spec calls for `C:/msys64/usr/bin/bash.exe -lc …` but the briefing agent runs in a Linux sandbox where that path does not exist. Last known good build is S199 ~2 hours ago. **Recommendation:** the first code session that runs today should kick a clean build immediately and report status before doing any work.
2. **3 stale stashes** dating to v0.0.51–v0.0.56 era. Mike to decide drop vs keep.
3. **6 prunable worktrees**, all containing no unmerged work. Safe to `git worktree prune` but worth one human eye before pruning.
4. **B-129, B-128, sky pink (B-18)** all marked "FIXED — awaiting visual confirmation from Mike." None of these can be closed without a Mike playtest. Sessions should not redo this work.
5. **B-130 / B-126 / B-112 / D13** all blocked on user-captured logs. No code session should be assigned to "fix" any of them until logs land.
6. **Doc-only uncommitted diffs on dev** (3 context files) — these are the leftovers from S199 housekeeping. Recommendation: commit + push as the first action of the next session so we start clean.

---

*Briefing prepared autonomously per the pd2-context-briefing scheduled task. No write actions taken on the codebase. Next code session should begin by committing the pending context-file diffs and pruning the 6 prunable worktrees.*

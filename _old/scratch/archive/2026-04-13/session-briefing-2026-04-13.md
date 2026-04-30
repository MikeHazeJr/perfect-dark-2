# PD2 PROJECT CONTEXT BRIEFING
**Generated:** 2026-04-13T00:00 (automated scheduled task)
**Branch:** `dev` @ `bfa578a4` (Merge worktree zealous-ardinghelli: seamless audio mod network sync)
**Build Health:** CANNOT VERIFY FROM SANDBOX — MSYS2 shell not available in Cowork sandbox environment. Last known good build was S222 (2026-04-12). Mike must verify locally before any new work.

---

## Git State

- **Uncommitted changes:** YES — git index is CORRUPT (`unknown index entry format 0x4c460000` / `0x75740000`). `git status` and `git diff` fail. Many `.d` dependency files show as deleted in partial diff output. **ACTION REQUIRED:** Mike should run `git reset` or repair the index before any new work. This is likely caused by interrupted git operations or worktree cleanup.
- **Unpushed commits:** 8 commits ahead of `origin/dev`:
  - `bfa578a4` Merge worktree zealous-ardinghelli: seamless audio mod network sync
  - `1601745d` feat: seamless audio mod network sync
  - `088a331e` Merge worktree zealous-ardinghelli: fix audio ini-based mod discovery and network transfer
  - `d73fe6a3` fix: audio ini-based mod discovery, registration, and network transfer
  - `d8440b53` Build v0.0.90 auto-commit
  - `efa0117f` Merge worktree condescending-sanderson: fix charpreview black FBO + FR weapon challenge crash
  - `6cc8906d` fix: charpreview black FBO + FR weapon challenge crash
  - `f493086a` Build v0.0.90 auto-commit
- **Stashes:** 3 old stashes (v0.0.51, v0.0.56 era) — can be cleaned up
- **Active worktrees:** 5, ALL marked `prunable`, ALL with zero commits ahead of `dev`:
  - `condescending-sanderson` — merged, safe to prune
  - `elastic-nightingale` — merged, safe to prune
  - `gallant-leakey` — merged, safe to prune
  - `quirky-kare` — needs verification (at `6ef0667f`, not on `dev` history)
  - `zealous-ardinghelli` — merged, safe to prune

**Recommendation:** Run `git worktree prune` to clean up all 5 worktrees. Verify `quirky-kare` contents before pruning — its commit `6ef0667f` may contain unmerged work not yet on dev. Fix the corrupt git index first.

---

## Active Workstreams

### 1. D5 Phase 3 — Remaining Menu Screens (COMPLETE)
- **Task IDs:** D5 Phase 3 (tasks-current.md)
- **Status:** COMPLETE as of 2026-04-11 (11 batches across S192-S209)
- **Key files:** `port/fast3d/pdgui_menu_*.cpp`, `port/include/pdgui_menus.h`
- **Last known state:** All 11 batches done. 79 screens ported to ImGui. ImGui is the sole menu system.
- **Dependencies:** None remaining.

### 2. Audio Mod System (RECENTLY COMPLETED)
- **Task IDs:** S219-S222 work
- **Status:** Feature complete. Network sync done (S222 + zealous-ardinghelli merge). Three playtest fixes merged.
- **Key files:** `port/fast3d/pdgui_menu_audiomod.cpp`, `port/src/modmusic.c`, `port/src/net/netmsg.c`, `port/include/net/net.h`
- **Last known state:** MP3 path resolution fixed, import feedback added, music selection in room menu added. Seamless network sync with playlist manifest, per-client DL status, SVC_MUSIC_ADVANCE, catalog rebroadcast.
- **Dependencies:** None. Protocol bumped to v34.

### 3. Skin Editor (FEATURE COMPLETE)
- **Task IDs:** S-1 through S-9
- **Status:** All 9 batches complete (S211-S217). Feature complete.
- **Key files:** `port/fast3d/pdgui_skin_editor.cpp`, `pdgui_skin_canvas.cpp`, `pdgui_skin_quantize.cpp`, `pdgui_skin_uv.cpp`, `port/include/pdgui_skin_editor.h`
- **Last known state:** Canvas + draw tools + fill/line/brush + undo + save-as-mod + image import + PD-style quantize + blend modes + UV wireframe + network sync all done.
- **Dependencies:** None.

### 4. File Browser Widget (DONE)
- **Task IDs:** S219
- **Status:** Complete.
- **Key files:** `port/fast3d/pdgui_filebrowser.cpp`, `port/include/pdgui_filebrowser.h`
- **Last known state:** Shared ImGui file browser, used by audio mod and skin editor importers.

### 5. Character Preview System (STABILIZED)
- **Task IDs:** B-133 (S220), charpreview fixes (S219, condescending-sanderson)
- **Status:** Fixed. Two separate bugs resolved — inline Vp GBI crash (S220) and black FBO + FR weapon challenge crash (condescending-sanderson).
- **Key files:** `port/fast3d/pdgui_charpreview.c`, `port/fast3d/gfx_pc.cpp`
- **Last known state:** Static Vp, loading deadlock fixed, black FBO fixed. Awaiting playtest confirmation.

### 6. Input System (STABILIZED)
- **Task IDs:** S218, S221 controller binding work
- **Status:** Complete. Controller radial menu accessible, default bindings reworked, LSTICK direction display fixed.
- **Key files:** `port/src/actionmap.cpp`
- **Last known state:** X=interact+reload, Y=weapon cycle, DpadLeft=radial, A=jump, B=crouch. Analog override guard prevents digital snap. All input bugs (B-124a-d, B-127, B-132) fixed.

### 7. Playtest Bug Fixes (ONGOING)
- **Task IDs:** Verification pass items in tasks-current.md
- **Status:** Awaiting Mike's verification playtest
- **Remaining verification items:**
  - A=jump (not doors), Y=interact, B=crouch (not doors)
  - R3 unbound, LSTICK sprint
  - CrouchMode=2 toggle
  - Mission 1 completion + JSON save
  - Sky tearing on outdoor stages (B-128)
  - B-18 pink sky on Skedar Ruins
- **Dependencies:** Requires Mike's in-game testing

### 8. Open Investigations
- **B-126:** Silent crash after ~8 min in MP (8 bots). Instrumentation deployed (heartbeat, SIGABRT handler, chr index tracker). Root cause unknown — possibly stack canary smash. Awaiting next repro.
- **B-112:** Chr pointer corruption in 31-bot matches. Entry guard + per-chr index tracker deployed. Root cause unknown.
- **B-118:** Crash during CI intro cutscene loop. 56 models late-added to SP manifest. Open since S163.
- **D13:** Update system parse failure. Instrumentation deployed. Likely GitHub rate-limit 403.

---

## Collision Map

### HIGH RISK — netmsg.c / net.h
- **Conflicting workstreams:** Audio mod network sync (protocol v34), any future networking work, B-72 (SVC_LOBBY_STATE)
- **Collision type:** Protocol version coupling + message format changes
- **Mitigation:** NET_PROTOCOL_VER is now 34. Any session adding new network messages MUST bump to v35 and coordinate. Serialize all netmsg.c changes.

### MEDIUM RISK — actionmap.cpp
- **Conflicting workstreams:** Input system (S218/S221), any future binding changes
- **Collision type:** Edit conflict on default binding tables
- **Mitigation:** Input system is considered stable. No planned changes. If new bindings needed, read S218/S221 session logs first.

### MEDIUM RISK — pdgui_menu_mainmenu.cpp
- **Conflicting workstreams:** D5 Phase 4 (themes), any new top-level menu items, settings changes
- **Collision type:** This file is very large (3000+ lines) and growing. Multiple features add tabs/views here.
- **Mitigation:** New features should create their own `pdgui_menu_*.cpp` files and only add minimal routing in mainmenu.

### MEDIUM RISK — pdgui_charpreview.c / gfx_pc.cpp
- **Conflicting workstreams:** Any model preview changes, rendering pipeline work
- **Collision type:** Character preview is shared infrastructure (agent select, room lobby, skin editor, bot setup)
- **Mitigation:** Static Vp and loading fix are recent (S219-S220). Allow playtest stabilization before touching.

### LOW RISK — CMakeLists.txt
- **Conflicting workstreams:** New source files from any feature work
- **Collision type:** `file(GLOB_RECURSE)` auto-discovers new .c/.cpp files, so adding files doesn't require CMake edits. Only structural changes (new targets, new link libraries) would conflict.
- **Mitigation:** Low risk due to GLOB_RECURSE. Only coordinate if adding new build targets.

### LOW RISK — context/ files
- **Conflicting workstreams:** Multiple sessions updating task status, session log
- **Collision type:** Merge conflicts on markdown files
- **Mitigation:** Only one session should update context files at a time. Use append-only pattern for session-log.md.

---

## Recommended Session Plan

### Safe for Parallel Execution
These touch completely different file sets:

1. **D5 Phase 4 — Theme System** (remaining menu work): Touches `pdgui_menu_theme_editor.cpp`, theme loader files. Isolated from other workstreams.
2. **B-97 — Special Assignments separation**: Touches `pdgui_menu_solomission.cpp` only. No overlap with current work.
3. **B-60 — Stray 'g'+'s' in Settings**: Touches `pdgui_menu_mainmenu.cpp` — minor fix, do in isolation from other mainmenu work.

### Must Serialize
These touch overlapping resources:

1. **Any networking work** (B-72, future MP features) → serialize with audio mod sync work. Protocol v34 is current.
2. **Any mainmenu.cpp changes** → serialize all tasks that add/modify top-level menu items or settings tabs.

### Specific Execution Order Required
1. **Fix corrupt git index** → MUST happen before any code work
2. **Prune worktrees** → after index fix, before new worktree creation
3. **Mike's verification playtest** → before marking any S208/S218/S221 fixes as fully resolved
4. **Build verification** → after index fix, confirm clean build before starting new features

### Should NOT Be Delegated Without Human Review
1. **B-126 / B-112 investigation** — Deep crash debugging, root cause unknown. Requires instrumented playtest data.
2. **D13 update system** — Requires live GitHub API testing with real rate limits.
3. **Any protocol version bump** — Affects all connected clients. Mike should approve.

---

## Critical Build/Project Notes

Every code session MUST follow these rules:

1. **Use ONLY Edit tool on existing files, NEVER Write** — Write tool has caused file truncation in the past (see S190 `_dev-window.ps1` incident). Use Edit for surgical changes.
2. **Build command:** `TEMP=/tmp` with MSYS2 shell — `C:/msys64/usr/bin/bash.exe -lc "cd /c/Users/mikeh/Perfect-Dark-2/perfect_dark-mike && TEMP=/tmp cmake --build build/client"` (and `build/server`)
3. **types.h defines `#define bool s32`** — Use `s32`, not `_Bool`, in legacy game code. Modern port code (C++) can use standard `bool`.
4. **NET_PROTOCOL_VER is currently 34** — Coordinate any bumps. Last bump was for audio mod network sync.
5. **Do NOT use worktrees for new work** — Standing instruction in CLAUDE.md says to merge worktree changes safely. Given the 5 prunable worktrees and corrupt index, prefer working directly on `dev` for now until stability is confirmed.
6. **Run headless build check before reporting any task as complete.**
7. **EVERY code session must merge its changes into the main working copy, build-verify BOTH client and server, and confirm the merge is clean before reporting done.** No handing off unmerged work.
8. **Context files are code** — Update tasks-current.md, bugs.md, session-log.md immediately when changes are made. Don't defer.

---

## Open Questions / Blockers

1. **BLOCKER: Corrupt git index** — `git status` and `git diff` both fail with "unknown index entry format." Must be repaired before any new work. Likely fix: `git read-tree HEAD` or `git reset`.

2. **Worktree `quirky-kare` at `6ef0667f`** — This commit is not visible in the dev branch log. Needs investigation: does it contain unmerged work? What was its purpose?

3. **8 unpushed commits on dev** — Should Mike push to origin? This includes build auto-commits and merge commits for audio mod sync and charpreview fixes.

4. **Verification playtest outstanding** — 10 items from tasks-current.md awaiting Mike's in-game confirmation. Several bugs (B-128 sky tearing, B-18 pink sky, input bindings) are marked "FIXED, awaiting confirmation."

5. **B-126 / B-112 crash investigations stalled** — Both have instrumentation deployed but no new crash data since S191. Need Mike to reproduce with instrumented build.

6. **D5 Phase 3 task status vs remaining screens** — tasks-current.md says "COMPLETE 2026-04-11" and roadmap says "61/120 screens still need ImGui ports." The discrepancy may reflect that the task list was updated but roadmap was not. Needs reconciliation.

7. **Build version** — Last auto-commit says v0.0.90. Session log references v0.0.83 playtest. Current actual version unclear.

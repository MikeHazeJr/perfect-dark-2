# Session Log (Active)

> Recent sessions only. Archives: [1–6](sessions-01-06.md) · [7–13](sessions-07-13.md) · [14–21](sessions-14-21.md)
> Back to [index](README.md)

---

## Session 26 — 2026-03-22

**Focus**: Build test triage (S22–S24 cumulative), root cause analysis, new feature requests

### Build Test Results
- **CI corruption (S24)**: PASS — CI clean at boot and after MP return
- **Stage decoupling Phase 1 (S23)**: PASS — Kakariko loads, spawning works
- **Pause menu (S22)**: PARTIAL — Tab opens it, but START double-fires (B-14), Back on controller noop (B-16), OG Paused text bleeds through (B-15)
- **Match end**: PASS — Both normal end and pause→End Game work without ACCESS_VIOLATION. B-10 likely resolved via ImGui path. OG endscreen still shows (broken but escapable).
- **Not tested**: Look inversion, updater diagnostics, verbose persistence

### New Bugs Diagnosed
- **B-12**: 24-bot cap. `MAX_BOTS=24` + u32 chrslots bitmask. Need u64 + dynamic limit.
- **B-13**: GE prop scaling ~10x on mod stages. `model.c` renders with `model->scale` only, ignoring `model->definition->scale`. `modelGetEffectiveScale()` exists for collision but not rendering.
- **B-14**: START on controller opens/closes pause immediately (input passthrough)
- **B-15**: OG 'Paused' text renders behind ImGui menu (cosmetic, will be stripped)
- **B-16**: Back on controller does nothing in ImGui pause menu

### UX Feedback
- End Game confirm/cancel: too small, should be overlay dialog, B cancels to pause
- Settings: B should back one level, not exit to main menu
- General: prefer docked buttons over scroll-hidden, minimize scrolling
- Update tab: can browse versions but can't apply one

### New Feature Requests
- **Starting Weapon Option**: Toggle + specific weapon or random-from-pool. Match setup field.
- **Spawn Scatter**: Distribute across map pads facing away from nearest wall (not circle spawn).
- **Dynamic Bot Limit**: Default 32, cheat-expandable to arbitrary. Director directive.

### Decisions
- Bot limit architecture: fully dynamic, not just bumped from 24→32. Default 32, cheat-unlockable beyond.
- B-10 status: "likely resolved" — new ImGui path bypasses crash. Full resolution when Custom Post-Game Menu replaces OG endscreen.
- Priority reordered: B-12 (bot cap) and B-13 (prop scale) jump to top of queue.

### Code Written (This Session)

**B-13 Fix** (model.c):
- Lines 857–858, 883–884: Changed `model->scale` to `modelGetEffectiveScale(model)` in both rendering paths.
- Uses existing `modelGetEffectiveScale()` which correctly multiplies `definition->scale * model->scale`.
- Propagation check: ~50+ other `model->scale` usages analyzed. Other paths are physics/supplementary transforms — not the same bug class (would double-apply if changed).

**B-12 Phase 1** — Dynamic Participant System:
- **New files**: `src/include/game/mplayer/participant.h`, `src/game/mplayer/participant.c`
- Pool lifecycle: `mpParticipantPoolInit()`, `mpParticipantPoolFree()`, `mpParticipantPoolResize()`
- Slot management: `mpAddParticipant()`, `mpRemoveParticipant()`, `mpRemoveClientParticipants()`, `mpClearAllParticipants()`
- Queries: `mpIsParticipantActive()`, `mpGetActiveBotCount()`, `mpGetActivePlayerCount()`, etc.
- Iteration: `mpParticipantFirst()`/`Next()`, `mpParticipantFirstOfType()`/`NextOfType()`
- Legacy compat: `mpParticipantsToLegacyChrslots()`, `mpParticipantsFromLegacyChrslots()`
- **Parallel writes in mplayer.c**: Pool init in `mpInit()`, sync after chrslots changes in `mpStartMatch()`, `mpCreateBotFromProfile()`, `mpCopySimulant()`, `mpRemoveSimulant()`, challenge config load, save file load.

**Build Tool** (build-gui.ps1 → v3.2):
- GIT section in sidebar with "Commit XX changes" button
- Dynamic change count (refreshes every 5s via gameTimer)
- Click opens dialog: pre-filled commit message, "Push to GitHub" checkbox (default on)
- Stages all → commits → pushes → refreshes count

### Architecture Doc
- `context/b12-participant-system.md` — Full design for dynamic participant system

### Build Test Results (Mid-Session)
- **Commit button**: PASS (after race condition fix, `--set-upstream` fix, double-v prefix fix)
- **`<stdbool.h>` conflict**: Build failed — `participant.h` included `<stdbool.h>` which redefined `bool` to `_Bool`. Project uses `#define bool s32` in `types.h`. Fixed by replacing `<stdbool.h>` + `constants.h` with `types.h`. New constraint added.
- **B-13 prop scale**: Some mod stages show fixed scale, but wrong maps are loading (B-17). Need to verify on correct maps.
- **B-12 Phase 1**: No observable regression (expected — parallel system, no behavior change)
- **Mod stages (B-17)**: Wrong maps loading for bonus stages. Kakariko selection loads different map. 4 entries at end of list with garbled names. Skedar Ruins has wrong textures. Needs deeper diagnosis.

### Additional Code Written (Continuation)
**B-14 Fix** (pdgui_menu_pausemenu.cpp):
- Root cause: bondmove→ingame.c opens pause, then ImGui render sees same GamepadStart press and closes it — same frame.
- Added `s_PauseJustOpened` frame guard: set on open, checked+cleared in render. Skips close checks on open frame.
- Added `pauseActive` to `pdguiProcessEvent` input consumption (pdgui_backend.cpp) so keyboard events are consumed when pause is open.

**B-16 Fix** (pdgui_menu_pausemenu.cpp):
- Root cause: `ImGuiKey_GamepadFaceRight` (B button) was never handled.
- Added B button handling: if End Game confirm is showing → cancel; otherwise → close pause menu.

**Build Tool v3.3** (build-gui.ps1):
- Commit dialog expanded (520×380) with read-only "Changes" detail area
- Shows categorized summary: modified/added/deleted files grouped by area (Game, Port, Headers, Context, Build Tool, etc.)

### Constraint Discovered
- `bool` is `#define bool s32` in types.h/data.h. Never include `<stdbool.h>` in game code. Added to constraints.md.

### Next Steps
- Build and test: B-14 + B-16 controller fixes + commit dialog details
- Deeper diagnosis of B-17 (mod stage loading — may be pre-existing stage table issue)
- B-12 Phase 2: Migrate chrslots callsites to participant API
- B-12 Phase 3: Remove chrslots entirely
- Then resume stage decoupling Phase 2

---

## Session 25 — 2026-03-22

**Focus**: Context system reorganization

### What Was Done

Full restructure of the context catalog from 2 monolithic files (tasks.md 51KB, session-log.md 72KB) into a modular, linked encyclopedia:

- **README.md** rewritten as live index — session summaries grouped with links, domain file map, staleness audit
- **Session log split** into 3 archives (1–6, 7–13, 14–21) + slim active log (22–25)
- **Task tracker split**: [tasks-current.md](tasks-current.md) (active punch list) + [tasks-archive.md](tasks-archive.md) (completed work)
- **Bug tracking split**: [bugs.md](bugs.md) (one-off issues) + [systemic-bugs.md](systemic-bugs.md) (architectural pattern catalog, replaces bug-patterns.md)
- **[infrastructure.md](infrastructure.md)** created — phase execution tracker (D1–D16 + D-MEM status)
- **Stale file audit**: rendering-trace.md partially stale (header claims no ImGui menus), menu-storyboard.md partially superseded

### Decisions
- roadmap.md stays as long-term vision; infrastructure.md tracks execution
- One-off bugs and systemic patterns tracked separately
- Session archive boundary: 22+ active, 1–21 archived in 3 groups
- tasks-current.md kept razor-thin — only actionable next items + blockers

### Files Created
- `tasks-current.md`, `tasks-archive.md`, `bugs.md`, `systemic-bugs.md`, `infrastructure.md`
- `sessions-01-06.md`, `sessions-07-13.md`, `sessions-14-21.md`

### Files Rewritten
- `README.md` (live index), `session-log.md` (slim active log)

### Files Deprecated
- `bug-patterns.md` → content migrated to `systemic-bugs.md`
- Old `tasks.md` → split into `tasks-current.md` + `tasks-archive.md`

---

## Session 24 — 2026-03-22

**Focus**: Fix Carrington Institute corruption with mods enabled; fix bundled mod ID mismatch

### Root Cause: CI Overlay Corruption

When STAGE_CITRAINING loads as main menu background, `romdataFileLoad` resolves every file through `modmgrResolvePath`, iterating all enabled mods. GEX mod provides 158 replacement files including CI-specific props. `g_NotLoadMod` was BSS zero-init (false), so boot CI load had no protection.

### Changes
1. **mainmenu.c** — `g_NotLoadMod = true` (was BSS zero-init false)
2. **server_stubs.c** — `g_NotLoadMod = 1` (server parity)
3. **lv.c** — `lvReset()` sets `g_NotLoadMod = true` for non-gameplay stages
4. **modmgr.c** — Fixed bundled mod IDs: `"darknoon"` → `"dark_noon"`, `"goldfinger64"` → `"goldfinger_64"`

### Next Steps
- Build and verify CI looks normal at boot and after returning from MP

---

## Session 23 — 2026-03-22

**Focus**: Dynamic stage decoupling — Phase 1 safety net for mod stage index collisions

### Root Cause: Paradox Crash
`g_StageSetup.intro` pointer for Paradox (stagenum 0x5e, stageindex 85) lands into props data section. Intro cmd loop reads garbage → crashes before safety guard fires.

### Phase 1 Changes (Safety Net — All DONE)
| File | Change |
|------|--------|
| setup.c | Validate `g_StageSetup.intro` after relocation: proximity + cmd type range check |
| playerreset.c | Bounds-check `g_SpawnPoints[24]`, rooms[] init with -1 sentinels, pad-0 fallback spawn |
| player.c | NULL check on cmd before intro loop, `playerChooseSpawnLocation` divide-by-zero guard |
| scenarios.c | Iteration limit on intro loop in `scenarioReset()` |
| endscreen.c | Bounds-check `stageindex < NUM_SOLOSTAGES` before besttimes writes |
| training.c | Bounds-check in `ciIsStageComplete()` |
| mainmenu.c | Bounds-check in `soloMenuTextBestTime()` |

### Additional Fixes
- **Mod manager path**: `modmgrScanDirectory()` now tries CWD, exe dir, base dir
- **Stage range check**: Widened from `> 0x50` to `> 0xFF` in `modConfigParseStage()`

### Next Steps
- Phase 2: Dynamic stage table (heap-allocated g_Stages, g_NumStages)
- Phase 3: Index domain separation (soloStageGetIndex() lookup)

---

## Session 22 — 2026-03-22

**Focus**: New feature batch — combat sim pause menu, scorecard overlay, Paradox crash investigation, look inversion, updater diagnostics

### What Was Done
1. **Combat Sim ImGui Pause Menu** — `pdgui_menu_pausemenu.cpp` (~650 LOC). Tabs: Rankings, Settings, End Game. Replaces legacy for combat sim only.
2. **Hold-to-Show Scorecard Overlay** — Tab/GamepadBack hold, semi-transparent, sorted by score.
3. **Paradox crash** — Diagnostic logging added (Session 22), root-caused and fixed (Session 23).
4. **Look inversion** — Checkbox in ImGui controls settings. Uses `optionsGetForwardPitch()`/`Set`.
5. **Updater diagnostics** — Tag prefix mismatch + version parse failure logging in parseRelease().
6. **Updater pipeline aligned** — Dual-tag system (client-v/server-v), two channels (Stable/Dev).
7. **Bundled mod ID mismatch found** (fixed in Session 24).

Full file manifests in [tasks-archive.md](tasks-archive.md).

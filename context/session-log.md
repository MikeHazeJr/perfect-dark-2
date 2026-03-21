# Session Log

Reverse-chronological. Each entry is a self-contained summary of what happened.

---

## Session 8 — 2026-03-21 (late evening)

**Focus**: Updates tab wiring, download button UX, spawn pads, branch consolidation

### What Was Done

1. **Wired "Updates" tab into Settings menu** (`pdgui_menu_mainmenu.cpp`)
   - Added 5th tab "Updates" (index 4) to `renderSettingsView()`
   - Added `selFlag4` for bumper-driven tab selection
   - Updated bumper wrap range from 0-3 to 0-4
   - Updated `s_SettingsSubTab` comment to document all 5 tabs
   - Added `extern "C"` forward declaration for `pdguiUpdateRenderSettingsTab()`

2. **Refactored update UI for inline Settings rendering** (`pdgui_menu_update.cpp`)
   - Extracted `renderVersionPickerContent(tableH)` from `renderVersionPicker()`
   - `renderVersionPicker()` now wraps content in floating window (for notification banner)
   - New `pdguiUpdateRenderSettingsTab()` renders content inline in Settings tab
   - Auto-triggers version check on first tab view (one-shot `s_TabCheckTriggered`)
   - Avoids double-render: Settings tab renders inline, floating picker only from banner

3. **Fixed download button UX bugs** (`pdgui_menu_update.cpp`)
   - Added `s_DownloadFailed` state flag — set on `UPDATER_DOWNLOAD_FAILED`
   - Download failure now shown as red text with error message from `updaterGetError()`
   - Button label changes to "Retry Download" after failure, resets on click
   - Added null check on `updaterGetCurrentVersion()` — shows "(unknown)" if null
   - Added null guard on `cur` in `versionCompare` calls (isCurrent, rollback check)

4. **Expanded spawn pad arrays** (`src/game/player.c`)
   - Changed `verybadpads[24]`, `badpads[24]`, `padsqdists[24]` to `[MAX_MPCHRS]` (32)
   - Updated comment: removed `@dangerous` warning, documented MAX_MPCHRS sizing
   - Prevents overflow on custom maps with >24 spawn points

### Files Modified
- `port/fast3d/pdgui_menu_mainmenu.cpp` — 5th Settings tab (Updates), bumper wrap, selFlag4
- `port/fast3d/pdgui_menu_update.cpp` — content extraction refactor, inline tab, download failure UX, null safety
- `src/game/player.c` — spawn pad arrays 24→MAX_MPCHRS (32)

5. **Consolidated to single-branch workflow**
   - Merged `release` branch into `dev` (resolved all conflicts in favor of dev)
   - Removed branch switcher dropdown from `build-gui.ps1`
   - Replaced with read-only "Channel" indicator (derived from version: dev > 0 = Dev, else = Stable)
   - Removed `Switch-Branch`, `Refresh-BranchList` functions entirely
   - Push Release now uses `git branch --show-current` instead of dropdown selection
   - Root cause of yesterday's entire bug cascade: switching to `release` branch built stale code missing all UI work

### Files Modified
- `port/fast3d/pdgui_menu_mainmenu.cpp` — 5th Settings tab (Updates), bumper wrap, selFlag4
- `port/fast3d/pdgui_menu_update.cpp` — content extraction refactor, inline tab, download failure UX, null safety
- `src/game/player.c` — spawn pad arrays 24→MAX_MPCHRS (32)
- `build-gui.ps1` — removed branch switcher, added channel indicator derived from version

### Decisions
- **Single branch forever**: no dev/release split, channel is a version flag
- Update content renders inline in Settings tab (not as floating dialog)
- Floating version picker kept for notification banner "View updates" flow
- `pdguiUpdateRenderSettingsTab()` is the canonical Settings entry point
- Version field `dev > 0` = prerelease (Dev channel), `dev = 0` = Stable channel

---

## Session 7 — 2026-03-21 (evening)

**Focus**: Branch reconciliation, multi-bug triage, full system audit

### Problem Statement
Mike reported 5 issues after building from `release` branch for buddy testing:
1. Update menu tab missing from Settings
2. Tons of libcurl DLLs required (should be static)
3. Server crashed when joining
4. Can't add bots in local lobby
5. Can't reach online lobby

### Root Cause Analysis

**Root Cause 1: Branch Divergence** — `dev` branch had working UI (Update tab,
menu fixes, matchsetup improvements) that was never merged into `release`. The
`release` branch got backend changes (strncpy, 32-slot expansion) that `dev` didn't
have. Result: release was missing half the UI work.

**Root Cause 2: MATCH_MAX_SLOTS Struct Mismatch** — The 32-slot expansion
(Session 6) changed `matchsetup.c` (C) to `MATCH_MAX_SLOTS = MAX_MPCHRS = 32`
but `pdgui_menu_matchsetup.cpp` (C++) still had `MATCH_MAX_SLOTS = 16`. Since
both files define `struct matchconfig` with `slots[MATCH_MAX_SLOTS]`, all fields
after `slots[]` were at wrong offsets in C++. This caused: bot addition failure
(numSlots read as garbage), corrupted match data flowing to server, server crash
on join, and client unable to reach lobby (malformed SVC_STAGE_START).

**Root Cause 3: Static libcurl OpenGL Crash** — Static linking of curl + OpenSSL +
30 transitive deps caused Windows GDI to init before the GPU's OpenGL ICD,
resulting in GL 1.1 fallback instead of full driver. Fix: switch to dynamic linking.

### What Was Done

1. **Switched to dev branch** as working base (all UI working there)
2. **Cherry-picked strncpy fixes** (e0a8853 from release) → commit `dad0256`
3. **Cherry-picked 32-slot expansion** (8c6e47a from release) with critical fix:
   synced MATCH_MAX_SLOTS in C++ from 16 to MAX_MPCHRS (32) → commit `9722013`
4. **Switched libcurl to dynamic linking** — replaced static curl CMake block with
   simple dynamic link + post-build DLL copy (16 DLLs) + CURLSSLOPT_NATIVE_CA
   for Windows cert store → commit `9eb899e`
5. **Full system audit** (4 parallel audits):

   **Menu Audit** — ALL PASS. Controller nav, MKB, focus management, B/Escape,
   combo boxes, tab switching all working correctly across all 8 menu files.

   **Lobby/Network Audit** — ALL PASS. State transitions sound, leader election
   secure with non-leader rejection, bot sync correct up to 24, async tick
   scheduler sound, protocol v19 enforced at all entry points. No O(n²) loops.
   Estimated bandwidth: ~180 KB/s downstream with 32 characters (acceptable).

   **Spawn System Audit** — PASS with caveat. All arrays correctly sized for 32
   characters. chrslots bitmask correct (u32, bits 0-7 players, 8-31 bots).
   **One vulnerability**: `playerChooseSpawnLocation()` has `verybadpads[24]`,
   `badpads[24]`, `padsqdists[24]` — safe for all stock maps (max 21 pads)
   but will overflow on custom maps with >24 spawn points. Needs expanding.

   **Update Tab Audit** — CRITICAL FINDING: The "Updates" tab exists on `dev`
   branch's code. `pdguiUpdateShowPicker()` is defined but never called from
   the Settings menu. Need to wire it into Settings as a 5th tab.

### Commits on dev (this session)
- `dad0256`: strncpy null-termination (cherry-pick from release)
- `9722013`: 32-slot expansion + MATCH_MAX_SLOTS C/C++ sync fix
- `9eb899e`: Dynamic libcurl + DLL copy + SSL CA cert fix

### Still In Progress
- **Add "Updates" tab to Settings** — need to add 5th tab to renderSettingsView()
  that calls pdguiUpdateShowPicker(), update bumper wrap from 3→4, add selFlag4
- **Fix Update download button bugs** — download failure not displayed, button
  clickable after failure but does nothing, missing null check on current version
- **Expand spawn pad arrays** from 24 to MAX_MPCHRS (32) in player.c

### Decisions
- Dev branch is now the primary development branch (release was a dead end)
- Dynamic curl is the permanent approach (static caused OpenGL init race)
- MATCH_MAX_SLOTS must always be defined via MAX_MPCHRS, never hardcoded

---

## Session 6 — 2026-03-21

**Focus**: strncpy null-termination audit, ADR-001, build tool commit

### What Was Done
- Re-applied netmsg.c fixes lost during context compaction (3 strncpy + 1 strcpy→snprintf)
- **Propagation check**: Scanned entire port/ codebase for the same strncpy class bug
  - Found 17 additional instances across 8 files (net.c, updater.c, modmgr.c, fs.c, libultra.c, config.c, input.c, optionsmenu.c)
  - Fixed all 17 with consistent `buf[SIZE - 1] = '\0'` pattern
- Created ADR-001 documenting the architecture audit findings
- Previous session's build tool changes committed: static linking fix, headless server, auto-stash, version increment buttons, release overwrite

### Files Modified
- `port/src/net/netmsg.c` — 3 strncpy + 1 strcpy fix
- `port/src/net/net.c` — 4 strncpy fixes
- `port/src/updater.c` — 2 strncpy fixes
- `port/src/modmgr.c` — 4 strncpy fixes
- `port/src/fs.c` — 5 strncpy fixes
- `port/src/libultra.c` — 3 strncpy fixes
- `port/src/config.c` — 3 strncpy fixes
- `port/src/input.c` — 1 strncpy fix
- `port/src/optionsmenu.c` — 2 strncpy fixes
- `context/ADR-001-lobby-multiplayer-architecture-audit.md` — NEW

### Decisions
- strncpy null-termination is now a project-wide standard; future code should prefer snprintf

---

## Sessions 4-5 — 2026-03-20

**Focus**: Architecture audit, build tool improvements, release tooling

### What Was Done
- Complete architecture audit of lobby/multiplayer systems
- Fixed CLC_MAP_VOTE_START ID collision (was 0x09, same as CLC_LOBBY_MODE → shifted to 0x0A)
- CMakeLists.txt: Fixed static linking path resolution (SDL2, zlib, libcurl)
- build-gui.ps1: Server headless launch, auto-stash branch switching, version increment buttons
- release.ps1: Release overwrite support, DLL warning cleanup
- Identified and documented 3 verified false positives in the audit

### Decisions
- Server launches headless by default (avoids OpenGL context contention)
- Auto-stash on branch switch with tagged restore
- Release overwrite: delete existing GitHub release + tags before recreating

---

## Session 3 — 2026-03-19

**Focus**: Phase 3 dedicated server, lobby system

### What Was Done
- Completed Phase 3: Dedicated-server-only multiplayer model
- New multiplayer menu (server browser, direct IP connect)
- Lobby system rewrite with leader election
- Lobby screen with game mode selection
- Server GUI (4-panel layout) and headless mode
- CLC_LOBBY_START protocol for match launching
- Cleanup: renamed "Network Game" to "Multiplayer", removed stale host menus

---

## Session 2 — 2026-03-18

**Focus**: ImGui debug menu, styling, build system

### What Was Done
- PD-authentic styling with pixel-accurate shimmer from menugfx.c
- 7 built-in palettes including Black & Gold
- F12 debug menu with mouse capture/release
- Build tool: colored progress bar, gated run buttons, process monitoring
- Font size 16pt → 24pt

---

## Session 1 — 2026-03-17

**Focus**: Menu system Phase 2 (agent create, delete, typed dialogs, network audit)

### What Was Done
- Agent Create screen with 3D character preview (FBO)
- Agent Select enhancements (contextual actions, delete confirmation)
- Typed dialog system (DANGER + SUCCESS)
- Network audit began (ENet protocol, message catalog)
- Skedar/DrCaroll mesh fix (duplicate array indices in g_MpBodies)

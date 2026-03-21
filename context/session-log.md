# Session Log

Reverse-chronological. Each entry is a self-contained summary of what happened.

---

## Session 13 — 2026-03-21

**Focus**: Fix client crash — model loading at boot before subsystems ready

### What Was Done

1. **Identified true root cause** — Rebuild confirmed: files DO exist in ROM (`romdataFileGetData` returns non-NULL). The real problem: `catalogValidateAll()` ran at `pdmain.c:309`, BEFORE `texInit()` (line 319), `langInit()` (line 320), and other subsystems. Model loading via `modelPromoteOffsetsToPointers` / `modeldef0f1a7560` touches texture and skeleton systems that aren't initialized at that point → ACCESS_VIOLATION on ALL 151 models. 151 VEH/longjmp cycles corrupted heap state; game died silently after "End Validation".

2. **Removed `catalogValidateAll()` from boot (pdmain.c)** — The original port never bulk-validated models at boot. Replaced the call with an explanatory comment. Models will be validated lazily via `catalogGetSafeBody()`/`catalogGetSafeHead()` → `catalogValidateOne()` when first accessed during gameplay, by which point all subsystems are initialized.

3. **Kept defensive improvements from earlier in session** — These are still valuable for the lazy validation path:
   - `file.c` (`fileLoadToNew`): pre-check `romdataFileGetData` — returns NULL for non-existent files
   - `modeldef.c` (`modeldefLoad`): clears `g_LoadType` on NULL early-return
   - `modelcatalog.c` (`catalogValidateOne`): pre-check `romdataFileGetData` + `#include "romdata.h"`

4. **Confirmed ROM is valid** — `romdataInit: loaded rom, size = 33554432` (32MB NTSC-Final). All segments loaded successfully. No ROM corruption.

### Key Insight
Boot-time model validation was doomed by init ordering. `catalogValidateAll()` sat between `mempSetHeap()` (line 304) and the subsystem init cascade (`texInit` at 319, `langInit` at 320, etc.). Models depend on those subsystems — they can only be loaded after the full init sequence completes. The correct architecture is lazy/on-demand validation during gameplay.

### Files Changed
- `port/src/pdmain.c` — Removed `catalogValidateAll()` call, replaced with comment
- `src/game/file.c` — `fileLoadToNew`: pre-check for non-existent ROM files
- `src/game/modeldef.c` — `modeldefLoad`: clear `g_LoadType` on NULL return
- `port/src/modelcatalog.c` — `catalogValidateOne`: pre-check + romdata.h include

### Next Steps
- Rebuild and test — game should boot past init to title screen
- Verify server launch from build tool works (confirmed working in log)

---

## Session 12 — 2026-03-21

**Focus**: Client crash-on-launch diagnosis and fix; server log filename bug

### What Was Done

1. **Diagnosed client crash-on-launch** — Client opened and immediately closed after build. Root cause: `catalogValidateAll()` was called in `main.c:198` BEFORE `mempSetHeap()` (called later in `pdmain.c:303` inside `mainInit()`). Every `modeldefLoadToNew()` call triggered `mempAlloc()` on uninitialized pool state, causing 151 consecutive access violations caught by VEH. Memp internals corrupted by longjmp, program crashed silently in `mainProc()`.

2. **Fixed init ordering** — Moved `catalogValidateAll()` from `main.c` to `pdmain.c` after `mempSetHeap()`. Added `#include "modelcatalog.h"` to pdmain.c. Left explanatory comment at the old call site.

3. **Added defensive guard** — `catalogValidateAll()` now checks `mempGetStageFree() == 0` before attempting model loads. If the pool system isn't ready, it logs an error and returns safely instead of crashing.

4. **Fixed server log filename** — `PerfectDarkServer.exe` was writing to `pd-client.log` instead of `pd-server.log`. Root cause: `sysInit()` checked `sysArgCheck("--dedicated")` (CLI args) but the server sets `g_NetDedicated = 1` directly without passing `--dedicated` in argv. Fix: check both `g_NetDedicated` variable AND `sysArgCheck("--dedicated")`.

5. **Boot path audit** — Scanned all init functions called before `mempSetHeap()` (romdataInit, gameInit, modmgrInit, catalogInit, filesInit) — confirmed none call `mempAlloc`. catalogValidateAll was the only offender.

### Files Modified
- `port/src/main.c` — Removed `catalogValidateAll()` call, added comment
- `port/src/pdmain.c` — Added `catalogValidateAll()` after `mempSetHeap()`, added include
- `port/src/modelcatalog.c` — Added `mempGetStageFree()` guard, added `lib/memp.h` include
- `port/src/system.c` — Log path selection now checks `g_NetDedicated`/`g_NetHostLatch` variables

### Key Takeaway
When adding new init-time systems (like the model catalog), always verify the full dependency chain. The "heap is allocated" != "pool allocator is initialized". The raw memory (`sysMemZeroAlloc`) must be passed through `mempSetHeap()` before `mempAlloc()` can use it.

---

## Session 11 — 2026-03-21

**Focus**: Log channel filter system, always-on logging, debug menu wiring, build tool polish, release cleanup

### What Was Done

1. **Always-on logging** (`port/src/system.c`)
   - Removed `sysArgCheck("--log")` gate — logging is now unconditional in `sysInit()`
   - Log filename: `pd-server.log` (dedicated), `pd-host.log` (host), `pd-client.log` (client)
   - Removed `--log` from build-gui.ps1 launch args (no longer needed)

2. **Log channel filter system** (`port/include/system.h`, `port/src/system.c`)
   - 8 channels: Network, Game, Combat, Audio, Menu, Save, Mods, System
   - Bitmask constants (LOG_CH_NETWORK through LOG_CH_SYSTEM) + LOG_CH_ALL/LOG_CH_NONE presets
   - `sysLogClassifyMessage()` — maps ~30 known string prefixes to channel bitmask
   - Filter logic in `sysLogPrintf()`: warnings/errors always pass, LOG_NOTE filtered by channel, untagged messages always pass
   - `sysLogSetChannelMask()` / `sysLogGetChannelMask()` with change logging
   - Zero changes to existing 470+ log call sites (prefix-based classification)

3. **ImGui debug menu log section** (`port/fast3d/pdgui_debugmenu.cpp`)
   - `pdguiDebugLogSection()` — All/None preset buttons, per-channel checkboxes, hex mask readout
   - Wired into `pdguiDebugMenuRender()` after Theme section

4. **Build tool version layout fix** (`build-gui.ps1`)
   - Moved increment/decrement buttons from beside version fields to below them
   - Layout now: `[0] . [0] . [0]` row, then `[-][+] [-][+] [-][+]` row beneath
   - All downstream section Y-positions shifted to accommodate

5. **Latest released version display** (`build-gui.ps1`)
   - VERSION section shows `dev: X.Y.Z` and `stable: X.Y.Z` from GitHub Releases
   - Added `Prerelease` field to release cache system
   - Labels refresh on startup (disk cache) and after each API fetch
   - `*` suffix when showing cached (offline) data

6. **Window height reduction** (`build-gui.ps1`)
   - Form 880x680 → 880x560, sidebar trimmed to fit content
   - Console, utility bar, progress bar repositioned to sit just below sidebar

7. **Release script cleanup** (`release.ps1`)
   - Added step 6/6: local backup + cleanup after push
   - Stable releases: zip backed up to `backups/` before cleanup
   - Dev releases: no local backup, GitHub is source of truth
   - Staging directories auto-cleaned after push + orphans cleaned
   - Added `backups/` to .gitignore

8. **Version reset** — CMakeLists.txt VERSION_SEM_PATCH 4→1

9. **CMake icon.rc fix** (`CMakeLists.txt`)
   - Both `add_executable` blocks referenced `dist/windows/icon.rc` which didn't exist
   - Guarded both with `if(WIN32 AND EXISTS ...)` to prevent configure failures
   - Affects both client (line ~419) and server (line ~482) targets

10. **CMake error log surfacing** (`build-gui.ps1`)
    - On configure failure, reads last 40 lines of `CMakeFiles/CMakeError.log` and `CMakeFiles/CMakeConfigureLog.yaml` (or `CMakeOutput.log`)
    - Prints with color classification (errors red, warnings orange, info white)
    - Helps diagnose build failures without leaving the build tool

11. **LOG_VERBOSE level** (`port/include/system.h`, `port/src/system.c`, `port/fast3d/pdgui_debugmenu.cpp`)
    - New `LOG_VERBOSE` enum value below `LOG_NOTE` — trace-level detail, off by default
    - `sysLogGetVerbose()` / `sysLogSetVerbose()` API
    - Dropped early in `sysLogPrintf()` unless `--verbose` CLI flag or debug menu toggle
    - Subject to channel filtering same as LOG_NOTE
    - Debug menu: Verbose checkbox + mask readout shows `+V` when enabled
    - Stdout routing: verbose goes to stdout (like NOTE/CHAT), not stderr

12. **Font size increase** (`build-gui.ps1`)
    - All UI font sizes bumped: form 9→10, title 14→16, section headers 8→9, buttons 9→10, small labels 7→8
    - Console output stays 9pt Consolas (monospace)
    - Error count 7→8, progress label 8→9

13. **Custom font — Handel Gothic Regular** (`build-gui.ps1`)
    - `PrivateFontCollection` loads `fonts/Menus/Handel Gothic Regular/Handel Gothic Regular.otf`
    - `New-UIFont` helper creates fonts from Handel Gothic (falls back to Segoe UI)
    - All Segoe UI instances replaced with `New-UIFont` calls (console Consolas kept)
    - Bold variant supported via `[switch]$Bold` parameter

14. **Build tool version label** (`build-gui.ps1`)
    - "Build tool version 3.1" in lower-left corner, very dim gray (70,70,70)
    - Version stored in `$script:BuildToolVersion` for easy bumping

15. **Settings restructure** (`build-gui.ps1`)
    - Moved Settings from File menu to new Edit menu (standard convention)
    - Settings dialog now uses TabControl with two tabs: General, Asset Extraction
    - General tab: GitHub auth, repo, sounds toggle (unchanged functionality)
    - Asset Extraction tab: ROM path field with Browse dialog, extraction tools section
    - ROM path persisted in `.build-settings.json`, auto-detected on startup
    - `Resolve-RomPath` helper: opens `OpenFileDialog` if ROM not found
    - Extraction tools: reusable `New-ExtractToolRow` creates button + status label rows
    - Sound Effects tool: functional (runs `extract-build-sounds.py`)
    - Models & Textures, Animations, Levels: placeholder rows (disabled until tools exist)
    - Each tool row auto-detects existing extracted files and shows count in green

### Files Modified
- `port/include/system.h` — LOG_CH_* defines, LOG_VERBOSE, verbose API, extern arrays
- `port/src/system.c` — Unconditional logging, filter state, classifier, filter logic, verbose state, --verbose flag
- `port/fast3d/pdgui_debugmenu.cpp` — Log section + render wiring + verbose checkbox
- `build-gui.ps1` — Layout, version labels, window resize, --log removal, CMake error surfacing, font system, version label, Settings restructure (Edit menu, tabs, ROM path, extraction tools)
- `release.ps1` — Post-push cleanup, stable backup to backups/
- `CMakeLists.txt` — Version patch reset to 1, icon.rc EXISTS guards
- `.gitignore` — Added backups/

---

## Session 10 — 2026-03-21 (continued)

**Focus**: Connect code system completion, ROM missing dialog, build tooling polish

### What Was Done

1. **Connect code system** (`port/src/connectcode.c`, `port/include/connectcode.h`)
   - Cleaned up stale `s_WordTable[256]` from first attempt, keeping only `s_Words[256]`
   - 256 unique PD-themed words: characters, locations, weapons, gadgets, vehicles, missions, multiplayer, sci-fi
   - Encode: IP (4 bytes) + port (2 bytes) → 6 words (e.g. "JOANNA FALCON CARRINGTON SKEDAR PHOENIX DATADYNE")
   - Decode: case-insensitive, supports space/hyphen/dot separators
   - Added to CMakeLists.txt server source list (client auto-discovers via GLOB_RECURSE)

2. **Server GUI connect code** (`port/fast3d/server_gui.cpp`)
   - Status bar now shows connect code in green when UPnP provides external IP
   - "Copy Code" button copies connect code to clipboard via SDL
   - Raw IP:port shown underneath in subdued gray for reference
   - Falls back to port-only display when UPnP inactive

3. **In-game server overlay connect code** (`port/fast3d/pdgui_lobby.cpp`)
   - Dedicated server overlay shows connect code + "Copy" button when public IP available
   - Same subdued raw IP display underneath

4. **Server console connect code** (`port/src/net/netupnp.c`)
   - UPnP success log now also prints the connect code for easy sharing from console

5. **Client join flow — connect code support** (`port/fast3d/pdgui_menu_network.cpp`, `port/src/net/netmenu.c`)
   - Direct Connect input now accepts either raw IP:port OR word-based connect codes
   - Auto-detects input type by checking for alpha characters
   - Connect code decoded to IP:port string, then passed to `netStartClient()` as usual
   - Both ImGui and native menu paths updated
   - Hint text added: "Enter IP:port or connect code"

6. **ROM missing dialog** (`port/src/romdata.c`)
   - Replaced bare `sysFatalError()` SDL message box with custom `SDL_ShowMessageBox`
   - Dialog clearly states: exact ROM filename (`pd.ntsc-final.z64`), data folder path, z64 format requirement
   - Two buttons: "Open Folder" (opens data dir in system file manager) and "Exit"
   - Cross-platform: `explorer` on Windows, `open` on macOS, `xdg-open` on Linux

### Files Modified
- `port/src/connectcode.c` — cleaned up stale array
- `CMakeLists.txt` — added connectcode.c to server source list
- `port/fast3d/server_gui.cpp` — connect code display + copy button in status bar
- `port/fast3d/pdgui_lobby.cpp` — connect code in dedicated server overlay
- `port/src/net/netupnp.c` — connect code in UPnP success log
- `port/fast3d/pdgui_menu_network.cpp` — connect code decode in ImGui join flow
- `port/src/net/netmenu.c` — connect code decode in native menu join flow
- `port/src/romdata.c` — ROM missing dialog with Open Folder button

### Decisions Made
- Connect code detection is simple alpha-char check — if input contains letters, treat as code; otherwise as raw IP
- ROM dialog uses SDL_ShowMessageBox (not ImGui) because ROM loading happens before ImGui is initialized
- Raw IP still shown in subdued color alongside connect code for debugging/advanced users

### Next Steps
- Build test all changes (Mike needs to compile)
- Verify connect code round-trip encoding/decoding works correctly
- Test ROM missing dialog on Windows
- Continue with master server / server browser if needed

---

## Session 9 — 2026-03-21 (continuation)

**Focus**: Version format simplification, build tool polish, exe renaming, release packaging, connect code design

(See previous session summary for details — this session covered extensive changes to
version format from 4-field to 3-field, dark menu theme, taskbar visibility, GitHub
release checking, offline cache support, executable renaming to PerfectDark.exe /
PerfectDarkServer.exe, release.ps1 restructuring, and began connect code implementation.)

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

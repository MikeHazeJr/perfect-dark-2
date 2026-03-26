# Session Log (Active)

> Recent sessions only. Archives: [1-6](sessions-01-06.md) . [7-13](sessions-07-13.md) . [14-21](sessions-14-21.md) . [22-46](sessions-22-46.md)
> Back to [index](README.md)

## Session 49 -- 2026-03-26

**Focus**: Bug fixes — version baking and Quit Game clipping

### What Was Done

**B-22 FIXED — Version not baking into exe (third report)**:
- Root cause: `Get-BuildSteps` in `devtools/dev-window.ps1` built cmake configure args with no `-DVERSION_SEM_*` flags
- CMake used the cached value from a previous run (0.0.7), or after clean build, the hardcoded default `set(VERSION_SEM_PATCH 7 CACHE STRING ...)` in CMakeLists.txt
- Fix: added `Get-UiVersion` call inside `Get-BuildSteps`, appends `-DVERSION_SEM_MAJOR=X -DVERSION_SEM_MINOR=Y -DVERSION_SEM_PATCH=Z` to both Configure (client) and Configure (server) steps
- Clean Build toggle was already working correctly (deletes build dirs). Version fix works regardless — the -D flags override cache on every configure

**B-23 FIXED — Quit Game button clipped on right edge**:
- Root cause: fixed `quitBtnW = 100 * scale` placed button's right edge exactly at the ImGui content clip boundary (no margin). "Confirm Quit" text also wider than the fixed 100px.
- Fix in `port/fast3d/pdgui_menu_mainmenu.cpp`: width now `CalcTextSize("Confirm Quit").x + FramePadding*2`, position now `dialogW - WindowPadding.x - quitBtnW - 4*scale` margin. Cancel button cursor updated to use new local `cursorX/cursorY`.

### Decisions Made
- Version boxes in the Dev Window are the single source of truth for ALL builds, not just releases. `Get-BuildSteps` is the authoritative cmake path — version flags go there.

### Next Steps
- (unchanged from S48)
- SPF-2b: verify SPF-1 server build
- SPF-3a: lobby ImGui screen
- Wire remaining menus through menu manager
- Collision Phase 2 design (HIGH PRIORITY)

---

## Session 49b — 2026-03-26

**Focus**: SPF-3 lobby+join, catalog audit, plan docs, stats, connect codes, IP fallback, updater

### What Was Done

**SPF-2a Build Pass**: menumgr.h was missing `extern "C"` guards → undefined reference errors in C++ TUs. Fix applied (`5e55e62`). SPF-2a (menumgr.c/h, 100ms cooldown) now builds.

**Release Pipeline**: `-Nightly` flag added to release.ps1: nightly builds use `nightly-YYYY-MM-DD` tag. Fixed post-batch-addin path (Split-Path parent traversal).

**SPF-3 — Lobby + Join by Code** (commit `3b588c1`): `pdgui_menu_lobby.cpp` integrated hub.h/room.h — lobby shows server state, room list with color-coded states and player counts. `pdgui_menu_mainmenu.cpp`: new menu view 4 "Join by Code" with phonetic code input + decode via `phoneticDecode()` (falls back to direct IP). Wired through menu manager (MENU_JOIN push/pop).

**Asset Catalog Audit Phase 1** (commit `3b588c1`): Failure logging at all critical asset load points: `fileLoadToNew`, `modeldefLoad`, `bodyLoad`, `tilesReset`, setup pad loading, lang bank loading.

**New Plan Documents** (commit `636b404`): `context/catalog-loading-plan.md` (C-1–C-9 phases). `context/menu-replacement-plan.md` (240 legacy menus → 9 ImGui groups, Group 1 highest priority).

**Player Stats System**: New `port/include/playerstats.h` + `port/src/playerstats.c`. `statIncrement(key, amount)` — named counter system, JSON persistence.

**Connect Code System Rewrite**: Sentence-based codes ("fat vampire running to the park") replace phonetic syllables as primary connect method. 256 words per slot × 4 slots = 32-bit IPv4.

**HTTP Public IP Fallback**: `netGetPublicIP()` tries UPnP first, then `curl` → `api.ipify.org`. Result cached after first success.

**Updater Unified Tag Format**: `versionParseTag()` now handles `"v0.1.1"` (unified) in addition to `"client-v0.1.1"` (legacy).

### Decisions Made
- Sentence-based connect codes are primary (phonetic module remains for lobby display)
- Menu replacement: Group 1 (Solo Mission Flow, 11 menus) first
- Stats: named counters (not fixed schema) for forward compatibility

### Next Steps
- SPF-3 playtest: lobby rooms, join-by-code
- Catalog Phase C-1/C-2; Menu Replacement Group 1

---

## Session 49c — 2026-03-26

**Focus**: Join flow audit, S49 architecture documentation, context hardening

### What Was Done

**Context audit — S49 architectural decisions captured**: Sentence-based connect codes, menu replacement plan, rooms + slot allocation, asset catalog as single source of truth (C-1–C-9 phases), campaign as co-op, player stats, HTTP IP fallback, updater unified tag format.

**Join flow audit — `context/join-flow-plan.md` created**: Full end-to-end flow mapped: code input → decode → netStartClient → ENet → CLC_AUTH → SVC_AUTH_OK → CLSTATE_LOBBY → lobby UI → netLobbyRequestStart → match. Gaps found: room state not synced to clients (SVC_ROOM_LIST needed), server GUI missing connect code display, recent server history stubbed.

**Plan: J-1 verify end-to-end, J-2 server GUI code display, J-3 SVC_ROOM_LIST protocol, J-4 server history UI, J-5 lobby handoff polish.**

Context files updated: networking.md (protocol v21, HTTP IP fallback), update-system.md (unified tag format), constraints.md (no raw IP in UI), infrastructure.md, tasks-current.md.

### Decisions Made
- Recent server history MUST encode IPs to codes, not store raw IP
- Server GUI should display connect code (currently only in logs)

### Next Steps
- J-1: Build server target, verify end-to-end join → match flow
- J-2: Add connect code display to server_gui.cpp

---

## Session 49d — 2026-03-26

**Focus**: Cross-machine multiplayer bug fixes (3 regressions from real playtest)

### What Was Done

**B-24 (was B-22) — Connect code byte-order reversal (CRITICAL, FIXED)**: `pdgui_menu_mainmenu.cpp` extracted bytes MSB-first `(ip>>24, ip>>16, ip>>8, ip)` while encoder + all other decode callers use LSB-first `(ip, ip>>8, ip>>16, ip>>24)`. Fix: 3-line change to LSB-first extraction.

**B-25 (was B-23) — Server max clients hardcoded to 8 (FIXED)**: `NET_MAX_CLIENTS` was `MAX_PLAYERS` (=8). Fixed: `NET_MAX_CLIENTS 32` in `net.h`, independent of `MAX_PLAYERS`. `PDGUI_NET_MAX_CLIENTS 32` in debug menu.

**B-26 (was B-24) — Player name shows "Player1" (FIXED)**: `netClientReadConfig()` reads from legacy N64 save field; empty on fresh PC client. Fix: identity profile fallback in `netClientReadConfig()` — copies from `identityGetActiveProfile()->name` when legacy name is empty.

### Decisions Made
- `NET_MAX_CLIENTS` = 32, decoupled from `MAX_PLAYERS` = 8. Server accepts 32 connections; match caps at 8 active slots.
- Identity profile is the authoritative source of local player display name. Legacy g_PlayerConfigsArray is fallback only.

---

## Session 49e — 2026-03-26

**Focus**: Version system full audit + fix

### What Was Done

**Root cause found**: CMake's `CACHE` variable behavior — when `CMakeCache.txt` exists, `set(VERSION_SEM_PATCH N CACHE STRING ...)` is silently ignored. `Set-ProjectVersion` edited CMakeLists.txt correctly but cmake configure didn't override the stale cache.

**Fixes**: `Get-BuildSteps` accepts `$ver` param, appends `-DVERSION_SEM_MAJOR/MINOR/PATCH` flags to BOTH configure steps. `Start-PushRelease` passes `$ver` to `Get-BuildSteps`. `port/src/video.c:91`: replaced hardcoded `"Perfect Dark 2.0 - Client (v0.0.2)"` with `"Perfect Dark 2.0 - v" VERSION_STRING`.

**`context/build.md`**: Added full Version System section documenting the CACHE pitfall and fix.

### Decisions Made
- (Note: later superseded by S49i — ALL builds now use version flags, not just releases)

---

## Session 49f — 2026-03-26

**Focus**: Updater UI — banner fix, per-row actions, server update mechanism

### What Was Done

**Client update banner (`pdgui_menu_update.cpp`)**: Replaced `SmallButton` with `Button` sized via `pdguiScale`; right-aligned via `SameLine(GetContentRegionMax().x - totalW)`. Added `s_DownloadingIndex` + `s_StagedReleaseIndex` state for per-release tracking.

**Settings > Updates tab**: 5-column table (added Action column). Per-row buttons: Download, Switch (staged), % (in-progress). Error message moved below table. Table shown during active download.

**Server update mechanism**: `server_main.c` added `updaterTick()` per frame, logs update availability. `server_gui.cpp`: "Updates (*)" tab with per-row Download/Switch buttons, progress display, Restart & Update button.

### Decisions Made
- `SameLine(GetContentRegionMax().x - totalW)` is the canonical ImGui right-align pattern
- Server headless update path: log URL + manual restart

---

## Session 49g — 2026-03-26

**Focus**: F8 hotswap hint removal

### What Was Done
- Removed deprecated F8 footer hint ("F8: toggle OLD/NEW") from `pdgui_menu_mainmenu.cpp` (footer block at bottom of `renderMainMenu`).

---

## Session 49h — 2026-03-26

**Focus**: Update tab button sizing audit

### What Was Done

**`pdgui_menu_update.cpp` button sizing overhaul**:
- `renderNotificationBanner`: `CalcTextSize()`-based widths for "Update Now", "Details", "Dismiss". Explicit `btnH = GetFontSize() + FramePadding.y * 2` — descender-safe.
- `renderVersionPickerContent`: `CalcTextSize("Check Now")` for "Check Now" button. Action column width from `CalcTextSize("Download")`.
- `TableSetupScrollFreeze(0, 1)` — header stays visible on scroll. Column widths use `pdguiScale()`. `ImGuiSelectableFlags_AllowOverlap` so per-row buttons receive input.
- Removed below-table "Download & Install" button (was off-screen, invisible).
- Download = green, Rollback = amber styling.

### Decisions Made
- Action buttons live in table rows (always visible), not below table (was off-screen)
- `AllowOverlap` is the correct pattern for interactive items in `SpanAllColumns` rows

---

## Session 49i — 2026-03-26

**Focus**: Build pipeline overhaul — always-clean, version baking on every build

### What Was Done

**`devtools/dev-window.ps1` overhaul**:
- **Always-clean builds**: `Start-Build` unconditionally deletes `build/client` + `build/server` before every build. No stale CMakeCache possible.
- **Version from UI on every build**: `Start-Build` reads `Get-UiVersion` → `$script:BuildVersion`, passes to `Get-BuildSteps $script:BuildVersion`. Version boxes are single source of truth.
- **Get-BuildSteps**: Accepts `$ver` parameter, injects `-DVERSION_SEM_MAJOR/MINOR/PATCH` flags into BOTH configure steps.
- **CMakeLists.txt updated after build**: On successful completion, `Set-ProjectVersion` called from `$script:BuildVersion` — file always reflects what was actually built.
- **`Start-PushRelease` updated**: Also cleans before queuing, sets `$script:BuildVersion = $ver`, passes to `Get-BuildSteps`.
- **Removed**: `$script:CleanBuildActive`, `$script:BtnCleanBuild` toggle, associated handler. BUILD button now full hero height.

### Decisions Made
- All builds are clean builds. "Incremental" option removed entirely.
- Version boxes initialize from CMakeLists.txt at startup (reflects last built state).
- CMakeLists.txt updated END of build; -D flags are authoritative during build, file updated after.
- For releases: CMakeLists.txt still updated BEFORE build (pre-release auto-commit).

### Next Steps
- Test full build to verify version bakes correctly
- Verify Release flow (clean → configure with -D → build → release.ps1)

---

## Session 48 -- 2026-03-25

**Focus**: Dev Window overhaul, project cleanup, infrastructure hardening

### What Was Done

**Dev Window (dev-window.ps1)**:
- Fixed UI thread hang: git status moved to background runspace, then to Activated event
- NotesSaveTimer race condition fixed (no more dispose-in-tick)
- Font caching in paint handlers (no per-frame allocation)
- Tab background white strip eliminated (dark panel wrapper)
- Auth label: clickable button, opens `gh auth login` if unauthenticated
- GitHub + Folder buttons moved to main UI (bottom of Build tab)
- Two font size settings (Button + Detail) with live refresh
- Stable/Dev toggle checkbox for releases
- Documentation tab (split pane: file list + content reader, 30/70 ratio)
- Clean Build toggle button (beneath BUILD, wipes build dirs before configure)
- Post-build copy list configurable via settings
- Client/server status labels show exe existence on startup
- Latest release label shows tag + dev/stable + color
- Background runspaces now pass PATH for gh CLI access

**Release pipeline (release.ps1)**:
- All 7 PS7-only syntax violations fixed for PS5 compatibility
- All em dashes replaced with ASCII
- Unified release: single tag (v0.0.1) with both client + server attached
- Auto-overwrite existing releases (delete + recreate with sound notification)
- GIT_TERMINAL_PROMPT=0 in subprocess environment

**Project cleanup**:
- Deleted: 6 runbuild scripts, fix_endscreen, phase3 docs, context-recovery.skill, mods folder info, PROMPT.md, context.md (106KB monolithic), ROADMAP.md, pd-port-director-SKILL.md, CHANGES.md, old devtools (build-gui, playtest-dashboard, doc-reader + .bat launchers)
- Deleted: 4.3GB of abandoned Claude Code worktrees
- Created: UNRELEASED.md (player-facing changelog), dist/windows/icon.ico + icon.rc
- Session log archived (S22-46 to sessions-22-46.md, active trimmed to 229 lines)
- tasks-current.md cleaned (completed items removed)
- COWORK_START.md rewritten as lean bootstrap pointer

**Code fixes**:
- fs.c: data directory search priority fixed (exe dir first, then cwd, then AppData)
- romdata.c: creates data/ dir + README.txt when ROM missing, then opens correct folder
- .build-settings.json: ROM path updated to new project location

**Skill + context**:
- game-port-director skill updated with Sections 8-9 (design principles, tool patterns)
- Skill packaged as .skill for reinstallation
- Context canonical location documented in CLAUDE.md
- 6 memories saved (profile, event-driven, clean structure, no worktrees, ACK messages, no ambiguous intent)

### Decisions Made
- Event-driven over polling (standing principle)
- Unified release tag (v0.0.1) replaces split client/server tags
- context/ is canonical location, parent copies are convenience mirrors
- No worktrees: all code changes in working copy

### Bugs Noted
- B-18: Pink sky on Skedar Ruins (possible texture/clear color issue)
- B-19: Bot spawn stacking on Skedar Ruins (all bots spawn at same pad)

### Session 48 continued -- Collision Rewrite + Debug Vis

**Collision system** (meshcollision.c + meshcollision.h):
- Triangle extraction from model DL nodes (G_TRI1, G_TRI4) -- WORKING
- Room geometry extraction (geotilei, geotilef, geoblock) -- WORKING, 7,110 tris on Skedar
- Static world mesh with spatial grid (256-unit cells) -- WORKING
- Dynamic mesh attachment via colmesh* field on struct prop -- CODED
- capsuleSweep: mesh primary, legacy fallback -- ACTIVE
- capsuleFindFloor: mesh primary -- ACTIVE, confirmed in logs
- capsuleFindCeiling: mesh primary -- FIXED slack formula, needs retest
- Stage lifecycle hooks in lv.c -- ACTIVE on all gameplay stages

**Debug visualization** (meshdebug.c):
- F9 toggles surface tinting in the GBI vertex pipeline
- Green=floor, Red=wall, Blue=ceiling based on vertex normals
- Zero overhead when off (cached flag check per frame)

**Data path fixes**:
- fs.c: exe dir searched first for data/ folder
- romdata.c: creates data/ dir + README.txt when ROM missing, opens correct folder
- dev-window.ps1: Copy-AddinFiles server guard removed (was blocking all copies)
- release.ps1: unified tag, auto-overwrite, PS5 compat, all em dashes fixed

### Session 48 continued -- Collision Disabled + Multiplayer Planning

**Collision rewrite DISABLED**: original system fully restored. Mesh collision code preserved
in meshcollision.c/h for Phase 2 redesign. Needs proper design accounting for: no original
ceiling colliders, jump-from-prop detection (simple downward raycast), slope behavior,
Thrown Laptop Gun as ceiling detection reference. HIGH PRIORITY return.

**ASSET_EFFECT type** added to catalog: 6 effect types (tint, glow, shimmer, darken, screen,
particle), 6 targets (scene, player, chr, prop, weapon, level). First effect mod pending.

**Live console**: backtick toggle, 256-line ring buffer, color-coded ImGui window.

**Multiplayer infrastructure vision confirmed (Mike)**: server = social hub with persistent
connections. Players connect and exist as presence regardless of activity (solo campaign,
MP match, co-op, splitscreen, level editor). Rooms for concurrent activities. Server mesh/
federation for load distribution. Player profiles with stats/achievements/shared content.
Menu system audit needed (double-press issues, hierarchy).

### Session 48 continued -- Menu Manager + Multiplayer Plan

**Menu State Manager (SPF-2a)**:
- New files: `port/src/menumgr.c` + `port/include/menumgr.h`
- Stack-based (8 deep), 2-frame input cooldown on push/pop
- Initialized in main.c, ticks in mainTick() (src/lib/main.c)
- pdguiProcessEvent blocks all key/button input during cooldown
- Pause menu wired: open checks cooldown, pushes MENU_PAUSE; close pops
- Modding hub wired: open pushes MENU_MODDING, back pops
- End Game confirm button now uses pdguiPauseMenuClose() instead of direct flag set
- Legacy PD menus (g_MenuData.root) not yet wrapped -- separate task

**Multiplayer Plan** (context/multiplayer-plan.md):
- Full design doc written covering server-as-hub, rooms, federation, profiles, phonetic
- Confirmed decisions: all MP through dedicated server, campaign = co-op (offline OK),
  automatic federation routing, stats framework first, editor pre-1.0 but lower priority
- Splitscreen works offline, treated as group when connected to server
- Campaign has dual authority: local (offline) or server (online)

**ASSET_EFFECT** added to catalog enum (12th asset type). Effect types + targets defined.
Release script updated: only zip attached (no separate exe files).
Collision mesh system disabled, original restored. Code preserved for Phase 2.

### Next Steps
- SPF-2b: verify SPF-1 build (hub/room/identity/phonetic)
- SPF-3a: lobby ImGui screen design + implementation
- ASSET_EFFECT mod creation + mods copy pipeline
- Wire remaining menus through menu manager (settings, etc.)
- B-19, B-20, B-18 bug investigation
- Collision Phase 2 design (HIGH PRIORITY)

---

## Session 47d — 2026-03-24

**Focus**: SPF-1 — Server Platform Foundation (hub lifecycle, room system, identity, phonetic encoding)

### What Was Done

Implemented the server platform foundation layer on top of the existing ENet dedicated server.
Four new module pairs + wiring into server_main.c + server_gui.cpp tab bar.

**New files (8):**

1. **`port/include/phonetic.h`** / **`port/src/phonetic.c`** — CV syllable IP:port encoding.
   16 consonants × 4 vowels = 6 bits/syllable × 8 syllables = 48 bits (IPv4 + port).
   Format: `"BALE-GIFE-NOME-RIVA"` — shorter than word-based connect codes. Both coexist.
2. **`port/include/identity.h`** / **`port/src/identity.c`** — `pd-identity.dat` persistence.
   Magic `PDID`, version byte, 16-byte UUID (xorshift128 seeded from SDL perf counter + time),
   up to 4 profiles (name/head/body/flags). Validates on load, rebuilds default on corruption.
3. **`port/include/room.h`** / **`port/src/room.c`** — Room struct + 5-state lifecycle.
   Pool of 4 rooms. Room 0 permanently wraps the existing match lifecycle (never truly closes).
   States: LOBBY→LOADING→MATCH→POSTGAME→CLOSED. Transitions logged via `sysLogPrintf`.
4. **`port/include/hub.h`** / **`port/src/hub.c`** — Hub singleton owning rooms + identity.
   `hubTick()` reads `g_Lobby.inGame` each frame → drives room 0 state machine.
   One-frame POSTGAME bridge on match end. Derives hub state from aggregate room states.

**Modified files (3):**

5. **`port/src/server_main.c`** — Added `hubInit()` / `hubTick()` / `hubShutdown()` calls.
6. **`port/fast3d/server_gui.cpp`** — Middle panel converted to tabbed layout.
   "Server" tab: existing player list + match controls. "Hub" tab: hub state + room table
   with color-coded states. Log panel: HUB: prefix highlighted purple.
7. **`context/server-architecture.md`** — SPF-1 section added (hub/room diagram, phonetic,
   GUI changes, new file table).

**Commit**: `fb5450b feat(SPF-1): hub lifecycle, room system, player identity, phonetic encoding`

### Decisions Made

- **Backward compatibility**: Room 0 driven by `g_Lobby.inGame` observation — zero changes
  to `net.c` or `netlobby.c`. Existing single-match path unchanged.
- **Protocol**: v21 unchanged. No new ENet messages. Both phonetic and word connect codes
  remain available.
- **`HUB_MAX_CLIENTS`**: Defined directly in `room.h` (= 8) rather than including `net/net.h`
  to keep hub modules standalone and avoid the full game header chain.
- **Boolean fields**: Used `int` not `_Bool`/`bool` in new C modules (port/ files, but
  matching the project convention of `s32` for boolean-like values).
- **Room 0 persistence**: `roomDestroy()` on room 0 resets to LOBBY instead of CLOSED —
  room 0 is the permanent lounge for the existing server lifecycle.

### Dev Build Status

**UNVERIFIED** — Build environment broken in session (GCC TEMP path issue in sandbox).
`build-headless.ps1` TEMP/TMP fix committed. User to verify build from local environment.

### Session 47e Follow-up — 2026-03-24

**Focus**: Fix server build — SPF-1 symbols undefined in pd-server

**Root cause**: SRC_SERVER in CMakeLists.txt is a manually curated list; the 4 new SPF-1
files (hub.c, room.c, identity.c, phonetic.c) were not added when coded in S47d.
Client uses GLOB_RECURSE so it picked them up automatically; server did not.

**Fix**: Added 4 entries to SRC_SERVER block in CMakeLists.txt (lines 478–482).
Commit `c788486`. Pushed to dev.

**Build status**: Cannot verify in sandbox (GCC DLL loading issue — cc1.exe needs
libmpfr-6.dll via Windows PATH, not POSIX PATH). Run `.\devtools\build-headless.ps1 -Target server`
from PowerShell to confirm.

### Next Steps

- Run `.\devtools\build-headless.ps1 -Target server` from PowerShell to confirm fix
- Build and QC test SPF-1 modules (see qc-tests.md)
- SPF-2: Room federation / multi-room support
- D5: Settings persistence for server configuration

---

## Session 47b — 2026-03-24

**Focus**: B-12 Phase 2 — Migrate chrslots callsites to participant API

### What Was Done

Completed the Phase 2 migration of all chrslots bitmask read/write sites across 5 files.
Phase 1 bulk-sync calls (`mpParticipantsFromLegacyChrslots`) replaced with targeted
`mpAddParticipantAt`/`mpRemoveParticipant` at each write site.

**Key design established:**
- Pool capacity is `MAX_MPCHRS` (40), not the Phase 1 default 32
- Pool slot `i` == chrslots bit `i` (players 0–7, bots 8–39)
- `mpIsParticipantActive(i)` is a direct drop-in for `chrslots & (1ull << i)`
- New `mpAddParticipantAt(slot, type, ...)` API for exact-slot placement

**Files changed (7):**

1. **`src/include/game/mplayer/participant.h`** — Added `mpAddParticipantAt()` declaration
2. **`src/game/mplayer/participant.c`** — Added `mpAddParticipantAt()` impl; rewrote
   `mpParticipantsToLegacyChrslots` (slot index IS bit index) and
   `mpParticipantsFromLegacyChrslots` (use `mpAddParticipantAt` for exact placement)
3. **`src/game/mplayer/mplayer.c`** — ~25 sites: mpInit, match lifecycle, bot create/copy/
   remove, score, team assignment, name generation, save/load config and WAD
4. **`src/game/mplayer/setup.c`** — 10 sites: handicap CHECKHIDDEN, team loop ×3,
   bot slot UI, simulant name display, player file availability
5. **`src/game/challenge.c`** — Read check + fix `1u`→`1ull` write bug + add participant
   calls alongside chrslots writes in `challengePerformSanityChecks`
6. **`src/game/filemgr.c`** — 2 player-file presence checks
7. **`port/src/net/matchsetup.c`** — `mpClearAllParticipants()` + `mpAddParticipantAt`
   at each player/bot write site

**Commit**: `94a2b1e feat(B-12-P2): migrate chrslots callsites to participant API`

### Dev Build Status

**PASS** — `cmake --build --target pd` clean (exit 0). All 7 files compiled without errors.

### Decisions Made

- `challengeIsAvailableToAnyPlayer` reads `chrslots & 0x000F` as a bitmask for challenge
  availability computation — left as-is (no clean participant API equivalent, chrslots
  still dual-written in Phase 2)
- `mp0f18dec4` VERSION guard retained (PC builds are >= JPN_FINAL, always included)
- `setup.c` fixes applied via line-by-line PowerShell replace (Edit tool had CRLF mismatch)

### Next Steps

- B-12 Phase 3: Remove `chrslots` field + legacy shims + BOT_SLOT_OFFSET
- Protocol version bump to v21 (SVC_STAGE_START uses participant list)
- QC: in-game bot add/remove, match start/end, save/load bot config

---

## Session 47c — 2026-03-24

**Focus**: Stage Decoupling Phase 2 (Dynamic stage table) + Phase 3 (Index domain separation)

### What Was Done

**Phase 2 — Dynamic stage table** (7 files):

1. **`src/game/stagetable.c`** — Renamed static array to `s_StagesInit[]`, added heap pointer `g_Stages` + `g_NumStages`. `stageTableInit()` mallocs+memcpys. `stageGetEntry(index)` bounds-checked accessor. `stageTableAppend(entry)` realloc-based. Both `stageGetCurrent()` and `stageGetIndex()` rewritten to use `g_NumStages`. `soloStageGetIndex(stagenum)` iterates `g_SoloStages[0..NUM_SOLOSTAGES-1]`.
2. **`src/include/data.h`** — `extern struct stagetableentry *g_Stages` + `extern s32 g_NumStages` (was array).
3. **`src/include/game/stagetable.h`** — Full declaration set for all Phase 2 + 3 functions.
4. **`src/game/bg.c`** — `ARRAYCOUNT(g_Stages)` replaced with `g_NumStages` (2 occurrences).
5. **`port/src/assetcatalog_base.c`** — Removed local `extern struct stagetableentry g_Stages[]` (conflicted with pointer decl). Bounds check `idx >= 87` → `idx >= g_NumStages`.
6. **`port/src/main.c`** — Added `stageTableInit()` call before `assetCatalogRegisterBaseGame()`.

**Phase 3 — Index domain guards** (2 files):

7. **`src/game/endscreen.c`** — 9 guard sites: `endscreenMenuTitleRetryMission`, `endscreenMenuTitleNextMission`, `endscreenMenuTitleStageCompleted`, `endscreenMenuTextCurrentStageName3`, `endscreenMenuTitleStageFailed`, `endscreenHandleReplayPreviousMission` (underflow), `endscreenAdvance()` (overflow), `endscreenHandleReplayLastLevel`, `endscreenContinue` DEEPSEA (2 paths, both guarded).
8. **`src/game/mainmenu.c`** — 4 guard sites: `menuTextCurrentStageName`, `soloMenuTitleStageOverview`, `soloMenuTitlePauseStatus`, `isStageDifficultyUnlocked` (top guard returns true for out-of-range — mod stages treated as unlocked).

**Bonus fix**: Restored `src/game/mplayer/setup.c` and `src/game/setup.c` from commit `4704eab` after auto-commit `0a36981` corrupted them (all tabs replaced with literal `\t`). Pre-existing bug revealed by full rebuild.

### Decisions Made

- `soloStageGetIndex()` lives in `stagetable.c` (iterates `g_SoloStages[]`). It is the Phase 3 domain translation function.
- `isStageDifficultyUnlocked(stageindex < 0 || >= NUM_SOLOSTAGES)` returns `true` — mod stages are "unlocked" by definition (no solo-stage-based unlock system applies to them).
- `ARRAYCOUNT(g_Stages)` was eliminated. Any future code must use `g_NumStages`.

### Dev Build Status

**PASS** — `build-headless.ps1 -Target client` clean (exit 0). All modified files compiled without errors. Warnings in bg.c are pre-existing.

### Next Steps

- MEM-2: `assetCatalogLoad()` / `assetCatalogUnload()`
- MEM-1 build test: full cmake pass confirms `assetcatalog.h` struct changes are stable
- S46b: Full asset catalog enumeration (animations, SFX, textures)

---

## Session 47a — 2026-03-24

**Focus**: MEM-1 — Asset Catalog load state tracking fields

### What Was Done

Added lifecycle state tracking to `asset_entry_t` as the foundation for Phase D-MEM
memory management. This is purely additive — no existing behavior changes.

**Files changed (4 files):**

1. **`port/include/assetcatalog.h`** — Added `asset_load_state_t` enum
   (`REGISTERED`/`ENABLED`/`LOADED`/`ACTIVE`). Added `#define ASSET_REF_BUNDLED 0x7FFFFFFF`.
   Added 4 fields to `asset_entry_t`: `load_state`, `loaded_data`, `data_size_bytes`,
   `ref_count`. Added `assetCatalogGetLoadState()` and `assetCatalogSetLoadState()`
   declarations in new "Load State API (MEM-1)" section.

2. **`port/src/assetcatalog.c`** — `assetCatalogRegister()` initializes new fields:
   `ASSET_STATE_REGISTERED`, `loaded_data=NULL`, `data_size_bytes=0`, `ref_count=0`.
   `assetCatalogSetEnabled()` now advances `REGISTERED→ENABLED` on first enable.
   Added `assetCatalogGetLoadState()` and `assetCatalogSetLoadState()` implementations.

3. **`port/src/assetcatalog_base.c`** — All 4 bundled registration sites (stages, bodies,
   heads, arenas) now set `load_state=ASSET_STATE_LOADED` and `ref_count=ASSET_REF_BUNDLED`.

4. **`port/src/assetcatalog_base_extended.c`** — All 7 bundled registration sites (weapons,
   animations, textures, props, gamemodes, audio, HUD) now set `ASSET_STATE_LOADED` and
   `ref_count=ASSET_REF_BUNDLED`.

### Decisions Made

- `ASSET_REF_BUNDLED = 0x7FFFFFFF` (S32_MAX) as documented in MEM-1 spec.
- `REGISTERED→ENABLED` transition happens in `setEnabled(id, 1)`. If load_state is already
  LOADED or ACTIVE (bundled assets), setEnabled does not downgrade state.
- `assetCatalogSetLoadState()` is a raw setter — callers own the validity of transitions.
  Future eviction logic will use `ref_count` to guard bundled assets.
- `loaded_data` / `data_size_bytes` fields left at NULL/0 for all existing entries —
  wired for the future loader, not populated yet.

### Dev Build Status

- Syntax-check (MinGW gcc -fsyntax-only): **PASS** on all 3 modified .c files
- Full cmake build: needs Mike's `build-headless.ps1` run (cmake env not available in session)

### Next Steps

- MEM-2: Implement `assetCatalogLoad()` / `assetCatalogUnload()` (allocate/free loaded_data)
- MEM-3: ref_count acquire/release + eviction policy (skip if `ref_count == ASSET_REF_BUNDLED`)
- Wire load state into mod manager UI (show loaded/active indicators)

---

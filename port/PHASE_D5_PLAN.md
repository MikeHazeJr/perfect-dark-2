# Phase D5: Combat Simulator Lobby & Engine Modernization — Implementation Plan

## Status as of Session End (March 19, 2026)

### Completed
- [x] Match setup bypass — `matchStart()` in matchsetup.c directly configures g_MpSetup, bypasses old menutick dialog stack
- [x] ImGui match setup lobby — two-column layout with slot list, detail editor, match settings, scenario/arena/options
- [x] Body model scale clamping — body.c clamps bad scale to 1.0 instead of rejecting (fixes mod_allinone crash)
- [x] Player name fix — reads from g_GameFile.name instead of uninitialized g_PlayerConfigsArray
- [x] Name length expansion (Tier 1) — matchslot.name[32], lobbyplayer.name[32], LOBBY_NAME_LEN=32, all bridge functions updated
- [x] Model catalog system — modelcatalog.c/h: startup pre-scan of all 152 body/head entries, validation, metadata caching, safe accessors
- [x] New save system design — savefile.c/h: JSON-based save/load for agent, MP player, MP setup, system settings
- [x] Multi-select batch editing — lobby slot list supports Ctrl+click, Shift+click; team applies to all, bot settings to bots only
- [x] Main menu entry points — "Local Play" → match setup, "Network Play" → server browser → lobby

### Needs Testing (build and verify)
- [ ] Scale clamping fix — verify crash is gone when loading Combat Simulator
- [ ] Player name fix — verify "MikeHazeJr" shows instead of "Player 1"
- [ ] Model catalog init — check log output for scan summary (valid/clamped/invalid/missing counts)
- [ ] Multi-select UX — verify Ctrl+click, Shift+click, batch team/bot settings
- [ ] Build errors — verify the 3 fixes (langGet include, fsFullPath, g_LanguageId version guard) resolve all errors

---

## Remaining Work — Ordered by Priority

### Priority 1: Core Functionality (get matches playable)

#### 1A. Verify and fix match start flow
**Files:** matchsetup.c, pdgui_menu_matchsetup.cpp
**Steps:**
1. Build and test "Local Play" → match setup → Start Match
2. Check logs for MATCHSETUP entries showing correct chrslots, body/head assignments
3. Verify the match loads the correct arena and scenario
4. If crash persists, check logs for which body model fails and trace through catalog fallback
5. Test with 1 player + 1-4 bots at various difficulties
6. Test all 6 scenarios (Combat, Hold the Briefcase, etc.)

#### 1B. Fix any remaining Combat Simulator crashes
**Files:** body.c, player.c, matchsetup.c, modelcatalog.c
**Steps:**
1. If models still crash, enable CATALOG verbose logging (catalogLogSummary) to identify which models are INVALID/MISSING
2. For MISSING models: check if the filenum exists in ROM data
3. For INVALID models: examine the modeldef fields in the log
4. Ensure catalogGetSafeBody/Head fallbacks are actually being used in matchStart
5. Consider pre-loading the fallback model at catalog init time to guarantee it's cached

#### 1C. Network lobby integration
**Files:** pdgui_menu_lobby.cpp, pdgui_lobby.cpp, netlobby.c, matchsetup.c
**Steps:**
1. Wire match setup into the network lobby flow (leader sees match setup, others see waiting screen)
2. Sync matchconfig over the network (leader broadcasts slot/settings changes)
3. Add SVC_MATCH_CONFIG packet type for broadcasting matchconfig to all clients
4. Each client populates their local g_MpSetup from the received matchconfig
5. Leader's "Start Match" sends SVC_STAGE_START; clients call matchStart locally
6. Test with 2 players over localhost

---

### Priority 2: Engine Modernization

#### 2A. Expand engine struct name fields
**Files:** types.h (mpchrconfig, gamefile, bossfile)
**Steps:**
1. Expand `mpchrconfig.name[15]` → `name[32]`
2. Expand `gamefile.name[11]` → `name[32]`
3. Expand `bossfile.teamnames[MAX_TEAMS][12]` → `[32]`
4. Search for all sizeof/strncpy/hardcoded offsets referencing these fields
5. Update all string copy operations across the engine
6. Update text rendering code that assumes 14-char name widths (menu.c, setup.c)
7. Build and grep for any remaining hardcoded sizes

#### 2B. Upgrade slot limits beyond 8+8
**Files:** types.h (mpsetup), constants.h, matchsetup.c, mplayer.c, player.c
**Steps:**
1. Change `MAX_PLAYERS` from 8 to 16 (or make player/bot slots unified)
2. Change `MAX_BOTS` from 8 to 16
3. Expand `chrslots` from `u16` to `u32` (or use a bitfield array for >32 slots)
4. Expand `g_PlayerConfigsArray[]` and `g_BotConfigsArray[]`
5. Update `mpStartMatch()` chrslot bitmask construction
6. Update `playerTickChrBody()` and all chr iteration loops
7. Update HUD rendering to handle >4 local players (splitscreen limits are separate from MP player count)
8. Test with 9+ total participants

#### 2C. Wire new save system into game flow
**Files:** savefile.c, gamefile.c, mplayer.c, bossfile.c, filemgr.c
**Steps:**
1. Hook `saveInit()` into main.c startup (after fsInit, before game loads)
2. Replace `gamefileLoad()` call with `saveLoadAgent()`
3. Replace `gamefileSave()` call with `saveSaveAgent()`
4. Replace MP player save/load in mplayer.c with `saveLoadMpPlayer()`/`saveSaveMpPlayer()`
5. Replace MP setup save/load with `saveLoadMpSetup()`/`saveSaveMpSetup()`
6. Replace boss file save/load with `saveLoadSystem()`/`saveSaveSystem()`
7. Implement `saveMigrateFromEeprom()` to convert old saves on first run
8. Implement `saveListAgents()` directory scanning
9. Test: create new agent, save, quit, reload, verify data persists
10. Test: load old eeprom.bin, verify migration creates correct JSON files

---

### Priority 3: Visual Polish

#### 3A. Model catalog thumbnails as Agent pictures
**Files:** modelcatalog.c, pdgui_charpreview.c/h, pdgui_menu_agentselect.cpp
**Steps:**
1. Add a thumbnail rendering queue to the catalog (batch render at init or on-demand)
2. For each valid body model: request a 128x128 FBO render via pdguiCharPreviewRequest
3. Read back pixels and create a GL texture, store in catalogentry.thumbnailTexId
4. Update Agent Select screen to use thumbnailTexId instead of placeholder boxes
5. Add a small preview image next to each body in the match setup Character dropdown
6. Consider lazy rendering (render thumbnails as they scroll into view) for performance

#### 3B. Character model previews in lobby
**Files:** pdgui_menu_matchsetup.cpp, pdgui_charpreview.c/h
**Steps:**
1. Add a 3D character preview panel to the match setup UI (right side or popup)
2. When a slot is selected, render that slot's body/head in the preview FBO
3. Display the preview texture with ImGui::Image
4. Allow rotation via mouse drag or auto-rotate
5. Show equipment/weapon preview if applicable

#### 3C. Lobby UI polish
**Files:** pdgui_menu_matchsetup.cpp
**Steps:**
1. Add weapon set selector (preset dropdowns: Slayer, Farsight, etc.)
2. Add individual weapon slot editing for custom weapon sets
3. Add "Select All" / "Deselect All" buttons for the slot list
4. Add drag-to-reorder for slots
5. Add team grouping view (slots grouped by team color)
6. Add tooltip previews showing bot descriptions
7. Keyboard/gamepad navigation for the full UI

---

### Priority 4: Full g_HeadsAndBodies Replacement

#### 4A. Create accessor API functions
**Files:** modelcatalog.c/h
**Steps:**
1. Add `catalogGetFilenum(index)`, `catalogGetScale(index)`, `catalogGetAnimscale(index)`
2. Add `catalogGetModeldef(index)` — returns cached modeldef pointer
3. Add `catalogIsMale(index)`, `catalogGetType(index)`, `catalogGetHeight(index)`
4. Add `catalogGetHandFilenum(index)`

#### 4B. Redirect engine access points (14 files, ~85 accesses)
**Files:** body.c, bodyreset.c, player.c, mplayer.c, bot.c, botmgr.c, bondgun.c, chraction.c, menu.c, setup.c, training.c
**Steps:**
1. Start with the most-accessed file: body.c (~50 accesses)
2. Replace `g_HeadsAndBodies[idx].field` with `catalogGetField(idx)` calls
3. For cached modeldefs: use `catalogGetModeldef(idx)` instead of `g_HeadsAndBodies[idx].modeldef`
4. For bodyreset.c: replace the NULL-ing loop with a `catalogResetModeldefs()` call
5. Work through each file, building the accessor API as needed
6. Add compile-time deprecation warning on direct g_HeadsAndBodies access
7. Extensive testing after each file conversion

---

## Architecture Notes for Next Session

### Key file locations:
- **Match setup:** `port/src/net/matchsetup.c` + `port/fast3d/pdgui_menu_matchsetup.cpp`
- **Model catalog:** `port/src/modelcatalog.c` + `port/include/modelcatalog.h`
- **Save system:** `port/src/savefile.c` + `port/include/savefile.h`
- **Network lobby:** `port/fast3d/pdgui_menu_lobby.cpp` + `port/src/net/netlobby.c`
- **Bridge functions:** `port/fast3d/pdgui_bridge.c` (C→C++ struct marshaling)
- **Main menu:** `port/fast3d/pdgui_menu_mainmenu.cpp`
- **Body loading:** `src/game/body.c` (scale clamping at line ~220)
- **Player spawn:** `src/game/player.c` (body model loading at playerTickChrBody)
- **Engine types:** `src/include/types.h` (all struct definitions)
- **Constants:** `src/include/constants.h` (MAX_PLAYERS, MAX_BOTS, slot counts)

### C++/C boundary rules:
- ImGui `.cpp` files CANNOT include `types.h` (because `#define bool s32` breaks C++)
- Struct definitions must be duplicated in C++ files with matching layouts
- Bridge functions in `pdgui_bridge.c` marshal data via raw byte offsets
- Always verify struct layout consistency when changing field sizes

### Build system:
- CMake + MinGW on Windows (MSYS2), cannot build from Linux VM
- `GLOB_RECURSE` auto-discovers `port/*.c` and `port/*.cpp`
- Default ROMID is `ntsc-final` (VERSION=2)
- `g_LanguageId` only exists when VERSION >= VERSION_PAL_BETA (3)

### Current name length chain:
- `matchslot.name[32]` (our lobby struct) ✓ expanded
- `lobbyplayer.name[32]` (network lobby) ✓ expanded
- `mpchrconfig.name[15]` (engine internal) — needs Tier 2 expansion
- `gamefile.name[11]` (save format) — needs Tier 2 expansion
- `bossfile.teamnames[][12]` — needs Tier 2 expansion
- New JSON save files store full 32-char names

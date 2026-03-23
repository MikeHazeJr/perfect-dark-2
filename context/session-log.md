# Session Log (Active)

> Recent sessions only. Archives: [1–6](sessions-01-06.md) · [7–13](sessions-07-13.md) · [14–21](sessions-14-21.md)
> Back to [index](README.md)

---

## Session 33 — 2026-03-23

**Focus**: D3R-5 map cycle test crash fix

### Diagnosis

Map cycle test v2 crashed with 0xc0000005 (access violation) on the 5th arena (Sewers, stagenum 0x42). Log analysis revealed:
- All 4 previous maps reported "LOADED in 1 frame" — `normmplayerisrunning` was **never reset** between maps
- `matchStart()` was called during active gameplay without ending the previous match
- `mpEndMatch()` (which resets match state) was never called; `mainEndStage()` was avoided because it triggers the broken OG endscreen
- After 4 rapid match-starts without cleanup, cumulative state corruption → crash

Root cause: The v2 state machine called `matchStart()` directly from gameplay. `normmplayerisrunning` stays `true` (only reset by `menutick.c` during endscreen→menu flow), so the game never saw a clean "no match active" state between maps.

### Fix

**New bridge function** `pdguiMapTestResetMatchState()` in `pdgui_bridge.c`:
- Resets `normmplayerisrunning`, `mplayerisrunning`, `lvmpbotlevel` to false/0
- Clears `g_MainIsEndscreen` flag
- Resets pause mode to `MPPAUSEMODE_UNPAUSED`
- Deliberately avoids `mainEndStage()`/`mpEndMatch()` which trigger the OG endscreen

**New CLEANUP state** in the map test state machine:
- LOADED (30 frames) → CLEANUP (reset match state) → 5-frame delay → launch next arena → WAIT_LOAD
- WAIT_LOAD now works correctly: `normmplayerisrunning` starts as `false` (was reset), so it properly detects the new map loading when the flag goes `true`

### Files Modified
- `port/fast3d/pdgui_bridge.c` — added `pdguiMapTestResetMatchState()`
- `port/fast3d/pdgui_menu_matchsetup.cpp` — CLEANUP state, extern "C" decl, state machine rewrite

---

## Session 32 — 2026-03-23

**Focus**: D3R-5 — Catalog bootstrap, standalone resolution, and catalog-as-source-of-truth (B-17 fix)

### What Was Done

1. **D3R-5 Step 1: Catalog Bootstrap** (from previous compacted session)
   - Wired `assetCatalogInit()` + `assetCatalogRegisterBaseGame()` + `assetCatalogScanComponents()` into `main.c` startup sequence (after `modmgrInit()` and `catalogInit()`)
   - Added `modmgrGetModsDir()` accessor to `modmgr.c/h` — stores the resolved mods directory path for use by the catalog scanner
   - **BUILD PASS** confirmed

2. **D3R-5 Step 2: Standalone Filesystem Resolution**
   - **NEW**: `port/include/assetcatalog_resolve.h` — 4 functions: activate/deactivate stage, find map by stagenum, resolve path
   - **NEW**: `port/src/assetcatalog_resolve.c` (~170 lines) — context-aware resolver with exact-match file checking
   - **MODIFIED**: `port/src/fs.c` — `fsFullPath()` calls `assetCatalogResolvePath()` as first-priority check before `modmgrResolvePath()`
   - **MODIFIED**: `src/game/lv.c` — `lvReset()` calls `assetCatalogActivateStage(stagenum)` to set active component context
   - **MODIFIED**: `port/src/server_stubs.c` — stubs for all 4 resolve functions (server doesn't compile the resolve module)
   - **BUILD PASS** — client and server both green

3. **D3R-5 Step 3: Catalog as Single Source of Truth (B-17 fix)**
   - Director directive: "Our validated dynamic catalog should be our single source of truth."
   - **REWRITTEN**: `assetcatalog_resolve.c` (~270 lines) — smart bgdata redirect architecture
   - **REWRITTEN**: `assetcatalog_resolve.h` — updated doc comments for new architecture
   - **BUILD PASS** — Paradox loaded correctly, Kakariko loaded (visual correctness unverified)

### Architecture: Catalog as Source of Truth

The resolver now has two resolution strategies:

**Smart bgdata redirect** (for bgdata files — the B-17 fix):
- On stage activation, `scanBgdataDir()` reads the component's `bgdata/` directory
- Each file is classified by suffix into a role: `.seg` (geometry), `_padsZ` (pads), `_tilesZ` (tiles), `_setupZ` (setup), `_mpsetupZ` (MP setup)
- When the game requests any bgdata file, the resolver matches by **role suffix** — not filename
- The component's actual file wins regardless of what `g_Stages[]` file IDs resolve to
- This bypasses the broken `modConfigParseStage()` patching entirely

**Exact match** (for non-bgdata files — textures, props, etc.):
- Checks if the exact requested file exists in the component directory
- Falls through to legacy if not found

### Why This Fixes B-17

B-17 root cause: `modConfigParseStage()` in `mod.c` patches `g_Stages[]` with wrong file IDs. The game then requests wrong filenames from romdata. With the old exact-match resolver, these wrong filenames wouldn't be found in the component directory.

With smart redirect: the game asks for `bgdata/bg_WRONG.seg` (because `g_Stages[]` was corrupted), but the resolver sees the `.seg` suffix, looks up the component's actual `.seg` file, and returns that instead. The catalog's files always win.

### Code Review Findings (Fixed)

- **Missing server stubs**: Added stubs for `assetCatalogActivateStage`, `assetCatalogDeactivateStage`, `assetCatalogFindModMapByStagenum`
- **dirent.h safety**: Confirmed already used in `assetcatalog_scanner.c` and `modmgr.c` — safe on MinGW

### Files Modified This Session
- `port/src/assetcatalog_resolve.c` — NEW then REWRITTEN (smart redirect)
- `port/include/assetcatalog_resolve.h` — NEW then REWRITTEN (updated docs)
- `port/src/fs.c` — catalog priority check in `fsFullPath()`
- `src/game/lv.c` — `assetCatalogActivateStage()` call in `lvReset()`
- `port/src/server_stubs.c` — 4 function stubs
- `port/src/main.c` — catalog init calls (from previous session)
- `port/src/modmgr.c` — `modmgrGetModsDir()` accessor (from previous session)
- `port/include/modmgr.h` — `modmgrGetModsDir()` declaration (from previous session)

### Next Steps
- More thorough B-17 testing: verify all mod maps load correctly (especially GEX bonus stages)
- Arena/body/head accessor migration to catalog (menu code, D3R-5 continued)
- Consider: is `modConfigParseStage()` patching now dead code for catalog-resolved stages?

---

## Session 29 — 2026-03-23

**Focus**: D3R-1 redo — correct decomposition in persistent location, MOD_CONVERSION_GUIDE.md

### Issues Fixed (from Session 28)

1. **Wrong location**: Session 28 created 69 components in `build/client/mods/` (ephemeral — erased on clean build). All deleted. Correct location is `post-batch-addin/mods/` (persistent, xcopy'd by build.bat).

2. **Fabricated maps**: Session 28 created 50 map components from directory listings and comments. Only bgdata files that actually exist in each mod's `files/bgdata/` directory are real maps. Corrected.

3. **Stagenum resolution (Option 3)**: Director chose full decomposition. Every map's stagenum is now derived from its bgdata filename → base stage table lookup (§2.2 of MOD_CONVERSION_GUIDE.md). No more `0x00` defaults.

### Decomposition Results (Correct)

**56 map components, 42 character components, 5 texture packs** created as `_components/` subdirectories inside each `mod_*` folder:

| Mod | Maps | Characters | Textures | Notes |
|-----|------|------------|----------|-------|
| mod_allinone | 17 | 8 | 1 (14 files) | Suburb has allocation override |
| mod_gex | 30 | 10 | 1 (711 files) | 4 stage_patch maps (Bunker, Facility BZ, Train, Archives 1F) |
| mod_kakariko | 4 | 8 | 1 (234 files) | 1 stage_patch map (Kakariko Village with weather) |
| mod_dark_noon | 1 | 8 | 1 (39 files) | Simplest mod |
| mod_goldfinger_64 | 4 | 8 | 1 (104 files) | Named: Mall, Steel Mill, Tunnels, Junkyard |

**Resolution types**: 5 maps use `resolution = stage_patch` (have explicit modconfig.txt stage declarations that redirect file pointers). 51 maps use `resolution = dedicated` (file replacement only, stagenum from bgdata lookup).

**Structure**: Each component has a typed `.ini` manifest and symlinks to original files:
```
mod_gex/_components/maps/bg_arec/
├── bg_arec.ini          ← manifest
└── bgdata/
    ├── bg_arec.seg      → ../../../../files/bgdata/bg_arec.seg (symlink)
    ├── bg_arec_padsZ    → ../../../../files/bgdata/bg_arec_padsZ
    └── bg_arec_tilesZ   → ../../../../files/bgdata/bg_arec_tilesZ
```

### Documents Created
- `docs/MOD_CONVERSION_GUIDE.md` — comprehensive conversion guide with:
  - Full file-to-stage mapping reference (bgdata → stagenum)
  - Two loading mechanisms explained (file replacement vs. stage patching)
  - Field mapping table (modconfig.txt → .ini)
  - Per-mod conversion notes with gotchas
  - Step-by-step conversion process

### Next Steps
- D3R-1 build test: verify game still boots with old mod loader (`_components/` ignored by existing scanner)
- D3R-3: Base game cataloging — register all 63 bodies, 76 heads, 87 stages with `base:` prefix
- D3R-4: Scanner + loader — parse .ini from `_components/`, populate catalog
- Consider: shim loader (ADR-002 Option A) vs. proceeding directly to D3R-4

---

## Session 28 — 2026-03-23

**Focus**: D3R-1 and D3R-2 implementation — architecture decisions, filesystem decomposition, Asset Catalog core

### Architecture Decision Records

- **ADR-002** (D3R-1): Component filesystem decomposition with shim loader. Chose Option A — restructure filesystem + compatibility shim over deferring loading or building catalog first.
- **ADR-003** (D3R-2): Asset Catalog core design. Extend modelcatalog (not replace), open addressing with linear probing, dynamic allocation (no hard caps).

### Director Decisions (Open Questions Resolved)

1. **Entry cap**: No cap — dynamic allocation, grows on demand.
2. **Override behavior**: Mods can override `base:` assets. Total conversions are first-class. "The game itself is essentially a mod."
3. **Hash function**: Dual-hash — FNV-1a for hash table distribution, CRC32 for network identity. Each entry stores both. No trade-off.

### Code Written

**D3R-2: Asset Catalog Core (2 new files)**
- `port/include/assetcatalog.h` (288 lines) — 14 asset types, entry struct with union, 20 public functions
- `port/src/assetcatalog.c` (704 lines) — FNV-1a + CRC32, open addressing, dynamic growth, full API
- Auto-discovered by CMake glob (`port/*.c`), no CMake changes needed
- Coexists with existing `modelcatalog.c` — no existing code modified

**D3R-1: Initial attempt (SUPERSEDED by Session 29)**
- Created in wrong location (`build/client/mods/`) with fabricated maps and wrong stagenums
- All cleaned up in Session 29

### Verification

- **CRC32 table**: Regenerated correct table from `0xEDB88320` polynomial. Verified all 5 test strings match bitwise `modmgrHashString()` implementation.

### Documents Created
- `context/ADR-002-component-filesystem-decomposition.md`
- `context/ADR-003-asset-catalog-core.md`

---

## Session 27 — 2026-03-23

**Focus**: Component Mod Architecture design (D3 Revised) — discussion only, no code written

### What Was Designed

Complete architectural redesign of the mod system from monolithic (one mod = one directory) to **component-based** (each asset = independent folder + `.ini` manifest). This is the foundational design for the future of the project's mod system, asset loading, and extensibility.

### Key Design Decisions

1. **Component-based architecture**: Every asset (map, character, skin, bot variant, weapon, prop, texture pack, etc.) lives in its own folder under `mods/{category}/{asset_id}/` with a self-describing `.ini` file.

2. **Name-based asset resolution (PROJECT CONSTRAINT)**: No numeric lookups, ever. All asset references go through a string-keyed Asset Catalog. `catalogResolve("gf64_bond")` replaces `body[0x3A]`. Eliminates root cause of B-13, B-17, and the entire class of index-shift bugs.

3. **Category = grouping label**: The `category` field in `.ini` (e.g., `category = goldfinger64`) groups related components in the Mod Manager. Toggling a category disables all its components. No explicit "mod registration" needed — grouping is emergent from tags.

4. **Soft dependencies**: `depends_on` field lists required components (e.g., a map depending on a shared texture pack). Missing dependencies fall back to base game assets gracefully. Maps that can't fully load don't appear in menus.

5. **Skins as soft references**: `target = gf64_bond` in a skin's `.ini` creates a lazy reference. If the target character doesn't exist, the skin silently doesn't appear. Works for any character (base or mod).

6. **Dynamic memory only**: N64-era shared memory pools (modconfig.txt `alloc` values) stripped. Each component uses standard `malloc`. Advisory `hint_memory` for UI only.

7. **One format, multiple interfaces**: `.ini` is the source of truth. Bot Customizer (in-game), Level Editor (future, in-game + external), INI Manager tool (in-game), and direct hand-editing all produce the same format.

8. **Network: delta pack distribution**: Server identifies missing components, bundles only what's needed, streams to client. Options: Download (permanent), Download This Session Only (to `mods/.temp/`), Cancel (stay in lobby, spectate).

9. **Lobby spectator experience**: Combat log with pre-resolved display names (not asset IDs) — server sends strings because spectators may not have the relevant mods. Kill feed with detail: "MeatSim1 killed MeatSim3 with a headshot using the Dragon."

10. **Crash recovery for temp mods**: Graduated response — first crash: Keep/Disable/Discard prompt. Second crash: suspect mod flagged. Third crash: auto-disable all temp mods, launch clean.

11. **Base game in the catalog**: All base assets registered with `"base:"` prefix. The entire game speaks the same lookup language — no special cases for base vs. mod content.

12. **Category-first scanning**: Two-pass — enumerate categories first, then components within. Tolerates unknown categories. Errors logged per category.

### New Constraints Added
- **Name-based asset resolution only** (Active — see constraints.md)

### Removed Constraints Added
- Shared memory pools for mods (N64 pre-allocation)
- Monolithic mod structure (single directory per mod)
- Numeric asset lookups (ROM addresses, table indices)

### Documents Created
- `context/component-mod-architecture.md` — full design document (13 sections)

### Documents Updated
- `context/constraints.md` — new active + 3 new removed constraints
- `context/tasks-current.md` — D3R implementation phases (11 steps)
- `context/README.md` — new document references (pending)

### No Code Written
This was a design-only session. All work was architectural discussion and documentation.

### Next Steps
- D3R-1: Decompose existing 5 bundled mods to component filesystem
- D3R-2: Build Asset Catalog core (hash table, string-keyed resolution)
- Document conversion process in `docs/MOD_CONVERSION_GUIDE.md`

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

---

## Session 30 — 2026-03-23

**Focus**: D3R-3 (base game asset registration) + D3R-4 (component scanner + INI loader) + stagenum data integrity fix

### What Was Done

1. **D3R-3: Base game asset registration** (`port/src/assetcatalog_base.c`)
   - Registers 87 base stages with `"base:{name}"` IDs (e.g., `"base:villa"`)
   - Uses `g_Stages[idx].id` — the logical stage ID from constants.h, NOT the array index
   - Registers 63 base bodies (MP character models)
   - Added 75 base heads (MP head models) — new `s_BaseHeads[]` name table
   - Arenas intentionally skipped — they're stage references for the MP menu, not standalone assets. Arena migration deferred to D3R-5 callsite work.

2. **D3R-4: Component scanner + INI loader** (`port/src/assetcatalog_scanner.c`, `port/include/assetcatalog_scanner.h`)
   - INI parser: `iniParse()`, `iniGet()`, `iniGetInt()`, `iniGetFloat()`
   - Category/section type mapping: 13 asset types (maps, characters, skins, bot_variants, weapons, textures, sfx, music, props, vehicles, missions, UI, tools)
   - `registerComponent()` — populates type-specific union fields (map.stagenum, character.bodyfile/headfile, skin.target_id, bot_variant accuracy/reaction_time/aggression)
   - `assetCatalogScanComponents(modsdir)` — top-level scanner, walks `mod_*/_components/{category}/{component}/`

3. **Critical stagenum fix** — All 56 map .ini files had array indices (from `/*0xNN*/` comments in stagetable.c) instead of logical stage IDs (from STAGE_* constants in constants.h)
   - 51 dedicated maps: converted array index → stage ID via mapping table
   - 5 stage_patch maps: reverted to modconfig.txt values (which were already stage IDs, incorrectly converted by the batch script)
   - Example: bg_eld was 0x18 (array index) → corrected to 0x2c (STAGE_VILLA)
   - Also fixed allocation misattribution: mod_allinone `stage 0x18` allocation is for STAGE_TEST_ARCH (Archives), not bg_eld (Villa). Moved allocation from bg_eld.ini to bg_arch.ini.

4. **MOD_CONVERSION_GUIDE.md** — Fixed §2.2 bgdata-to-stagenum mapping table (49 entries corrected). Added TODO note for Stage Slot Usage tables (§2.1) which still use array indices and have confused GEX stage_patch annotations.

### Files Created/Modified
- **NEW**: `port/src/assetcatalog_base.c` (~420 lines)
- **NEW**: `port/src/assetcatalog_scanner.c` (~496 lines)
- **NEW**: `port/include/assetcatalog_scanner.h` (~107 lines)
- **MODIFIED**: 56 map .ini files under `post-batch-addin/mods/mod_*/_components/maps/`
- **MODIFIED**: `docs/MOD_CONVERSION_GUIDE.md` (stagenum corrections + TODO note)
- **MODIFIED**: `context/tasks-current.md` (D3R-3/D3R-4 marked done)

### Known Issues
- MOD_CONVERSION_GUIDE.md §2.1 Stage Slot Usage tables still use array indices and have confused GEX annotations
- Weapons not yet registered in D3R-3 (noted in task description as ~30 weapons; deferred)

---

## Session 31 — 2026-03-23

**Focus**: Fix assetcatalog_scanner.c build failure

### Build Error Root Cause
The `/**` block comment at top of `assetcatalog_scanner.c` (lines 1–18) contained the path `mods/mod_*/_components/` on line 6. The `*/` in `mod_*/` is the C block comment terminator — it prematurely closed the comment at line 6, column 58. Everything after that point was parsed as code:
- Line 6: `_components/)` → invalid C tokens
- Line 14: `#` in `# comments` → stray preprocessor directive
- All subsequent includes failed → cascading `u8`/`s32`/`size_t` type resolution failures throughout the entire include chain

### Fix Applied
Rewrote the block comment to eliminate all `*/` sequences within comment text. Paths like `mods/mod_*/_components/` replaced with plain English descriptions. `# comments` replaced with `Hash and semicolon comments`.

`assetcatalog_base.c` was unaffected — its block comment had no `*/` sequences.

### Fix Applied
- **MODIFIED**: `port/src/assetcatalog_scanner.c` (lines 1–18: block comment rewritten)

### Build Result
- **BUILD PASS** — confirmed by director. D3R-3 and D3R-4 are fully green.
- The block comment was the sole compilation error in the entire D3R-3/D3R-4 refactor.

### Propagation Check
Scanned all other `port/src/*.c` block comments for embedded `*/` sequences (paths, glob patterns). No other instances found. `assetcatalog_base.c` block comment was clean.

### State After This Session
- D3R-1 through D3R-4: **COMPLETE AND BUILDING**
- Asset Catalog infrastructure: hash table core, base game registration (87 stages, 63 bodies, 75 heads), component scanner with INI parsing — all compiled and linked
- 56 map .ini files with correct stage IDs, 42 character .ini files, 5 texture pack .ini files
- **Next**: D3R-5 — Callsite migration (replace numeric lookups with catalog queries)

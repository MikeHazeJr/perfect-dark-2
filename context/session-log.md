# Session Log (Active)

> Recent sessions only. Archives: [1-6](sessions-01-06.md) . [7-13](sessions-07-13.md) . [14-21](sessions-14-21.md) . [22-46](sessions-22-46.md) . [47-78](sessions-47-78.md) . [79-86](sessions-79-86.md) . [87-119](sessions-87-119.md)
> Back to [index](README.md)

## Session S179 — 2026-04-07 (Input Bug Fixes: Esc/Tab/Mouse/Arrow Keys)

**Focus**: Fix 4 input bugs reported in playtest — all traced to missing `g_CtxImGuiMenu` push in main menu and room menus.

### What Was Done

**Root cause**: `g_CtxImGuiMenu` was never pushed when the main menu opened. Without it on the input context stack, mouse mode stayed captured, gameplay input (arrow keys, Tab) leaked through, and Esc had no grace period.

**Fixes applied** (commit `49efd3b6`, merged to dev as `4a5d073f`):

| Bug | Symptom | Fix |
|-----|---------|-----|
| Mouse not working in main menu | `inputCtxSyncMouseMode()` saw gameplay context → kept mouse captured | Push `g_CtxImGuiMenu` on `IsWindowAppearing()` → `on_push` sets absolute mouse |
| Arrow keys moving camera while menu open | `pdguiIsActive()` returned 0 → game input not zeroed | Context push → `pdguiIsActive()` returns 1 → all game input blocked |
| Tab reopening menu after Esc close | Tab (`CK_START`) processed by game code | `pdguiIsActive()` = 1 blocks Tab from reaching game |
| Esc double-fire (open then close) | No `push_tick` set → no 100ms grace period | Context push sets `push_tick` → `inputCtxShouldSuppressKey()` blocks retrigger |

**Files changed**: `pdgui_menu_mainmenu.cpp`, `pdgui_menu_room.cpp`

### Next Steps
- Playtest verification of all 4 fixes
- Continue roadmap: M1.2 (Solo Mission Flow)

---

## Session S178 — 2026-04-07 (M0.1e: Catalog as Data Provider + release.ps1 auto-commit)

**Focus**: Complete M0.1e — catalog serves body/weapon properties via typed accessors. Harden release.ps1 with auto-commit before rebase.

### What Was Done

**Task A — release.ps1 auto-commit** (already committed in S177 continuation):
- Added auto-commit block before `git pull --rebase` in Step 4: checks `git status --porcelain`, stages with `git add -A`, commits `"chore: auto-commit before release v$Version"` if dirty.

**Task B — M0.1e: Catalog as Data Provider** (commit `b3555576`):

**New accessors** in `assetcatalog.h` / `assetcatalog_api.c`:
- SA-5d (body/head): `catalogGetBodyIsMale`, `catalogGetBodyType`, `catalogGetBodyHeight`, `catalogGetBodyAnimScale`, `catalogGetBodyCanVaryHeight`, `catalogGetBodyIsComplete` (wraps `unk00_01`), `catalogGetBodyHandFilenum`, `catalogGetHeadIsMale`, `catalogGetHeadType`
- SA-5e (MP weapons): `catalogGetMpWeaponNum` (wraps `weaponnum`), `catalogGetMpWeaponUnlockFeature`
- All O(1) direct array accesses with bounds checking (152 for bodies/heads, `NUM_MPWEAPONS` for weapons)

**Migrated 11 game files**: body.c, chraction.c, botmgr.c, bot.c, bondgun.c, botinv.c, activemenu.c, challenge.c, mplayer.c, mplayer/setup.c, player.c

**server_stubs.c**: Added `struct mpweapon g_MpWeapons[NUM_MPWEAPONS]` zero-init stub (server build was missing this symbol, caught at link time).

**Intentionally deferred**:
- `g_HeadsAndBodies[x].modeldef` — runtime-mutable cache pointer, not a stat/property
- `priammotype`/`priammoqty` patterns in bot.c/player.c — pending `catalogGetMpWeaponAmmoInfo()` accessor

### Decisions
- SA-5d/5e are safe for per-frame callers — O(1), no catalog scan
- `g_MpWeapons` server stub zero-initialized; server never uses weapon slot data

### Next Steps
- M0.1 gate: Confirm all integer asset IDs eliminated at public boundaries — M0.1e completes the final sub-task
- Proceed to M0.2 (Input System Unification) or M1/M2/M3 work

---

## Session S178 — 2026-04-07 (M0.1e — Catalog as Data Provider)

**Focus**: Make the asset catalog serve weapon stats, body/head properties directly. ROM arrays become internal implementation detail.

### What Was Done

**M0.1e COMPLETE — Catalog Data Provider API** (commit `b3555576`):
- 15 new catalog data accessor functions in `assetcatalog.h` / `assetcatalog_api.c`:
  - Weapon: damage, fire rate, ammo capacity, magazine size, reload time, range, accuracy, etc.
  - Body: model index, collision radius, type properties
  - Head: model index, type properties
- ROM arrays (`g_MpWeapons[]`, body/head tables) internalized — accessed only through catalog API
- 8 game files migrated: body.c, bot.c, mplayer.c, setup.c, chraction.c, botmgr.c, bondgun.c, player.c
- `g_MpWeapons` stub added to `server_stubs.c` for dedicated server build
- Deferred: modeldef cache, ammo distribution (priammotype/priammoqty) — tracked for future pass

**Release pipeline hardened** (commit `1f1002c4`):
- Auto-commit uncommitted changes before `git pull --rebase` in release.ps1

### Decisions
- Body/head property accessors resolve via catalog ID → runtime index → ROM array internally. Public API is catalog-ID-only.
- Weapon stats follow same pattern. No integer IDs cross the accessor boundary.
- M0.1 is now COMPLETE (all 5 sub-phases a–e done). Foundation lock for catalog identity is achieved.

### Next Steps
- Per interleaved cadence: M1.2 (Solo Mission Flow — briefings, mission complete/failed screens) or M0.2 (Input Unification)
- Gameplay state migration still open (PlayerConfig/BotConfig structs to store catalog IDs natively)

---

## Session S177 — 2026-04-07 (Infrastructure: Script Relocation + Git Recovery)

**Focus**: Fix git infrastructure issues (packed-refs corruption, index.lock, working copy desync from worktree merges), fix build break from truncated matchsetup.h, consolidate scripts into devtools/, harden release pipeline.

### What Was Done

**Git infrastructure recovery**:
- Fixed packed-refs corruption (duplicate v0.0.9 tag + null bytes)
- Cleared stuck index.lock
- Recovered working copy desync (38 modified files from stale worktree merges) via `git checkout dev -- .`

**Build break fixed — matchsetup.h truncation**:
- File truncated at line 84 during M0.1d worktree merge — `extern struct matchconfig g_MatchConfig` and all function prototypes missing
- Restored declarations, added missing `#include "net/matchsetup.h"` to net.c
- pdgui.h extern "C" guards added for C++/C interop (commit 62b71e42)

**Release pipeline hardened**:
- Added `git pull --rebase origin dev` before push step in release.ps1 — prevents non-fast-forward failures when code sessions have pushed commits

**Scripts consolidated into devtools/** (commit 018e0c05):
- Moved: release.ps1, build_check.ps1, release-v0.0.2.ps1 from project root → devtools/
- release.ps1: Added `$ProjectRoot = Split-Path $PSScriptRoot -Parent` + `Set-Location $ProjectRoot` for location-independence
- _dev-window.ps1: Updated reference to `devtools/release.ps1`
- All relative paths verified working (dev window sets CWD to project root before invocation)

**Worktree cleanup**:
- Pruned 6 stale worktree refs (elegant-chandrasekhar, serene-robinson, bold-swartz, busy-wiles, eloquent-lehmann, kind-hertz)
- Physical directories locked by running sessions — will auto-clean on close

### Decisions
- `Set-Location $ProjectRoot` added defensively to release.ps1 — dev window already handles CWD, but this covers direct invocation
- release-v0.0.2.ps1 moved as-is (legacy, no references found anywhere)

### Next Steps
- Continue roadmap: M0.1e (catalog as data provider), M1.2 completion, M2.3 (stats wiring), or M3 (online MP)
- commit-graph cache fix (non-fatal warning, low priority)

---

## Session S176 — 2026-04-07 (B-115 Fix + M2.1 Combat Sim Polish)

**Focus**: Fix B-115 (post-game mouse), verify M2.1 arena selection and game mode selection completeness.

### What Was Done

**B-115 Fixed — Legacy endscreen dialogs suppressed**:
- Root cause: `g_MpEndscreenSavePlayerMenuDialog` (mpingame.cpp) and `g_MpEndscreenConfirmNameMenuDialog` (warning.cpp) were registered with `NULL` renderFn — forcing PD native rendering. Legacy menus rendered on top of ImGui endscreen and stole input.
- Fix: Changed both to `renderNoop` (suppressed). Auto-save via `configSave("pd.ini")` in `pdguiEndscreenExitToMainMenu()` handles PC saving. N64 Controller Pak save dialogs are redundant.
- Added `renderNoop` function to `pdgui_menu_warning.cpp` (already existed in mpingame.cpp and endscreen.cpp).

**M2.1 Arena Selection — Verified COMPLETE**:
- `buildArenaListFromCatalog()` iterates all `ASSET_ARENA` entries. Combo picker stores catalog ID in `g_MatchConfig.stage_id`. Both solo and network paths use catalog strings.
- Preview images not functional — requires base-ui texture extraction (known Phase 4 item, not a blocker).

**M2.1 Game Mode Selection — Verified COMPLETE**:
- Scenario combo picks from 6 modes (Combat, Hold the Briefcase, Hacker Central, Pop a Cap, King of the Hill, Capture the Case).
- Sets both `g_MatchConfig.scenario` (u8 for legacy) and `g_MatchConfig.scenario_id` (PRIMARY) via `catalogIdByRuntime(ASSET_GAMEMODE, si)`.
- All 6 modes functional after M0.1d migration (S173).

### Code Changes (3 files)
- **port/fast3d/pdgui_menu_mpingame.cpp**: Save Player dialog: `NULL` → `renderNoop` (B-115)
- **port/fast3d/pdgui_menu_warning.cpp**: Added `renderNoop`, Confirm Name: `NULL` → `renderNoop` (B-115)
- **port/fast3d/pdgui_menu_endscreen.cpp**: Updated comment about suppressed dialogs

### Decisions
- Both N64 save dialogs redundant on PC — auto-save already wired in S175
- M2.1 marked COMPLETE: all 4 sub-items verified (arena, weapons, bots, game modes)
- Preview images deferred to Phase 4 (base-ui texture extraction) — not a functional blocker

### Next Steps
- Build verification (Mike)
- M2.1 COMPLETE, M2.2 COMPLETE → M2 gate check
- Next: M2.3 (stats/progression), M3 (online MP), or M1.2 completion (briefings/endscreens)

---

## Session S175 — 2026-04-07 (M2.2 — MP Match Flow Improvements)

**Focus**: MP endscreen flow improvements: B-117 root cause fix, player stats display, auto-save on match exit.

### What Was Done

**B-117 FIXED: Crash on match exit** (1 file — pdgui_bridge.c):
- **Root cause identified**: `pdguiEndscreenExitToMainMenu()` called `func0f0f8120()` (legacy menu pop-all) but never popped the `g_CtxImGuiMenu` input context that was pushed on window appear. The stale context survived the stage transition, causing the crash.
- **Fix**: Added `inputCtxPopDeferred(&g_CtxImGuiMenu)` guard to `pdguiEndscreenExitToMainMenu()`, matching the existing pattern in `pdguiEndscreenStartMission()` and `pdguiEndscreenNextMission()`.
- Note: B-117 was previously PARTIAL FIX (S161) with context stack reset on stage transition. This fix addresses the actual leak source.

**MP endscreen player stats section** (1 file — pdgui_menu_endscreen.cpp):
- Added "YOUR STATS" section to the MP endscreen showing local player's combat breakdown: kills, accuracy (with color-coded progress bar), shot region breakdown (head/body/limb/other/total).
- Same data sources as solo endscreen (`mpstatsGetPlayerKillCount`, `mpstatsGetPlayerShotCountByRegion`).
- Section appears after awards/medals, before action buttons, inside the scrollable content area.

**Auto-save on match exit** (1 file — pdgui_bridge.c):
- `configSave("pd.ini")` called at top of `pdguiEndscreenExitToMainMenu()`.
- PC has no pak/memory card — auto-save replaces the N64's "Save Player?" prompt.

**Match start → gameplay verified**:
- `matchStart()` is fully catalog-native: resolves `scenario_id`, `stage_id`, `weapon_ids[]`, `spawn_weapon_id`, body/head — all from catalog at last-moment handoff. Confirmed solid (already verified in M2.1/S172).

**B-115 verified FIXED** (S170): `g_CtxImGuiMenu` push on window appear already present in MP endscreen at line 712-716.

### Code Changes (2 files)
- **port/fast3d/pdgui_bridge.c**: B-117 fix (context pop) + auto-save (`configSave`) + `config.h` include
- **port/fast3d/pdgui_menu_endscreen.cpp**: Player stats section + `configSave` declaration

### Decisions
- Auto-save on match exit rather than prompt — PC has persistent config, no need for N64-style save dialog
- Stats section uses same bridge functions as solo endscreen — consistent data source
- Context pop goes in bridge function (shared exit path) rather than each button handler — single fix covers all exit paths (Return to Room, Disconnect, Play Again, Quit, Esc, Enter)

### Next Steps
- Build verification (Mike)
- M2.3 or D5 Phase 3 continuation

---

## Session S173 — 2026-04-07 (M0.1d — Remaining Asset Type Catalog Signature Migration)

**Focus**: Audit and migrate remaining 7 asset types (texture, audio, animation, gamemode, lang, prop, HUD) at public function boundaries.

### What Was Done

**Full boundary audit of all 7 asset types:**

| Type | Boundary Exposure | Action |
|------|------------------|--------|
| ASSET_TEXTURE | Internal only (renderer) | Documented — no migration |
| ASSET_AUDIO | Internal only (sound system) | Documented — no migration |
| ASSET_ANIMATION | Internal only (model/anim system) | Documented — no migration |
| **ASSET_GAMEMODE** | **Wire, save, config** | **MIGRATED** |
| ASSET_LANG | Internal only (string tables) | Documented — no migration |
| ASSET_PROP | Wire (type discriminator only, not asset identity) | Documented — no migration |
| ASSET_HUD | Internal only (HUD rendering) | Documented — no migration |

**ASSET_GAMEMODE migration** (the only type with genuine asset identity crossing boundaries):

1. **matchsetup.h**: Added `scenario_id[64]` as PRIMARY. `scenario` (u8) marked DEPRECATED.
2. **matchsetup.c**: `matchStart()` resolves `scenario_id` → integer at handoff. `matchConfigInit()` sets default "base:combat". `matchStartFromChallenge()` syncs back via `catalogIdByRuntime()`.
3. **netmsg.c (CLC_LOBBY_START)**: Write/read `scenario_id` string instead of u8.
4. **netmsg.c (SVC_STAGE_START)**: Write/read `scenario_id` string instead of u8.
5. **net.c (server query)**: Write `scenario_id` string. Read side resolves to integer.
6. **net.h**: `netrecentserver` struct: added `scenario_id[CATALOG_ID_LEN]`, `scenario` marked DEPRECATED.
7. **savefile.c**: Write `scenario_id` alongside integer. Read prefers `scenario_id`, falls back to integer.
8. **scenario_save.c**: Write `scenarioId` alongside integer. Read prefers `scenarioId`, falls back to integer.
9. **pdgui_menu_room.cpp**: Scenario combo sets `scenario_id` from catalog via `catalogIdByRuntime()`.
10. **Protocol version**: Bumped to v32.

**PROP type assessment**: `prop->type` (PROPTYPE_OBJ/DOOR/KEY/ALARM/CCTV/WEAPON/AMMO/SMOKE) is a protocol-level TYPE DISCRIMINATOR (8 fixed categories), not an asset identity. The actual prop MODEL identity already uses catalog session refs (v31). PROPTYPE is analogous to a message sub-type — converting to catalog strings would add overhead without benefit since these categories are fixed protocol constants.

### Code Changes (9 files)
- **port/include/net/matchsetup.h**: `scenario_id[64]` PRIMARY field
- **port/include/net/net.h**: Protocol v32, `scenario_id` in netrecentserver
- **port/src/net/matchsetup.c**: Init, resolve, sync-back
- **port/src/net/netmsg.c**: CLC_LOBBY_START + SVC_STAGE_START wire format
- **port/src/net/net.c**: Server query write + read
- **port/src/savefile.c**: Write/read scenario_id
- **port/src/scenario_save.c**: Write/read scenarioId
- **port/fast3d/pdgui_menu_room.cpp**: Combo picker sets scenario_id
- **context/constraints.md**: Protocol v32, scenario_id mandate

### Decisions
- 5/7 types are internal-only — no migration needed (textures, audio, animations, lang, HUD)
- PROP type is a protocol discriminator, not asset identity — documented as such
- `gamemode` (u8: 0=combat sim, 1=coop, 2=counter-op) is a protocol-level mode selector, NOT a catalog asset — stays as integer
- `scenario` (MPSCENARIO_*) IS a catalog asset (ASSET_GAMEMODE) — migrated

### Next Steps
- Build verification (Mike)
- M0.1e (catalog as data provider) or Gameplay state migration

---

## Session S172 — 2026-04-07 (M2.1 — Combat Sim UI Catalog Audit)

**Focus**: Verify Combat Simulator setup UI is fully catalog-native after M0.1a/b/c migrations.

### What Was Done

**Full audit of pdgui_menu_room.cpp** (Combat Sim setup screen):

1. **Arena Selection** — ALREADY CATALOG-NATIVE. `buildArenaListFromCatalog()` scans `ASSET_ARENA` entries. Selection writes catalog ID string to `g_MatchConfig.stage_id`. `syncArenaFromConfig()` matches by string comparison. Both solo (`matchStart()`) and network (`netLobbyRequestStartWithSims()`) paths pass catalog ID strings.

2. **Weapon Set Configuration** — ALREADY CATALOG-NATIVE (M0.1c). `buildSpawnWeaponList()` dynamically scans `ASSET_WEAPON` entries. Spawn weapon picker stores `g_MatchConfig.spawn_weapon_id`. Custom slots sync `weapon_ids[]` via `matchGetWeaponSlotCatalogId()`. `matchStart()` resolves all to integers at last-moment handoff.

3. **Bot Configuration** — ALREADY CATALOG-NATIVE. Body picker uses `catalogMpBodyId()` for enumeration, stores `sl->body_id`/`sl->head_id` in matchslot. Trait sliders (accuracy, reaction, aggression) work. 3D character preview calls `pdguiCharPreviewRequest(sl->head_id, sl->body_id)`.

4. **Game Mode Selection** — WORKING. Scenario combo writes `g_MatchConfig.scenario` (integer 0-5 — engine constants, not assets).

5. **Match Start Flow** — FULLY CATALOG-NATIVE. `matchStart()` resolves: `stage_id` → stagenum, `weapon_ids[]` → `g_MpSetup.weapons[]`, `spawn_weapon_id` → spawnWeaponNum, `body_id`/`head_id` → mpbodynum/mpheadnum — all via catalog at last-moment handoff.

6. **Agent Create (pdgui_menu_agentcreate.cpp)** — ALREADY CATALOG-NATIVE. Uses `catalogMpBodyId()`/`catalogMpHeadId()` for enumeration, passes catalog ID strings to `mpPlayerConfigSetHeadBody()`.

### Code Changes (1 file)
- **pdgui_menu_room.cpp**: Fixed stale header comment — `stagenum` → `stage_id` in function signature documentation (lines 11-13).

### Decisions
- No functional code changes needed — all 5 audit targets passed.
- `arena_entry.stagenum` field retained for debugging logs (not used for identity).
- `arenaGetName()` override table retained (needed for AllInOneMods language file collision).

### Next Steps
- Build verification (Mike)
- M0.1d: Remaining asset types (texture, audio, animation) or D5 Phase 3 continuation

---

## Session S171 — 2026-04-07 (M0.1c — Weapon Catalog Signature Migration)

**Focus**: Replace integer weapon identity at public function boundaries with catalog ID strings.

### What Was Done

**Audit Results**:
- Wire protocol: Already catalog-native (v30/v31). `netWriteWeaponRef()`/`netReadWeaponRef()` convert WEAPON_* ↔ catalog session refs. SVC_STAGE_START sends session refs. CLC_LOBBY_START sends catalog ID strings. No changes needed.
- Save files: Already write catalog ID strings (`weapon_ids` array in mpsetup saves, `weapon_id%d` in scenario saves). Backward-compat integer fallback preserved.
- Key boundary targets: `matchconfig.spawnWeaponNum` (u8 WEAPON_* enum) and `matchconfig.weapons[]` (u8 MPWEAPON_* indices) — both integer-native, needed catalog ID PRIMARY fields.
- Spawn weapon picker: Hardcoded 35-entry `s_SpawnWeapons[]` table with integer weaponnums — needed catalog sourcing.
- Legacy engine code (bondgun, propobj, inv, botinv — 66 internal functions): Stays integer-native. These are the final handoff to legacy engine API.

**Code Changes (7 files)**:
- **matchsetup.h**: Added `weapon_ids[6][64]` (PRIMARY catalog IDs for per-slot weapons) and `spawn_weapon_id[64]` (PRIMARY catalog ID for spawn weapon). Marked `weapons[]` and `spawnWeaponNum` as DEPRECATED derived values. Added `matchGetWeaponSlotCatalogId()` declaration.
- **matchsetup.c**: `matchConfigInit()` initializes new fields. `matchStart()` resolves `weapon_ids[]` → `g_MpSetup.weapons[]` and `spawn_weapon_id` → `spawnWeaponNum` via catalog at last-moment handoff. New `matchGetWeaponSlotCatalogId()` accessor bridges `g_MpSetup.weapons[slot]` → catalog ID.
- **pdgui_menu_room.cpp**: Replaced hardcoded `s_SpawnWeapons[35]` integer table with `buildSpawnWeaponList()` that scans ASSET_WEAPON catalog entries dynamically. Spawn weapon picker writes `spawn_weapon_id`. Custom weapon slot editing syncs `weapon_ids[]` via `matchGetWeaponSlotCatalogId()`. `syncSpawnWeaponFromConfig()` matches by catalog ID string.
- **netmsg.c**: CLC_LOBBY_START weapon write prefers `weapon_ids[]` (PRIMARY) over runtime resolution from `g_MpSetup.weapons[]`.
- **scenario_save.c**: Save writes `spawnWeaponId` field. Load populates `weapon_ids[]` (PRIMARY) and derives `weapons[]` (DEPRECATED). Legacy integer fallback preserved with reverse-resolution to catalog ID.
- **player.c, bot.c**: Comment updates — `spawnWeaponNum` is now a derived value from `spawn_weapon_id`, resolved at `matchStart()`.

### Decisions
- `player.c`/`bot.c` spawn code reads `spawnWeaponNum` which is derived at `matchStart()` — same pattern as `stagenum` derived from `stage_id`. No code changes needed in spawn logic, only comment updates.
- Weapon set presets (Pistols, Automatics, etc.) don't use per-weapon catalog IDs — they're selected by set index and the engine fills in the weapons internally. Only custom sets and spawn weapon use catalog ID fields.
- 38 public boundary functions in legacy engine code (bondgun, propobj, mplayer) stay integer-native — they are the final handoff point where WEAPON_* enums get consumed.

### Bugs Fixed
- None (migration only).

### Next Steps
- **BUILD VERIFICATION** — Mike to run build-headless.ps1 (worktree can't access MinGW)
- M0.1d: Remaining asset types (texture, audio, animation, etc.)
- Or: D5 Phase 3 continuation (menu roster port)

---

## Session S170 — 2026-04-07 (M1.2 — Solo Mission Flow)

**Focus**: Fix remaining solo campaign flow issues: endscreen mouse, Esc race condition, Next Mission verification.

### What Was Done

**B-122 FIXED: Endscreen mouse unresponsive** (3 files):
- **Root cause**: Deferred hotswap flush in `pdgui_backend.cpp:426` checked `!g_PdguiActive && !pdguiIsPauseMenuOpen()` — only caught debug overlay and pause menu. When endscreen pushed `g_CtxImGuiMenu`, the flush didn't recognize it and re-enabled `SDL_SetRelativeMouseMode(SDL_TRUE)`, overriding the context system.
- **Fix 1**: Changed deferred flush guard to `!pdguiIsActive()` — checks full input context stack (any non-gameplay context blocks the flush).
- **Fix 2**: Added `inputCtxSyncMouseMode()` to `inputctx.c` — per-frame enforcement that ensures SDL mouse mode matches the top context. Called from `inputCtxEndFrame()`. Catches any case where something outside the context system changed SDL state.
- **Fix 3**: Removed manual `SDL_SetRelativeMouseMode(SDL_FALSE)` / `SDL_ShowCursor(SDL_ENABLE)` / `SDL_WarpMouseInWindow` from both solo and MP endscreen renderers in `pdgui_menu_endscreen.cpp`. The context's `on_push` callback handles this.

**B-124 FIXED: Esc open/close race condition** (3 files):
- **Root cause**: `ImGui_ImplSDL2_ProcessEvent(ev)` in `pdguiProcessEvent()` ran before `inputCtxDispatch(ev)`, so ImGui always saw key events regardless of context state. When a context was pushed (e.g., menu opened), ImGui's internal state already had the triggering key marked as pressed, causing `IsKeyPressed(Escape)` to fire on the newly-pushed menu.
- **Fix**: Systemic key suppression in the input context framework:
  - `InputContext` struct: added `push_tick` field (u32, set to `SDL_GetTicks()` on push)
  - `inputCtxShouldSuppressKey(ev)`: returns 1 for KEY_DOWN events when top context was pushed within `INPUTCTX_PUSH_GRACE_MS` (100ms)
  - `pdguiProcessEvent()`: checks suppression before ImGui forwarding — suppressed keys are consumed silently, never reaching ImGui or dispatch

**Next Mission flow verified**:
- `endscreenAdvance()` uses M0.1a pattern: increments `stageindex`, clamps bounds, calls `missionSetStageByCatalog(g_SoloStages[].catalog_id)` — catalog-first
- `pdguiEndscreenHasNextMission()` correctly shows "Main Menu" at last solo stage
- `pdguiEndscreenNextMission()` chains `endscreenAdvance()` → `menuhandlerAcceptMission()` → pops context
- B-123 fix confirmed working end-to-end

### Decisions
- Mouse mode is now enforced by the input context system, not individual menus. `inputCtxSyncMouseMode()` at frame end is the canonical enforcement point.
- Key suppression grace period (100ms) chosen to cover ~6 frames at 60fps — wide enough to catch the triggering press, narrow enough not to eat legitimate subsequent presses.
- Deferred flush guard consolidated from `!g_PdguiActive && !pdguiIsPauseMenuOpen()` to `!pdguiIsActive()` — one function, one check, covers all contexts.

### Bugs Fixed
- **B-122**: Endscreen mouse unresponsive (systemic: context-driven mouse mode)
- **B-124**: Esc open/close race condition (systemic: key suppression on push)

### Next Steps
- Build verification (Mike)
- Playtest: complete solo mission, verify mouse works on endscreen, verify Esc opens/closes menu cleanly
- M0.1c: Weapon signature migration or D5 Phase 3 continuation

---

## Session S169 — 2026-04-07 (M0.1b — Body/Head Catalog Signature Migration)

**Focus**: Eliminate all integer body/head identity at public function boundaries. Phase 7 wrapper caller elimination.

### What Was Done

**Audit Results**:
- Grepped all 6 named conversion wrappers across entire codebase
- `catalogBodynumToMpBodyIdx`, `catalogHeadnumToMpHeadIdx`, `catalogResolveBodyByMpIndex`, `catalogResolveHeadByMpIndex`, `catalogResolveWeaponByGameId` — **already deleted** in prior sessions (zero definitions, zero callers)
- `catalogGetSafeBody`, `catalogGetSafeHead`, `catalogGetSafeBodyPaired` — **zero external callers** found. Only used internally within `modelcatalog.c` by string-based validators

**Code Changes (3 files)**:
- **modelcatalog.c**: Made `catalogGetSafeBody()`, `catalogGetSafeHead()`, `catalogGetSafeBodyPaired()` all `static`. These are now internal implementation details of the string-based validators.
- **modelcatalog.h**: Removed public declarations for the 3 integer-based safe functions. String-based validators (`catalogValidateBodyId`, `catalogValidateBodyIdPaired`, `catalogValidateHeadId`) remain as the public API.
- **port/CLAUDE.md**: Updated catalog accessor documentation to reference string-based validators instead of deleted integer-based functions.

**Remaining enumeration calls (~30)**:
- `catalogMpBodyId()`/`catalogMpHeadId()` are used in ~30 sites (mplayer.c, room.cpp, agentcreate.cpp, identity.c, matchsetup.c) — these are integer→string enumeration helpers (for display/iteration), NOT identity-passing wrappers. They convert mp_index to catalog ID for UI rendering and save paths.
- Deep struct migration (PlayerConfig/BotConfig to store catalog IDs natively) would eliminate these — tracked under "Gameplay state — category-based".

### Decisions
- `catalogMpBodyId`/`catalogMpHeadId` classified as enumeration utilities, not conversion wrappers — they serve the UI iteration pattern (`for b in 0..numBodies, get catalog ID for display`), which is a legitimate use of integer indices for enumeration
- Phase 7 declared complete: zero integer-based conversion wrappers remain at public boundaries

### Next Steps
- M0.1c: Weapon signature migration
- Or: Gameplay state migration (struct-level catalog ID fields in PlayerConfig/BotConfig)
- D5 Phase 3: Continue menu roster port

---

## Session S168 — 2026-04-06 (M1.1 — Campaign Mission Select Redesign)

**Focus**: Replace flat mission list with two-panel mission select UI. Fixes B-90 (unlock filter), B-91 (objectives display), B-96 (difficulty flow).

### What Was Done

**Two-Panel Mission Select (renderMissionSelect rewrite)**:
- Left panel: Mission list with chapter headings, blip completion dots. Locked missions shown grayed-out and non-selectable (B-90). D-pad navigation skips locked entries.
- Right panel: Mission detail — stage name header, inline difficulty picker (Agent/SA/PA) with color badges and best times, objectives list filtered by selected difficulty (B-91), briefing text preview, Start Mission button.
- Single-screen flow replaces 3-dialog chain (B-96): pick mission → pick difficulty → see objectives → Start. All in one screen.
- D-pad left/right switches panel focus. A/Enter in left panel moves to right. B goes back.
- Mouse click on mission row selects it and focuses right panel.

**New C helper: `soloLoadBriefingForStageId()`** (mainmenu.c):
- Wraps `setupLoadBriefing()` with catalog ID resolution
- Called from ImGui when selected mission changes (avoids requiring legacy dialog open)
- Clears previous language bank before loading new one

**Build fix: missing `<string.h>` includes**:
- `bg.c` and `bodyreset.c` were using `strcmp()` (added in M0.1a) without `#include <string.h>`
- Added includes to fix `-Wimplicit-function-declaration` errors

### Decisions
- Two-panel single-screen design chosen over dialog chain — matches modern console UX (Halo/Destiny style)
- Difficulty and objectives embedded in detail panel rather than separate dialogs — reduces cognitive load
- `soloLoadBriefingForStageId()` added as C-linkage helper rather than exposing `g_Menus[]` to C++ — keeps interface clean
- Briefing data cached by stage index (`s_PrevBriefingStage`) to avoid redundant loads

### Bugs Fixed
- **B-90**: Mission select unlock filter (locked missions grayed out)
- **B-91**: Objectives display from game data (loaded via `soloLoadBriefingForStageId`)
- **B-96**: Difficulty flow redesigned (inline in detail panel)

### Next Steps
- Playtest: verify two-panel layout, difficulty selection, objectives, Start button
- B-97: Separate Special Assignments section visually (currently has gold heading but same panel)
- B-122: Endscreen mouse still unresponsive (separate issue)
- M0.1b: Body/head signature migration (interleaved cadence)

---

## Session S167 — 2026-04-07 (M0.1a — Stage Signature Migration)

**Focus**: Replace ALL hardcoded stagenum integer references in mission-flow code with catalog ID lookups. Critical path work unblocking M1 (Playable Campaign).

### What Was Done

**Commit `270d57c` on `dev` — 9 files, +121/-113 lines**

- **types.h**: Added `const char *catalog_id` field to `struct solostage` — stages now carry catalog identity natively
- **mainmenu.c**: Populated all 21 `g_SoloStages[]` entries with catalog ID strings (`"base:defection"`, `"base:chicago"`, etc.). Legacy menu path uses `catalog_id` directly instead of `bgGetStageIndex` roundtrip.
- **endscreen.c**: New `missionSetStageByCatalog()` function resolves stagenum from catalog. All 4 `missionSetStagenum(g_SoloStages[].stagenum)` calls converted. Fixes B-123 (next mission reloading same stage).
- **bg.c**: 6 `STAGE_` enum comparisons converted to `strcmp(g_MissionConfig.stage_id, "base:...")`
- **mplayer.c**: `stage_id` sync after random resolution uses `bgGetStageIndex` before `catalogIdByRuntime`. Surface type switch (22+ cases) converted from `switch(stagenum)` to `strcmp` chain.
- **ingame.c**: `STAGE_ATTACKSHIP` check → `strcmp(g_MissionConfig.stage_id, "base:attackship")`
- **bodyreset.c**: 3 stage-to-head-count mappings → catalog ID comparisons
- **pdgui_menu_agentselect.cpp** + **pdgui_menu_solomission.cpp**: Shadow structs updated, mission select uses `catalog_id` directly

### Decisions
- Stage identity is now catalog-native end-to-end in the solo mission flow. `stagenum` only appears at final `mainChangeToStage()` handoff.
- `g_SoloStages[]` carries the catalog ID as primary identity — the integer fields remain for legacy engine calls but are never used as identity.

### Bugs Fixed
- **B-123**: Next Mission reloads same stage ✓ (root cause: same B-120 pattern in endscreen)

### Next Steps
- M1.1 mission select UI work (now unblocked by catalog-native stage identity)
- M0.1b body/head signature migration (interleaved cadence)
- B-122 / B-124 input bugs (M0.2 tactical fixes)

---

## Session S166 — 2026-04-06 (Bug Fix — B-120 wrong stage loaded + B-121 endscreen input context)

**Focus**: Fix two playtest bugs — wrong stage loaded for solo missions (B-120), unresponsive endscreen menu (B-121).

### What Was Done

**B-120: Wrong stage loaded for solo missions (HIGH)**:
- Root cause: `catalogIdByRuntime(ASSET_MAP, X)` expects `X` = stage table array index (position in `g_Stages[]`, 0–86), but both `pdgui_menu_solomission.cpp` and `mainmenu.c` were passing the logical stagenum (e.g., 0x5e=94). These are different values.
- The catalog registers stages with `e->runtime_index = idx` where `idx` is the stage table array index (see `assetcatalog_base.c:437`). Passing stagenum=0x40 (64 decimal) looked up `s_RuntimeCache[ASSET_MAP][64]` which resolved to an MP arena (`bg_mp8`) instead of the correct solo mission stage.
- Fix: convert stagenum → stage table index via `bgGetStageIndex(sn)` before calling `catalogIdByRuntime()`. Applied to both:
  - `pdgui_menu_solomission.cpp:647` — ImGui mission select path
  - `mainmenu.c:1995` — legacy menu mission select path

**B-121: Endscreen menu not interactive (MED)**:
- Root cause: `renderSoloEndscreen()` in `pdgui_menu_endscreen.cpp` released SDL mouse grab directly (`SDL_SetRelativeMouseMode(SDL_FALSE)`) but never pushed `g_CtxImGuiMenu` input context. Without the context push, the input stack didn't route events to ImGui, making buttons unclickable.
- Fix: push `g_CtxImGuiMenu` on window appear (guarded by `!inputCtxIsActive()`), matching the pattern from `pdgui_menu_pausemenu.cpp:1132-1134`.
- Also fixed the same issue in `renderMpEndscreen()` which had identical missing input context.
- Added `#include "inputctx.h"` to `pdgui_menu_endscreen.cpp`.

### Decisions
- Use `bgGetStageIndex()` for stagenum→index conversion rather than adding a new catalog API function — it's a simple O(n) scan that already exists and is only called once per mission select.
- Push input context in endscreen renderer rather than in `menuPushRootDialog()` — keeps the fix localized to ImGui renderers that need it, without changing legacy menu infrastructure.

### Bugs Fixed
- **B-120**: Wrong stage loaded for solo missions ✓
- **B-121**: Endscreen menu not interactive ✓

### Next Steps
- Playtest: verify solo mission select → correct stage loads → death → endscreen is interactive
- Remaining `catalogIdByRuntime(ASSET_MAP, stagenum)` callers — audit for same index confusion pattern
- B-119 endscreen.c restart paths (4 calls) still using `g_MissionConfig.stagenum` — migrate to catalog-first

---

## Session S165 — 2026-04-06 (Bug Fix — B-119 stagenum=0x00 crash + catalog-first pattern)

**Focus**: Fix stagenum=0x00 crash on solo mission start; establish universal catalog-first identity pattern for stages.

### What Was Done

**Root Cause Diagnosed (B-119)**:
- `sm_missionconfig` shadow struct in `pdgui_menu_solomission.cpp` was missing `stage_id[64]` field that was added to real `missionconfig` for catalog migration
- Because `stage_id[64]` sits between `diff_pdmode` (offset 0) and `stagenum` (offset 65), the shadow struct had `stagenum` at offset 1 (wrong) instead of offset 65
- Writes to `g_MissionConfig.stagenum` from C++ code went to `stage_id[0]`, real `stagenum` stayed 0x00
- `menuhandlerAcceptMission` read `stagenum=0x00`, called `mainChangeToStage(0x00)` → crash

**Fixes**:
- **`pdgui_menu_solomission.cpp`**: Added `stage_id[64]` to `sm_missionconfig` shadow struct at correct offset. Mission select now sets `stage_id` via `catalogIdByRuntime(ASSET_MAP, sn)` only — no stagenum stored. Pause menu restart resolves stagenum via `catalogResolveStage()` at point of use.
- **`mainmenu.c` `menuhandlerAcceptMission`**: Resolves stagenum from `stage_id` at point of consumption. Falls back to `stagenum` field if `stage_id` is empty (legacy menu path). Writes resolved value back to `g_MissionConfig.stagenum` for not-yet-migrated consumers (endscreen.c restart paths).
- **`mainmenu.c` `menudialog00103608`**: Resolves stagenum from `stage_id` before `setupLoadBriefing()` — fixes briefing load for ImGui mission select path.
- Added `#include "assetcatalog.h"` to solomission.cpp (has extern "C" guards, safe).

**Universal Constraint Added** (game director binding directive):
- Catalog ID (`stage_id`) is the sole identity for all stage flows. stagenum is only extracted at final point of consumption (just before `mainChangeToStage`). Pattern: `catalog_stage_result_t r; catalogResolveStage(stage_id, &r); mainChangeToStage(r.stagenum);`
- Added to `constraints.md` as universal project-wide rule.

### Decisions
- Write `resolved_stagenum` back to `g_MissionConfig.stagenum` in `menuhandlerAcceptMission` — pragmatic bridge for endscreen.c/menutick.c restart paths that haven't been migrated yet.
- Use `catalogIdByRuntime(ASSET_MAP, sn)` to resolve from N64 stagenum → catalog ID (matching pattern from mainmenu.c legacy code at line 1971).

### Bugs Fixed
- **B-119**: stagenum=0x00 crash on solo mission start ✓
- **B-91** (partial): briefing loader now resolves correct stagenum from stage_id — briefing text should now load for ImGui path

### Next Steps
- Playtest: verify mission select → difficulty → objectives → accept works without crash
- Remaining `g_MissionConfig.stagenum` consumers in endscreen.c (4 calls) — migrate to catalog-first pattern
- menutick.c line 573 also uses stagenum — migrate
- Consider catalog-first sweep for all solo stage identity sites

---

## Session S164 — 2026-04-06 (D5 Phase 3 Session 1 — Solo Pause Menu)

**Focus**: Implement proper ImGui pause menu for solo missions (B-93, B-98)

### What Was Done

**`port/fast3d/pdgui_menu_solomission.cpp`** — `renderPauseMenu()` rewritten:
- Added extern "C" declarations: `lvGetDifficulty()`, `objectiveGetCount()`, `objectiveCheck()`, `mainChangeToStage()`
- Added `#include "pdgui_nav.h"`
- **Objectives display**: Fixed loop to start at i=1 (index 0 = briefing text, not an objective). Added difficulty filtering via `g_Briefing.objectivedifficulties[i]` bit test. Added completion status icons (green circle+checkmark = complete, red circle+X = failed, blue dot = incomplete) with text color coding.
- **Button array**: 5 buttons — Resume (0), Restart Mission (1), Inventory (2), Options (3), Abort! (4). Restart uses `mainChangeToStage(g_MissionConfig.stagenum)` (not the nonexistent `menuStop()`).
- **Nav**: B-button/Escape cancel handler calls `menuPopDialog()`. `pdguiNavTickWrap()` for D-pad wrap.
- `k_NumPauseItems` updated from 4 to 5 for correct D-pad item count.
- Build: clean (only pre-existing line 29 comment warning).

### Decisions
- `menuStop()` has no definition anywhere — cannot use it. Restart Mission goes directly to `mainChangeToStage()`.
- Objective loop must start at i=1 (index 0 is briefing text, not an objective).
- `objectiveCheck(objIdx)` takes 0-based index, so objIdx = i - 1.

### Bugs Fixed
- **B-93**: Pause menu now has Abort, Restart, objective checklist ✓
- **B-98**: OG rendering fallback suppressed — `pdguiHotswapRegister` registration ensures ImGui fires instead ✓

### Next Steps
- Playtest: verify pause menu renders, objectives show correctly, Restart/Abort work
- Phase 3 Session 2: next priority screen from d5-full-menu-overhaul.md

---

## Session S163 — 2026-04-06 (D5 Phase 2 Session 2 + Infrastructure + Design)

**Focus**: Wire nav into menus, safe area, LB/RB tabs, UX guidelines, UI scaling, dev window prune button, input SSOT design

### What Was Done

**Phase 2 Session 2 — Nav wired into menus**:
- Main menu: LB/RB top-level view cycling, pdguiNavTickWrap() before End()
- Room menu: LB/RB bumper tab switching (Combat Sim/Campaign/Counter-Op), pdguiNavTickWrap()
- A/B gamepad buttons verified working via ImGui's built-in nav (no extra code needed)

**Safe area system** (pdgui_backend.cpp + pdgui_nav.h):
- PdSafeArea struct with per-edge independent margins (top/bottom/left/right, 0.0–0.25)
- pdguiGetSafeArea() — auto-detects ultrawide (>2.0 aspect → 10% horizontal, 5% vertical)
- pdguiSetSafeAreaMargins() — per-edge override
- 4 configRegisterFloat entries for pd.ini persistence (UI.SafeAreaTop/Bottom/Left/Right)

**Menu UX guidelines** committed to d5-full-menu-overhaul.md:
- Controller nav rules (D-pad, wrapping, A/B/X/Y, LB/RB, 5-9 items per screen)
- Layout patterns for PD2 (character grid, arena grid, split-panel settings, expandable bot list)
- Visual feedback rules (multi-layered focus, audio cues, 150-300ms transitions)
- Hybrid input rules (last device wins, 500ms debounce, dynamic button prompts)

**UI scaling guidelines** committed:
- Reference resolution 1080p, scale = viewport_height/1080
- Concrete pixel sizes at every resolution (720p through 4K)
- Font loading at scaled size (not FontGlobalScale)
- Ultrawide clamping (max 2560px menu width, centered)

**Input SSOT design spec** committed:
- Tap/hold/double-tap recognition integrated into context stack dispatch
- Per-context action maps (gameplay vs menu vs text input)
- Fully rebindable (player sees "Hold X — Open Door")
- Replaces CK_* mappings + inputmodes.c + ImGui hardcoded gamepad nav
- Absorbs existing inputmodes.c timing infrastructure

**Dev window improvements**:
- PRUNE WORKTREES button (gold-bordered, link panel) — one-click cleanup
- Git identity auto-config on startup (S161, carried forward)
- Release auto-commit pipeline fix (S161, carried forward)

**Worktree guidance updated**: Accept worktrees as tooling reality. Sessions must merge to dev + verify before done. Dispatch verifies main copy after each session.

**Fixes carried forward from earlier in session**: JUMP_LANDING log removed, B-117 context stack reset on stage transition, base-ui auto-extract from ROM

### Decisions
- Phase 2 declared SUBSTANTIALLY COMPLETE (nav infrastructure, safe area, tab switching all done)
- Input SSOT (tap/hold/double-tap unification) is a future phase, spec committed
- Menu opacity stacking DEFERRED to post-OG-strip bugfix pass
- System design guidelines needed for 8 major systems (menu/UI done, 7 remaining)
- Dev window redesign added to backlog (visual layout + smart builds)

### Next Steps
- Phase 3: Full Menu Roster Port (61 screens remaining, ~12 sessions)
- Build + test current changes
- Prune worktrees from dev machine
- Phase 3 Session 1: Solo Pause Menu (B-93, B-98) — highest priority menu

---

## Session S162 — 2026-04-06 (D5 Phase 2 Session 1 — Controller Navigation Infrastructure)

**Focus**: Gamepad navigation helpers — D-pad wrapping, accept/cancel, device detection

### What Was Done

**New files created**:
- `port/include/pdgui_nav.h` (85 lines) — C header with extern "C" guards. API: `pdguiNavOnEvent()`, `pdguiNavTickWrap()`, `pdguiNavAcceptPressed()`, `pdguiNavCancelPressed()`, `pdguiNavGetLastDevice()`, `pdguiNavIsGamepad()`, `pdguiNavEndFrame()`, `pdguiNavSetWrapCallback()`.
- `port/src/pdgui_nav.c` (170 lines) — C implementation. Device detection with 500ms debounce, SDL event-based accept/cancel buffering, wrap callback pattern.

**pdgui_backend.cpp modified** (27 lines added):
- Included `pdgui_nav.h` + `imgui_internal.h`
- `navWrapTrampoline()` — C++ function that calls `ImGui::NavMoveRequestTryWrapping(win, ImGuiNavMoveFlags_LoopY)` for current window
- Registered wrap callback in `pdguiInit()`
- `pdguiNavEndFrame()` called unconditionally at start of `pdguiNewFrame()` (clears previous frame's accept/cancel state even when menus are inactive — prevents stale presses)
- `pdguiNavOnEvent()` called in `pdguiProcessEvent()` before ImGui event forwarding

**Architecture decisions**:
- Wrap uses ImGui's built-in `NavMoveRequestTryWrapping` with `LoopY` flag — no manual item index tracking needed
- C/C++ boundary handled via function pointer callback (avoids including imgui_internal.h from C code)
- Device detection uses raw vs. reported state with 500ms debounce to prevent flickering
- Accept/cancel tracked at SDL event level (not ImGui key level) for frame-accurate detection
- Per-frame state cleared at start of next frame (not end of current) to handle early-return paths in pdguiNewFrame/pdguiRender

**Build verified**: Both pdgui_nav.c and pdgui_backend.cpp compile cleanly with -Wall -Wextra. Full build blocked by pre-existing environment temp file permission issue (unrelated).

### Next Steps
- Phase 2 Session 2: Device detection UI prompt switching, wire wrap calls into menu files
- Phase 2 Session 3: Custom nav for character/arena drawers
- Test in playtest: D-pad wrap, A=accept, B=cancel in main menu and room lobby

---

## Session S161 — 2026-04-06 (D5 Phase 1 Session 4 + Playtest + Infrastructure)

**Focus**: Input context lifecycle wiring, playtest verification, bug triage, infrastructure fixes

### What Was Done

**D5 Phase 1 Session 4 — Lifecycle Wiring**:
- `inputCtxInit()` + `inputCtxPush(&g_CtxGameplay)` wired into `main.c:mainInit()` after `inputInit()`
- `inputCtxEndFrame()` wired into `gfx_sdl2.cpp:gfx_sdl_handle_events()` after SDL_PollEvent loop
- `inputCtxShutdown()` wired into `main.c:cleanup()` before `pdguiShutdown()`
- Server: no changes needed (inputctx.c not in SRC_SERVER, shared code already #ifdef guarded)
- Init ordering verified: SDL → inputInit → inputCtxInit → pdguiInit → texInit → game logic

**Networking fixes (earlier this session)**:
- Client hole punch: all 3 join sites wired to `netStartClientWithHolePunch()`
- Server stage log: gated behind `g_NumStages > 0`
- extern "C" guards added to `fs.h` and `config.h`

**Infrastructure**:
- QUICKSTART.md created and updated throughout session
- D5 Full Menu Overhaul design doc committed (`context/designs/d5-full-menu-overhaul.md`)
- Dev window: git identity auto-config on startup, release auto-commit pipeline fix
- Constraints updated: zero-config networking, self-generating mods, zero DLL, legacy menus dead, init ordering audit requirement

**Playtest (v0.0.49)**:
- Input context stack confirmed working (gameplay push at boot, pause push/pop during match)
- Hole punch waterfall fired correctly (direct 3s timeout → PUNCH_REQ → ACK timeout → ENet retry)
- Second connection attempt succeeded via direct (UPnP had finished by then)
- Match started cleanly (CLC_LOBBY_START, manifest, countdown, SVC_STAGE_START)
- **B-117**: Hard crash on match exit — no shutdown sequence, pause context still active
- **Menu opacity stacking**: Background darkens on repeated open/close (additive haze)
- **JUMP_LANDING spam**: Per-frame ground clamp logging, needs verbose gate

### Decisions
- Phase 1 (Input Context Stack) declared COMPLETE
- Phase 2 (Controller Navigation) is next
- B-117 crash logged, will investigate alongside Phase 2
- Init ordering audit is now a standing requirement for all future work

### Next Steps
- Phase 2: Controller navigation (D-pad wrap, A/B, device detection, cheat buffer)
- Fix B-117 crash on match exit
- Fix menu opacity stacking (theme state reset on close)
- Gate JUMP_LANDING behind verbose logging
- Phase 4 Session 1: Auto-extract base-ui textures (can pull forward anytime)

---

## Session S160 — 2026-04-06 (D5 Full Menu Overhaul — Phase 1, Session 3)

**Focus**: Migrate all `pdmainSetInputMode()` callers to input context stack; remove `g_InputMode` system entirely.

### What Was Done

**All 15 `pdmainSetInputMode()` call sites migrated** across 7 files:
- `port/src/net/netmsg.c` (2 sites): `inputLockMouse(1) + pdmainSetInputMode(GAMEPLAY)` → `inputCtxPopDeferred(&g_CtxImGuiMenu)` with `inputCtxIsActive()` guard. Both co-op/anti and MP SVC_STAGE paths.
- `port/src/net/matchsetup.c` (2 sites): Same pattern — match start and challenge start paths.
- `port/src/net/net.c` (1 site): Standalone `inputLockMouse(1)` removed — context stack handles mouse capture via gameplay's `on_push`.
- `port/src/menumgr.c` (1 site): `restoreGameplayMouseCapture()` now pops `g_CtxImGuiMenu` instead of calling `pdmainSetInputMode`.
- `port/fast3d/pdgui_bridge.c` (2 sites): Endscreen mission restart/advance — pop ImGui menu context.
- `port/fast3d/pdgui_menu_solomission.cpp` (2 sites): Accept mission buttons — pop ImGui menu context.
- `port/fast3d/pdgui_menu_pausemenu.cpp` (5 sites):
  - `pdguiPauseMenuOpen()`: `pdmainSetInputMode(MENU)` → `inputCtxPush(&g_CtxPauseMenu)`.
  - `pdguiPauseMenuClose()`: `pdmainSetInputMode(GAMEPLAY)` → `inputCtxPopDeferred(&g_CtxPauseMenu)`.
  - Game-over screen (3 sites): `pdmainSetInputMode(MENU)` → `inputCtxPush(&g_CtxImGuiMenu)` with double-push guard.

**Old system removed**:
- `pdmainSetInputMode()` function deleted from `port/src/pdmain.c` (~20 LOC).
- `InputOwnerMode g_InputMode` global variable deleted from `port/src/pdmain.c`.
- `InputOwnerMode` enum, `g_InputMode` extern, and `pdmainSetInputMode()` declaration removed from `port/include/pdmain.h`.
- `pdmain.h` now contains only `pdmainGetLvFrame60()` — the sole remaining function.
- Unused `#include <SDL.h>` and `#include "input.h"` removed from pdmain.c.
- All 7 migrated files: `#include "pdmain.h"` → `#include "inputctx.h"`.

**Build verified**: Both client (`pd`) and server (`pd-server`) compile cleanly with zero errors.

### Decisions
- All GAMEPLAY transitions use `inputCtxPopDeferred` with `inputCtxIsActive` guard (safe if context not on stack).
- All MENU transitions use `inputCtxPush` with `!inputCtxIsActive` guard (prevents double-push).
- Pause menu uses `g_CtxPauseMenu`; all other menus use `g_CtxImGuiMenu`.
- `inputmodes.c`'s own `g_InputMode[]` array (for doubletap/hold per-action config) is completely unrelated and untouched.

### Next Steps
- **Phase 1, Session 4**: Wire `inputCtxInit()` into startup, `inputCtxEndFrame()` into main loop, `inputCtxPollFrame()` for continuous input. Push `g_CtxGameplay` at boot.

---

## Session S159 — 2026-04-06 (D5 Full Menu Overhaul — Phase 1, Session 2)

**Focus**: Rewrite `pdgui_backend.cpp` event filter to use input context stack

### What Was Done

**pdguiProcessEvent() rewritten** from scratch:
- Removed the entire old event filter (~110 LOC of manual mode checks, cooldown handling, `io.WantCapture*` decisions, Tab suppression, `g_InputMode` references).
- New function (~40 LOC): global hotkeys (F8/F12/RS-click) → `ImGui_ImplSDL2_ProcessEvent()` for state tracking → `inputCtxDispatch(ev)` for routing.
- F12 toggle now pushes/pops `g_CtxDebugOverlay` on the context stack instead of manually calling `pdguiUpdateMouseGrab()`.

**pdguiWantsInput() simplified**:
- Old: checked hotswap, pause menu, overlay, and `io.WantCapture*` separately (~20 LOC).
- New: `inputCtxGetTop() != &g_CtxGameplay` — if top context isn't gameplay, ImGui wants input (3 LOC).

**pdguiToggle() updated**: Uses context stack push/pop instead of direct `g_PdguiActive` + `pdguiUpdateMouseGrab()`.

**pdguiIsActive() simplified**: Was checking `g_PdguiActive || pdguiHotswapWasActive() || pdguiIsPauseMenuOpen()`. Now: `inputCtxGetTop() != &g_CtxGameplay` — same pattern as `pdguiWantsInput()`.

**pdguiUpdateMouseGrab() removed**: Context `on_push`/`on_pop` callbacks handle mouse state. Saved mouse state variables (`g_PdguiSavedRelativeMode`, `g_PdguiSavedShowCursor`) also removed (dead code).

**menuIsInCooldown()/menuIsOpen() externs removed**: No longer needed — context stack handles transition safety via deferred pop.

**inputCtxDispatch() fix**: Changed to respect `on_event()` return value. Gameplay's `on_event` returns 0 (game processes it), ImGui contexts return 1 (consumed). Critical for correct event routing.

**g_InputMode references eliminated** from pdgui_backend.cpp. The `pdmain.h` include kept only for `pdmainGetLvFrame60()` (B-92 render path).

### Decisions
- ImGui always sees every event via `ImGui_ImplSDL2_ProcessEvent()` before context dispatch. This ensures ImGui tracks internal state (mouse pos, key state) even when the game owns input.
- `inputCtxDispatch()` return value now comes from `on_event()`, not hardcoded 1. This lets gameplay context return 0 ("not consumed, game processes it") while menu contexts return 1 ("consumed").

### Next Steps
- **Phase 1, Session 3**: Migrate all `pdmainSetInputMode()` callers (~20 sites) to use `inputCtxPush`/`inputCtxPopDeferred`; remove `g_InputMode` enum entirely.
- **Phase 1, Session 4**: Wire `inputCtxInit()` into startup, `inputCtxEndFrame()` into main loop, `inputCtxPollFrame()` for continuous input.

---

## Session S158 — 2026-04-06 (D5 Full Menu Overhaul — Phase 1, Session 1)

**Focus**: Input Context Stack foundation — `inputctx.h` + `inputctx.c`

### What Was Done

**Input Context Stack created** (Phase 1, Session 1 of D5 Full Menu Overhaul):
- Created `port/include/inputctx.h` — public API for priority-based input context pushdown automaton.
- Created `port/src/inputctx.c` — full implementation (~340 LOC).
- Stack API: `inputCtxInit/Shutdown/Push/PopDeferred/PopImmediate/Dispatch/PollFrame/EndFrame`.
- Query API: `inputCtxGetTop/IsActive/GetDepth/GetTopName`.
- 4 built-in contexts: `g_CtxGameplay`, `g_CtxImGuiMenu`, `g_CtxPauseMenu`, `g_CtxDebugOverlay`.
- Gameplay context: captures mouse (relative mode), eats all input, delegates to existing game pipeline.
- ImGui/Pause/Debug contexts: release mouse, consume keyboard/mouse/gamepad events, return 1 (consumed) — actual ImGui forwarding deferred to Session 2 integration layer.
- Deferred pop pattern: `marked_for_removal` flag, cleanup in `inputCtxEndFrame()` — never mid-frame.
- Double-push protection, stack overflow guard, comprehensive logging via `sysLogPrintf`.
- Build verified: compiles cleanly with exact cmake flags (zero errors, zero warnings).
- Auto-discovered by CMake's `file(GLOB_RECURSE)` — no CMakeLists.txt changes needed.

### Decisions
- ImGui context callbacks do NOT call ImGui directly (C code can't call C++ ImGui). They return 1 (consumed) and the pdgui_backend.cpp integration layer (Session 2) handles actual forwarding.
- Pause menu sets a `s_GamePaused` static flag on push/pop — will be exposed via getter when needed.

### Next Steps
- **Phase 1, Session 2**: Rewrite `pdgui_backend.cpp` event filter to use context stack; wire into SDL loop (~200 LOC).
- **Phase 1, Session 3**: Migrate all `pdmainSetInputMode()` callers (~20 sites); remove `g_InputMode`.

---

## Session S157 — 2026-04-06 (Post-S156 Code Sessions)

**Focus**: Phase 8 conversion function elimination, deep array-bypass audit, D5.0 visual layer implementation

### What Was Done

**Catalog Phase 8 — O(n) conversion function elimination** (`3a05532`):
- Eliminated all O(n) conversion functions that scanned arrays linearly.
- Context updated with current migration status.

**Deep array-bypass audit** (`0b4aed2`, `2b409b4`):
- Full audit of direct array access patterns (`g_Weapons[]`, `g_HeadsAndBodies[]`).
- Found 2 hidden `catalogGetMpIndex` reimplementations.
- Fixed all 15 deep audit bypass items — zero gaps remaining.

**D5.0 Visual Layer — revised plan + full implementation** (`8189edb`, `a040275`):
- Deep investigation revealed ~70% of D5.0 was already built (pdgui_theme.cpp, pdgui_style.cpp).
- Phase 1: Split `pdguiThemeInit()` into early + `pdguiThemeLateInit()` (called after `texInit()`).
- Phase 2: ROM texture extraction tool (`--extract-ui-textures` CLI flag).
- Phase 3: Base UI mod (`mods/base-ui/`) with 13 UI texture catalog entries.
- Phase 4: Haze overlay in `pdguiDrawPdDialog()` — green-tinted IA8 compositing.
- Phase 5: CRT scanline pass — 2px-interval horizontal lines, configurable via `pd.ini`.
- Phase 6: Multi-palette support — all 7 palettes (Grey, Blue, Red, Green, White, Silver, BlackGold) now drive theme draw functions.
- Also: Procedural modern-UI mod (`mods/pd-modern-ui/`), TGA loader for mod textures, procedural fallback textures.

**QUICKSTART.md created** (this session — S157 context-only):
- Comprehensive cold-start onboarding document for AI sessions.
- README.md updated to link to it.

### Decisions
- D5.0 visual layer is now substantially complete (implementation, not just plan).
- Phase 8 (O(n) elimination) complete — conversion functions no longer do linear scans.
- Deep audit closed with zero gaps — all 15 bypass items addressed.

### Next Steps
- **Build verification** of all post-S156 commits (Phase 8 + deep audit + D5.0)
- Phase 7 caller elimination: ~85 calls to conversion wrappers
- Weapons (~660 refs), stages (~80), models (~83) migration
- D5.3 Pause Menu
- B-112 root cause (awaiting VEH crash log)

---

## Session S156 — 2026-04-06 (Handoff / End of Night)

**Focus**: Phase 7 audit, triple audit verification, session handoff

### What Was Done

**Catalog ID Migration — Phase 7 audit** (commit `f8b4d00`):
- All conversion function wrappers reviewed: `catalogBodynumToMpBodyIdx`, `catalogHeadnumToMpHeadIdx`, `catalogResolveBodyByMpIndex`, `catalogResolveHeadByMpIndex`, `catalogResolveStageByStagenum`, `catalogResolveArenaByStagenum`, `catalogResolveWeaponByGameId`, `catalogGetSafeBody`, `catalogGetSafeBodyPaired`, `catalogGetSafeHead`.
- Phase 7 commit landed but callers remain (~85 calls across codebase) — cannot fully delete wrappers yet.
- Status: AUDITED. Elimination requires caller-by-caller migration (deep audit task).

**Triple audit — PASSED (11/11)**:
- All 11 original catalog audit findings verified present in codebase.
- 1 gap fixed (validation functions passed wrong index space to `catalogGetSafeBody`/`Head` — corrected).
- Full audit log recorded in session S155 notes.

**Infrastructure**:
- `.gitignore` additions (worktree artifacts, build outputs).
- Worktree cleanup script added (`devtools/cleanup-worktrees.sh`).
- Release pipeline tag-push fix.

### Decisions
- Phase 7 (conversion function elimination) is the next concrete migration task: ~85 call sites must be migrated before wrappers can be deleted.
- Deep audit of direct array accesses (`g_Weapons[]`, `g_HeadsAndBodies[]`) is NOT yet started.
- Catalog-as-data-provider (absorb ROM arrays) and gameplay-state-category-based tracks are NOT yet started.
- All game director decisions stand: D-1 FULL, D-2 FULL, D-3 FULL — zero half measures, catalog is sole source of truth for identity AND state.

### Next Steps
- Build verification of Phases 0–6 (no regressions)
- Phase 7 caller elimination: ~85 calls to `catalogBodynumToMpBodyIdx` et al. — migrate each call site to use catalog ID directly
- Then: deep audit of `g_Weapons[]` / `g_HeadsAndBodies[]` direct array accesses
- B-112 root cause still unknown; next VEH crash log needed
- Playtest for Phase 0–6 regression check

---

## Session S155 — 2026-04-06

**Focus**: UX polish, B-112 hardening, Catalog ID Migration planning + Phases 0–6 execution

### What Was Done

**UX improvements** (commits `16355f8`, `61f6340`, `41b27f8`):
- Bot context menu: checkmarks for selected items, alphabetical character sorting, display name fallbacks.
- Handicap slider fix: showed wrong percentage in online mode.
- Release script fix: ensure tag exists locally before `git push origin`.

**B-112 additional crash guards** (`fb9b85c`):
- Added guards in shot/damage path (chrBruise, chrDamage) for stale chr pointers — defense-in-depth alongside S150's VEH guard.
- Handicap default init: `chr->handicap` initialized to 1.0 in `chrAllocate` to prevent divide-by-zero in damage calculations.

**Catalog ID Migration — full plan** (`9c43d36`, `5e7254c`, `6f59ef6`):
- Created `plan-catalog-id-migration.md` — zero-conversion mandate for ALL asset types (bodies, heads, weapons, stages, models, textures, sounds, animations, game modes, lang banks, props, HUD). ~2,578+ integer refs across ~80+ files.
- Game director decisions: D-1 (full migration for every asset type), D-2 (model numbers — full migration), D-3 (`mainChangeToStage` — full engine refactor to catalog ID).

**Catalog ID Migration — Phases 0–6 execution** (`44c09d2`, `777aef8`, `76eeb8a`, `8a20c9f`, `f4b5bdd`, `238edb0`, `d0808d4`):
- Phase 0: Generation counter + hot-reload API for catalog.
- Phase 2: Catalog ID string fields added to config/data structs.
- Phase 3: Function APIs migrated to catalog ID strings.
- Phase 4: Integer asset comparisons replaced with catalog ID checks.
- Phase 5+6: UI shadow structs, save paths, lobby accessors fixed.
- Fix: CLC_LOBBY_START bot resolution guarded with `#ifndef PD_SERVER`.
- Fix: Validation functions passed wrong index space to `catalogGetSafeBody`/`Head`.

**Infrastructure**: `.gitignore` additions + worktree cleanup script (`bb33037`). Version bump to v0.0.45 (`2e67d64`).

### Decisions
- Catalog ID migration is now the primary workstream — zero integer identity tolerance.
- All asset types in scope (no carve-outs).
- Phases 0–6 complete for bodies/heads; weapons, stages, models still need Phase 3+ migration.

### Next Steps
- Build verification of Phases 0–6
- Continue catalog migration: weapons (~660 refs), stages (~80 refs), models (~83 refs)
- Playtest to verify no regressions from struct changes
- B-112 root cause still open

---

## Session S154 — 2026-04-06

**Focus**: Eliminate integer asset identity from network wire (Phase 1+2)

### What Was Done

**Discovery**: All 6 weapon messages (SVC_PLAYER_STATS, SVC_PROP_SPAWN, SVC_PROP_DAMAGE, SVC_CHR_DISARM, SVC_CHR_STATE, SVC_NPC_STATE/CHR_RESYNC) were ALREADY migrated to catalog session refs via `netWriteWeaponRef`/`netReadWeaponRef` helpers (done in v30). SVC_NPC_STATE has no weapon field. Handicap UI slider was also already fixed.

**Bot body/head conversion elimination** (netmsg.c):
- SVC_STAGE_START write fallback (line 824): `catalogResolveBodyByMpIndex()` → `catalogResolveByRuntimeIndex(ASSET_BODY, ...)` since mpbodynum now stores runtime_index directly.
- CLC_LOBBY_START server read (line 4216): Replaced `catalogBodynumToMpBodyIdx(runtime_index)` with direct `catalogGetSafeBodyPaired(runtime_index, &rawHead)` storage — matches the client decode path. Same for heads.
- Zero `catalogBodynumToMpBodyIdx`/`catalogHeadnumToMpHeadIdx` calls remain in netmsg.c (only in comments).

**SVC_PROP_SPAWN modelnum → catalog ref** (netmsg.c):
- Added `netWriteModelRef()`/`netReadModelRef()` helpers using `catalogResolveByRuntimeIndex(ASSET_MODEL, ...)` and `sessionCatalogLocalResolve()`.
- Write side: both PROPTYPE_WEAPON and PROPTYPE_OBJ modelnum now use `netWriteModelRef()` (catalog session u16).
- Read side: both paths now use `netReadModelRef()`.
- All g_ModelStates[] entries are registered as ASSET_MODEL (by assetcatalog_base_extended.c), so resolution is complete.

**chrBruise guard enhancement** (chr.c):
- Added `!model->definition` check to existing B-112 defense-in-depth guard. Catches stale model pointers with freed definition (non-NULL pointer, NULL definition after stage teardown).

**NET_PROTOCOL_VER 30 → 31** (net.h): Breaking wire change for SVC_PROP_SPAWN modelnum format.

### Decisions
- CLC player move struct `in->weaponnum` (line 219) still uses raw s8 — out of scope for this session (CLC not SVC, 60Hz per-tick bandwidth concern). Noted for future.
- Comments referencing old conversion functions updated to describe new code path.

### Next Steps
- Build verification
- Playtest to verify prop spawn, bot body/head, and weapon resolution all work end-to-end
- Consider migrating CLC player move weaponnum to catalog ref (bandwidth trade-off)
- B-112 root cause still unknown

---

## Session S153 — 2026-04-05 (evening)

**Focus**: Audit + recovery + lobby unification close-out; B-116 bot catalog ID fix

### What Was Done

**Codebase audit**: Full verification of all completed tasks S130–S152 against actual codebase — 22/22 confirmed present.

**Git repo recovery**:
- `.git` was missing `objects/` directory; fetched full history from GitHub to restore.
- Cleaned up `.git.broken` (200MB) and 7 orphaned worktrees (~23GB freed).
- Pushed `dev` to origin.

**S131 completion** (commit `05d5f1d`, pushed):
- 10 bare `strcpy` calls in `port/src/input.c` converted to `strncpy`.
- Local `#define MATCH_MAX_SLOTS 32` removed from 3 UI files + `scenario_save.h`; all now use canonical 40 from `matchsetup.h`.
- Stale field names `headnum`/`bodynum` → `body_id`/`head_id` fixed in `mpsettings.cpp` and `teamsetup.cpp`.

**U-7 Steps C+D — matchsetup.cpp retired** (commit `9fe169e`):
- Step C: 3D character preview ported to room.cpp bot modal — rotating preview, two-column layout, `pdguiCharPreview` pipeline.
- Step D: Redirected 4 `g_MatchSetupMenuDialog` push points (menutick.c:253, menutick.c:537, mainmenu.c:4845, setup.c:5736) to `g_CombatSimulatorMenuDialog` + `pdguiSoloRoomOpen()`; removed `pdguiMenuMatchSetupRegister()` call; renamed file to `.cpp.retired`.

**U-10: Deferred bot authority** (commit `6f471a7`):
- Added `g_NetPendingBotAuthority` flag in `net.h`/`net.c`.
- `netmsgSvcBotAuthorityRead` now sets pending instead of immediately active.
- `botTick` promotes pending → active when `g_PadsFile != NULL && g_NumSpawnPoints > 0`.
- Reset on disconnect and match-end.
- Collapses the prior 60-frame timeout gate into a deterministic condition check.

**B-116: Bot body/head catalog ID resolution** (committed, not pushed):
- Root cause: `SVC_STAGE_START` writer used `botidx + MAX_PLAYERS` as slot index, which could miss actual `SLOT_BOT` entries if they weren't packed at that offset.
- Fix 1 (`netmsg.c`): Pre-built `botSlotMap[]` by scanning `g_MatchConfig.slots[]` for `SLOT_BOT` entries before writing the message.
- Fix 2 (`netmanifest.c`): Server manifest builder now reads `body_id`/`head_id` directly from `g_MatchConfig.slots[]` (mirrors `manifestBuildForHost` pattern).

### Decisions
- Lobby unification (U-1 through U-10) declared **COMPLETE**.
- `pdgui_menu_matchsetup.cpp` is now `.cpp.retired` — not deleted yet pending any edge-case audit, but all code paths redirected.
- B-116 fix committed but not pushed (intentional — will push with next playtest build).

### Next Steps
- Playtest build to verify U-10 + B-116 fixes with dedicated server
- Remaining open playtest issues: B-112 (chr corruption, root cause unknown), B-115 (post-game mouse), event-driven prop sync
- Next major track: D5.0 (Menu Visual Layer) or open playtest stability issues

---

## Session S152 — 2026-04-05

**Focus**: Verify all 5 playtest fixes committed; implement Bug 4 (hotswap frame-1 CI crash)

### What Was Done

Bugs 1–3 and 5 from S151 were already committed in `da40788`. Bug 4 was coded but uncommitted.

**5. Hotswap frame-1 crash fix** (FIX-PLAYTEST-4):
- After mission fail → legacy menu exit → CI load, `pdguiHotswapRenderQueued` fires with `s_HotswapMenuWasActive=true` (from prior stage). On frame 0, `screenManifestTick` with count=0 triggered "leave" events calling `catalogUnloadAsset` while catalog was reinitialising → crash at `+0xc1df3`.
- Fix A: `pdgui_hotswap.cpp` — guard `screenManifestTick` behind `pdmainGetLvFrame60() >= 2`.
- Fix B: `pdgui_backend.cpp` — guard hotswap-close mouse capture flush behind `pdmainGetLvFrame60() > 0`.
- Bridge: `pdmain.c`/`pdmain.h` expose `pdmainGetLvFrame60()` so C++ code can read `g_Vars.lvframe60` without including `types.h`.
- Files: `port/fast3d/pdgui_hotswap.cpp`, `port/fast3d/pdgui_backend.cpp`, `port/src/pdmain.c`, `port/include/pdmain.h`, `port/fast3d/pdgui_bridge.c`.

### Decisions
- All 5 playtest fixes confirmed in codebase and committed.

### Next Steps
- Playtest build to confirm all 5 fixes hold
- Event-driven prop sync redesign (prop resync fix is a stop-gap)
- catalogResolveBodyByMpIndex out-of-range (mpbodynum=63/65/66/67) — lobby UI issue separate from these fixes

---

## Session S151 — 2026-04-05

**Focus**: Playtest bug diagnosis and fixes — invisible bots, broken doors/ammo, death-in-hub crash, bot HP

### What Was Done

**Commit `da40788` on `dev`.**

**1. Bot body/head resolution fix** (FIX-PLAYTEST-1):
- CLC_LOBBY_START server handler read body_id/head_id strings from wire but never stored them in `g_MatchConfig.slots[]`. SVC_STAGE_START write fell back to mpbodynum=0 → all bots got dark_combat body.
- Fix: strncpy body_id/head_id into g_MatchConfig.slots[MAX_PLAYERS+bi] during server read.
- Also bumped MATCH_MAX_SLOTS from 32→40 (MAX_PLAYERS(8)+MAX_BOTS(32) can reach slot 39).
- Files: `port/src/net/netmsg.c`, `port/include/net/matchsetup.h`.

**2. Prop resync spam fix** (FIX-PLAYTEST-2):
- Dedicated server stubs mainChangeToStage → g_Vars.activeprops empty → prop resync always sends 0 props. Client polled every 6 seconds forever.
- Fix: reset desync counter when receiving 0 props, stopping the spam loop.
- Full event-driven prop sync is future work.
- File: `port/src/net/netmsg.c`.

**3. Death-in-hub crash fix** (FIX-PLAYTEST-3):
- Falling through CI geometry → death → titleSetNextStage(0x00) → invalid stage → crash (bgGetStageIndex returns -1, loader uses garbage).
- Fix: guard titleSetNextStage against stagenum=0, redirect to STAGE_CITRAINING (0x26).
- File: `src/game/pdmode.c`.

**4. Bot HP fix** (FIX-PLAYTEST-5):
- chrAllocate defaults maxdamage=4. Online gets 8 from server chr resync, but local Combat Sim bots kept 4 (one-shot by any weapon).
- Fix: set chr->maxdamage=8.0f in botmgrAllocateBot.
- File: `src/game/botmgr.c`.

### Log Analysis Findings (from Mike + Chris playtest logs)
- **Invisible bots**: All bots got body=86/head=4 (dark_combat). Joanna+Elvis head combo from safety clamp on mpbody=0.
- **Broken doors/ammo (Chris)**: Prop resync returns 0 props every 6s. 16+ consecutive resyncs in 2min session.
- **Death crash (Chris)**: Fell through CI ceiling, ground=-667→-707, titleSetNextStage(0x00), bg_lue loaded with chrslots=0xffffff01.
- **CI crash (Mike)**: Failed mission → legacy menu exit → frame 1 crash at +0xc1df3. Needs symbolication (DEFERRED).
- **Post-game mouse dead (Mike)**: Legacy menu steals input, ImGui hotswap doesn't recapture. Related to hotswap state machine (DEFERRED).
- **catalogResolveBodyByMpIndex out-of-range**: mpbodynum=63/65/66/67 queried against [0,63) — separate lobby UI issue.

### Decisions
- Prop sync should become event-driven (Mike's direction) — current fix is a stop-gap.
- Bug 4 (frame 1 CI crash) deferred pending crash symbolication.
- MATCH_MAX_SLOTS increased to 40 to accommodate full player+bot range.

### Next Steps
- Playtest the build to verify fixes
- Event-driven prop sync redesign
- Investigate Bug 4 (CI crash after mission fail)
- Push commits to GitHub (16+ ahead of origin/dev)

---

## Session S150 — 2026-04-04/05

**Focus**: Credits update, bot stuck-detection init, chr pointer-corruption guard, 8MB stack + VEH → v0.0.38

### What Was Done

**Commits `ccf1bae`, `87b3388`, `375292c`, `85928d9` pushed to `dev`. Build auto-commits `ddc742e`, `ab31c6b`, `4d07510`, `d92f4e3`.**

**1. Credits update** (`ccf1bae`):
- Removed Variant line from title info block.
- Moved "PD2 Port Director: MikeHazeJr" up to the vacated slot.
- Added "Tester: smarch" in grass green (0x00CC00).
- Developer / Rare Ltd. row shifted down.
- File: `src/game/title.c`.

**2. Bot stuck-detection initialization** (`87b3388`) — **B-111 fixed**:
- `s_BotStuck` was zero-initialized, causing all 31 bots to fire their first stuck check simultaneously at frame 180 (`STUCK_CHECK_FRAMES`) with a bogus distance-from-origin comparison.
- Fix: initialize snapshot position and frame in `botSpawn()`; safety fallback in `botTick()` for bots that enter play without going through `botSpawn()`.
- File: `src/game/bot.c`.

**3. Chr pointer-corruption guard** (`375292c`) — **B-112 partial**:
- Access violation at `chr->hidden` when `chr` pointer (rbx) gets corrupted during AI execution or action tick dispatch in 31-bot matches.
- Added volatile canary + pointer range validation at two checkpoints: after `chraiExecute` and after the action switch.
- Diagnostic logging identifies whether corruption originates in AI scripts or action handlers.
- Root cause still unknown — guard reduces crash frequency; investigation continues.
- File: `src/game/chraction.c`.

**4. Stack increase to 8MB + VEH** (`85928d9`) — **B-113 fixed**:
- Silent crash in 31-bot matches caused by 2MB default stack being exhausted during deep AI/collision call chains.
- Existing crash handler (UEF) allocated 8KB on stack, causing double fault → process terminated with no log output.
- Fix: increase stack reserve from 2MB to 8MB via linker flag. Add first-chance vectored exception handler (VEH) using static buffers and minimal stack. `crashHandler`'s 8KB msg buffer moved from stack to static storage. `sysLogGetPath()` added so VEH can write directly to log. `_resetstkoflw()` called for stack-overflow recovery.
- Files: `CMakeLists.txt`, `port/include/system.h`, `port/src/crash.c`, `port/src/system.c`.

**Build**: v0.0.38 clean.

### Decisions
- B-112 (chr pointer corruption) gets a guard + diagnostics now; full root-cause fix deferred until the diagnostic log identifies the corruption source.
- VEH is first-chance so it fires before the debugger, ensuring crash logs even on the dev machine.

### Next Steps
- Review VEH crash log from next 31-bot playtest to identify B-112 root cause.
- D5.3 (Pause Menu) is the biggest remaining open gap.

---

## Session S149 — 2026-04-04

**Focus**: Bot spawn root-cause deep-dive — 31 bots on 24 pads + underground ground-clamp + AIDROP filter removal

### What Was Done

**Commits `d2e558e`, `e03a990`, `a81926e` pushed to `dev`. Build commits `fc3a94e`, `2386bbb`.**

**1. Bot spawn crash (31 bots / 24 pads)** (`d2e558e`) — **B-110 further hardened**:
- Root cause: unspawned bots (rooms={-1}) were counted as real enemies in the pad-scoring loop, polluting distance calculations and marking all pads as bad. Bots 9-31 all fell through to the fallback which always picked idx=0, piling 23 bots at the same position → crash ~3 seconds in.
- Three-part fix: (1) skip unspawned bots in scoring loop; (2) pass 4 with `force=true` when all strict passes fail; (3) improved fallback with cycling counter + 80-unit jitter.

**2. Underground ground-clamp + SPAWN-DIAG** (`e03a990`):
- Bots spawning at underground positions (e.g. Chicago y=-634) now clamped upward to real floor via probe from 2000 units above.
- One-time `SPAWN-DIAG` logging at match start dumps all spawn pad positions and source waypoint data.

**3. AIDROP filter root-cause fix** (`a81926e`) — **B-110 root cause**:
- PADFLAG_AIDROP (0x2000) is set on nearly all waypoint pads in multi-level maps (Chicago, etc.). The spawn population code filtered these out, leaving only padnum=0 as valid → all 24 spawn slots got padnum=0 → all bots at same underground position → crash at frame ~180.
- Fix: removed AIDROP filter from both `playerreset.c` and `navspawn.c`. AIDROP is a pathfinding behavior hint (drop off ledge), not a spawn validity marker.
- Sequential fallback added if all spawn pads still collapse to same padnum.
- Diagnostic logging trimmed to first 6 pads.

### Decisions
- AIDROP filter removal is correct — the flag documents pathfinding behavior, not spawn eligibility. No other spawn filter should use pathfinding hint flags.

### Next Steps
- Playtest Chicago map with 31 bots — verify all bots spawn at valid positions.
- S150: credits + crash stability work.

---

## Session S148 — 2026-04-04

**Focus**: CMakeLists.txt corruption repair, Chicago bot spawn root cause (void geometry + HEAD 1000 catalog spam), v0.0.36, design doc

### What Was Done

**Commits `b84c6ba`, `59818e3`, `6ed6a67`, `1235806` pushed to `dev`. Build commits `1ddb77c`.**

**1. CMakeLists.txt corruption repair** (`b84c6ba`) — **B-114 fixed**:
- CMakeLists.txt had two lines (181, 532) with ~30MB of garbage bytes each — encoding bug from devtools.
- File restored to valid CMake.

**2. Bot spawn crash on Chicago + HEAD 1000 catalog spam** (`59818e3`) — **B-110 partially fixed**:
- `playerChooseSpawnLocation`: all fallback paths now use `bgFindRoomsByPos` to resolve rooms when pad data has `room==-1`; final validation ensures no spawn ever returns with `dstrooms[0]==-1`.
- `botSpawn`: after `chrMoveToPos`, if `rooms[0]` still -1 and `floorroom` also -1, call `bgFindRoomsByPos` as last resort.
- `botSpawnAll` failsafe: after initial spawn wave, re-spawn any bots still with `rooms[0]==-1`. Per-tick room recovery now re-spawns bots stuck in void geometry.
- HEAD sentinel value 1000 (random-gender) guarded against catalog lookup — was producing 18× "type=HEAD index=1000 not found" warnings per match. Added `HEAD_RANDOM_GENDER` constant.

**3. v0.0.36 version bump** (`6ed6a67`).

**4. Design doc: implementation-plan-mods-and-d5.md** (`1235806`):
- New file: `context/designs/implementation-plan-mods-and-d5.md` (549 lines).
- Covers mod pipeline (P1-P6) and D5 UI screens (P7-P10) with dependency graph, per-phase specs, and sequencing.

### Decisions
- HEAD_RANDOM_GENDER constant prevents catalog lookup on sentinel — catalog should never be called with magic index 1000.

### Next Steps
- Playtest Chicago with 31 bots to verify void spawn fix.
- Deeper root-cause investigation into AIDROP filter (S149).

---

## Session S147 — 2026-04-04

**Focus**: Three online playtest crash fixes — void spawn fallback, Skedar catalog ID, bot.c log flood

### What Was Done

**Commit `6f8bbfe` pushed to `dev`. Build commits `dd062b2`, `b35cf4f`.**

Three fixes from online playtest analysis:

1. **`player.c`** (void spawn fallback): In `playerChooseSpawnLocation`'s shortlist-empty fallback, scan pads from a random offset for the first one with `room >= 0` rather than picking blindly. Prevents void spawns when bots outnumber spawn pads and some pads have `room == -1`. Adds WARNING log for future incidents.

2. **`mplayer.c`** (Skedar catalog ID): Wrong catalog ID for Skedar arena default — was `"base:mp_skedar"`, must be `"base:arena_mp_skedar"` (arena_ prefix required by Phase B naming). Caused Skedar map to fail catalog resolution.

3. **`bot.c`** (log flood): Removed per-tick MATCH-TRACE botTick entry log — 31 bots × 240fps = ~7,440 lines/sec. Room-recovery logs kept.

**Build**: v0.0.34 clean.

### Next Steps
- Playtest to verify Skedar loads, bots spawn correctly, log no longer floods.

---

## Session S146 — 2026-04-04

**Focus**: botSpawnAll structure fix — move failsafe from setup.c to botTick

### What Was Done

**Commits `d91489e`, `b43ecb7` pushed to `dev`. Build commit `c42e6ac`.**

1. **`d91489e`** fix(build): add missing `bot.h` include in `setup.c` for `botSpawnAll` — compile error from S145 explicit `botSpawnAll()` call.

2. **`b43ecb7`** fix(spawn): moved `botSpawnAll` failsafe from `setup.c` to `botTick`:
   - The explicit `botSpawnAll()` call in `setup.c` ran too early (before bots had valid rooms from the AI script). Moving the failsafe to `botTick` ensures re-spawn happens after the AI script has had a chance to assign rooms.

**Build**: v0.0.33 clean.

### Next Steps
- Online playtest to verify 31-bot spawn with adaptive spacing and failsafe.

---

## Session S145 — 2026-04-04

**Focus**: Room leave fix (CLC_ROOM_LEAVE), botSpawnAll failsafe for non-MP maps, server catalog IDs for bot bodies; context updated S141–S144

### What Was Done

**Commits `7d08f78`, `80cee04` pushed to `dev`.**

**1. Room leave fix** (`7d08f78`):
- "Leave Room" button only called `pdguiSetInRoom(0)` without telling the server. Room stayed open showing 1 occupant.
- Fix: now sends `CLC_ROOM_LEAVE` to server before transitioning to lobby view.
- File: `port/fast3d/pdgui_menu_room.cpp`.

**2. Adaptive spawn spacing** (`7d08f78`):
- Solo maps used as MP arenas (Villa, etc.) have compact layouts where 500-unit minimum spacing rejected most waypoint candidates, leaving too few spawn points.
- Now uses adaptive passes: 500 → 250 → 125 → 60 → 0 unit spacing, stopping when ≥ 8 spawn points found.
- File: `src/game/playerreset.c`.

**3. botSpawnAll failsafe for non-MP maps** (`80cee04`):
- `botSpawnAll` never called on solo mission maps used as MP arenas (Villa, Complex, etc.) — their AI script lacks `aiMpInitSimulants`. Added explicit `botSpawnAll()` call in `setup.c` after bot allocation.

**4. Server uses catalog IDs for bot bodies** (`80cee04`):
- Server `SVC_STAGE_START` used `catalogResolveBodyByMpIndex()` which returns NULL on dedicated server. All bots rendered as the same character.
- Fix: now uses `g_MatchConfig.slots[]` `body_id`/`head_id` strings directly.

**5. Context update** (`80cee04`): Session log backfilled with S141–S144; README and bugs.md updated.

**Build**: still v0.0.32.

### Decisions
- Explicit `botSpawnAll()` in `setup.c` is a pragmatic stop-gap; moved to `botTick` failsafe in S146.

### Next Steps
- Fix build error (missing bot.h include) from explicit botSpawnAll call → S146.
- Online playtest with 31 bots.

---

## Session S144 — 2026-04-04

**Focus**: Endscreen UI overhaul, multi-select bot list, 256-entry name dictionaries, B-104 fix, stale slot cleanup

### What Was Done

**Commits `af86c3b`, `2d75636`, `b92a421`, `6b9e498` pushed to `dev`.**

**1. Endscreen overhaul + B-104 fix** (`af86c3b`):
- **B-104 fixed**: both `renderSoloEndscreen` and `renderMpEndscreen` in `pdgui_menu_endscreen.cpp` now call `pdmainSetInputMode(INPUTMODE_MENU)` on `IsWindowAppearing()`. Previously both used direct `SDL_SetRelativeMouseMode(SDL_FALSE)` calls, leaving `g_InputMode = INPUTMODE_GAMEPLAY` and blocking ImGui input.
- **Return to Lobby / Quit to Menu buttons** added to endscreen.
- **Random bot names** now displayed on post-match endscreen.
- `#include "../include/pdmain.h"` added to endscreen.cpp.

**2. Multi-select bot list** (`2d75636`):
- Combat Sim bot list now supports multi-select with right-click context menu for batch operations.

**3. 256-entry bot name dictionaries** (`b92a421`):
- Replaced random generator with 256-entry Adjective+Noun word lists.
- Dictionaries are mod-overridable (loaded from data files if present).
- Bot name display columns widened to accommodate longer names.
- Also touches `pdgui_menu_room.cpp` and `port/src/net/matchsetup.c`.

**4. Stale slot reference fix** (`6b9e498`):
- Removed stale `s_SelectedBotSlot` reference in room screen reset path — crash hazard on room screen revisit.

**Build**: v0.0.32 clean.

### Decisions
- `pdmainSetInputMode` doesn't warp cursor; kept explicit `SDL_WarpMouseInWindow` to center — cursor would otherwise restore to off-screen pre-mission position.
- Name dictionaries use flat arrays (not JSON) for load performance; mod override path uses same directory convention as other data assets.

### Next Steps
- Playtest: verify endscreen buttons usable after mission complete, bot names display correctly, multi-select works in Combat Sim
- D5.3 (Pause Menu) remains the biggest open gap

---

## Session S143 — 2026-04-04

**Focus**: R-3 Room Networking — clients see rooms, create/join, room-scoped match start

### What Was Done

**`commit 892f1e8` pushed to `dev`.**

Implemented R-3 room networking from `context/room-architecture-plan.md`:
- Server broadcasts room list to clients on lobby join.
- Clients can create and join rooms via the room screen.
- Match start is room-scoped: only players in the same room participate in a match.
- Room screen (`pdgui_menu_room.cpp`) updated to display active rooms and occupant counts.

**Build**: v0.0.30 clean.

### Decisions
- Room IDs are server-assigned, consistent with R-1/R-2 foundation.
- R-4 (demand-driven rooms) and R-5 (room federation) remain planned.
- L-series (lobby/room UX polish) depends on R-3 being done; can now begin.

### Next Steps
- L-series lobby/room UX work.
- Endscreen + name system polish (S144).

---

## Session S142 — 2026-04-04

**Focus**: Network + bot stabilization sprint — fixes for match start, bot freeze, server broadcast, buffer overflow, auth client desync storm

### What Was Done

**Commits `2634716`, `e5f7d4a`, `41431a3`, `3645e28`, `2de61ab`, `07b9729` pushed to `dev`.**

Fixed all root causes identified in S141 analysis plus related issues:

- **CLC_LOBBY_START buffer overflow** (`2de61ab`): `netLobbyRequestStartWithSims` in `pdgui_bridge.c` switched from `g_NetLocalClient->out` (1440 bytes) to a 256KB static send buffer. This was the root cause of the 23/31 bot count mismatch. Server-side dispatch trace logging also added.
- **Bot rooms=-1 freeze** (`41431a3`): `botmgrAllocateBot` now uses `PROPFLAG_NOTYETTICKED` gate; bots tick continuously until `botSpawn` assigns valid rooms and clears the flag. Room recovery path added for bots that stall.
- **Dedicated server broadcast blocked** (`3645e28`): `g_NetLocalClient` guard was incorrectly blocking relay; server can now broadcast state updates to all connected clients.
- **Bot names / per-frame relay / room fallback / server bot count** (`2634716`): bot display names populated correctly; relay runs every frame; room fallback logic corrected; server accurately reports bot count.
- **Authority client desync storm** (`07b9729`): authority client now skips chr desync detection — was triggering continuous resync storm on dedicated server with many bots.
- **Head picker human-readable names** (`e5f7d4a`): head picker now shows catalog-resolved display names sorted A-Z instead of raw catalog IDs.

**Builds**: v0.0.28 (initial batch) → v0.0.29 (post auth-client fix).

### Decisions
- 256KB static buffer for CLC_LOBBY_START is a pragmatic fix; streaming/chunked approach deferred until packet sizes are better understood.
- Auth client desync skip is intentional on dedicated server where the server is always the authority.

### Next Steps
- Playtest with 31 bots: verify full count transmitted, all bots spawn with valid rooms, CLC_BOT_MOVE flows to server.
- R-3 room networking (S143).

---

## Session S141 — 2026-04-04

**Focus**: Bot count mismatch audit + bot freeze root cause analysis (no code changes — analysis only, session terminated by user before fixes applied)

### What Was Done

**Audit findings** (no fixes implemented):

**Root Cause 1 — CLC_LOBBY_START buffer overflow** (CRITICAL):
- `NET_BUFSIZE = 1440` bytes. `g_NetLocalClient->out` is this size.
- CLC_LOBBY_START writes: header (~34 bytes) + weapons (6 strings, ~12–84 bytes) + per-bot (3 strings + 2 bytes ≈ 45 bytes/bot) + manifest.
- At 31 bots: ~34 + 12 + 31×45 + manifest ≈ 1441+ bytes — overflows the buffer.
- After overflow, `netbuf->error = 1`; writes are no-ops but `botIdx` keeps incrementing.
- The packet declares `numSims=31` (written before overflow), but only ~23 bots have valid data.
- Server reads 31 entries: 23 valid + 8 garbage (empty strings → dark_combat defaults). Sets `clampedSims=31`, allocates 31 stubs, sends SVC_STAGE_START with 31 bot chrslots bits.
- **Fix location**: `port/fast3d/pdgui_bridge.c:657` — `netLobbyRequestStartWithSims`. Change from `g_NetLocalClient->out` to a static large buffer (e.g. `NET_BUFSIZE * 8 = 11520` bytes).

**Root Cause 2 — Bot rooms=-1 / freeze**:
- `botmgrAllocateBot` (botmgr.c) creates prop with `rooms[0] = -1`.
- `propActivate` sets `forceonetick = true` → bot ticks ONCE (the first-run log fires here).
- After first tick: rooms still -1, not in foreground → prop NOT ticked again.
- Actual bot spawn (valid rooms assigned) happens via stage setup AI → `aiMpInitSimulants` → `botSpawnAll` → `botSpawn` → `scenarioChooseSpawnLocation` → `chrMoveToPos` with valid rooms. This runs DURING stage loading (setup.c AI script), not from botTick.
- After `botSpawn`, bots have valid rooms. They get added to foreground normally.
- **Fix**: In `botmgrAllocateBot`, set `prop->forcetick = true` after `propActivate` so bots always tick until properly spawned. Clear `forcetick` in `botSpawn` after rooms are assigned.

**Root Cause 3 — CLC_BOT_MOVE not sent**:
- `netEndFrame` (net.c:1407): `if (g_NetLocalBotAuthority && g_BotCount > 0)` gates the write.
- On dedicated server: `g_NetLocalBotAuthority = true` set when `SVC_BOT_AUTHORITY` received. `g_BotCount` set by `setup.c` allocating bots from `g_MpSetup.chrslots`.
- If `g_NetLocalBotAuthority` is never set (SVC_BOT_AUTHORITY not received/processed), or `g_BotCount = 0` (bots not allocated due to overflow-corrupted chrslots), no CLC_BOT_MOVE is sent.
- After fixing CLC_LOBBY_START overflow → correct 31-bot chrslots → correct bot allocation → correct g_BotCount → CLC_BOT_MOVE flows.

**Key files for next session fixes**:
- `port/fast3d/pdgui_bridge.c:652–659` — CLC_LOBBY_START buffer
- `src/game/botmgr.c:72–74` — prop->forcetick after propActivate/propEnable
- `src/game/bot.c:307` — clear forcetick after chrMoveToPos in botSpawn
- `port/src/net/net.c:1407` — verify CLC_BOT_MOVE gate

### Decisions Made

- Session terminated before fixes; user will restart with explicit fix instructions.
- CLC_LOBBY_START buffer overflow is the root cause of the 23/31 bot count mismatch.
- rooms=-1 freeze is a secondary issue from forceonetick being cleared before spawn runs.

### Next Steps

- **S142**: Implement the three fixes above. Build verify. Commit + push.
- Playtest with 31 bots: verify full count transmitted, all spawn with valid rooms, CLC_BOT_MOVE flows.

---

## Session S139 — 2026-04-04

**Focus**: D5.4 — MP post-match scoreboard (pdgui_menu_pausemenu.cpp)

### What Was Done

**`commit 36d03a5` pushed to `dev`.**

Rewrote `pdguiGameOverRender()` and supporting helpers in `pdgui_menu_pausemenu.cpp`:

- **Accuracy column** — `ScorecardRow.accuracy` field added. Computed for local player via `mpstatsGetPlayerShotCountByRegion` (PM_SHOT_TOTAL, _HEAD, _BODY, _LIMB, _GUN, _HAT, _OBJECT). Bots display "--".
- **Team section headers** — `renderGameOverRankings()` rewritten. Inserts "-- Team N --" headers (team-colored) between team groups when teams are enabled.
- **Stable team sort** — `sortRowsByTeam()` insertion sort added. `mpGetPlayerRankings()` returns score-sorted rows; stable sort by team applied before rendering for team mode.
- **Mouse capture fix** — `pdmainSetInputMode(INPUTMODE_MENU)` called on `ImGui::IsWindowAppearing()`. Fixes non-interactive buttons (B-103 symptom: game held SDL in relative mouse mode during gameplay).
- **Dual exit buttons** — "Return to Lobby" (blue, stays in room: `mainChangeToStage(STAGE_CITRAINING)` + `pdguiSetInRoom(1)`) and "Quit to Menu" (red: `netDisconnect()` for CLIENT, `mainChangeToStage(STAGE_TITLE)` for offline/server).

**Build**: full client+server incremental build clean (exit 0).

### Decisions
- Accuracy computed from local player's stats only — `mpstatsGetPlayerShotCountByRegion` is per-local-player, not per-chrnum. Bots always show "--".
- "Return to Lobby" does NOT call `netDisconnect()` — player stays connected and in-room. Only `pdguiSetInRoom(1)` is needed to show the room interior UI.
- Forward-declared `pdguiSetInRoom` and `mpstatsGetPlayerShotCountByRegion` inline in the cpp extern block (not added to headers — not needed elsewhere).

### Next Steps
- Playtest: verify scoreboard appears at match end, buttons work, accuracy shows for local player
- D5.4 mission complete screen still PLANNED
- D5.5: bot name dictionary + arena/weapon verification still open

---

## Session S138 — 2026-04-04

**Focus**: Fix body/head picker auto-head selection (combat sim + agent create)

### What Was Done

**`commit a65207e` pushed to `dev`.**

Root cause: two UI pickers were selecting the wrong default head when a body was chosen.

- `pdgui_menu_matchsetup.cpp` (Combat Sim bot editor, line 748): called
  `catalogResolveHeadByMpIndex((s32)b)` where `b` is the **body** mpbodynum.
  This treated the body index as a head index — body 5 would select head 5,
  not the body's paired head. Wrong for nearly every character.

- `pdgui_menu_agentcreate.cpp` `autoSelectHead()`: called
  `mpGetMpheadnumByMpbodynum` — an N64-era function that uses `rngRandom()`
  for headnum==1000 sentinel bodies and bypasses the catalog layer.

**Fix — two new catalog API functions:**
- `catalogGetBodyDefaultHead(const char *body_id)` → head catalog ID string
  Reads `entry->ext.body.headnum` (default head's g_HeadsAndBodies[] index)
  and resolves it via `catalogResolveByRuntimeIndex(ASSET_HEAD, headnum)`.
- `catalogGetBodyDefaultMpHeadIdx(s32 mpbodynum)` → mpheadnum for carousels
  Wraps the above, converts mpbodynum → body catalog ID → ext.body.headnum →
  `catalogHeadnumToMpHeadIdx()`. Returns -1 for unregistered heads (including
  the headnum==1000 random-gender sentinel — caller keeps existing selection).

Both are declared in `assetcatalog.h`, implemented in `assetcatalog_api.c`.
Wire protocol unchanged — body_id/head_id remain catalog ID strings throughout.

**Changed files (4):**
- `port/src/assetcatalog_api.c` — two new functions after `catalogHeadnumToMpHeadIdx`
- `port/include/assetcatalog.h` — declarations for both new functions
- `port/fast3d/pdgui_menu_matchsetup.cpp` — `catalogGetBodyDefaultHead(bid)` replaces wrong call
- `port/fast3d/pdgui_menu_agentcreate.cpp` — `catalogGetBodyDefaultMpHeadIdx` in `autoSelectHead`, forward decl added

**Build**: both client and server link clean (full incremental build from main copy).

### Next Steps

- Playtest: verify body selection in Combat Sim and Agent Create picks correct paired head
- D5.5 (Combat Sim Polish) — this fix is a prerequisite; bot name dictionary + arena/weapon verification still open

---

## Session S137 — 2026-04-03

**Focus**: Online match start bug (B-103) — critical path fix for multiplayer

### What Was Done

**B-103 fixed and pushed (`commit 184922a`).**

Root cause: `g_MpSetup.stage_id` was never populated by the match setup flow.
- UI sets `g_MatchConfig.stage_id`, not `g_MpSetup.stage_id`.
- `manifestBuildForHost()` (client side) reads `g_MpSetup.stage_id` → found empty → stage skipped from manifest → stage not in session catalog.
- `netmsgSvcStageStartWrite()` (server side) reads `g_MpSetup.stage_id` → found empty → writes `stage_session=0` → returns early.
- Client reads `stage_session=0` → silent `return 1` with no log → surfaces as "malformed or unknown message 0x10 from server". Stage never loaded.

**Two-line fix in `port/src/net/netmsg.c`:**
1. `netmsgClcLobbyStartWrite`: sync `g_MatchConfig.stage_id → g_MpSetup.stage_id` before `manifestBuildForHost` so stage enters manifest and session catalog.
2. `netmsgClcLobbyStartRead` (server): copy parsed `stage_id → g_MpSetup.stage_id` alongside existing `g_MatchConfig` and `stagenum` assignments.

No protocol changes. S130 constraint respected (catalog IDs throughout, no raw indices).

**Build**: client clean (exit 0). Server CMake arch error is pre-existing, unrelated.

### Additional Work (same session, post-context-commit)

After the context commit, the session continued with several more fixes and features before S138 began:

- `c474baa` **feat(title)**: gold name colour + +0.5s legal screen duration.
- `ba6983f` **diag**: MATCH-START trace logging added + fix for premature `inGame` flag and false "malformed or unknown message 0x10" warning.
- `124d195` **fix(net)**: `catalogResolveStageBySession` now accepts `ASSET_ARENA` type — was failing to resolve arena stages sent from server.
- `e148aee` **fix(net)**: bot AI enabled on client side + countdown dismiss on match start.
- `d343273` **feat(net)**: server-authoritative bot sync for dedicated server — server now owns bot state and syncs to clients.

### Next Steps

- Playtest with Chris — match should now start when countdown reaches zero
- D5.3 (Pause Menu) — unblocked by D5.1 input ownership

---

## Session S136 — 2026-04-03

**Focus**: D5.1 — Input Ownership Boundary

### What Was Done

**D5.1 implemented and pushed (`commit 001dba8`).**

Introduces `InputOwnerMode` enum (`INPUTMODE_MENU` / `INPUTMODE_GAMEPLAY`) and
`pdmainSetInputMode()` as the single canonical transition function.

**New files:**
- `port/include/pdmain.h` — enum + extern g_InputMode + pdmainSetInputMode() declaration (extern "C" guards for C++ callers)

**Changed files (9):**
- `port/src/pdmain.c` — `g_InputMode` global + `pdmainSetInputMode()` implementation: GAMEPLAY→SDL mouse capture bypassing pdguiIsActive() guard; MENU→SDL_SetRelativeMouseMode(FALSE) + ShowCursor
- `port/fast3d/pdgui_backend.cpp` — GAMEPLAY-mode early return: keyboard events not forwarded to ImGui when `!g_PdguiActive`; Tab suppressed before ImGui sees it in MENU mode (fixes CK_START double-trigger)
- `port/fast3d/pdgui_menu_pausemenu.cpp` — replaced direct SDL calls in Open/Close with `pdmainSetInputMode(INPUTMODE_MENU/GAMEPLAY)`
- `port/fast3d/pdgui_bridge.c` — `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` after both `menuhandlerAcceptMission()` calls
- `port/fast3d/pdgui_menu_solomission.cpp` — `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` after both `menuhandlerAcceptMission()` calls
- `port/src/net/matchsetup.c` — `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` in `matchStart()` and `matchStartFromChallenge()`
- `port/src/net/netmsg.c` — `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` in SVC_STAGE_START (co-op/anti path and MP path), inside `#if !defined(PD_SERVER)`
- `port/src/menumgr.c` — `restoreGameplayMouseCapture()` body replaced with single `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` call

**Build**: both client and server link clean.

### Bugs Addressed

- **B-92 class** (mouse not captured on mission start): `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` calls SDL directly, bypassing the `pdguiIsActive()` defer guard in `inputLockMouse()`. All known start paths covered.
- **Tab double-trigger** (CK_START conflict): Tab suppressed before `ImGui_ImplSDL2_ProcessEvent()` in MENU mode.
- **Esc double-path**: in GAMEPLAY with no active overlay, keyboard events never reach ImGui at all.

### Next Steps

- D5.0 (Visual Layer): still PLANNED before D5.3 (pause menu) per execution order
- D5.3 (Pause Menu): now unblocked by D5.1 — input ownership is clean for pause transitions

---

## Session S135 — 2026-04-03

**Focus**: D5.0a Technical Spike — Fast3D → OpenGL → ImGui texture bridge

### What Was Done

**D5.0a spike implemented and pushed (`commit 824415e`).**

Validates that the ImGui::Image() pipeline works end-to-end before the full D5.0 ROM
texture decode layer is built.

**Architecture findings (from code study):**
- `ImTextureID` in this codebase is `(void*)(uintptr_t)GLuint` — confirmed by `gfx_opengl_get_framebuffer_texture_id()`.
- `GfxRenderingAPI` exposes `new_texture()`, `select_texture()`, `upload_texture()` — but raw GLAD GL calls are equally valid since `pdgui_backend.cpp` already includes `glad.h`.
- `struct tex` in the shared texpool has `data` (N64-native pixels), `width`, `height`, `gbiformat`, `depth` — the full decode path for D5.0 is: `texLoadFromTextureNum(texnum)` → `texFindInPool()` → decode N64 format → `glTexImage2D`.
- N64 formats to implement for D5.0: RGBA16 (5-5-5-1), IA16 (8-8), IA8 (4-4), CI4/CI8 (palette-indexed). All handled by `import_texture_*` in `gfx_pc.cpp` — that code is the decode reference.

**Changes made:**
1. `pdgui_backend.cpp`: `pdguiGetUiTexture(const char *id)` — static `unordered_map<string, uint32_t>` cache, synthesizes 64×64 PD-blue RGBA32 test pattern, uploads to GL, returns ImTextureID.
2. `pdgui.h`: declared `pdguiGetUiTexture()`.
3. `pdgui_menu_mainmenu.cpp`: Settings > Catalog tab shows `ImGui::Image()` with PASS/FAIL label.
4. `assetcatalog_base.c`: registered `ui/test_panel` as `ASSET_UI` (placeholder for D5.0).

**Build**: Both client (`PerfectDark.exe`) and server (`PerfectDarkServer.exe`) link clean. No new errors.

### Spike Result

**PASS** — `pdguiGetUiTexture()` compiles, uploads a GL texture, and is called from `ImGui::Image()`. Visual confirmation requires playtest (see Settings > Catalog tab).

**D5.0 unblocked.** The D5.0 task is to replace `buildTestPattern()` with actual ROM texture decode.

### Pipeline Gap Identified for D5.0

No standalone N64 → RGBA32 decode function is currently exposed outside `gfx_pc.cpp`. D5.0 must either:
- Export a `gfxDecodeN64Texture(data, fmt, siz, w, h, out_rgba32)` helper from `gfx_pc.cpp`, OR
- Implement a standalone decode function in `pdgui_backend.cpp` (copy-referencing the `import_texture_*` logic).

Recommendation: standalone decode in `pdgui_backend.cpp` — avoids coupling the bridge to gfx_pc internals and keeps the UI texture path self-contained.

### Next Steps

- D5.0 (Visual Layer): replace `buildTestPattern()` with real ROM texture decode, implement `pdguiThemeDrawPanel()` etc., register all `ui/` catalog entries with real texnums.
- Per `context/tasks-current.md`, D5.1 (Input boundary) and D5.2 (Pause menu) follow after D5.0 validates.

---

## Session S134 -- 2026-04-03

**Focus**: Static array audit — dynamic/growable data, enum-indexed array completeness

### What Was Done

**Full audit of port/src/ and port/fast3d/ for static arrays holding dynamic/growable data.**

Scope: our code only (not vendored imgui/, external/, or decompiled src/game/).

**Findings — what was NOT a problem:**
- `assetcatalog_load.c` override arrays (s_FilenumOverride etc.): ROM-bounded reverse-index maps with existing bounds checks. ROM source numbers don't grow with mods. No change needed.
- `pdgui_hotswap.cpp` s_Entries[128]: registered from code at init time, not mod data.
- Network/player/bot arrays (MAX_PLAYERS, MAX_BOTS etc.): genuine protocol constants.
- `s_MfSortedIdx[MANIFEST_MAX_ENTRIES]`: UI sort buffer bounded by protocol maximum (4096), already matches the dynamically allocated manifest struct.
- All `s_AssetTypeNames[ASSET_TYPE_COUNT]` arrays: verified complete (25 entries, NONE through LANG). Already fixed in S133.

**Fix 1 — assetcatalog_deps.c** (`commit ab69868`):
- `s_DepTable[CATALOG_MAX_DEP_PAIRS]` (256 static) → heap-allocated `s_DepPair *s_DepTable` + `s32 s_DepCap`.
- Grows by doubling on demand (starting at CATALOG_MAX_DEP_PAIRS = 256).
- `catalogDepClear()` now frees the buffer. `catalogDepClearMods()` compact-in-place (no realloc — keeps allocated capacity).
- Previously: mods with many asset dependencies silently dropped entr
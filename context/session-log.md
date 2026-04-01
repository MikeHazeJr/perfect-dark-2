# Session Log (Active)

> Recent sessions only. Archives: [1-6](sessions-01-06.md) . [7-13](sessions-07-13.md) . [14-21](sessions-14-21.md) . [22-46](sessions-22-46.md) . [47-78](sessions-47-78.md) . [79-86](sessions-79-86.md)
> Back to [index](README.md)

## Session S99 -- 2026-04-01

**Focus**: Group 1 Solo Mission Flow — 11 legacy dialogs migrated to ImGui

### What Was Done

#### pdgui_menu_solomission.cpp — Group 1 complete (commit 30a1d9e)

Created `port/fast3d/pdgui_menu_solomission.cpp` and wired it into `pdgui_menus.h`.

**8 full ImGui replacements:**
- **Mission Select** (`g_SelectMissionMenuDialog`): full stage list with mission group headers (Mission 1–9 + Special Assignments), progressive unlock logic mirroring `menuhandlerMissionList`, best-time column, special stage section via `getNumUnlockedSpecialStages`/`func0f104720`.
- **Difficulty** (`g_SoloMissionDifficultyMenuDialog`): Agent/SA/PA rows with per-row best times and lock state (`isStageDifficultyUnlocked`). PD Mode row shown only when Skedar Ruins PA is beaten; opens `g_PdModeSettingsMenuDialog` (legacy). Calls `lvSetDifficulty` and pushes accept dialog.
- **Briefing** (`g_SoloMissionBriefingMenuDialog`): scrollable `langGet(g_Briefing.briefingtextnum)` via wrapped renderer.
- **Pre/Post Briefing** (`g_PreAndPostMissionBriefingMenuDialog`): shares the same briefing renderer with a distinct ImGui window ID.
- **Accept Mission** (`g_AcceptMissionMenuDialog`): objectives list with difficulty-bit indicators (A/SA/PA), Accept/Decline buttons. Accept calls `menuhandlerAcceptMission(MENUOP_SET, NULL, NULL)` to start the mission.
- **Pause** (`g_SoloMissionPauseMenuDialog`): objectives display, Resume/Inventory/Options/Abort nav. Inventory pushes `g_SoloMissionInventoryMenuDialog` (legacy), Options pushes `g_SoloMissionOptionsMenuDialog`.
- **Abort** (`g_MissionAbortMenuDialog`): danger dialog — switches to Red palette (index 2), restores on exit. Calls `menuhandlerAbortMission(MENUOP_SET, NULL, NULL)`. Cancel is the default selection for safety.
- **Options hub** (`g_SoloMissionOptionsMenuDialog`): buttons for Audio/Video/Control/Display/Extended, each calling `menuPushDialog`.

**3 legacy-preserved (NULL renderFn — keeps 3D model/controller previews):**
- `g_SoloMissionInventoryMenuDialog`, `g_FrWeaponsAvailableMenuDialog`, `g_SoloMissionControlStyleMenuDialog`

**Key design decisions:**
- Legacy C dialog handlers still fire on OPEN/CLOSE/TICK — `menudialog00103608` (loads briefing) and `soloMenuDialogPauseStatus` (copies objectives) continue to run, so `g_Briefing` is always correctly populated before ImGui renders.
- `struct sm_missionconfig` and `struct sm_gamefile` manually mirrored with explicit padding to avoid including types.h in C++.
- Language text IDs hard-coded as numeric constants rather than including types.h.

**Build**: Clean (only pre-existing `/*` in comment warnings). `pd` + `pd-server` both link.

### Decisions Made
- Inventory/FrWeapons/ControlStyle kept legacy: these dialogs use 3D weapon model previews and a controller button diagram — key interactive UX features that must not be degraded.
- `menuhandlerAcceptMission` and `menuhandlerAbortMission` called with NULL item/data — both handlers only branch on `operation == MENUOP_SET` and ignore item/data in that branch.

### Next Steps
- Playtest: navigate solo mission flow from main menu → mission select → difficulty → accept → mission. Verify briefing, objectives, pause, abort.
- Group 2 (End Screens) or Group 4 expansion (simulants/weapons sub-menus).

---

## Session S98 -- 2026-04-01

**Focus**: Group 3 Settings completion — extend existing Settings tabs with all remaining legacy options

### What Was Done

#### 1. Settings Tabs — Group 3 Completion (commit e47f1e0)

Extended `port/fast3d/pdgui_menu_mainmenu.cpp` to cover all legacy options dialogs (`g_VideoOptionsMenuDialog`, `g_MissionDisplayOptionsMenuDialog`, `g_AudioOptionsMenuDialog` sub-options) not yet in the ImGui Settings.

**Video tab — "Gameplay Screen" section added:**
- Screen Size dropdown (Full / Wide / Cinema) — maps `optionsGetScreenSize/Set`, controls 3D viewport letterboxing
- 2-Player Screen Split dropdown (Horizontal / Vertical) — maps `optionsGetScreenSplit/Set`
- Both have tooltips explaining the modes

**Game tab — three new sections:**
- "HUD & Display": Sight on Screen, Ammo on Screen, Show Gun Function, Always Show Target, Show Zoom Range, Show Mission Time, Head Roll (all per-player 0, `optionsGet*/Set*`)
- "Subtitles": In-Game Subtitles + Cutscene Subtitles (`optionsGetInGameSubtitles/optionsGetCutsceneSubtitles`)
- "Visual Effects": Paintball Mode with tooltip
- "Combat Assist": Aim Mode (Hold/Toggle via `optionsGetAimControl`), Auto Aim, Look Ahead with tooltip

**Debug tab — "About" section added:**
- Displays `v{VERSION_MAJOR}.{VERSION_MINOR}.{VERSION_PATCH}` (cmake-generated macros)
- Build ID, commit hash (12 chars), ROM target, branch

**Infrastructure additions:**
- 20 new `extern "C"` forward declarations for `options*` functions
- `PD_SCREENSIZE_*` and `PD_SCREENSPLIT_*` constants defined locally (avoid including constants.h)
- `versioninfo.h` macros used directly (already force-included via cmake `-include`)

**Build**: Clean 9/9 — no errors, only pre-existing `/*` warnings.
**Next**: Playtest to confirm all new options take effect in-game.

---

## Session S97 -- 2026-04-01

**Focus**: Crash triage, universal numeric aliases, manifest completeness sweep, kill plane, Catalog Settings tab

### What Was Done

#### 1. Boot/Match Crash Triage (commits c7bfa43, c5486ee, f355ff6 — March 31, 9–10pm)

- **`c7bfa43` — MP body/head runtime_index off-by-one**: `assetcatalog_base.c` was storing `i` (the `g_MpBodies[]`/`g_MpHeads[]` array index) as the `runtime_index` for base game MP body/head catalog entries. These entries must use the `g_HeadsAndBodies[]` index (i.e., `g_MpBodies[i]` / `g_MpHeads[i]`) as the runtime_index, because that is what `bodyAllocateChr`, `catalogGetBodyFilenumByIndex`, and all downstream code use. Using the wrong index space caused boot/match-start crashes when the session catalog tried to resolve these entries. 2-line fix in `assetcatalog_base.c`.

- **`c5486ee` — Match start fix + string ID logging**: Three improvements:
  - `netmsg.c`: `sessionCatalogGetId()` now called directly on stage/weapon IDs rather than going through an intermediate layer; return 1 on malformed stage-start input rather than crashing.
  - `assetcatalog_api.c`: catalog log lines now print the entry's string ID (name + filenum) instead of raw integers — dramatically improves readability of [CATALOG-*] log output.
  - `romdata.c`: `#include "assetcatalog.h"` added; each catalog-resolved ROM load now logs the entry string ID.

- **`f355ff6` — Stage aliases + session catalog hash backfill**: Two related fixes for [SESSION-CATALOG-ASSERT] hash=0x00000000 warnings on match start:
  1. `assetcatalog_base.c`: each base stage now registers a second catalog entry with `"stage_0x%02x"` ID (e.g. `"stage_0x43"`) alongside the existing `"base:name"` entry. The manifest and `netmsg.c` always generate stage IDs in this format, so `assetCatalogResolve("stage_0x43")` now returns a valid entry with a non-zero CRC32 net_hash. Fixes the root cause of 9× [SESSION-CATALOG-ASSERT] hash=0 warnings.
  2. `sessioncatalog.c`: after string-ID fallback resolution succeeds on the client, back-fills `e->net_hash` from the local catalog entry when the server sent hash=0. Defensive measure for any residual hash=0 entries.

#### 2. Arena List Cleanup (commit 9a698c3 — March 31, 10:43pm)

Removed GoldenEye X stage entries (indices 32–54) and junk Random entries (73–74) from `assetcatalog_base.c`. These were never part of this project and produced garbage display names (language file maps those lang IDs to wrong strings like "Load A Saved Head").

In `pdgui_menu_matchsetup.cpp`:
- Dropped "GoldenEye X" and "GoldenEye X Bonus" group names (ARENA_NUM_GROUPS 7→6)
- Added "Mods" as a catch-all group (ARENA_MODS_GROUP = 5)
- `arenaCollectCb` falls through to "Mods" for any ASSET_ARENA entry whose category doesn't match a named base-game group — mod-registered arenas appear automatically

#### 3. Universal Numeric Aliases (commits 1c801a3, 8f6de5e, 554759e — March 31, 11pm–midnight)

**Root cause addressed**: Session catalog hashes were 0x00000000 for weapons/bodies/heads because the manifest pipeline generates `"body_%d"` / `"head_%d"` / `"weapon_%d"` IDs but the catalog only registered `"base:{name}"` primary entries. `assetCatalogResolve("body_3")` would fail → `sessionCatalogBuild` set `net_hash=0` → `catalogResolveWeaponBySession` crashed.

Fix: register numeric alias entries alongside all primary entries:

| Commit | Aliases added | Count |
|--------|--------------|-------|
| 1c801a3 | `body_%d`, `head_%d`, `weapon_%d` | ~63 + ~76 + ~34 |
| 1c801a3 | Server `g_MpArenas[75]` stub expanded to full table with correct STAGE_* stagenum | 75 |
| 8f6de5e | `arena_%d`, `prop_%d`, `gamemode_%d`, `hud_%d`, `model_%d` | ~75 + 8 + 6 + 6 + ~440 |
| 554759e | `anim_%d` (1207), `tex_%d` (3503), `sfx_%d` (1545) | 6255 |

Large ROM tables (1207 anim, 3503 tex, 1545 sfx) given numeric aliases for mod override support — mods can now reference assets by `"anim_42"`, `"tex_100"`, `"sfx_7"` via the catalog.

#### 4. Manifest Completeness Sweep (commits 990d512, 6e1addc, 12f6922, a4cd903 — April 1, midnight–12:40am)

- **`990d512` — `manifestEnsureLoaded` in `bodyAllocateModel`**: The S96 wiring only covered `bodyAllocateChr` (stage-setup path). AI-command spawns (`Obj1` and `chrSpawnAtPad`/`chrSpawnAtChr`) go through `chrSpawnAtCoord` → `bodyAllocateModel` directly, bypassing `bodyAllocateChr`. Added `manifestEnsureLoaded` at the top of `bodyAllocateModel` — the single chokepoint for all spawn paths. Fixes Obj1 runtime crashes. Existing calls in `bodyAllocateChr` left intact (covers `headnum < 0` path); `manifestEnsureLoaded` is idempotent.

- **`6e1addc` — `manifestEnsureLoaded` in `setupLoadModeldef`**: `setupLoadModeldef` is the single chokepoint for all non-character model loads (prop objects, weapon models, hat models, projectile models). Adding `manifestEnsureLoaded(MANIFEST_TYPE_MODEL)` here ensures mid-mission weapon drops, laptop guns, debris, and objective-triggered props are tracked in the SP asset manifest. No-op in MP mode or before the SP manifest is built.

- **`12f6922` — MANIFEST_TYPE_ANIM/TEXTURE defined + hooks**: Added `MANIFEST_TYPE_ANIM=6` and `MANIFEST_TYPE_TEXTURE=7` to `netmanifest.h`. Extended `s_type_names[]` in `manifestLog`/`manifestCheck`. Wired `manifestEnsureLoaded` into `animLoadHeader` and `texLoadFromTextureNum`. **(Hooks immediately reverted — see below.)**

- **`a4cd903` — Remove anim/tex `manifestEnsureLoaded` hooks (spam/freeze fix)**: The hooks fired on every ROM-based anim/texture load with synthetic IDs (`anim_N`, `tex_N`) that hit the O(n) manifest scan + LOG_WARNING path on each lookup miss. During stage load: 64,000+ log lines + O(n²) spiral → game froze at match start. **Fix**: removed both `manifestEnsureLoaded` calls from `animLoadHeader` and `texLoadFromTextureNum`. `MANIFEST_TYPE_ANIM/TEXTURE` remain defined for future mod override use — hooks will be added per-mod at load time, not on every DMA fetch from ROM.

#### 5. Kill Plane (commit 9ff6daa — April 1, 12:03am)

Adaptive void death boundary in `player.c:playerTick()`. When a player falls below world geometry, force-kill + trigger normal respawn. Threshold = `miny - max(level_height * 6, 2000)` using `g_WorldMesh` collision bounds; falls back to `Y < -10000` if mesh not ready. Check runs each frame in `TICKMODE_NORMAL` only. Covers combat sim, any stage with a fall-off map.

#### 6. Catalog Settings Tab (commit 1aa0c93 — April 1, 12:23am)

New "Catalog" tab (index 6) added to the in-game Settings menu in `pdgui_menu_mainmenu.cpp`:
- **Summary section**: total registered entries, per-type breakdown (BODY/HEAD/STAGE/WEAPON/ANIM/TEX/AUDIO/etc.), loaded count, mod-overridden count
- **Entry browser**: type-filter dropdown + text search; columns for ID (green for mod entries), type, load state (green when loaded), runtime index, net hash, base/MOD source
- **Stage manifest section**: entry count + table of id/type/slot_index/net hash for the active `match_manifest_t`

Infrastructure additions:
- `assetCatalogGetPoolSize()` added to `assetcatalog.h/c` so callers can iterate the pool via `assetCatalogGetByIndex()` without guessing bounds
- LB/RB bumper wrap updated from 5 to 6 tabs

#### 7. SA-2 / SA-3 / SA-4 — Previously Completed, Not Logged

Completed in unlogged sessions between S91 and S92 (commits on 2026-03-31 before SA-5a):

- **`4945ff3` SA-2**: Modular catalog API layer — per-type resolution functions (`catalogResolveBody`, `catalogResolveHead`, `catalogResolveStage`, `catalogResolveWeapon`) + wire helper structs replacing ad-hoc `catalogResolve()` calls.
- **`af6036b` SA-3**: Network wire protocol migration — replaced raw N64 body/head/stage/weapon indices in SVC_*/CLC_* messages with u16 session IDs from the session catalog. ~180 call sites, ~20 message types migrated.
- **`574f7b6` SA-4**: Persistence migration — session IDs in savefile, identity, and scenario save. Save format now uses catalog-based asset identity, not raw N64 indices.

#### 8. UI Scaling — v0.1.0 Blocker Closed

- **`video.c`**: Added `vidUiScaleMult` static float (default 1.0), `videoGetUiScaleMult()` / `videoSetUiScaleMult()` (clamps 0.5–2.0), config registration as `Video.UIScaleMult`.
- **`video.h`**: Declared the two new functions.
- **`pdgui_scaling.h`**: Forward-declared `videoGetUiScaleMult()`; `pdguiScaleFactor()` now multiplies the auto-scale by the user's setting. Combined result floor: 0.25.
- **`pdgui_menu_mainmenu.cpp`**: Added `videoGetUiScaleMult`/`videoSetUiScaleMult` to `extern "C"` block; added "UI Scale" `PdSliderFloat` (50–200%) in Display section of Video settings tab.

### Build Status

Clean build at `02fb3f7`. Both `PerfectDark.exe` and `PerfectDarkServer.exe` link clean.

### Key Decisions

- **Numeric aliases are canonical**: All asset types now reachable by `"type_%d"` ID from catalog. This is the stable contract for the mod pipeline; no manifest/net code needs to know `"base:name"` primary IDs.
- **Large-table aliases (anim/tex/sfx) exist for mod support, not manifest tracking**: Adding hooks for these in hot paths is wrong. Hooks get wired per-mod at load time only.
- **`bodyAllocateModel` is the spawn chokepoint**: Not `bodyAllocateChr`. Any future manifest or catalog work relating to runtime chr spawns must hook `bodyAllocateModel`.
- **SA series complete**: SA-1 through SA-7 all done. The catalog migration track is finished.
- **UI Scaling done via multiplier**: `pdguiScaleFactor() = (h/720) * userMult`. Keeps auto-scaling but gives user override range 50–200%.

### New Bugs Found

- **B-57** (NEW): Scenario save only stores `weaponset` index, not individual weapon selections. If a player customizes a non-standard weapon loadout, save/reload will restore the weaponset default, not the custom picks.
- **B-58** (NEW): `catalogResolveByRuntimeIndex(type=16, index=103)` assert fires on the scenario save path. Type 16 is out of range for the catalog type enum. Triggered when scenario save tries to resolve a weapon reference.

### Next Steps

- **SP playtest SA-6**: Two consecutive missions — verify Joanna stays in `to_keep`; Counter-Op mode — verify anti-player body/head in manifest log.
- **B-57/B-58**: Scenario save weapon persistence investigation.
- **Countdown UX**: Match start countdown display on Room screen (reads `g_MatchCountdownState`).
- SA series is done. SA-2 is the next dependency for R-series (room sync protocol).
- **v0.1.0**: All critical blockers cleared — needs QC pass (see tasks-current.md).

---

## Session S96 -- 2026-03-31

**Focus**: SA-6 follow-up — manifest completeness (counter-op assets + runtime safety net)

### What Was Done

1. **`manifestBuildMission` counter-op scan** (`port/src/net/netmanifest.c`):
   - Added scan of `g_Vars.antibodynum` / `g_Vars.antiheadnum` when `g_Vars.antiplayernum >= 0`
   - These assets are not in the props spawn list — they were missing from the SP manifest in Counter-Operative mode
   - Adds `body_N`/`head_N` catalog entries with slot_index=1 (the anti player's slot)

2. **`manifestEnsureLoaded(catalog_id, asset_type)`** added to `netmanifest.c/h`:
   - Checks `g_CurrentLoadedManifest` by FNV-1a hash; if absent, resolves via `assetCatalogResolve()`, adds to manifest, advances catalog state to `ASSET_STATE_LOADED`
   - No-op when `g_CurrentLoadedManifest.num_entries == 0` (MP mode or pre-load) — safe to call unconditionally
   - Synthetic hash fallback with warning log when catalog can't resolve the ID (suppresses repeat spam)

3. **`bodyAllocateChr` wired** (`src/game/body.c`):
   - Added `#include "net/netmanifest.h"`
   - After bodynum/headnum are resolved (post-`bodyChooseHead`), calls `manifestEnsureLoaded("body_N", MANIFEST_TYPE_BODY)` and conditionally `manifestEnsureLoaded("head_N", MANIFEST_TYPE_HEAD)` (skips headnum == -55555 / built-into-body case)
   - Catches any CHR missed by the static pre-scan: deferred objective spawns, random-body resolution, etc.

### Build Status

27/27 clean. Both `PerfectDark.exe` and `PerfectDarkServer.exe` link clean. No new warnings. Commit `002a16a` pushed to `dev`.

### Key Decisions

- **Counter-op is the only gap in the static scan**: all other CHRs (including objective-deferred spawns) are in `g_StageSetup.props` and already covered by the SA-6 props walk. The "objective command lists" are interleaved in the same props stream — no separate structure.
- **`manifestEnsureLoaded` as safety net, not primary path**: the static scan is the primary mechanism. `bodyAllocateChr` wiring catches edge cases and provides a clear log line when the pre-scan missed something.
- **No MP interference**: the `num_entries == 0` guard means `manifestEnsureLoaded` is fully inert in MP mode where `g_CurrentLoadedManifest` is never populated.

### Next Steps

- SA-2: Modular catalog API layer — typed query functions (bodies, heads, stages, weapons, sounds)
- SP playtest for SA-6/S96: two consecutive missions, verify Joanna stays in `to_keep`, check Counter-Op mode adds anti-player assets to log

---

## Session S95 -- 2026-03-31

**Focus**: SA-7 — Session Catalog migration cleanup / consolidation

### What Was Done

1. **modelcatalog.c decision**: Kept as thin facade. Contains unique VEH/SIGSEGV model validation logic, lazy validation, scale clamping, and thumbnail queue system — none duplicated in assetcatalog.c. Absorbing would be a large risky refactor, not cleanup. Decision: **keep**.

2. **modmgr.c shadow arrays**: Confirmed absent. `g_ModBodies[]`, `g_ModHeads[]`, `g_ModArenas[]` do not exist anywhere in the codebase.

3. **Dead code removed** from modelcatalog.c and modelcatalog.h:
   - `catalogGetEntry(s32 index)` — zero external callers, removed
   - `catalogGetBodyByMpIndex(s32 mpIndex)` — zero external callers, removed
   - `catalogGetHeadByMpIndex(s32 mpIndex)` — zero external callers, removed
   - Kept: `catalogGetSafeBody`, `catalogGetSafeHead`, `catalogGetSafeBodyPaired` (4 call sites each in matchsetup.c + netmsg.c)

4. **Audit: netmanifest.c synthetic IDs**: Clean. `assetCatalogResolve("body_0")` and `assetCatalogResolve("head_0")` go through the catalog — not synthetic bypasses.

5. **Audit: g_HeadsAndBodies[** accesses**: Clean. All accesses confined to: `assetcatalog_base.c`, `assetcatalog_api.c`, `modelcatalog.c`, `server_stubs.c` (extern definition only). `netmsg.c` reference is comment-only.

6. **Audit: netclient.settings.bodynum/headnum**: These fields do not exist. Architecture already uses `body_id`/`head_id` string fields and session hashes.

7. **port/CLAUDE.md updated**: Added "Asset Catalog — Mandatory Rules (SA-7)" section documenting:
   - `catalogWriteAssetRef`/`catalogReadAssetRef` as only permitted net message asset reference functions
   - Catalog-first file resolution principle with permitted accessor list
   - Allowed `g_HeadsAndBodies[]` read sites

### Build Status

711/711 clean (fresh worktree configure + build). Both `PerfectDark.exe` and `PerfectDarkServer.exe` link clean.

### Key Decisions

- **modelcatalog.c stays**: Unique responsibility (platform exception handling for model validation). Not a candidate for absorption into assetcatalog.c without significant risk.
- **Deleted 3 dead functions**: SA-5f had already flagged these with zero-caller audit notes. SA-7 completes the removal.
- **All 6 audits passed**: No violations found. The catalog migration from SA-1 through SA-6 was clean.

### Next Steps

- SA-2: Modular catalog API layer — new typed query functions (bodies, heads, stages, weapons, sounds)
- SP playtest for SA-6: two consecutive missions, verify Joanna stays in `to_keep`

---

## Session S94 -- 2026-03-31

**Focus**: SA-6 — SP load manifest + diff-based asset lifecycle

### What Was Done

1. **`manifest_diff_t` struct** added to `port/include/net/netmanifest.h`:
   - `manifest_diff_entry_t` (id[64] + net_hash + type) as the per-entry type
   - `manifest_diff_t` with fixed `to_load[]`, `to_unload[]`, `to_keep[]` arrays (MANIFEST_MAX_ENTRIES each) + counts

2. **5 new functions** implemented in `port/src/net/netmanifest.c`:
   - `manifestBuildMission(stagenum, out)` — builds manifest for SP stage via `catalogResolveStage()` + Joanna (body_0/head_0); two TODOs for spawn-list characters and prop models
   - `manifestDiff(current, needed, out)` — two-pass O(n²) diff; pass 1 classifies needed entries into to_load/to_keep; pass 2 finds to_unload entries
   - `manifestDiffFree(diff)` — memset (fixed arrays, no heap); named for future MEM-2 swap-in
   - `manifestApplyDiff(needed, diff)` — calls `assetCatalogSetLoadState()` for unloads (→ENABLED) and loads (→LOADED); updates `g_CurrentLoadedManifest`
   - `manifestSPTransition(stagenum)` — convenience wrapper using module-static buffers to keep caller stack small

3. **`g_CurrentLoadedManifest` global** added (tracks current SP load state for diff baseline)

4. **`mainChangeToStage()` wired** in `port/src/pdmain.c`: added `#include "net/netmanifest.h"` + `STAGE_IS_GAMEPLAY()` guard + `manifestSPTransition(stagenum)` call before `g_MainChangeToStageNum` is set

### Build Status

711/711 clean. Both `PerfectDark.exe` and `PerfectDarkServer.exe` link clean. No new warnings from SA-6 code.

### Key Decisions

- **Fixed arrays, not malloc**: `manifest_diff_t` uses MANIFEST_MAX_ENTRIES-sized fixed arrays (~26 KB struct). Module statics in netmanifest.c avoid stack bloat in callers. `manifestDiffFree` is a memset stub — named for easy MEM-2 upgrade.
- **State-tracking only**: `manifestApplyDiff` calls `assetCatalogSetLoadState` to advance through the lifecycle but doesn't move memory yet. That's MEM-2's job. The value of SA-6 is the tracking infrastructure.
- **Joanna hardcoded for now**: `body_0`/`head_0` covers the current SP use case. Full spawn-list enumeration requires pre-loaded setup file data — deferred with TODO comments.
- **Wired in `mainChangeToStage()`**: This is the canonical SP stage transition call, equivalent to `matchStart()` for MP. Only fires for `STAGE_IS_GAMEPLAY()` stages (not title/credits/menus).

### Next Steps

- SP playtest: run two consecutive missions, check log for `MANIFEST-SP:` lines. Joanna should appear in `to_keep` on second mission; mission-unique stage should appear in `to_load`.
- SA-2: Modular catalog API layer (typed result struct functions replacing ad-hoc `catalogResolve()` calls) — this is the next major SA track

---

## Session S93 -- 2026-03-31

**Focus**: SA-5f — deprecation pass, final Phase 5 sub-phase

### What Was Done

1. **Full legacy accessor audit** across `src/` and `port/`:
   - Confirmed all load-path `.filenum` sites are already migrated (SA-5a through SA-5e)
   - Confirmed all `.bgfileid`/`.padsfileid`/`.setupfileid`/`.tilefileid`/`.mpsetupfileid` sites migrated (SA-5b)
   - Confirmed all `g_ModelStates[].fileid` load sites migrated (SA-5c)
   - Found 3 remaining raw `g_HeadsAndBodies[bodynum].filenum` in diagnostic LOG messages in `body.c` — annotated as intentional (`/* SA-5f: raw access for diagnostic log only */`)
   - Found 1 bug: `catalogGetBodyScaleByIndex` silently fell back to `g_HeadsAndBodies[bodynum].scale` on catalog miss — design doc flags this as wrong

2. **Bug fixed** (`port/src/assetcatalog_api.c`): `catalogGetBodyScaleByIndex` error path changed from silent legacy fallback to CATALOG-FATAL pattern (sets `g_CatalogFailure=1`, `g_CatalogFailureMsg`, returns `1.0f`). Now matches the pattern used by `catalogGetBodyFilenumByIndex`, `catalogGetHeadFilenumByIndex`, and `catalogGetStageResultByIndex`.

3. **Deprecated attribute audit** (`port/include/modelcatalog.h`):
   - Added `__attribute__((deprecated(...)))` to `catalogGetEntry`, `catalogGetBodyByMpIndex`, `catalogGetHeadByMpIndex`
   - Built → **zero new warnings** — confirmed these three functions have zero external callers
   - Removed deprecated attributes per SA-5f protocol (job done: compiler-enforced audit confirmed migration complete)
   - Replaced with SA-5f audit note in doc comments

4. **Functions left in place** (still actively used):
   - `catalogGetSafeBody` — internal to `catalogGetSafeBodyPaired` only
   - `catalogGetSafeHead` — 4 active call sites in matchsetup.c + netmsg.c (defense-in-depth guards)
   - `catalogGetSafeBodyPaired` — 4 active call sites; design doc says these are intentional safety nets
   - `g_HeadsAndBodies[bodynum].handfilenum` (bondgun.c:
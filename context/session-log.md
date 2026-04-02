# Session Log (Active)

> Recent sessions only. Archives: [1-6](sessions-01-06.md) . [7-13](sessions-07-13.md) . [14-21](sessions-14-21.md) . [22-46](sessions-22-46.md) . [47-78](sessions-47-78.md) . [79-86](sessions-79-86.md)
> Back to [index](README.md)

## Session S117 -- 2026-04-01

**Focus**: Build fix — langSafe linker error post-manifest sprint

### What Was Done

**Commit a770cd6** — 1 file, 1 line:

- **Linker error**: `pdgui_menu_agentselect.cpp` forward-declared `langSafe` as an external symbol (`const char *langSafe(s32 textid);`). But `langSafe` is defined `static` in `pdgui_menu_solomission.cpp` — translation-unit local, not exported. Linker couldn't resolve it.
- **Fix**: Replaced the forward declaration with a local `static` definition inline: `static const char *langSafe(s32 textid) { const char *s = langGet(textid); return s ? s : ""; }`. Matches the copy in solomission.cpp exactly.
- S116's `pdgui_menu_agentselect.cpp` fix added the `langSafe()` *calls* but left the forward declaration pointing to a non-existent external — the calls themselves were correct, only the linkage declaration was wrong.
- Both `PerfectDark.exe` and `PerfectDarkServer.exe` link clean. Pushed.

### Next Steps
- Playtest: solo mission difficulty select should now show correct text (S116 Bug 1 fix now actually links). SP spawn should no longer generate "missed by pre-scan" log spam (S116 Bug 2).
- Next major track: Room Architecture (R-2 onward) or Phase 3 participant system (remove chrslots).

---

## Session S116 -- 2026-04-01

**Focus**: Post-S115 audit fixes — langSafe propagation, manifest hash mismatch, synthetic ID cleanup

### What Was Done

**Commit 93a2a26** — 3 files, 28 insertions / 48 deletions:

- **Bug 1 (langSafe)**: Replaced all remaining unsafe `langGet()` calls with `langSafe()` in `pdgui_menu_solomission.cpp` (8 sites: stage name snprintf, briefing text ternary, overview/status title snprintf, objectives `TextUnformatted`, pause menu struct initializer for "Inventory"/"Abort!" buttons, abort confirm `TextColored`). Struct initializer was most dangerous — storing NULL `const char*` in a `PauseBtn` array then passing to `ImGui::Button` was a guaranteed crash when the lang bank isn't loaded. Also fixed the same snprintf pattern in `pdgui_menu_agentselect.cpp` (agent ranking screen, stage name).
- **Bug 2 primary (hash mismatch)**: `manifestEnsureLoaded()` was computing `hash = s_fnv1a(catalog_id)` for the dedup check, but manifest entries store `e->net_hash` which is CRC32 (from `assetCatalogRegister`). These hash algorithms produce different values — dedup check ALWAYS missed → 31+ spurious "missed by pre-scan" log entries per spawn. Fix: resolve catalog entry FIRST (`assetCatalogResolve`), use `e->net_hash` for dedup, fall back to `s_fnv1a` only when not in catalog.
- **Bug 2 secondary (synthetic IDs)**: `manifestBuild()` (bots section) and `manifestBuildMission()` (setup scan + counter-op) generated synthetic `"body_N"`/`"head_N"` entries when `catalogResolveByRuntimeIndex` returned NULL. Phase 4 validate() zeros these (not in catalog), generating log spam. Fix: skip instead of adding synthetic entries.
- **manifestLog() / manifestCheck()**: Added "LANG" to `s_type_names[]` in both functions so `MANIFEST_TYPE_LANG=8` entries no longer log as "?".
- Build: CMake reconfigure needed (screenmfst.c was added after last configure). 530/530 clean (both PerfectDark.exe + PerfectDarkServer.exe).
- **Propagation check**: Scanned all `langGet()` call sites across 8 `.cpp` files. `pdgui_menu_warning.cpp` and `arenaGetName()` callers are all null-guarded. `pdgui_menu_mainmenu.cpp` and `pdgui_menu_agentcreate.cpp` only have forward declarations. No other unsafe sites found.

### Decisions Made
- Bug 3 (Catalog Settings tab "nothing loaded") was already fixed by Phase 1 (`manifestSPTransition` wired in `pdmain.c`). No additional code change needed.
- `catalogComputeStageDiff` was incorrectly flagged as dead code in S114 session log — it IS called from `src/game/lv.c:287`. No code change; just awareness correction.
- Synthetic fallback entries in the player section of `manifestBuild` (using actual string IDs from `ncl->settings`) are different from the `body_N`/`head_N` pattern — left those unchanged as they represent legitimate "ID not in server catalog" cases.

### Next Steps
- Playtest: solo mission difficulty select should now show correct text (Bug 1). SP spawn should no longer generate "missed by pre-scan" log spam (Bug 2).
- Next major track: Room Architecture (R-2 onward) or Phase 3 participant system (remove chrslots).

---

## Session S115 -- 2026-04-01

**Focus**: Manifest Lifecycle Sprint — Phase 6: Menu/UI Asset Manifesting

### What Was Done

**Phase 6 (commit d624022):**
- `port/include/screenmfst.h` (new): `screenManifestRegister()`, `screenManifestTick()`, `screenManifestShutdown()`. Constants `SMFST_MAX_IDS_PER_SCREEN=32`, `SMFST_MAX_SCREENS=64`, `SMFST_MAX_ACTIVE=16`. Uses `void*` for dialogdef to avoid pulling game headers into port code.
- `port/src/screenmfst.c` (new): C89 implementation. Registry of `ScreenEntry { void *dialogdef; char ids[][64]; u8 types[]; s32 count; }`. `screenManifestTick()` compares active_defs against last-frame snapshot: enter → `catalogLoadAsset` per ID; leave → `catalogUnloadAsset` per ID. Phase 5 ref counting handles shared assets across overlapping screens. `screenManifestShutdown()` unloads currently-active screens before clearing state.
- `port/fast3d/pdgui_hotswap.cpp`: Added `#include "screenmfst.h"`. In `pdguiHotswapRenderQueued()`: collects `entry->dialogdef` pointers from the render queue, calls `screenManifestTick(active_defs, n)` before clearing the queue. In `pdguiHotswapShutdown()`: calls `screenManifestShutdown()` first.
- `port/fast3d/pdgui_menu_agentselect.cpp`: `screenManifestRegister` for `g_FilemgrFileSelectMenuDialog` with `base:dark_combat`, `base:head_dark_combat`, `base:lang_misc`.
- `port/fast3d/pdgui_menu_matchsetup.cpp`: `screenManifestRegister` for `g_MatchSetupMenuDialog` with `base:lang_mpmenu`, `base:lang_mpweapons`.
- `port/fast3d/pdgui_menu_network.cpp`: `screenManifestRegister` for `g_NetMenuDialog` with `base:lang_mpmenu`, `base:lang_misc`.
- Build: 721/721 clean (both pd.exe + pd-server.exe).

### Decisions Made
- Per-frame enter/leave detection via dialogdef* set diff (not lifecycle callbacks) — cleanest integration with existing hotswap render queue without requiring new hook points.
- `void*` for dialogdef pointer key — avoids menudialogdef header in port C code while remaining type-safe at the registration sites (C++ casts).
- Base-game bundled assets (bundled=1): `catalogLoadAsset`/`catalogUnloadAsset` are no-ops. This is intentional — the registry documents intent and provides forward compatibility for mod overrides of those assets.
- Manifest Lifecycle Sprint: **ALL 6 PHASES COMPLETE**.

### Next Steps
- Playtest all six phases: SMFST enter/leave events visible as `"SMFST: enter/leave 0x..."` in log when navigating between hotswapped menus.
- Optional: register mini-manifests for more screens as mod content grows.
- The sprint is done; next major track is likely Room Architecture (R-2 onward) or Phase 3 of the participant system (remove chrslots).

---

## Session S114 -- 2026-04-01

**Focus**: Manifest Lifecycle Sprint — Phase 5: Proper Unload/Cleanup

### What Was Done

**Phase 5 (commit 8af6919):**
- `port/src/assetcatalog_load.c`:
  - Added `#include "assetcatalog_deps.h"` for dep cascade support.
  - Added static `s_catalogUnloadDepCallback()` — calls `catalogUnloadAsset(dep_id)` for each registered dep when a parent is freed; safe with manifest-direct dep unloads because a dep that was cascade-freed has `loaded_data == NULL`, making the subsequent manifest call a silent no-op (no double-free possible).
  - Rewrote `catalogUnloadAsset()`: tracks `old_ref`/`new_ref`; when ref hits 0 with data present, calls `catalogDepForEach(assetId, s_catalogUnloadDepCallback, NULL)` before `sysMemFree`; logs `"MANIFEST: unload '%s' ref=%d->%d (freed)"` on free and `"MANIFEST: unload '%s' ref=%d->%d (retained)"` when ref > 0 after decrement; silent no-op if already fully unloaded (`old_ref == new_ref == 0`). Bundled asset guard unchanged.
- `port/src/net/netmanifest.c`:
  - `manifestApplyDiff()`: swapped order to **loads first, unloads second**. Previous unload-then-load ordering created the 56-asset crash window (stage 0x1f → 0x26): 56 assets freed before the 1 new menu asset was resident → 0xc0000005. New ordering ensures new assets are resident before old ones are released.
  - Updated comments to document both the ordering rationale and the cascade behavior.
- Verification sweep: `assetCatalogSetLoadState` has zero callers. `sysMemFree(entry->loaded_data)` has exactly one call site (in `catalogUnloadAsset`). No force-free paths bypass ref counting.
- Both targets build clean: 5/5 (incremental).

### Decisions Made
- Dep cascade fires only when parent ref hits 0 (not on every decrement) — correct behavior for shared deps: a dep shared between two characters survives the first character's unload and is only freed when the second also hits 0.
- Cascade + manifest-direct dep unload coexist safely: idempotent because `loaded_data == NULL` after first free makes second call a no-op.
- Load-before-unload is the primary crash fix; cascade is the correctness fix for direct callers outside the manifest.

### Next Steps
- Phase 6: Menu/UI asset manifesting — screens register mini-manifests.
- Playtest: SP transition should show `"MANIFEST: unload '...' ref=1->0 (freed)"` or `"(retained)"` in log; no more 0xc0000005 on match→menu transition.

---

## Session S113 -- 2026-04-01

**Focus**: Manifest Lifecycle Sprint — Phase 4: Pre-validation Pass

### What Was Done

**Phase 4 (commit 98aa2ec):**
- `port/include/net/netmanifest.h`: `manifestValidate(manifest_diff_t *diff)` declared between `manifestApplyDiff` and `manifestSPTransition`. Full doc comment explaining the four validation checks.
- `port/src/net/netmanifest.c`:
  - `s_ValidateDepCtx` struct (owner_id + warn_count) for dep-chain callback.
  - `s_validateDepCallback()`: called by `catalogDepForEach`, logs warning for missing or disabled deps, increments warn_count.
  - `manifestValidate()`: iterates `diff->to_load`; for each entry:
    1. `assetCatalogResolve(id)` → fallback `assetCatalogResolveByNetHash(net_hash)`. Not found → zero id[0] + WARN.
    2. `!e->enabled` → zero id[0] + WARN.
    3. `MANIFEST_TYPE_LANG`: checks `e->type == ASSET_LANG` and `e->ext.lang.bank_id > 0`. Fail → zero id[0] + WARN.
    4. `catalogDepForEach(entry->id, ...)`: warns on missing/disabled deps, but keeps parent entry.
    5. Summary log: "all N valid" or "X of N invalid and skipped".
  - `manifestSPTransition`: `manifestValidate(&s_SpLastDiff)` inserted between `manifestDiff` and `manifestApplyDiff`.
  - `manifestMPTransition`: same insertion.
- **Also fixed**: stale CMake GLOB cache from Phase 3 — `langmanifest.c` was not being compiled into `pd.exe`. Clean `cmake` re-configure picked it up. 529/529 clean (both targets).

### Decisions Made
- Validate the diff's `to_load` list (not the manifest itself) — avoids contaminating the manifest and works with the existing `id[0] == '\0'` skip guard in `manifestApplyDiff`.
- Dep chain warnings only (don't zero parent) — parent asset still loads; missing deps are already absent from to_load and will be skipped naturally.
- Both SP and MP paths validated via the same function — log prefix is "MANIFEST-VALIDATE:" (distinct from "MANIFEST-SP:").

### Next Steps
- Phase 5: Proper unload/cleanup — targeted ref-counted unloads.
- Phase 6: Menu/UI asset manifesting — screens register mini-manifests.
- Playtest: SP transition should show "MANIFEST-VALIDATE: all N to-load entries valid" in log.

---

## Session S112 -- 2026-04-01

**Focus**: Manifest Lifecycle Sprint — Phase 3: Language Bank Manifesting

### What Was Done

**Phase 3 (commit 5d449cd):**
- `port/include/assetcatalog.h`: `ASSET_LANG` added to `asset_type_e` enum; `ext.lang.bank_id` (s32) member added to union.
- `port/src/assetcatalog_base_extended.c`: Static table of 68 base lang banks (LANGBANK_AME through LANGBANK_MP20); registered as `base:lang_*` catalog IDs with `ASSET_STATE_ENABLED` (not pre-loaded; langLoad() must be explicit). `bundled=1` prevents clearMods from removing them.
- `port/include/net/netmanifest.h`: `MANIFEST_TYPE_LANG = 8` constant added.
- `port/include/langmanifest.h` + `port/src/langmanifest.c` (new files): `lang_manifest_t` struct (`bank_ids[69]`, `count`); `langManifestReset()`, `langManifestRecordBank()`, `langManifestEnsureId()`, `langManifestReload()`, `langManifestGetCount()`. `langManifestEnsureId("base:lang_title")` resolves catalog ID → bank_id → langLoad if not loaded → records.
- `src/include/game/lang.h` + `src/game/lang.c`: `langIsBankLoaded(s32 bank)` accessor added (checks `g_LangBanks[bank] != NULL`).
- `src/game/langreset.c`: `langManifestReset()` called after bank array clear; `langManifestRecordBank()` called for each bank loaded by `langReset()` (both PAL and pre-PAL paths).
- `src/game/setup.c`: `langManifestRecordBank()` called after stage-specific bank load in `setupLoadLevel`.
- `port/src/assetcatalog_scanner.c`: `"lang_bank"` → `ASSET_LANG` in `sectionToType()`; `"lang_banks"` → `ASSET_LANG` in `categoryToType()`; `case ASSET_LANG:` block reads `bank_id` INI field.
- `langmanifest.c` NOT added to `SRC_SERVER` (server has no menu screens).
- Both targets (PerfectDark.exe + PerfectDarkServer.exe) build clean.

### Decisions Made
- `load_state = ASSET_STATE_ENABLED` (not LOADED) for lang bank catalog entries — ROM-resident but not pre-loaded into heap. langLoad() must be called explicitly.
- `langIsBankLoaded()` accessor rather than exposing `g_LangBanks[]` extern to port code — clean API boundary.
- `langManifestReload()` provided for future NTSC language-change support but does not replace existing `langReload()` callers (PAL path already handles reload correctly).

### Next Steps
- Phase 4: Pre-validation pass — verify all manifest entries exist before committing to a load.
- Phase 5: Proper unload/cleanup — targeted ref-counted unloads.

---

## Session S111 -- 2026-04-01

**Focus**: Manifest Lifecycle Sprint — Phase 2: Dependency Graph

### What Was Done

**Phase 2 (commit 2c761f1):**
- `port/include/assetcatalog_deps.h` + `port/src/assetcatalog_deps.c` (new): flat 256-pair dep table. `catalogDepRegister(owner_id, dep_id, is_bundled)`, `catalogDepForEach(owner_id, fn, userdata)`, `catalogDepClear()`, `catalogDepClearMods()`, `catalogDepCount()`. FNV-1a owner hash for O(1) lookup. Bundled pairs are stored but skipped during iteration (base-game assets always ROM-resident).
- `assetcatalog.c`: `catalogDepClear()` hooked in `assetCatalogClear()`, `catalogDepClearMods()` hooked in `assetCatalogClearMods()`.
- `assetcatalog_scanner.c`: INI "deps" field parsing (comma-separated, strtok) calls `catalogDepRegister(idbuf, dep, bundled)` per dep. ASSET_ANIMATION case: reverse dep registration via `target_body` field.
- `netmanifest.c`: added `s_DepExpandCtx`, `s_assetTypeToManifestType()`, `s_manifestDepAddEntry()`, `s_manifestExpandDeps()`. Wired `s_manifestExpandDeps(out, entry->id, slot_index)` after all 6 body/head catalog-resolved add sites: player body+head, bot body+head (`manifestBuild`); Joanna, stage CHR body+head, counter-op body+head (`manifestBuildMission`). Fallback synthetic-ID paths do not expand (no catalog entry to look up deps against).
- `server_stubs.c`: `catalogLoadAsset` (return 1) + `catalogUnloadAsset` (no-op) — Phase 1 introduced these calls to `netmanifest.c` but server stubs were missing.
- `CMakeLists.txt`: `assetcatalog_deps.c` added to `SRC_SERVER` explicit list.
- Both targets build clean: 4/4 (incremental from Phase 1 base).

### Decisions Made
- Zero change to `asset_entry_t` struct — separate parallel table avoids 3.6 MB overhead for mostly-empty per-entry dep arrays.
- Shared dep deduplication is free: `manifestAddEntry()` already deduplicates by net_hash. Two bodies sharing one animation → animation appears once in manifest.
- `catalogDepForEach` skips bundled pairs — base-game dep expansion is always a no-op, correct since their anims/textures are ROM-resident.

### Next Steps
- Playtest: enable a mod body with `deps = mod:anim1, mod:tex1` in its INI. Verify those entries appear in MANIFEST log on match start.
- Phase 3: Language bank manifesting.

---

## Session S110 -- 2026-04-01

**Focus**: Manifest Lifecycle Sprint — Phase 0 + Phase 1

### What Was Done

**Phase 0 (commit 4476d00) — already landed before this session:**
- Removed all numeric alias entries (`anim_NNN`, `tex_NNN`, `weapon_NNN`, etc.) from catalog registration
- Manifest now uses canonical catalog IDs ("base:falcon2", not "weapon_46")
- Cuts catalog size from ~14k entries to ~7k real assets

**Phase 1 (commit dd04701) — this session:**
- `manifestApplyDiff` upgraded: replaced `assetCatalogSetLoadState()` stubs with `catalogLoadAsset()` / `catalogUnloadAsset()` proper lifecycle calls. For bundled base-game assets these are no-ops; for mod assets they manage ref_count and free data when it hits 0.
- `manifestEnsureLoaded` upgraded: same fix for the late-add path.
- Added `manifestMPTransition()`: uses `g_ClientManifest` (populated via SVC_MATCH_MANIFEST) as the "needed" manifest, diffs against `g_CurrentLoadedManifest`, applies load/unload. Declared in `netmanifest.h`.
- `mainChangeToStage()` updated: routes to MP or SP path based on `g_ClientManifest.num_entries > 0`; clears `g_ClientManifest` on non-gameplay transitions to prevent stale MP manifest leaking into SP.
- Verification sweep: `assetCatalogSetLoadState` has zero callers remaining. `catalogComputeStageDiff` (old C-9 stub) confirmed unused.
- Both targets build clean: 468/468 client, 58/58 server.

### Decisions Made
- `manifestMPTransition` uses `g_ClientManifest` directly as the "needed" manifest (server already built it correctly via Phase B machinery).
- SP path unchanged: `manifestBuildMission` → diff → apply.
- MP/SP disambiguation: `g_ClientManifest.num_entries > 0` is the signal; cleared on menu transitions to avoid leakage.

### Next Steps
- Playtest: two consecutive SP missions — verify MANIFEST-SP: log lines show correct load/unload/keep counts; verify Joanna stays in to_keep on mission 2.
- Playtest: MP match start — verify MANIFEST-MP: log lines appear instead of MANIFEST-SP: for the match load.
- Phase 2: dependency graph (character → body + head + anims + textures).

---

## Session S109 -- 2026-04-01

**Focus**: Credits/storyboard, updater fix, mission select UX redesign, v0.0.22 release, catalog investigation

### What Was Done

**Credits + storyboard removal** (commit f5cf0da):
- Debug > About: changed title to "Perfect Dark 2.0, MikeHazeJr"
- Intro screen: added "PD2 Port Director: MikeHazeJr" to g_LegalElements[] in src/game/title.c
- Removed F11 storyboard system entirely: deleted pdgui_storyboard.cpp, pdgui_storyboard.h, pdgui_menubuilder.cpp, pdgui_menubuilder.h; removed all call sites from pdgui_backend.cpp
- Build: 719/719 clean

**Updater download freeze fix** (commit 3b593ed):
- Fixed UPDATER_ASSET_ZIP_SUFFIX in updater.h: "-win64.zip" → ".zip" (matching actual release naming)
- Fixed double SDL_LockMutex in downloadThread ZIP validation path causing deadlock on failed downloads
- Root cause of v0.0.20→v0.0.21 update freeze: URL couldn't find asset + game froze instead of showing error

**Mission select UX redesign** (pdgui_menu_solomission.cpp):
- Flat selectable list with 3 blip dots per row (lit=beaten on that difficulty, unlit=not yet)
- Click accessible mission → difficulty selection screen (shows record times per difficulty)
- Blip hover: shows difficulty name + best time
- Row hover: shows unlockable count ("0/1" per stage); omitted if 0/0
- Chapter header hover: shows summed group count ("0/3")
- Difficulty hover: shows objectives filtered to that difficulty's bit in objectivedifficulties
- Removed unused: computeAvailableStageCount, SoloRewardStub, soloGetReward, renderRewardTooltip

**v0.0.22 released to GitHub**:
- Full distribution ZIP (EXEs + data/ + .sha256 sidecars) — 26.1 MB
- ZIP named PerfectDark-v0.0.22.zip to match updater expectations
- release.ps1 hung again; manual release via gh CLI
- Previous releases were EXE-only (missing data/), now fixed

**Catalog investigation findings**:
- All 14,087 entries show "Loaded" — by design: bundled assets set ASSET_STATE_LOADED + ASSET_REF_BUNDLED at registration. State tracking works but only meaningful for mod assets.
- Numeric aliases (anim_NNN, tex_NNN, weapon_NNN, etc.) are full duplicate catalog entries, not lightweight pointers. Doubles catalog size from ~7k real assets to ~14k.
- Aliases are unnecessary: reverse-index maps (source_filenum/texnum/animnum) already handle index-to-entry lookups in the intercept layer.
- Recommended fix: remove aliases entirely, have manifest use human-readable catalog IDs ("base:falcon2" not "weapon_46"). Cuts catalog in half.

### Decisions Made
- Manifest should speak human-readable catalog IDs natively — no numeric alias indirection
- Alias removal becomes Phase 0 of the manifest lifecycle sprint
- release.ps1 needs rework (hangs every time) — low priority but noted

### Bugs Fixed This Session
- B-56: ImGui duplicate ID in arena dropdown — FIXED (PushID/PopID around selector)
- B-57: Scenario save weapon persistence — FIXED (serialize g_MpSetup.weapons[] alongside weaponset)
- B-58: catalogResolveByRuntimeIndex assert type=16 — FIXED (bounds checks against catalogGetNumHeads/Bodies)

### Next Steps
- Playtest v0.0.22 (updater download, credits, mission select UX, theme system)
- Begin manifest lifecycle sprint: Phase 0 (alias removal), then Phases 1-6
- Fix release.ps1 hanging issue (eventually)

---

## Session S106 -- 2026-04-01

**Focus**: Solo Campaign mission select — full redesign + NULL crash fix

### What Was Done

**File changed**: `port/fast3d/pdgui_menu_solomission.cpp`

**Root cause fixed (0xc0000005 crash)**: `langGet()` returns NULL when a language bank entry is zero or not loaded. Multiple ImGui paths (`ImGui::Button(nullptr)`, `ImDrawList::AddText(..., nullptr)`) crash on NULL. Fixed by adding a `langSafe()` helper that wraps `langGet()` with a `""` fallback, then replacing all 11 `langGet()` call sites that feed ImGui text functions (across `renderDifficulty`, `renderBriefingImpl`, `renderAcceptMission`, `renderAbortMission`, `renderOptions`).

**New `renderMissionSelect()` — tree-based chapter/mission hierarchy**:
- All 21 solo missions displayed as collapsible `ImGui::TreeNodeEx` sections
- Chapters 1–9 with langGet header names (fallback "-- Mission N --")
- Each mission shows chapter.position number prefix (e.g. "1.1 dataDyne Central - Defection")
- Mission nodes display colored A/S/P badge circles reflecting `g_GameFile.besttimes[i][d]` completion state (green=Agent, blue=SA, gold=PA, gray=unbeaten)
- Per-difficulty checkpoint rows inside each node; locked rows show dimmed dummy, unlocked rows are Selectable
- Hover tooltip via `renderRewardTooltip()` — fully stubbed (placeholder data structure for future unlock system wiring)
- Special Assignments (stages 17-20) in their own chapter group with gold text
- `pendingStage/pendingDiff` flag defers `menuPushDialog()` until after `ImGui::EndChild()` + `ImGui::End()` to avoid game state changes mid-render
- Mission selection directly sets `g_MissionConfig` + difficulty and pushes `g_AcceptMissionMenuDialog` — no intermediate difficulty dialog

**Stub infrastructure added** (after `computeAvailableStageCount()`):
- `langSafe(s32 textid)` — NULL-safe langGet wrapper
- `struct SoloRewardStub` + `soloGetReward(stageIdx, diff, tier)` + `renderRewardTooltip(stageIdx, diff)` — placeholder for future unlock/reward system
- `k_DiffBadgeColor[]` / `k_DiffShort[]` — badge styling constants
- `k_MissionGroups[]` + `stageToGroupIdx()` — chapter grouping table

**Build**: Clean compile, zero errors (2 pre-existing format-zero-length warnings on empty TreeNodeEx labels, 1 pre-existing `/*` in comment). Verified directly with `ninja PerfectDark.exe` on existing configured build.

### Decisions Made
- Eliminated intermediate difficulty dialog — chapter tree rows go directly to Accept Mission. Cleaner UX, fewer NULL crash surfaces.
- Reward tooltip is fully stubbed. No unlock system wiring yet — hook point is clear for a future session.
- `langSafe()` applied universally across all `langGet()` → ImGui paths in the file. The only safe caller of raw `langGet()` is `snprintf("%s", langGet(...))` (safe because glibc prints "(null)").

### Next Steps
- Playtest: verify chapter tree renders, completion badges show correctly, mission launches work
- Wire `soloGetReward()` when the unlock/reward system is implemented
- Commit this change

---

## Session S105 -- 2026-04-01

**Focus**: Level Editor foundation — new tab in the room/lobby screen

### What Was Done

**Level Editor tab added to `port/fast3d/pdgui_menu_room.cpp`** (tab index 3, "Level Editor"):

**State + data tables** inserted after scenario save state vars (~line 363):
- `le_spawned_t` struct: id, asset_type, pos[3], scale[3], uniform_scale, tex_override_idx, collision, interaction
- `le_cat_entry_t` struct: id, type (catalog snapshot entry)
- Static arrays: `s_LESpawned[128]`, `s_LECatalog[512]`
- `s_LETypeFilters[]` table: All / Props / Characters / Weapons / Models / Vehicles / Maps / Textures / Skins
- `s_LEInteractNames[]`: Static, Pickup, Use, Door
- `s_LEActive`, `s_LECamPos/Yaw/Pitch` (stub free-fly camera state)
- `leCatalogCollect()` callback + `leBuildCatalog()` snapshot builder (all types or filtered type)

**Three new render functions**:
- `renderLevelEditorTab(panelW, panelH)` — left panel: type dropdown, search field, catalog entry list with `[TYPE] asset_id` rows, "Spawn at Camera" button
- `renderLevelEditorObjectPanel(panelW, panelH)` — right panel (replaces player list): spawned objects list with X delete buttons, property editor (position read-only, scale X/Y/Z + uniform toggle, collision checkbox, interaction type dropdown, texture override dropdown)
- `renderLevelEditorOverlay()` — top-right floating window when editor is active: camera stub (pos/yaw/pitch), object count, selected object summary, "Exit Level Editor" button

**Wired into room screen**:
- `s_TabNames[]` expanded from 3 → 4 entries
- Tab loop changed from `t < 3` → `t < 4`
- Content switch: `case 3: renderLevelEditorTab(leftW, contentH)`
- Right panel: `if (s_ActiveTab == 3) renderLevelEditorObjectPanel(rightW, contentH) else renderPlayerPanel(...)`
- Footer: `if (s_ActiveTab == 3)` → "Launch Level Editor" button (available to all players, not just leader); sets `s_LEActive=true`, resets spawned list
- Overlay call after `ImGui::End()` when `s_LEActive`

**Build**: Compiled clean with exact CMake flags (c++20, -std=c++20). Zero errors, one pre-existing `/*` in comment warning.

### Decisions Made
- Free-fly camera is a **stub** — overlay shows cam pos/yaw/pitch variables but no actual game-camera binding yet. Actual free-fly needs game-side work (player.c or a new spectator camera).
- Empty level loading is also a **stub** — "Launch Level Editor" sets `s_LEActive` and logs intent; no actual empty stage is loaded yet. That requires a new GAMEMODE or a debug stage.
- Texture override shows "Texture 0..N" numbered slots — a proper texture name browser requires an iterator+name accessor that doesn't exist yet.
- All sizes use `pdguiScale()` throughout.

### Next Steps
- Playtest: verify "Level Editor" tab appears in room screen; catalog browser populates; property editor works
- Wire free-fly camera: add spectator/noclip mode to player.c or add a dedicated camera actor
- Wire empty level launch: route "Launch Level Editor" to load a minimal empty stage
- Replace numbered texture slots with actual catalog texture names when a by-index name accessor is available

---

## Session S104 -- 2026-04-01

**Focus**: Pre-match countdown UX — 3-2-1-GO overlay with cancel support

### What Was Done

**Commit**: `2a527d1` — `feat(ux): pre-match countdown popup with cancel support`

**Files changed**: `port/include/net/netmsg.h`, `port/src/net/netmsg.c`, `port/src/net/net.c`, `port/fast3d/pdgui_bridge.c`, `port/fast3d/pdgui_countdown.cpp` (new), `port/fast3d/pdgui_backend.cpp`, `port/fast3d/pdgui_menu_mainmenu.cpp`, `port/include/net/netmanifest.h`

1. **Network layer** — two new messages:
   - `CLC_LOBBY_CANCEL (0x0F)`: any CLSTATE_PREPARING client → server, abort countdown
   - `SVC_MATCH_CANCELLED (0x64)`: server → all clients, broadcast canceller name + reset client countdown state
   - `readyGateAbort()`: resets `s_ReadyGate`, returns all `CLSTATE_PREPARING` clients to `CLSTATE_LOBBY`, transitions room to `ROOM_STATE_LOBBY`, broadcasts `SVC_MATCH_CANCELLED`
   - Both messages registered in `net.c` dispatch tables

2. **Bridge functions** (`pdgui_bridge.c`):
   - `netLobbyRequestCancel()` — sends `CLC_LOBBY_CANCEL` from C++ overlay
   - `pdguiCountdownIsActive()`, `pdguiCountdownGetSecs()` — read `g_MatchCountdownState`
   - `pdguiCancelledIsActive()`, `pdguiCancelledGetName()`, `pdguiCancelledClear()` — read/clear `g_MatchCancelledState`

3. **ImGui overlay** (`pdgui_countdown.cpp`):
   - Triggered by `MANIFEST_PHASE_LOADING` countdown; no-op otherwise
   - Full-screen dim + centered popup box with large number (3/2/1) or "GO!"
   - Number colors: yellow → orange → red → green(GO)
   - Audio: `PDGUI_SND_SUBFOCUS` on each tick, `PDGUI_SND_SUCCESS` on GO
   - ESC / GamepadFaceRight sends cancel; cancel banner fades over 3s
   - Called from `pdguiRender()` after lobby sidebar

4. **Bug fix**: `netmanifest.h` `extern "C"` guard was closing before all function declarations — moved close brace to end of file; fixes C++ linkage for `manifestGetMaxEntries`/`manifestSetMaxEntries`

### Decisions Made
- Cancel is allowed by ANY player in CLSTATE_PREPARING (not just the leader) — matches spec
- Cancel ignored silently if no countdown is active (race-condition safety)
- `g_MatchCountdownState` cleared on cancel receipt so countdown overlay dismisses immediately
- Cancel banner auto-clears after 180 frames (3s), then calls `pdguiCancelledClear()`

### Next Steps
- In-game test: start a networked match, verify 3-2-1-GO overlay appears and cancel works
- Consider: should leader-only cancel be an option? Currently any player can cancel

---

## Session S103 -- 2026-04-01

**Focus**: Group 6 Training Mode — ImGui dialog replacements for Firing Range, Dark Training, Holo Training, Bio screens

### What Was Done

**Files changed**: `port/fast3d/pdgui_menu_training.cpp` (new), `port/include/pdgui_menus.h`

1. **Created `pdgui_menu_training.cpp`** — 22 training dialogs: 12 with ImGui renderers, 10 with NULL renderFn (3D model/GBI screens preserved legacy):
   - `renderFrDifficulty` — Bronze/Silver/Gold difficulty selector with score-tier locking via `ciGetFiringRangeScore`
   - `renderFrTrainingInfo` — pre/in-game details (5 label rows + scrollable weapon description + action buttons)
   - `renderFrStats` — Completed/Failed headline + all stats rows + Continue button (shared for both states)
   - `renderBioText` — scrollable misc bio text
   - `renderDtResult` — time + tip rows for DT failed/completed
   - `renderHtList` — selectable list with keyboard nav, PushID/PopID, writes `var80088bb4` to track selection
   - `renderHtResult` — time + tip rows for HT failed/completed
   - `renderNowSafe` — simple centered safe/unsafe message
   - NULL registrations: `g_FrWeaponListMenuDialog`, `g_FrWeaponDetailsMenuDialog`, `g_HtDetailsMenuDialog`, `g_HangarBioListMenuDialog`, `g_HangarBioDetailsMenuDialog`, `g_HangarVehicleListMenuDialog`, `g_HangarVehicleHolographMenuDialog`, `g_HangarVehicleDetailsMenuDialog`, `g_HangarLocationListMenuDialog`, `g_HangarLocationDetailsMenuDialog`
2. **Wired into `pdgui_menus.h`** — added `pdguiMenuTrainingRegister` declaration and call in `pdguiMenusRegisterAll()`
3. **Build verified** — zero errors, zero warnings (fixed one `/*` in comment string)

### Decisions Made
- 3D model screens (MENUITEMTYPE_MODEL) and GBI vehicle holograph registered with NULL renderFn — legacy rendering preserved cleanly
- Text accessors called with `nullptr` item arg (they never dereference it for these dialogs)
- `var80088bb4` written directly to track HT list selection before `menuPushDialog(&g_HtDetailsMenuDialog)`

### Next Steps
- Playtest training mode: FR difficulty selector, FR session info, FR stats, DT result screens, HT list + detail flow, Bio text scroll
- Check that NULL-registered dialogs (weapon list, weapon details, vehicle holograph) still render via legacy path

---

## Session S102 -- 2026-04-01

**Focus**: Update Settings UI — multiple bugs fixed, new feature, release v0.0.17

### What Was Done

**Files changed**: `port/fast3d/pdgui_menu_update.cpp`, `port/src/updater.c`, `port/fast3d/pdgui_menu_mainmenu.cpp`, `CMakeLists.txt`

1. **B-N/A: Download/Switch buttons missing in Updates tab** — `assetUrl` was empty for releases where the GitHub API didn't return an asset. Added fallback URL construction from tag name in `parseRelease()` in `updater.c`.
2. **B-N/A: "Update Now" banner button does nothing** — same root cause as above. Resolved by the assetUrl fallback.
3. **B-N/A: Notification banner at top** — moved from Y=0 to `io.DisplaySize.y - barHeight` (bottom dock).
4. **B-N/A: Changelog shows literal `\r\n`** — added `jp_copystr_unescape()` in `updater.c`; swapped in for `rel->body` parse.
5. **B-N/A: Version list UI polish** — tightened Title column (fixed 190px), color-coded rows (current=green, cached=yellow, other=gray), removed `(current)` text label.
6. **Feature: Update Channel selector** — added Stable/Dev combo to Game settings tab. Persists to `pd.ini` via `configRegisterInt` PD_CONSTRUCTOR in `updater.c`. Channel feeds into auto-checker.
7. **B-60: Stray 'g'+'s' behind Settings tab bar** — NOT FIXED. Investigated extensively; source not confirmed. Filed as B-60. See bugs.md.

### Decisions Made
- Bumped version to 0.0.17 (0.0.16 was the target but incrementing one more per Mike's request)
- Released as prerelease/dev build at https://github.com/MikeHazeJr/perfect-dark-2/releases/tag/v0.0.17

### Next Steps
- Investigate B-60 (stray 'g'+'s' in Settings tab bar) — likely fix: `GetForegroundDrawList()` for title text in `drawPdWindowFrame`, or check runtime Y values
- Playtest update download/switch flow to confirm buttons work

---

## Session S101 -- 2026-04-01

**Focus**: Critical bug investigation — Objective 1 crash (0xc0000005, manifest overflow)

### What Was Done

**Bug B-59: SP manifest overflow → crash in body/model allocation**

Full root-cause trace:

1. **Confirmed source is clean**: `anim.c` and `texdecompress.c` have no `manifestEnsureLoaded` calls — the `a4cd903` fix is intact. No worktree clobber.
2. **The crash log Mike showed had type=6 (ANIM) / type=7 (TEXTURE) late-adds**: those can only come from the pre-`a4cd903` binary. The crash log was from a stale build, not a regression.
3. **Root cause identified**: `manifestSPTransition` is called from `mainChangeToStage` before `g_StageSetup.props` is populated. So `manifestBuildMission` adds only stage + Joanna body/head — NOT the 60–100+ NPCs and prop models that a large mission actually needs. All of those become late-adds via `manifestEnsureLoaded` during stage load. Obj 1 has enough unique bodies + models to overflow `MANIFEST_MAX_ENTRIES = 128`, silently dropping entries. Dropped entries never reach `assetCatalogSetLoadState → ASSET_STATE_LOADED`, corrupting catalog state → 0xc0000005 in body/model allocation.
4. **Fix (commit c336257)**: Raised `MANIFEST_MAX_ENTRIES` from 128 to 1024. PC port has no memory constraint; 1024 covers the largest SP missions and any foreseeable MP lobby. Both `match_manifest_t` and `manifest_diff_t` use the constant — all arrays expanded together. Structs are static/global; no stack impact.

### Decisions Made
- 128 entry cap is an artificial legacy limit — removed per Mike's directive
- The late-add mechanism remains correct; just needed room to never drop
- Pre-scan timing (`manifestSPTransition` before props load) is a known limitation; late-adds via `manifestEnsureLoaded` remain the correct safety net
- Did NOT change the pre-scan timing — that would require knowing when `g_StageSetup.props` is ready vs when `mainChangeToStage` fires

### Next Steps
- Playtest Objective 1 to confirm crash gone
- Watch for "MANIFEST: manifest full" warnings — should not appear with 1024 limit

---

## Session S100 -- 2026-04-01

**Focus**: Group 2 End Screens — wire `pdguiMenuEndscreenRegister` into `pdgui_menus.h`

### What Was Done

`pdgui_menu_endscreen.cpp` (~885 lines) was already committed in `795ff96` but `pdguiMenuEndscreenRegister` was never declared or called in `pdgui_menus.h`, leaving all Group 2 dialogs dormant.

**Fix (commit 9a376eb):**
- Added `void pdguiMenuEndscreenRegister(void);` declaration to `pdgui_menus.h`
- Added `pdguiMenuEndscreenRegister();` call in `pdguiMenusRegisterAll()`
- Required cmake re-run (GLOB_RECURSE cache was stale for the `.cpp`)
- Build clean: 526 objects, both `pd` and `pd-server` link

### End Screen Coverage (already in 795ff96, now active)

**Solo end screens (polished, 2-column layout):**
- Mission Completed / Failed root dialogs + all sibling noops (objectives, retry, next, continue/retry)
- Two-column Table: left = stats (Status / Agent / Time / Difficulty / Kills / Accuracy with progress bar / shot breakdown), right = objectives filtered by difficulty with [+]/[X]/[ ] icons
- Cheat unlock section in gold (conditional)
- Retry | Next Mission (completed) or Retry | Main Menu (failed) buttons
- Keyboard/gamepad: Escape → Main Menu

**2P split-screen end screens:** H + V variants, completed + failed (4 root dialogs + 2 noops)

**MP end screens:**
- Ind Game Over / Team Game Over: rankings table (team-aware, local player in gold)
- Challenge Completed / Cheated / Failed: green/red palette with headline
- Player Ranking, Team Ranking, Player Stats: noopRender (registered to suppress legacy, root draws all tabs)
- Keyboard / controller left as legacy: `g_MpEndscreenSavePlayerMenuDialog`, `g_MpEndscreenConfirmNameMenuDialog`

### Decisions Made
- `pdgui_bridge.c` already had all 15 endscreen bridge functions from prior sessions
- Sibling dialogs registered as `noopRender` — suppress legacy, root panel draws everything
- `endscreenAdvance()` + `menuhandlerAcceptMission(MENUOP_SET, NULL, NULL)` for Next Mission (bypasses sub-dialog state machine complexity)

### Next Steps
- Playtest: complete a solo mission → verify ImGui end screen appears (completed and failed)
- Playtest: complete an MP match → verify rankings table and challenge overlays
- Group 4 expansion: simulants/weapons sub-menus still pending

---

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
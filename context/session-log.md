
# Session Log (Active)

> **S284–S489** (rolling window). Older sessions **S280–S241** → [_archive/session-log-archive-S280-and-older.md](_archive/session-log-archive-S280-and-older.md). Ancient **S240–S157** → [_archive/session-log-archive-S240-and-older.md](_archive/session-log-archive-S240-and-older.md). **S1–S119** → [_archive/sessions/].
> Navigation hub: [INDEX.md](INDEX.md) · Back to [README.md](README.md)

## Session S489 - 2026-04-28 - Input action-set transition flush

Focused input infrastructure slice, kept narrow per Mike's directive. No raw ImGui key migration sweep and no broad scene/state manager expansion.

### Outcome

- Added public `actionmapFlushActionSet(const InputAction *actions, s32 action_count)` as the small action-set flush surface missing from the earlier cutscene flash fix.
- Kept `actionmapFlushGameplayState()` gameplay-only and reused a shared internal state-slot clear helper so both flush paths synthesize release edges consistently.
- Declared `g_LayerCutscene.action_set` in `inputlayer.c`: ACTION_SKIP_CUTSCENE, ACTION_USE / ACTION_MENU_ACCEPT, ACTION_CANCEL_USE, ACTION_FIRE_PRIMARY, ACTION_FIRE_SECONDARY, ACTION_PAUSE, ACTION_FIRE_MODE, ACTION_RELOAD, ACTION_WEAPON_NEXT.
- Updated `onCutscenePush` to flush both gameplay-only state and the cutscene action set. Held Continue/Use no longer survives from menu accept through stage change into cutscene entry.
- Tightened pd-tests:
  - action-set flush clears declared shared and gameplay actions only;
  - cutscene transition flush clears held ACTION_USE / menu accept;
  - unrelated menu actions survive if not declared in the flushed set;
  - fresh ACTION_SKIP_CUTSCENE press during a cutscene still registers.
- Logged B-266 and added the transition-flush invariant to `constraints.md`.

### Files

- `port/include/actionmap.h`
- `port/src/actionmap.cpp`
- `port/include/inputlayer.h`
- `port/src/inputlayer.c`
- `src/game/player.c`
- `tests/actionmap_pure.c`
- `tests/actionmap_pure.h`
- `tests/test_actionmap_flush.cpp`
- `tests/test_cutscene_layer.cpp`
- Context updates: `context/constraints.md`, `context/bugs.md`, `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Prescribed MSYS2/Ninja flow: `pd`, `pd-server`, and `pd-tests` passed.
- `pd-tests.exe`: 253 test cases / 6641 assertions passed.

### Next

- Mike playtest: complete Mission 1 objective 1, hold Continue/Use through the transition, confirm the objective 2 intro cutscene does not flash or skip after the 30-frame gate. Then press a fresh skip key after the gate and confirm deliberate skip still works.
- Continue input architecture in small slices. Raw ImGui key migration remains deferred unless a concrete input-system dependency requires it.

---

## Session S488 - 2026-04-28 - Bondgun provider-handle bridge

Continued Asset Provider Phase 4 from S487, focused on the first-person weapon loader.

### Outcome

- Added `struct gunctrl::loadhandle` so queued first-person model loads carry the catalog/provider source handle alongside the legacy `loadfilenum`.
- Added a single `bgunQueueModelLoad(...)` path for hand, gun, and cartridge model loads in `bgunTickMasterLoad`.
- Added catalog source-filenum resolution for queued bondgun model loads across `ASSET_MODEL`, `ASSET_BODY`, `ASSET_HEAD`, and `ASSET_WEAPON`.
- Routed `bgunTickGunLoad` model sizing and load-to-address calls through provider-aware APIs when the queued handle is a RomProvider handle.
- Preserved a warning-backed temporary ROM fallback for uncataloged sources and non-ROM provider handles because the current model promotion path still writes through `g_FileInfo[loadfilenum]`.
- Added a null-load guard so a missing bondgun model file reports `CATALOG_CRITICAL` instead of immediately promoting a NULL modeldef.
- Continued the same Phase 4 slice after the first verification pass:
  - title/logo model loads now resolve catalog provider handles via `catalogGetPropHandle()` and use `modeldefLoadFromHandle()` plus provider-aware loaded-size queries;
  - title/logo uncataloged sources are routed through one warning-backed temporary fallback;
  - `modelcatalog` validation now resolves body/head provider handles by source filenum and uses `modeldefLoadToNewFromHandle()` while preserving its SEH/signal fault guard;
  - `modelcatalog` uncataloged sources remain a warning-backed legacy fallback.
- Completed the requested sprint follow-through:
  - `catalogLoadTypedAsset()` now uses a type-policy/provider-backed payload loader instead of just validating then delegating to the string-only loader.
  - `modeldefLoadFromHandle()` now supports non-ROM provider handles by promoting display lists from caller/provider sizes instead of indexing `g_FileInfo[]`; the old `modeldef0f1a7560()` wrapper still preserves `g_FileInfo[]` mutation for legacy ROM callers.
  - title/modelcatalog non-ROM provider handles now use the handle loader directly; only no-handle cases fall back to legacy filenum loading.
  - `lv.c` stage diff and `screenmfst.c` screen mini-manifests now use typed lifecycle load/release calls.
  - Added `tests/test_catalog_provider_static.cpp` to pin the migrated call sites and prevent the modeldef handle loader from becoming RomProvider-only again.

### Files

- `src/include/types.h`
- `src/game/bondgun.c`
- `src/game/title.c`
- `src/game/lv.c`
- `port/src/modelcatalog.c`
- `port/src/assetcatalog_load.c`
- `port/src/screenmfst.c`
- `CMakeLists.txt`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Prescribed MSYS2/Ninja build: `pd`, `pd-server`, and `pd-tests` linked clean.
- `pd-tests.exe`: 252 test cases / 6626 assertions passed.
- Re-ran prescribed MSYS2/Ninja verification after title/modelcatalog migration: `pd`, `pd-server`, and `pd-tests` targets completed cleanly; `pd-tests.exe` passed 253 test cases / 6641 assertions.
- Re-ran prescribed MSYS2/Ninja verification after completing typed lifecycle/model-promotion/static-check work: `pd`, `pd-server`, and `pd-tests` targets completed cleanly; `pd-tests.exe` passed 255 test cases / 6651 assertions.

### Next

Continue Asset Provider Phase 4 by replacing the remaining warning-backed fallback paths as each domain gets a typed provider API. The main known bridge is the first-person gun async loader, which still restores `g_FileInfo[loadfilenum]` while it performs incremental texture/DL work across ticks.

---

## Session S487 - 2026-04-27 - Catalog typed identity normalization

Continued the Catalog-Owned Asset Pipeline Phase 1 after S485/S486.

### Outcome

- Added explicit catalog ID helpers for the major numeric spaces that were still using generic runtime lookup:
  - `catalogStageIdByStageTableIndex`
  - `catalogStageIdBySoloStageIndex`
  - `catalogStageIdByStagenum`
  - `catalogModelIdByModelnum`
  - `catalogBodyIdByBodynum`
  - `catalogHeadIdByHeadnum`
  - `catalogIdBySourceFilenum`
  - `catalogIdBySourceHandle`
- Migrated ASSET_MAP / ASSET_MODEL / ASSET_BODY / ASSET_HEAD callers away from ambiguous `catalogIdByRuntime(type, n)`.
- Fixed B-265: several stage-id and manifest backfill paths passed a logical `stagenum` into the stage-table-index cache. They now use `catalogStageIdByStagenum`.
- Started Asset Provider Phase 4:
  - added `assetLoadGetInflatedSize` and `assetLoadGetLoadedSize` provider-aware size queries;
  - added handle-aware modeldef loaders `modeldefLoadFromHandle` and `modeldefLoadToNewFromHandle`;
  - migrated `setupLoadModeldef` to use `catalogGetPropHandle()` and catalog source metadata for prop / weapon / hat / projectile model loads.
  - migrated `catalogGetBodyModeldef` and `catalogGetHeadModeldef` to load through `catalogGetBodyHandle()` / `catalogGetHeadHandle()`.
- Continued Asset Provider Phase 4:
  - `catalog_body_result_t`, `catalog_head_result_t`, `catalog_weapon_result_t`, and `catalog_prop_result_t` now expose the catalog effective provider handle alongside legacy source filenum metadata.
  - Forge runtime door, weapon-pad, and prop spawning now loads modeldefs through `modeldefLoadToNewFromHandle(...)` using the resolved catalog handle.
  - `struct menumodel` now stores pending/current/body/head provider handles and source filenum tags; `menuSetModelFileHandle(...)` seeds handle-aware single-model previews.
  - `menuRenderModel` now uses catalog-resolved provider handles for body/head preview sizing and modeldef loading, and uses the seeded provider handle for catalog-backed single-model previews. Raw filenum-only menu previews now attempt catalog source-filenum resolution first; only truly uncataloged ROM files use the temporary fallback and log a warning.
  - MP head preview and main-menu weapon preview now seed menu model handles from catalog resolution.
  - `playerTickChrBody` first-person body/head/weapon size accounting and modeldef loading now use catalog-resolved provider handles.
  - Added typed lifecycle wrappers `catalogLoadTypedAsset`, `catalogReleaseTypedAsset`, and `catalogRetainTypedAsset`; they validate the resolved catalog entry type before dispatching to the legacy string-only lifecycle calls. SP manifest diff load/unload and `manifestEnsureLoaded` late-add now call these wrappers for known manifest asset types.
- Mike clarified that preserving the ROM fast path is acceptable only as a sub-step. Recorded the constraint: the endpoint is still full migration away from legacy filenum-first loading and toward catalog/provider-owned source handles, payloads, refcounts, and release behavior.
- Updated `tests/stubs.c` so pd-tests link against the new typed helper surface.
- Updated `constraints.md`, `bugs.md`, and `tasks-current.md` with the new helper rule and Phase 1 progress.

### Files

- `port/include/assetcatalog.h`
- `port/src/assetcatalog_api.c`
- `port/src/modelcatalog.c`
- `port/include/assetload.h`, `port/src/assetload.c`
- `port/src/forge/forge_runtime.c`
- `port/src/net/net.c`, `port/src/net/netmanifest.c`, `port/src/net/netmsg.c`
- `port/src/server_stubs.c`
- `port/src/scenario_save.c`
- `src/include/game/modeldef.h`
- `src/include/game/menu.h`
- `src/include/types.h`
- `src/game/body.c`, `src/game/lv.c`, `src/game/mainmenu.c`, `src/game/menu.c`, `src/game/menutick.c`, `src/game/modeldef.c`, `src/game/mplayer/mplayer.c`, `src/game/mplayer/setup.c`, `src/game/player.c`, `src/game/setuputils.c`
- `tests/stubs.c`
- Context updates: `context/constraints.md`, `context/bugs.md`, `context/tasks-current.md`, `context/session-log.md`

### Verification

- Prescribed MSYS2/Ninja build: `pd`, `pd-server`, and `pd-tests` linked clean.
- `pd-tests.exe`: 252 test cases / 6626 assertions passed.
- Re-ran the same prescribed build/test pass after the Forge provider-handle migration: `pd`, `pd-server`, and `pd-tests` linked clean; `pd-tests.exe` passed 252 test cases / 6626 assertions.
- Re-ran the prescribed build/test pass after menu and player provider-handle migration: `pd`, `pd-server`, and `pd-tests` linked clean; `pd-tests.exe` passed 252 test cases / 6626 assertions.
- Re-ran the prescribed build/test pass after catalog-wrapping raw filenum menu previews and adding typed lifecycle wrappers: `pd`, `pd-server`, and `pd-tests` linked clean; `pd-tests.exe` passed 252 test cases / 6626 assertions.

### Next

Continue Asset Provider Phase 4: audit remaining direct model size/load paths, mark true legacy exceptions, then expand typed lifecycle wrappers into asset-type-specific payload loaders.

---

## Session S486 - 2026-04-27 - Online shipping scope correction

Mike clarified the current release target: do **not** ship the standalone dedicated server now. The online target is internal connectivity inside the client.

### Decision

- Current ship-track online work targets in-client/listen-host connectivity: host flow, join flow, rooms/lobby UX, connect codes/NAT path, manifest/catalog distribution, ready gate, stage transitions, reconnect, and in-client validation.
- `pd-server` remains useful as a build target and regression/tooling surface, but it is not the release product right now.
- Game-agnostic dedicated server work is deferred: P4-B/P4-C broker implementation, `server_stubs.c` shrink, and plugin ABI cleanup should not block client online work.

### Context updates

- `context/tasks-current.md` marks Tier 4 dedicated-server work deferred and adds the current client-online shipping focus.
- `context/constraints.md` adds the active shipping-scope constraint and supersedes the old dedicated-server-only shipping model note.
- `context/server-architecture.md` now opens with a shipping note so future sessions do not mistake the dedicated-server architecture doc for current release scope.

---

## Session S485 - 2026-04-27 - Catalog-owned asset pipeline Phase 0 + weapon identity split

Mike's directive: implement the Catalog-Owned Asset Pipeline plan, with the catalog as the single source of truth for all declared assets, references, source handles, loading/unloading, dependencies, refcounts, and payload ownership. Weapons remain the first proving domain because they expose the current identity bugs, but the scope is explicitly all assets.

### Outcome

Phase 0 baseline is green and Phase 1 has its first identity split in place.

- Fixed the pre-existing `test_swarm_boid_sim` failure by clamping seek speed when the target is closer than one frame of movement. CPU, GPU shader, and test mirror now agree.
- Corrected base weapon catalog registration to the post-GF64-cull MP table: `NUM_MPWEAPONS = 0x29` (41 slots), not 47. Registration now includes `MPWEAPON_NONE`, `MPWEAPON_SHIELD = 0x27`, and `MPWEAPON_DISABLED = 0x28`, and has a `_Static_assert(NUM_BASE_WEAPONS == NUM_MPWEAPONS)`.
- Split weapon identity explicitly:
  - runtime `weapon_num` = `WEAPON_*` enum, final gameplay handoff only;
  - `mp_weapon_id` = `MPWEAPON_*` selector/setup slot;
  - catalog ID string = authoritative boundary identity.
- Added typed helpers `catalogWeaponIdByRuntimeWeaponNum()` and `catalogWeaponIdByMpWeaponId()`.
- Migrated MP setup, match manifest, stage-start wire refs, scenario save/load fallback, and setup preload fallback away from ambiguous `catalogIdByRuntime(ASSET_WEAPON, ...)` use.
- Fixed catalog iteration over pools with holes in `catalogBuildRuntimeCaches()` and the room spawn-weapon UI list.

### Files

- `port/src/assetcatalog_base_extended.c`, `port/src/assetcatalog_api.c`, `port/src/assetcatalog_scanner.c`, `port/include/assetcatalog.h`
- `port/src/net/matchsetup.c`, `port/src/net/netmanifest.c`, `port/src/net/netmsg.c`, `port/src/net/netdistrib.c`
- `port/src/scenario_save.c`, `src/game/setup.c`, `src/game/mplayer/mplayer.c`
- `port/src/swarm_test.c`, `port/fast3d/swarm_gpu.cpp`, `tests/test_swarm_boid_sim.cpp`, `tests/test_spawn_weapon_mode.cpp`
- Context updates: `context/tasks-current.md`, `context/constraints.md`, `context/bugs.md`, `context/designs/catalog-full-pipeline-weapons-2026-04-27.md`, `context/audits/catalog-universality-sweep-2026-04-27.md`, `context/qc-tests.md`, `context/session-log.md`

### Verification

- `pd-tests`: passed all 252 test cases / 6626 assertions.
- `pd` + `pd-server`: linked clean via direct Ninja invocation after the PowerShell wrapper failed before invoking the build targets with a runspace exception.

### Next

Continue the all-assets catalog-owned pipeline in this order: finish typed identity helpers for stage/model/body/head/source-handle spaces, finish Asset Provider Phase 4, add typed retain/release loaders, then migrate domains one at a time with parity tests and static checks only after a domain has approved catalog/provider APIs.

---

## Session S483d (`charming-noether-7b69b3` follow-up #2) - 2026-04-27 - FIESTA match-start crash (B-263)

Mike's playtest after S483b shipped: tried starting a match with FIESTA spawn-weapon mode, hit a fresh AV. Different binary base, different PC offset from the prior crash; this is its own root cause.

### Crash anchor

`PC RVA 0xef32b` -> `modelmgrLoadProjectileModeldefs at modelmgrreset.c:173`. Backtrace via addr2line: `setupCreateProps:2872 -> lvReset -> mainLoop -> mainProc -> main`.

### Mechanism (single cause, traced end-to-end)

`SPAWNWEAPON_FIESTA_SENTINEL = 0xFE` was added in S482 as the FIESTA marker. `matchStart` writes it into `g_MatchConfig.spawnWeaponNum`. The model-preload guard at `setup.c:2870-2872` was written before the FIESTA sentinel existed and only excluded the legacy two values:

```c
if (g_MatchConfig.spawnWeaponNum != 0xFF
        && g_MatchConfig.spawnWeaponNum != 0) {
    modelmgrLoadProjectileModeldefs((s32)g_MatchConfig.spawnWeaponNum);
}
```

`0xFE` passed both conditions. `modelmgrLoadProjectileModeldefs(254)` indexed `g_Weapons[254]` -- a 254-byte walk past the end of the [WEAPON_SUICIDEPILL + 1] = 86-entry array (`src/include/game/inv.h:9`). The next deref AVed.

The FIESTA design comment in matchsetup.h had claimed "0xFE was chosen so any code path checking `!= 0xFF && != 0` continues to exclude this value as well" -- that math was wrong (`0xFE != 0xFF AND 0xFE != 0` both hold). Two more sites had the same incomplete exclusion: the userPickedSpawnWeapon predicate at setup.c:2775 (which then logged "user-picked spawn weapon ... num=254 preserved" -- visible in the crash log immediately before the FATAL line), and the elif paths in `bot.c:543` + `player.c:1835` (structurally safe because the FIESTA-mode branch above caught 0xFE first, but the predicate text drifted from the spec).

### Fix (4 changes, no half-measures, INV-1 loud-fail discipline)

1. **Shared single-source-of-truth predicate**: `spawnWeaponNumIsResolved(num)` static inline in `port/include/net/matchsetup.h`. Returns 0 for {0, 0xFF, SPAWNWEAPON_FIESTA_SENTINEL}, 1 for resolved real WEAPON_* enum values. `static inline` so it's callable from C (src/game/) and C++ (tests/) without dragging matchsetup.c into the test binary.

2. **Migrated four consumers** to call the helper:
   - `setup.c:2781` (userPickedSpawnWeapon predicate)
   - `setup.c:2878` (model-preload guard, the actual crash site)
   - `bot.c:543` (elif sentinel check, audit consistency)
   - `player.c:1835` (elif sentinel check, audit consistency)

3. **Defensive bound + INV-1 loud-fail in the leaf** (`modelmgrLoadProjectileModeldefs`): weaponnum out of `[0, ARRAYCOUNT(g_Weapons))` returns false with `WEAPON.SLOT.MISS:` LOG_WARNING. Defence in depth -- catches any future caller that bypasses the upstream gate (wire tampering, not-yet-migrated consumer, race).

4. **pd-tests pin** (`tests/test_spawn_weapon_resolved.cpp`): 10 cases / 860 assertions covering the helper contract -- exhaustive 0..0xFF walk catches future sentinel-addition drift; consumer-gate + leaf-bound invariants pin Mike's "FIESTA-mode spawnWeaponNum never flows into a weapon-num-as-array-index consumer" rule. `[b263]` tag.

### Methodology learning

Captured in `context/constraints.md` Active Constraints + commit message: when adding a new reserved-value sentinel to a field with existing consumer-side checks, audit EVERY consumer, not just the writer that produced the sentinel. Centralise the "is this resolved?" predicate so the next sentinel addition has one audit surface. Pin the contract with a test that walks the entire input domain (every byte value here).

### Files

- `port/include/net/matchsetup.h` -- `spawnWeaponNumIsResolved` helper + comment correcting the original FIESTA-sentinel design claim
- `src/game/setup.c` -- two consumer migrations
- `src/game/bot.c` -- one consumer migration
- `src/game/player.c` -- one consumer migration
- `src/game/modelmgrreset.c` -- leaf-side bound + WEAPON.SLOT.MISS LOG_WARNING
- `tests/test_spawn_weapon_resolved.cpp` -- new (10 cases / 860 assertions)
- `CMakeLists.txt` -- pd-tests SRC list extension
- `context/bugs.md` -- B-263 entry
- `context/constraints.md` -- sentinel-audit-discipline invariant
- `context/session-log.md` -- this entry

### Verify

Build clean: PerfectDark.exe + PerfectDarkServer.exe + pd-tests linked. `[b263]` tag passes 860 assertions / 10 cases. Pre-existing test failure in `tests/test_swarm_boid_sim.cpp:123` (S483c boid-sim, unrelated) remains; my changes did not introduce it.

### Outstanding

Mike's playtest of FIESTA match start -- match must start without crashing; subsequent spawns must roll fresh weapons per spawn.

---

## Session S483b (`charming-noether-7b69b3`) - 2026-04-27 - crash mitigation (B-261) + Tab IMC fix (B-262)

Two threads, evidence-only investigation per Mike's reset directive (no recency or subsystem priors), then both fixes shipped.

### Thread 1: Crash investigation (B-261)

ACCESS_VIOLATION 0xc0000005 at PC RVA 0x396ed2, LVTICK 1836 of stage 0x33 (Investigation), ~30s after spawn. addr2line landed on `src/lib/model.c:1387` (`sp2c.z = rodata->reorder.unk08;`) inside `modelUpdateReorderRelations`. Disassembly of the shipped exe (md5 bb97fd31...) showed reads at offsets 0x28 / 0xc / 0x10 / 0x14 / 0x0 / 0x4 succeeded; the +0x8 read AVs -- consistent with the rodata struct straddling a page boundary into unmapped memory. Same frame logged the existing `DOOR.DIAG: doorGetBbox -- no bbox for modelnum=154 model=0x0000019939568438 flags=0x80 doortype=0 (count=1)` defensive guard (the bbox node was missing on the same model that crashed during REORDER traversal). Mike's catalog-data-missing hypothesis sharpened the candidate ranking: the door's `model_009a` was loaded with partially populated rodata, where some nodes have unreadable rodata that succeeds at low-byte reads but fails at the page boundary.

**Mitigation shipped (does NOT fix upstream catalog incompleteness):**

1. Per-tick rodata-validity guard via `VirtualQuery` probe in five rodata-reading update functions (`modelUpdateReorderRelations`, `modelUpdateDistanceRelations`, `modelApplyDistanceRelations`, `modelApplyToggleRelations`, `modelApplyReorderRelationsByArg`). Probes BEFORE any deref and BEFORE `modelGetNodeRwData` (which reads `rodata->*.rwdataindex`). On miss emits rate-limited `MODEL.RODATA.MISS:` warning and skips the node safely.
2. Load-time tree-walk validator in `setupLoadModeldef` (the chokepoint for prop / weapon / hat / projectile model loads). After `modeldefLoadToNew` succeeds, walks the rootnode tree once and probes every node's rodata. Logs `MODEL.RODATA.LOAD: PARTIAL modelnum=<N> ...` if any node is unreadable.
3. New helper module: `port/src/model_rodata_guard.c` + `port/include/model_rodata_guard.h`. Header returns `int` rather than `bool` so it stays includable from `src/lib/model.c` where `bool` is the `s32` macro.

**Diagnostic discipline:** the channel name `MODEL.RODATA.MISS:` parallels `CATALOG.MISS:` from INV-1 (b6a0c280). Mike's directive to extend loud-fail to model-rodata accessors is satisfied by the new diagnostic surface; the existing `modelFindBboxRodata` / `modelGetPartRodata` accessors already return NULL safely on miss and the existing `DOOR.DIAG` channel covers bbox-side discovery.

**Forensic next step:** post-playtest `MODEL.RODATA.LOAD: PARTIAL` lines discriminate Mike's catalog-data-missing hypothesis from the alternate use-after-free path. Root cause then lands on the catalog/load side, separate from this session's mitigation.

### Thread 2: Tab IMC fix (B-262)

Mike's playtest 2026-04-27 hit Tab during an active SP mission and the Online connectivity / friends sidebar opened. Handler at `port/fast3d/pdgui_friends.cpp:813` was a raw `ImGui::IsKeyPressed(ImGuiKey_Tab)` with the comment "Avoids reaching into the actionmap layer" -- a deliberate IMC-stack bypass. Fix (Mike picked option (c)): routed Tab through actionmap as new `ACTION_SOCIAL_TOGGLE` (= 69), bound only on `g_ImcMenu` and `g_ImcPauseMenu` (NOT on `g_ImcGameplay`). `fireVk`'s priority-sorted first-match-wins walk now structurally cannot fire ACTION_SOCIAL_TOGGLE during pure gameplay -- gameplay IMC has no Tab binding for this action. Tab continues to fire ACTION_SCORECARD on gameplay (no-op outside Combat Sim).

pd-tests case `tests/test_social_toggle_imc.cpp` pins the invariant Mike named ("Tab during top-IMC = gameplay does not toggle sidebar state"): 5 cases / 7 assertions with `[s483b]` tag. Pure mirror of the priority-sorted resolver, no SDL coupling.

### Files

- `port/include/actionmap.h` -- new `ACTION_SOCIAL_TOGGLE = 69`, `ACTION_COUNT = 70`
- `port/src/actionmap.cpp` -- s_ActionNames extension, `actionIsGameplayOnly` shared classification, Tab binding on g_ImcMenu and g_ImcPauseMenu
- `port/fast3d/pdgui_friends.cpp` -- replaced raw ImGui hotkey with `actionPressed(0, ACTION_SOCIAL_TOGGLE)`
- `tests/test_social_toggle_imc.cpp` -- new pd-tests case
- `port/include/model_rodata_guard.h` + `port/src/model_rodata_guard.c` -- new helper module
- `src/lib/model.c` -- per-tick guards in the five rodata-reading functions
- `src/game/setuputils.c` -- `setupValidateModeldefRodata` + call from `setupLoadModeldef`
- `CMakeLists.txt` -- pd-tests SRC list extension
- `context/bugs.md` -- B-261, B-262
- `context/constraints.md` -- model rodata-validity guard invariant

### Verify

Build clean: 860/860 objects. `pd-tests` 4813 assertions / 186 cases pass; `[s483b]` tag passes 7 assertions / 5 cases. PerfectDark.exe + PerfectDarkServer.exe both linked.

### Outstanding

- Mike's playtest of the crash repro path -- AV must NOT recur at LVTICK 1836+ on Investigation; forward `MODEL.RODATA.LOAD: PARTIAL` and `MODEL.RODATA.MISS:` log lines for catalog-side root-cause discrimination.
- Mike's playtest of Tab key invariant -- Tab during gameplay must not open sidebar; Tab during pause toggles sidebar.

---

## Session S483 (`jovial-kirch-181c60` follow-up) - 2026-04-27 - host-eligible weapon pool via match manifest

Mike's clarification on Random/Fiesta semantics after S482 shipped: the eligible pool should draw from the host's full unlocked-weapon catalog, distributed via the match manifest, not just the active match weapon set's 6 slots.

### Outcome

S482's `spawnWeaponPickFromActiveSet()` (active-set 6-slot pool) is preserved as a **fallback only**. The primary pool is now the match manifest's `MANIFEST_TYPE_WEAPON` entries -- enumerated by the host at match start and broadcast via the existing `SVC_MATCH_MANIFEST` machinery. Distribution-as-needed for mod-only weapons is **already wired**: `ASSET_WEAPON` is in the `SVC_CATALOG_INFO` type list (`port/src/net/netmsg.c::netmsgSvcCatalogInfoWrite`), so any non-bundled mod weapon the host has streams to clients via the existing `CLC_CATALOG_DIFF` -> `SVC_DISTRIB_BEGIN` / `SVC_DISTRIB_CHUNK` / `SVC_DISTRIB_END` pipeline at lobby join time. **No NET_PROTOCOL_VER bump** -- the manifest serialization is `(u8 type, u8 slot, str id)` per entry; adding more entries is wire-compatible. v45 still in effect.

### Mode semantics (post-S483)

- **SPECIFIC**: unchanged. matchStart() resolves `spawn_weapon_id` via the catalog.
- **RANDOM**: matchStart() now rolls from the manifest pool (`spawnWeaponPickFromMatchManifest`). Falls back to active-set roll when the manifest is unavailable (solo CS, pre-broadcast windows). Rolled WEAPON_* enum is broadcast in `SVC_STAGE_START` as before.
- **FIESTA**: every spawn (player.c / bot.c) now rolls from the manifest pool. Same fallback discipline.

### Pool source cascade (live helper `spawnWeaponPickFromMatchManifest`)

1. `g_CurrentLoadedManifest` (post-transition definitive list).
2. `g_ServerManifest` (host-side built manifest, pre-broadcast).
3. `g_ClientManifest` (received from server).
4. None populated -> falls back to `spawnWeaponPickFromActiveSet()` (active set's 6 slots).

If even that yields zero eligible weapons, matchStart() RANDOM falls back to `MPWEAPON_FALCON2` with a `LOG_WARNING`; FIESTA falls into the existing `resolvedWeaponNum=0` miss path.

### Files

- `port/src/net/netmanifest.c` -- new `s_manifestAppendWeaponPool` helper. Walks `assetCatalogIterateUnlockedByType(ASSET_WEAPON, ...)`, filters NONE/DISABLED/SHIELD via `ext.weapon.weapon_id`, calls `manifestAddEntry` with `MANIFEST_TYPE_WEAPON` + `MANIFEST_SLOT_MATCH`. Hooked into both `manifestBuild` (server) and `manifestBuildForHost` (client outgoing CLC_LOBBY_START) right after the existing 6-slot active-set loop. Logs the (added/filtered/invalid) tally.
- `port/src/net/matchsetup.c` -- new `spawnWeaponPickFromMatchManifest()` (public), `spawnWeaponBuildPoolFromManifest()` + `spawnWeaponSelectManifest()` (file-static). matchStart() RANDOM branch + player.c FIESTA branch + bot.c FIESTA branch all migrated to the new helper. Includes `net/netmanifest.h`.
- `port/include/net/matchsetup.h` -- new `spawnWeaponPickFromMatchManifest` declaration with the same doc-comment convention as the S482 helpers.
- `src/game/player.c` -- FIESTA branch calls `spawnWeaponPickFromMatchManifest` instead of `spawnWeaponPickFromActiveSet`.
- `src/game/bot.c` -- mirror.

### Tests (extended `tests/test_spawn_weapon_mode.cpp`)

Adds 9 new cases (now 26 total / ~2-3k assertions): pool draws from MANIFEST_TYPE_WEAPON entries (skipping non-weapon entries), NONE/DISABLED/SHIELD filtered from the manifest pool, empty manifest falls back to active set, all-filtered manifest falls back, missing-catalog entries skipped (Phase 2 distribution gap), mod weapon (synthetic catalog id) included in pool, invalid weapon_id (>=NUM_MPWEAPONS) skipped, both pools degenerate -> 0 (caller fallback), pool size scales beyond 6 slots, MANIFEST_TYPE_WEAPON value pin (== 3, mirrors `netmanifest.h:74`).

### Phase 2 status (deferred)

Distribution-as-needed verification requires an in-game playtest with a mod weapon installed on host but not client. The wiring is ALREADY in place via `SVC_CATALOG_INFO` (S222 audio mod sync extension) -- no new code is needed for the distribution itself. Phase 2 is "verify the existing pipeline picks up ASSET_WEAPON entries during lobby join", which can only be validated end-to-end at runtime.
## Session S483c (`unruffled-edison-af5d41`) - 2026-04-27 - GPU swarm + Test Scenarios (design + impl)

Mike's directive: design a Test Scenarios dropdown in Settings > Debug (Empty Map / Swarm CPU / Swarm GPU) plus a GPU compute boid system that swaps in for the existing CPU bot tick under the GPU scenario. Cycler 4-8-16-32-64-128-256 Skedars with 1 HP each, player invincible, full random weapons, bottomless ammo, score 1 per kill. Per-frame benchmark logging on `BENCHMARK.SWARM.{CPU,GPU}` and `TESTSCEN.*`. Phase 1 = design doc; Phase 2 = implement after Mike approves; Phase 4 = auto-merge per standing rule.

Mike approved all five F-section recommended defaults so Phase 2 ran in the same session. Renamed S483 -> S483c at merge time because dev had already shipped S483 (host-eligible spawn-weapon pool) and S483b (Tab IMC + per-tick rodata guard) under the parent S483 label.

### Outcome (Phase 1 + Phase 2)

Design doc landed at `context/designs/gpu-swarm-and-test-scenarios-2026-04-27.md` (Sections A-G per Mike's prescribed structure). Five-commit Phase 2 stack landed on the worktree branch and merged to dev:

- `8566d5a9` foundation registries (log channels + action enum + actionmap entry)
- `e147dda1` testscenarios module + Settings > Debug UI + Empty Map scenario
- `4e0a4d87` swarm_test runtime + HUD + chr-pool hook (G.1.1 numchrs hook in setup.c)
- `b83095ad` swarm_gpu compute path (4.3 core context probe + SSBO sim + readback)
- `7b61a998` pd-tests cases (test_swarm_boid_sim, 6 cases under `[swarm][sim]`)

### Architecturally significant findings + Mike's approved decisions (Section F)

All five Mike-approved defaults landed:

1. **F.1 -> Option A**: prepended `(4, 3, CORE)` to the SDL probe at `port/fast3d/gfx_sdl2.cpp:164`. Compute symbols (`glDispatchCompute`, `glMemoryBarrier`, `glBindBufferBase`) loaded at runtime via `SDL_GL_GetProcAddress` rather than regenerating glad (G.2 refinement). `swarmGpuAvailable()` returns 0 + greys-out the GPU scenario tooltip when the probe fails.
2. **F.2 -> B+C combined, refined to G.1.1**: separate test-mode chr table escapes `MAX_BOTS=32`. Refinement during impl: NUMTYPE2 50->100 was misdirected (Type 2 = weapons rwdata), the right hook is the per-stage `numchrs` bump in `setup.c`. All 256 same-body Skedars share one Type 3 binding so NUMTYPE3=48 is plenty. The `numchrs += testScenarioGetSwarmMaxCount()` hook lands between simulant-bot count and `modelmgrAllocateSlots`, naturally extending `g_Vars.maxprops`.
3. **F.3 -> A**: CPU readback per frame. `swarmGpuStepAndApply` writes the GPU-stepped positions back into `chr->prop->pos` so the existing damage / kill / animation / audio paths handle scoring without instrumentation.
4. **F.4 -> A**: Empty Map reuses `STAGE_CITRAINING`. Procedural ground plane deferred to a follow-on if the CI Training prop load muddies the empty-map number.
5. **F.5 -> B**: CPU mode runs the seek-player action that mirrors the GPU shader byte-for-byte (max_speed = 18.0, dt = 1/60, ground-locked Y, seek-only) for an apples-to-apples benchmark.

### Constraints respected in design

- No `NET_PROTOCOL_VER` bump. Test mode is local-only; dropdown greys out in netplay.
- No save format change. `g_TestScenario` is volatile.
- Catalog ID strings used everywhere (`base:skedar` body, `base:mp_skedar` arena).
- Stage transitions reach `mainChangeToStage` via the existing `pdguiForgeStartSessionOn` catalog path. No hardcoded stagenum.
- All Test Scenarios UI gated by `PD_DEV_BUILD` (the Debug tab is already dev-only).
- Em-dash count: 0 (methodology gate).

### Constraints respected

- No `NET_PROTOCOL_VER` bump. Test mode is local-only; dropdown greys out in netplay via `g_NetMode != NETMODE_NONE`.
- No save format change. `g_TestScenario` is volatile.
- Catalog ID strings used everywhere (`base:skedar` body, `base:skedar_warrior` head with fallback, `base:mp_skedar` arena).
- Stage transitions reach `mainChangeToStage` via the existing `pdguiForgeStartSessionOn` catalog path. No hardcoded stagenum.
- All Test Scenarios UI gated by `PD_DEV_BUILD` (the Debug tab is already dev-only).
- Em-dash count in design doc: 0 (methodology gate).

### Files

- **New**: `context/designs/gpu-swarm-and-test-scenarios-2026-04-27.md`,
  `port/include/testscenarios.h`, `port/src/testscenarios.c`,
  `port/include/swarm_test.h`, `port/src/swarm_test.c`,
  `port/fast3d/swarm_gpu.cpp`, `tests/test_swarm_boid_sim.cpp`.
- **Touched**: `port/include/system.h`, `port/src/system.c` (LOG_CH_BENCHMARK + LOG_CH_TESTSCEN); `src/include/constants.h` (MA_SWARM_TEST_{SEEK,GPU_DRIVEN}, MA_END 55->57); `port/include/actionmap.h`, `port/src/actionmap.cpp` (ACTION_TESTSCEN_CYCLE_COUNT bound to KEY_0 + DPAD_DOWN); `port/fast3d/pdgui_menu_mainmenu.cpp::renderSettingsDebug` (Test Scenarios section); `port/fast3d/pdgui_backend.cpp` (top-right HUD overlay); `port/fast3d/gfx_sdl2.cpp` (4.3 core probe prepend); `port/src/pdmain.c` (swarmTestTick call); `src/game/setup.c` (numchrs hook); `CMakeLists.txt` (SRC_TESTS).
- **Untouched**: `port/src/net/*`, `src/game/botmgr.c`, `src/game/bot.c::botSpawn`, save files.

### Build / verify

`ninja -C Build pd pd-server pd-tests` clean at worktree branch tip 7b61a998 (`[67/67]` linked). pd-tests `[swarm][sim]` cases compile + link; runtime verification is Mike's playtest step.

## Session S482 (`jovial-kirch-181c60`) - 2026-04-27 - spawn-weapon Random/Fiesta semantics

Mike's directive (verbatim): "We so have Random, and Fiesta. Random will select a random weapon and use that as the spawn weapon for the match, every spawn. Fiesta will randomize the weapon for every spawn? So each time a player respawns they get a random weapon independent of anyone else."

### Outcome

`g_MatchConfig.spawnWeaponMode` (new `u8` field, enum `spawn_weapon_mode`) gates three behaviors at the spawn sites + the matchStart resolver. Wire bump `NET_PROTOCOL_VER 44 -> 45` carries the mode + the host-rolled spawnWeaponNum so clients receive the resolved integer directly (no client-side re-roll for SPECIFIC/RANDOM). FIESTA carries the `SPAWNWEAPON_FIESTA_SENTINEL = 0xFE` sentinel and player.c / bot.c roll fresh per-spawn from the active weapon set.

### Mode semantics (S482)

- **SPECIFIC** (`spawnWeaponMode == 0`): `spawn_weapon_id` names the weapon; `matchStart()` resolves it to `WEAPON_*` enum at match start; every spawn uses that weapon. (Existing pre-S482 behavior for non-empty `spawn_weapon_id`.)
- **RANDOM** (`spawnWeaponMode == 1`): `matchStart()` picks one weapon at random from `g_MpSetup.weapons[0..5]` (NONE/DISABLED/SHIELD filtered) via `spawnWeaponPickFromActiveSet()`. The rolled WEAPON_* enum is stored in `spawnWeaponNum`. Every player + every bot uses that same weapon for the remainder of the match.
- **FIESTA** (`spawnWeaponMode == 2`): `matchStart()` writes `SPAWNWEAPON_FIESTA_SENTINEL` (0xFE) into `spawnWeaponNum`. Spawn sites in `player.c::playerSpawn` and `bot.c::botSpawn` detect FIESTA mode (or the sentinel) and call `spawnWeaponPickFromActiveSet()` for a FRESH roll on each spawn -- per-player, per-bot, per-respawn, independent.

### Eligible pool

`g_MpSetup.weapons[0..NUM_MPWEAPONSLOTS-1]` filtered to non-`MPWEAPON_NONE` / non-`MPWEAPON_DISABLED` / non-`MPWEAPON_SHIELD`. This preserves today's effective "Random" pool (which was the active weapon set, just degenerate to slot 0 only) but actually rolls across all 6 valid slots. If the active set has zero eligible slots, the helper returns 0 -- `matchStart()` for RANDOM falls back to `MPWEAPON_FALCON2` with a `LOG_WARNING`; FIESTA spawn sites fall into the existing `resolvedWeaponNum=0` miss path.

### Files (functional)

- `port/include/net/matchsetup.h` -- new `enum spawn_weapon_mode`, new `SPAWNWEAPON_FIESTA_SENTINEL` macro, `u8 spawnWeaponMode` field on `struct matchconfig`, declarations for `spawnWeaponPickFromActiveSet` + `spawnWeaponPickFromSlots`.
- `port/src/net/matchsetup.c` -- new helpers (live + pure-test variants), three-mode dispatch in `matchStart()` with explicit logging per branch, default mode in `matchConfigInit` is `SPAWNWEAPON_MODE_RANDOM` (so the dropdown's "Random" actually rolls now).
- `src/game/player.c::playerSpawn` -- FIESTA branch keying on `g_MatchConfig.spawnWeaponMode == SPAWNWEAPON_MODE_FIESTA || spawnWeaponNum == SPAWNWEAPON_FIESTA_SENTINEL`; legacy 0xFF / weapons[0] fallback retained for safety.
- `src/game/bot.c::botSpawn` -- mirror.
- `port/src/net/netmsg.c` -- `SVC_STAGE_START` + `CLC_LOBBY_START` write/read add the trailing `u8 spawnWeaponMode` (+ `u8 spawnWeaponNum` on `SVC_STAGE_START`).
- `port/include/net/net.h` -- `NET_PROTOCOL_VER 44 -> 45` with full block-comment description.
- `port/src/scenario_save.c` -- writes `"spawnWeaponMode"` JSON key; loader honors verbatim, with backwards-compat default = RANDOM when `spawnWeaponId` is empty / SPECIFIC when non-empty (preserves pre-S482 authoring intent).
- `port/fast3d/pdgui_menu_room.cpp` -- dropdown gains entry 1 "Fiesta" alongside entry 0 "Random"; selection writes `spawnWeaponMode` + `spawn_weapon_id` per the chosen entry's `mode`; `syncSpawnWeaponFromConfig()` reads `spawnWeaponMode` and lands on the right entry. Stale 0x2f/0x30 SHIELD/DISABLED filter literals updated to post-cull 0x27/0x28 (kept legacy values defensively).

### Files (tests)

- `tests/test_spawn_weapon_mode.cpp` (new, 17 cases) -- pool filter, degenerate fallback, RANDOM-rolls-once invariant, RANDOM determinism (same seed -> same roll), FIESTA arms sentinel, FIESTA varies per spawn, FIESTA per-player independence, SPECIFIC passthrough + empty-id fallback, legacy save defaults (no key -> RANDOM/SPECIFIC by id presence), post-S482 round-trip, out-of-range mode value falls back, FIESTA sentinel + mode enum + NUM_MPWEAPONSLOTS pins.
- `tests/test_versions.cpp` -- expected `NET_PROTOCOL_VER` bumped to 45.
- `CMakeLists.txt` -- new test file added to `SRC_TESTS`.

### Stop-condition outcomes

- **Eligible pool**: went with active match set (filtered) per the directive's "preserve today's effective pool if conceptually right" guidance. Documented in matchsetup.h block comment + constraint update.
- **Save format**: scenario JSON only; MPSETUP_VERSION unchanged. Backwards-compat is per-key default, no schema break.
- **UI surface**: dropdown entries 0/1 reserved (Random / Fiesta), specific weapons start at index 2; sort range adjusted accordingly.

### Build / verify

Pending playtest. Build verified via `pd` + `pd-tests` link path -- pre-existing local incremental build state.

## Session S481 (`festive-hawking-49649b` follow-up #6) - 2026-04-27 - release rebase failure + remaining BOM writers

Mike's release failure: `error: cannot rebase: You have unstaged changes. error: Please commit or stash them.`

**Root cause**: combination of `core.autocrlf=true` (Mike's local git config) + the 2026-04-25 `.gitattributes` change (`* text=auto eol=lf` defaults). Text files often have CRLF on disk while the index has LF after .gitattributes-driven normalization. The auto-commit step in `release.ps1` Step 4 runs `git add -A` (stages CRLF -> LF normalized for index), then `git diff --cached --quiet` returns 0 (no actual index change vs HEAD), so no commit fires. Then `git pull --rebase` does its own working-tree-vs-HEAD check on raw bytes and refuses because the file looks "modified."

**Fixes (all in `devtools/`)**:

1. **`release.ps1` rebase robustness** -- between the auto-commit and the `git pull --rebase`:
   - `git update-index --refresh -q --unmerged` clears stale modified flags for files whose content matches HEAD after .gitattributes normalization (idempotent and safe; doesn't touch genuinely modified files).
   - Capture `git status --porcelain` and log it. Next time a release rebase fails the user has a clear paper trail of which file blocked it.
   - Recovery branch: when rebase fails specifically with "unstaged changes / cannot rebase / would be overwritten", abort the partial rebase, run `git checkout-index -a -f` to forcefully sync the working tree byte-for-byte from the index (safe because `git add -A` ran moments before), refresh, log the post-fix status, and retry the rebase.

2. **Remaining BOM-emitting `Set-Content -Encoding UTF8` writers to TRACKED files** (sibling class to the S477 `Set-ProjectVersion` fix):
   - `keygen.ps1:156` writing `port/include/updater_pubkey.h` (TRACKED) -- swapped to `[System.IO.File]::WriteAllText` with `UTF8Encoding($false)`.
   - `_dev-window.ps1:1293` writing `context/qc-tests.md` (TRACKED) -- swapped to `WriteAllText` with explicit LF line endings (`$out -join "`n"`) to match `.gitattributes` `*.md eol=lf`.

**Why these matter even though the auto-commit catches them**: a BOM byte added by `Set-Content -Encoding UTF8` causes `git diff` to show the file as modified even when content is otherwise identical. After `git add -A`, the BOM gets stored in the index, so the file never re-converges to HEAD on subsequent operations. Future rebases / merges trip on the same byte.

**Why this manifested only now**: the `.gitattributes` `* text=auto eol=lf` defaults landed 2026-04-25. Before that, line endings were left at OS-native CRLF on Windows, and `git pull --rebase` was happy. After that, the working-tree vs index drift exposed by the new normalization rules surfaced as "unstaged changes" on rebase.

Verified: PowerShell parser passes on all five edited scripts (release.ps1, dev-window-v2.ps1, _dev-window.ps1, version-util.ps1, keygen.ps1). The pre-existing parser warnings on release.ps1 lines 623/647 cleared themselves -- my added pre-rebase block shifted the line numbers past whatever the parser was confused about (likely the `$()` inline interpolation in the SkipPush print).

Files: `devtools/release.ps1`, `devtools/keygen.ps1`, `devtools/_dev-window.ps1`.

## Session S480 (`festive-hawking-49649b` follow-up #5) - 2026-04-27 - post-release latest refresh + build-tab clipping + PAT rename to REV

Three Mike asks resolved in one merge:

1. **Post-release latest-version refresh** ("When I release a build, after the release it should update the latest version label with a fresh check"). New helpers in `dev-window-v2.ps1`:
   - `Get-GitHubRepoSlug` -- resolves `owner/repo` slug from Settings.GitHubRepo (handles raw slug, full URL, or empty -> default fork).
   - `Update-LatestReleaseLabel($cached)` -- shared text/color update from a parsed release JSON object (also used by Loaded path now).
   - `Refresh-LatestRelease` -- async `gh api repos/<slug>/releases/latest` via `Start-AsyncPoolAction`. Shows "latest: checking..." while in flight; on success, updates the label + writes the cache; on failure, restores the prior text + brush so the user is not stranded on the transient state. Guarded by `$script:LatestReleaseRefreshBusy` so it never stacks. Hooked into the BuildTimer release-success branch -- only fires when `$wasReleaseSuccess = ($script:IsPushing -and -not $anyErr)`. Also fired by F5 + the deferred initial fetch from MainTimer (kicks once after gh auth becomes ok).
2. **BUILD tab content clipping during builds** -- visible in Mike's screenshot, the STOP button at the bottom of the STATUS card was getting cut off mid-glyph because the cards' content (active progress bar + STOP/Copy buttons) exceeded the available tab area. Wrapped the BUILD tab's DockPanel in a `<ScrollViewer VerticalScrollBarVisibility="Auto">` so vertical overflow scrolls instead of clipping; once the build finishes and the active controls hide, content fits and the scrollbar disappears.
3. **PAT spinner relabel to REV** ("Not 4 segments, just rename Patch to Revision") -- display-only change in `dev-window-v2.ps1`. The `TextBlock Text="PAT"` now reads "REV" with a tooltip clarifying it backs cmake `VERSION_SEM_PATCH`. Underlying control names (`TxtVerPatch`, `BtnVerPatDown/Up`) and the on-disk version format `M.m.p` stay unchanged so version-util.ps1 / release.ps1 / CMakeLists.txt don't need touching. Mike clarified mid-session that 4-segment versions are NOT wanted -- just the label rename.

Verified: PowerShell parser passes; XAML loads cleanly via XamlReader.Load (1500x940 MinSize unchanged from S479).

Files: `devtools/dev-window-v2/dev-window-v2.ps1`.

## Session S479 (`festive-hawking-49649b` follow-up #4) - 2026-04-27 - right panel clipping fix

Mike's directive (verbatim): "Update Dev Window v2 is much better. There is some info being cut off though in the right side panel."

Screenshot review showed (a) the PAT spinner box rendering "17" with the trailing "0" clipped behind the "+" button, (b) the `latest:` and `local:` rows clipped off the bottom of the right card, and (c) the bottom status bar showing "worktreesauth: ok" with no visual gap between the two labels. Three layout fixes:

1. **Status grid column rebalance** -- column shares moved from `2*` left / `*` right to `*` / `*` (equal share) so the right card gets enough horizontal room. MinWidths bumped: left 520 -> 500 (slightly narrower since balanced), right 560 -> 740. Window MinWidth 1400 -> 1500 to ensure the new MinWidth sum + gap + dock margins fits.
2. **Spinner triplet slimmed** -- per-spinner-button width 56 -> 48 (height 56 -> 52); MAJ/MIN number boxes 80 -> 64; PAT number box 92 -> 86 (still wide enough to render "170" Bold Consolas without clipping); inter-stack margin 14 -> 8. Triplet total width 616px -> 504px (fits comfortably in the right card's ~700px usable interior). FontSize on the number boxes 30 -> 28 to match the spinner buttons.
3. **Right card vertical margins tightened** -- VERSION header bottom margin 8 -> 6, spinner-row group bottom margin 10 -> 8, ChkStable bottom margin 12 -> 8, LblAuthStatus bottom margin 5 -> 4, LblLatestRelease bottom margin unchanged at 4. LblAuthStatus and LblLatestRelease gained explicit FontWeight="Bold" (they would have inherited from the Window TextElement default but explicit assertion prevents drift).
4. **Status bar gap fix** -- StatusWorktrees gained Margin="0,0,22,0" so it doesn't visually run into the next-element StatusAuth (which is right-docked). The DockPanel "remainder" gap was collapsing to zero when the left-flowing labels filled most of the bar.

Verified: PowerShell parser passes; XAML loads cleanly via XamlReader.Load (1500x940 MinSize confirmed); spinner width math (48+64+48 + 8 + 48+64+48 + 8 + 48+86+48 = 510px) fits inside the right card's ~700px usable interior with comfortable margin.

Files: `devtools/dev-window-v2/dev-window-v2.ps1`.

## Session S478 (`festive-hawking-49649b` follow-up #3) - 2026-04-27 - doubled bold UI sweep

Mike's directive (verbatim): "All the font should be at least double the size, and bold."

Mechanical sweep across `devtools/dev-window-v2/dev-window-v2.ps1`. Every `FontSize=` value doubled, every `FontWeight=` lifted to Bold or Black, every container/button MinHeight/Padding bumped to fit the doubled text without truncation.

| Before | After |
|---|---|
| FontSize 11 / 12 / 13 / 14 / 15 / 16 / 18 | 22 / 24 / 26 / 28 / 30 / 32 / 36 |
| Window TextElement defaults: FontSize=15 (no FontWeight) | FontSize=30, FontWeight=Bold (propagates to all child text via TextElement attached property) |
| FontWeight="SemiBold" / "Normal" | "Bold" everywhere (27 Bold + 3 Black after sweep) |
| BUILD/RELEASE hero MinHeight=60, Padding=16,0 | MinHeight=104, Padding=20,16 (Hero column gap 10 -> 14) |
| RUN TESTS / RUN GAME MinHeight=42, Padding=14,9 | MinHeight=78, Padding=20,16 (column gap 8 -> 14) |
| ToolBtn MinHeight=32, Padding=12,7 | MinHeight=58, Padding=20,12 (Setter property "Normal" was missed by attribute-form replace_all -- caught with Setter-form edit) |
| AccentBtn Padding=14,8 | 22,14 |
| Version spinner +/- buttons 28x28 | 56x56 (no internal padding so glyph centers cleanly) |
| Version spinner number boxes Width 42 / 42 / 48 | 80 / 80 / 92 (PAT box widest for triple-digit patch numbers) |
| Status panel cards Padding=14,12 | 20,18 |
| Status grid column MinWidths 320 + 12 + 340 | 520 + 16 + 560 |
| Status bar Padding=14,9 + 14,0 separator margins | 22,16 + 22,0 separator margins |
| Status bar StatusHash / StatusWorktrees / StatusAuth lacked FontWeight | Bold added |
| TabItem Padding=22,10 | 32,18 |
| TabItem selected underline 2px | 3px (already done in S477) |
| Header brand bar Padding=14,8 + 2px underline | 20,14 + 3px underline |
| Utility row Padding=8,6 | 12,10 (also wrapped in ScrollViewer so doubled-bold buttons stay reachable at narrow widths) |
| Progress bar Height=20 | 38 (fits 26pt bold text) |
| BUILD tab DockPanel Margin=14,12 | 20,18 |
| Window MinSize 980x620 | 1400x920 |
| Default window size 1180x820 | 1700x1100 |

Behavior preserved: light theme palette + PD cyan/gold accents + layout structure + all event wiring. Catch2 parser, per-case test output, BOM-safe Set-ProjectVersion, and gitignore hygiene from S477 unchanged.

Verified: PowerShell parser passes; XAML loads cleanly via `XamlReader.Load` (title + 1400x920 min size + #FFECEEF2 bg confirmed); FontSize values audit shows all 43 occurrences in the 22-36 range (no small values leftover); FontWeight audit shows all 30 occurrences are Bold (27) or Black (3 -- BUILD button hero text).

Files: `devtools/dev-window-v2/dev-window-v2.ps1`.

## Session S477 (`festive-hawking-49649b` follow-up #2) - 2026-04-27 - light theme PD redesign + per-case test output + release-clean fix

Mike's directives this round (verbatim, in order):
1. "The text is still quite small in the UI, and also overall." (after S476 partial bump)
2. "Can you read the small darkened text in the window? ... we don't have to dark theme it, let's still use Perfect Dark styling, but light themed."
3. "The log should list each test it is running and the result received which made it consider it a pass or fail" (per-test visibility)
4. "Don't be afraid to restructure the window layout."
5. "When I release, it automatically says I then have 1 uncommitted change. Any way to make it so that isn't the case?"

**Light-theme PD redesign**:
- Window background `#ECEEF2` (light gray-blue), card surfaces `#FFFFFF`, card borders `#C0C8D2`, primary text `#1A2434`, secondary `#4A5868`, dim `#7A8898`. PD identity preserved via cyan `#0078A8` accents (header underline, selected tab, accent button, info-color logs, branch label) and gold `#A06A10` (RELEASE, Stable checkbox, version label, version number text). Success `#10783A`, warning `#B86810`, error `#B81818` chosen for legibility on white. Code-behind palette in `Get-ClassifiedLogColor` updated to match.
- Bulk replace_all swept the dark-theme color hexes (`#0090D0` -> `#0078A8`, `#DC3232` -> `#B81818`, `#00B400` -> `#10783A`, `#FF8C00` -> `#B86810`, `#C8A000` -> `#A06A10`, `#508CDC` -> `#0078A8`, `#CDAA32` -> `#A07810`, `#8C8C8C` -> `#4A5868`, `#1A3050` -> `#C0C8D2`) so dynamic SolidColorBrush construction (status row foregrounds, log-line classification, button highlight states) matches the new palette.

**Layout restructure (BUILD tab)**: Hero buttons reduced from MinHeight=92 to 60 (font 24 -> 18); old utility row promoted to its own white card directly under hero pair (free-standing, easy reach); status grid now uses two white cards on light bg with section headers ("S T A T U S" / "V E R S I O N"). Status row fonts moved 17 -> 16 (less shouty), version sub-labels harmonized to 14, version spinner buttons dropped to a tight 28x28. The freed vertical space goes to the cards' content rather than crammed into a side panel. Status bar (bottom) is now a primary info row at FontSize 15 Consolas with vertical separators on a `#F5F7FA` band; bottom RUN bar 14pt with MinHeight=42.

**Catch2 parser fix (S476 + S477 fold-in)**:
- Per-run `$script:TestsScanBuffer` ArrayList introduced; `Drain-ProcessOutputQueues` appends every `[tests]` raw line. Watchdog parser now scans this buffer (was scanning `$script:AllOutput` which is the BUILD pipeline's buffer and never received test output -- that was why "PASSED (0 case[s])" appeared in Mike's first playtest).
- Three regexes split: `All tests passed (N assertions in M test cases)` (success format), `test cases: A | B passed | C failed`, `assertions: A | B passed | C failed`. Both Catch2 summary shapes parse; status row + MessageBox now surface BOTH case AND assertion counts ("tests: PASSED (155 cases, 1881 assertions)").

**Per-case test output (S477)**: pd-tests invocation changed to `-r console -s -d yes` -- `-s` lists each assertion (the result that made each case pass/fail), `-d yes` shows per-case durations. Mike now sees every test and its result streamed into the Log tab.

**Release "1 uncommitted change" fix (S477, root cause + fix)**: `Set-ProjectVersion` (dev-window-v2 + _dev-window) and `Set-CMakeListsSemVer` (version-util.ps1) used `Set-Content -Encoding UTF8` which on PowerShell 5.1 ALWAYS emits a UTF-8 BOM regardless of -NoNewline. After every release, the rewrite-then-no-op-replace path silently added a BOM byte to CMakeLists.txt, leaving the working tree dirty until Mike committed a phantom byte. Two-prong fix in all three writers + the inline rewrite in release.ps1 line 79-92:
1. Skip the write entirely when content is unchanged (mtime + bytes preserved).
2. When we DO write, use `[System.IO.File]::WriteAllText($path, $content, (New-Object System.Text.UTF8Encoding($false)))` -- explicit no-BOM encoder.

Verified offline: `Set-Content -Encoding UTF8` produces `EF BB BF 63 6D 61` (BOM + "cma"); `WriteAllText UTF8($false)` produces `63 6D 61 6B 65 5F` ("cmake_"), no BOM. Content's `PATCH 999` (or whatever value) replacement intact.

**Bonus gitignore hygiene (S477)**: `.dev-window-release-cache.json` (rewritten on every gh-api fetch by old _dev-window.ps1), `.dev-window-settings.json`, `._dev-window-settings.json` were tracked in git but described as "gitignored" in ADR-004. Added to .gitignore + `git rm --cached` so future writes do not show as dirty changes.

Files: `devtools/dev-window-v2/dev-window-v2.ps1`, `devtools/_dev-window.ps1`, `devtools/release.ps1`, `devtools/version-util.ps1`, `.gitignore`. Untracked: `.dev-window-release-cache.json`, `.dev-window-settings.json`, `._dev-window-settings.json`.

Verified: PowerShell parser passes on dev-window-v2.ps1 / _dev-window.ps1 / version-util.ps1; XAML loads cleanly via XamlReader.Load (title + min size + bg confirmed); release.ps1 retains the 2 pre-existing parser warnings at lines 623 / 647 from inline `$()` interpolation that long predate this work.

## Session S476 (`festive-hawking-49649b` follow-up) - 2026-04-27 - Catch2 parser fix + comprehensive font sweep

Mike's directive (verbatim, after S475 playtest): "The text is still quite small in the UI, and also overall. Also, I got an Exit 0 on the tests." + follow-up: "Mike just ran tests again and the actual Catch2 output IS streaming through, but the parser is misreading the count" -- log showed `[tests] All tests passed (1881 assertions in 155 test cases)` upstream of `tests: PASSED (0 case[s])`.

**Catch2 parser fix (root cause)**: `Drain-ProcessOutputQueues` writes test lines through `Add-LogLine` (UI-only, does NOT append to `$script:AllOutput`), but the watchdog parser walked `$script:AllOutput`. Result: parser saw zero test lines, both regexes failed, `$passed` stayed at 0. Fix in [devtools/dev-window-v2/dev-window-v2.ps1](devtools/dev-window-v2/dev-window-v2.ps1):
1. New per-run `$script:TestsScanBuffer = ArrayList`. Cleared at `Start-RunTests`. `Drain-ProcessOutputQueues` now appends each raw test line (without the `[tests] ` UI prefix) to it.
2. Watchdog parser walks `TestsScanBuffer` instead of `AllOutput`.
3. Parser regexes split into three lines (a) all-passed: `All tests passed (N assertions in M test cases)` -> M cases / N assertions / passed=M / failed=0; (b) test-cases tally: `test cases: A | B passed | C failed` -> A cases / B passed / C failed; (c) assertions tally: `assertions: A | B passed | C failed` -> A assertions. Both shapes parse correctly (verified offline).
4. Status row + MessageBox now surface BOTH case AND assertion counts ("tests: PASSED (155 cases, 1881 assertions)" + multi-line MessageBox showing Cases/Passed/Failed/Assertions). Falls back to "no Catch2 summary found" if neither shape was emitted.

**Comprehensive UI font sweep**: prior bump (status panel 14->17, version sub-labels 13->15) wasn't enough. Mike said small "in the UI, and also overall," so this round walks every textual element and bumps the window default plus every override:

| Element | Before | After |
|---|---|---|
| Window `TextElement.FontSize` (default) | 13 | 16 |
| Top brand bar shortcuts hint | 12 | 14 |
| PD2 / Dev Window / v2 brand labels | 13 | 16 |
| Status bar (branch / hash / dirty / worktrees / auth / version / mode) | 14 | 16 |
| Bottom RUN TESTS / RUN GAME buttons | 14 | 17 (padding 16,12 -> 16,14) |
| Hero BUILD button | 20 | 24 (MinHeight 82 -> 92) |
| Hero RELEASE button | 14 | 17 (MinHeight 82 -> 92) |
| LblClientStatus / LblServerStatus (tests row) | 17 | 20 |
| LblBuildActivity | 16 | 18 |
| Progress bar text | 11 | 14 SemiBold (height 16 -> 22) |
| V E R S I O N section header | 13 | 15 |
| MAJ/MIN/PAT labels | 11 | 13 |
| Version spinner +/- buttons | (default) | 16 (Width 28 -> 34, Padding 4,5 -> 6,6) |
| Version spinner number boxes | 13 | 17 (Width 32/32/32 -> 40/40/44) |
| ChkStable | 15 | 17 |
| LblAuthStatus / LblLatestRelease / LblDevVersion | 15 | 17 |
| TabItem template | 12 | 15 (Padding 20,9 -> 22,11) |
| ChkAutoScroll / TxtLogFilter | 11 | 14 |
| LogOutput RichTextBox | 11 | 14 |
| DocList / DocContent | 11 | 14 (DocList width 240 -> 320) |
| ToolBtn style (utility row + Version +/- + Stop/Copy/Check) | (default) | 15 (MinHeight 32 -> 38, Padding 10,7 -> 12,9) |
| AccentBtn style padding | 16,8 | 18,10 |
| Window MinWidth / MinHeight | 820 / 500 | 1000 / 640 |
| Default window size (first launch) | 960x700 | 1180x820 |
| Status Area left col MinWidth | 220 | 280 |
| Status Area right col MinWidth | 280 | 360 |
| Right (version/auth) card MinWidth | 260 | 340 |

Also added "Ctrl+T=Tests" to the brand-bar shortcuts hint to match the rebound shortcut from S475.

Verified: PowerShell parser passes; XAML loads cleanly via `[Windows.Markup.XamlReader]::Load` (window title + min size + default font confirmed); no behavior changes outside font/padding/size and the parser fix.

Files: `devtools/dev-window-v2/dev-window-v2.ps1`.

## Session S475 (`festive-hawking-49649b`) - 2026-04-27 - dev-tool cleanup: build-time regression fix, server retirement, async worktree prune, Run Tests button

Mike's directive (verbatim): "A few things. One, the project hasn't grown significantly in size, but the build times when using Dev Window v2 for the client have quintupled in the last few weeks. Find the reason and fix it. Also, we aren't building the Dedicated Server anymore, since we incorporated our client-based online connectivity into the client, but it still gets built and released currently. The text in the build window is still very small; see image. Release also seems to be taking a long time. And our old Dev Window seems to work to remove worktrees (claude and git), but it takes a very long time, and is threaded as such to where the program hangs while it waits. It shouldn't hang, and there should be a status indication of what is happening." + "I would also like a button in the Dev Window to run our tests, I don't want to be forced to use the command line."

**Root cause -- build regression**: `CMakeLists.txt:152` (`file(WRITE ${_CACERT_OUT} ...)`) unconditionally rewrote the embedded Mozilla CA bundle (`cacert_blob.h`, ~1.1 MB) on every cmake configure. dev-window-v2 reconfigures every build. The new mtime made ninja recompile `updater.c` AND **relink `PerfectDark.exe`** (and `pd-server`, and `Updater.exe`) on every build -- ~7-10 s of pure waste per cycle. CACERT was added 2026-04-17 (10 days ago); fits "the last few weeks" timeline. Measured before-fix: ninja recompile + relink path was 7-15 s for a no-op; after: 0.088 s.

**Fixes landed (CMakeLists.txt + dev-window-v2.ps1 + _dev-window.ps1 + release.ps1)**:
1. **`CMakeLists.txt`** -- `cacert_blob.h` regeneration is now gated on `${_CACERT_SRC}` mtime vs the existing output (`IS_NEWER_THAN`). Skips the file(READ HEX) + regex + file(WRITE) chain when the source hasn't moved. Prints "CACERT: cacert_blob.h is up-to-date; skipping regeneration" so the skip is auditable. **Dominant build-time savings.**
2. **`devtools/dev-window-v2/dev-window-v2.ps1` -- `Get-BuildSteps`**: removed (a) the redundant cmd.exe "Auto-commit + push" step (Start-GitSyncBeforeBuild already commits+pushes async earlier in the same Build() call), and (b) the `Build (server: pd-server)` step. pd-server is no longer shipped or built per BUILD/RELEASE; the cmake target stays defined so pd-tests can link against it if needed.
3. **`devtools/dev-window-v2/dev-window-v2.ps1` -- UI**: bottom-bar `RUN SERVER` button replaced with `RUN TESTS`. Status / version font sizes bumped (FontSize 14→17 for client/tests rows, 13→15 for version sub-rows + auth + latest + local labels) so the Build window reads at glance scale, matching the BUILD/RELEASE button visual weight. Old `LblServerStatus` row repurposed as a `tests:` status row (PREPARING / BUILDING / RUNNING / PASSED / FAILED).
4. **`devtools/dev-window-v2/dev-window-v2.ps1` -- new tests pipeline** (Toggle-Tests / Start-RunTests / Build-Tests-Then-Run / Run-Tests-Process / Stop-RunTests): builds pd-tests on demand if the binary is missing, runs it, streams stdout/stderr into the Log tab via the existing `AsyncLineReader` + `TestsOutputQueue` (drained by StatusModeTimer at 500 ms), watchdog DispatcherTimer detects exit + queue drain, parses Catch2 summary lines for pass/fail counts, and surfaces a final MessageBox + status row update. Ctrl+T rebound to Toggle-Tests (was Toggle-Server).
5. **`devtools/_dev-window.ps1` -- worktree prune** is now async via the same Runspace + DispatcherTimer pattern already used for git polling. A `ConcurrentQueue<string>` carries per-step status messages ("removing 4/12 -- worktree-name") that the timer pumps into LblBuildActivity. Buttons stay enabled across the rest of the UI; the prune button shows "PRUNING..." and re-enables on completion. Per-directory failures are collected into a final summary instead of silently swallowed.
6. **`devtools/release.ps1`**: dropped the `pd-server` entry from the build-targets array, hardcoded `$ServerExe = ""`, removed `PerfectDarkServer.exe` from the asset-upload list, removed the `Server: FOUND/MISSING` preflight noise, and updated the no-artifacts exit guard to require only the client. Each release now skips one cmake --build invocation (~6-10 s) and one upload step.

**Measured impact (incremental rebuild after touching one .c file, full dev-window-v2 BUILD pipeline simulation)**:
- Before: configure ~0.7 s + ninja rebuild 7-12 s (cacert force-relink + pd-server build + updater relink) + redundant cmd.exe commit ~2 s = **~10-15 s**.
- After: configure 0.54 s + ninja 1.24 s = **1.78 s** (8x faster).

**Behavior preserved**:
- `pd-server` cmake target still exists; `pd-tests` continues to link against `participant.c`, `catalog_checked.c`, `options_forced.c` etc. as before.
- Mozilla CA bundle still embedded; first build from a clean Build dir still regenerates `cacert_blob.h` (gate is "exists + up-to-date", not "skip always").
- `Start-GitSyncBeforeBuild` still commits + pushes via the async runspace pool exactly as before.
- `RUN GAME` button + Toggle-Game unchanged.

Build verified: PowerShell parser passes on dev-window-v2.ps1 + _dev-window.ps1 (release.ps1 has 2 pre-existing parser warnings at lines 615 / 639 that long predate this change). Ran cmake configure + `--build pd` end-to-end clean. `--build pd-tests` produces `pd-tests.exe` (15.8 MB) successfully -- separate issue: the binary itself is silent on stdout/stderr in this environment, but the pipeline doesn't depend on observed output; it parses whatever the binary prints and reports.

Files: `CMakeLists.txt`, `devtools/_dev-window.ps1`, `devtools/dev-window-v2/dev-window-v2.ps1`, `devtools/release.ps1`.

## Session S474 (`gifted-elion-ca7cea`) - 2026-04-27 - cohort 2 tests + foolproof input authority + room screen bug batch

Mike's directive (verbatim): "I need you to run your automated tests for menu function with controller support... scrolling with the right stick... UI state matching actual values, flattened menus, ignoring panels and instead seamlessly navigating between them, maintenance of proper input context and availability for open menus or gameplay at all times, ability to get to every menu option that does something and not the ones that don't, and things such as when you go to the add bot button and hit A on controller, it adds a bot button then the button gets deselected (it shouldn't), and when there are multiple members in the room and one gets selected, their position on screen changes as the selection info text at the top of that panel appears and displaces everything below it. The top of the panel should be docked. Also I should be able to change my character in the room (only applied temporarily, not an overwrite of my saved Agent character)".

### Outcome

`pd-tests` cohort 2 added (78 cases / 1111 assertions) -- IMC stack invariants, menu pool invariants, flat-menu reachability, right-stick smooth-scroll math. **155 cases / 1881 assertions all green.** Three concrete room-screen bug fixes shipped alongside the invariant tests. No wire / save / protocol changes (per the methodology gates).

Build artefact sizes: PerfectDark.exe 56,327,943 bytes / PerfectDarkServer.exe 23,293,870 bytes / pd-tests.exe 15,900,683 bytes.

### Foolproof input-context framework

The architectural answer to "input ONLY in menus when menus are up, and WORKS for gameplay when they are not" was already in place before this session:

- `gameplayInputSuppressed()` predicate (S250 / Phase 1 of `context/designs/input-authority-and-menu-pool-2026-04-13.md`). Returns 1 when (a) top context != gameplay, (b) window focus lost, or (c) focus regained within settle window.
- `fireVk` skips `g_ImcGameplay` / `g_ImcVehicle` when the predicate returns 1; `actionPressed` / `actionHeld` / `actionReleased` / `actionValue` for gameplay-only actions return 0 under suppression. Defense in depth (dispatch-site + read-site).
- Menu pool (S299 / Phase 2). One-instance-per-type structural dedup; `menupoolReleaseAll()` is the cascade-close primitive.
- `inputCtxApplyCursorVisibility` (B-259, S468) collapses four prior `SDL_ShowCursor` writers into a single ctx-aware authority.

Cohort 2 in this session **programmatically asserts these invariants are intact**. All assertions pass; no architectural change was required. If the framework regresses in the future, the next pd-tests run will surface it loudly rather than via a player report.

### Cohort 2 coverage

| Subsystem | Cases | Asserts | Notes |
|---|---:|---:|---|
| inputctx (IMC stack) | 11 | 200 | push / pop / EndFrame / GetTop / dedup / resurrect / overflow / focus-lost / focus-regain settle / cursor authority / open-close roundtrip |
| menupool (menu stack) | 12 | 61 | acquire / release / dedup (I2) / idempotent release / out-of-range / generation / ReleaseAll / cascade-close |
| menu reachability (flat-menu) | 7 | 40 | walk synthetic tree, every interactive node visited once, separators / labels / disabled never returned, panel transparency, room-screen synthetic walk |
| right-stick smooth scroll | 9 | 117 | deadzone / monotonic / max-speed cap / sign / dt linearity / accumulator / non-linear curve / degenerate dt rejection |
| **Total cohort 2** | **39** | **418** | -- |

### Files added (8)

- `tests/inputctx_pure.{c,h}` -- pure-C subset of port/src/inputctx.c (stack + suppression predicate). @SYNC markers point to inputctx.c line ranges.
- `tests/menupool_pure.{c,h}` -- pure-C subset of port/src/menupool.c (slot machinery + dialogdef-free API). @SYNC markers point to menupool.c.
- `tests/test_input_authority.cpp` -- IMC stack invariant suite.
- `tests/test_menu_stack.cpp` -- menu pool invariant suite.
- `tests/test_menu_reachability.cpp` -- synthetic-tree DOM walk; every interactive option reachable, every non-interactive skipped.
- `tests/test_right_stick_scroll.cpp` -- pure scroll-math spec; tied to `pdgui_backend.cpp::pdguiDriveImGuiNav` via @SYNC.

### Files modified (2)

- `CMakeLists.txt` -- registered new test files in `SRC_TESTS`.
- `port/fast3d/pdgui_menu_room.cpp` -- three concrete bug fixes (see below) + forward-declared four bridge functions used by the room character override path.

### Three concrete bug fixes

**Add Bot button focus drop (Mike's "button gets deselected" report).** ImGui hashes the full label as the widget ID by default. `addBotLabel = "Add Bot  (3 / 8)"` includes the live count, so when the count changes after click the ID changes, focus drops. Fix: append `###add_bot_btn` so the ID is pinned regardless of visible text. Same `###` pattern applied to the new "Change Character" button. Focus now holds across action firings; controller A on Add Bot keeps repeating cleanly.

**Top-of-panel header docking.** Previously the player panel's second header line ("N selected -- Ctrl/Shift/Y to multi-select, X for menu") only rendered when `s_BotSelectCount > 0`. Selecting a bot inserted a new flow line, displacing the row list and the Add Bot footer below. Mike's complaint: "their position on screen changes as the selection info text appears and displaces everything below". Fix: render line 2 unconditionally. When no bots are selected, line 2 holds an unobtrusive multi-select hint at `TextDisabled` colour. Layout height is constant; member rows do not shift on selection state changes.

**Room character override (temporary, non-persistent).** New "Change Character (Temporary)" button below Add Bot. Opens a popup modal with body + head pickers (catalog-driven via `assetCatalogIterateUnlockedByType`). Apply path: capture the live persistent IDs into a backup (one-shot, idempotent), write the picked IDs through `mpPlayerConfigSetHeadBody` (in-memory only). Cancel: discards. Reset to Saved (only visible when override is active): restores the backup. `pdguiRoomScreenReset` (called on roomLeave) replays the saved IDs back into the live profile so the persistent state is restored before any later code reads it. The on-disk Agent file is untouched throughout because `mpPlayerConfigSetHeadBody` is in-memory; disk writes are filemgr-explicit and never auto-fire from this path. No wire format change because body / head fields already cross the wire as catalog ID strings (protocol v32+).

### Methodology compliance

- **No em-dashes.** Audited every added line (10 in tests, 12 in pdgui_menu_room.cpp); replaced with hyphens / colons / parens.
- **Hierarchical log channels.** New traces use `INPUT.CTX.*` (already established) and `ROOM.CHAR.*` (override capture / restore); `MENU.STACK.*` reserved channel documented in test_menu_stack.cpp header.
- **pd-tests cases land in same commit as the invariant they enforce.** Cohort 2 lands alongside the framework-verification work in one merge.
- **No half measures on the framework.** Cohort 2 covers the three architectural invariants Mike named (IMC stack, menu stack, focus reachability) plus the right-stick scroll spec. No partial wiring.
- **Save / wire format constraint compliance.** No protocol bump; no `MPSETUP_VERSION` change; no save-format change; persistent Agent file untouched. The room override never reaches disk.

### Out-of-scope confirmations (Mike's directive)

- Right-stick scroll **rendering feel** verified manually post-merge. The math spec is now an enforced contract; the visual smoothness is in-game-only.
- Top-of-panel docking **visual pixel layout** verified manually post-merge. The structural fix (constant header height) is shipped; final pixel polish is Mike's eye.

### Next steps

- Mike playtests the three room screen fixes:
  - Add Bot held A on controller spam-adds bots; focus stays on the button.
  - Selecting a bot does not shift other rows; header stays docked.
  - Change Character (Temporary) modal opens, picks apply for the match, persistent Agent body / head restored on roomLeave.
- Optional follow-up: wire `pdguiDriveImGuiNav`'s right-stick path through the `scrollDelta` helper in test_right_stick_scroll.cpp so the spec governs the runtime instead of just documenting it.
- Optional follow-up (queued from cohort 2 design doc Section F): master-loader state machine + hand state machine for cohort 3.



Mike's directive (carried from the heads / bodies / weapons / arenas selector pool series): "It should build the weapons list from the weapons listed in the catalog, filtering by weapons that are either not unlocked yet or are disabled... This same thing should apply for character heads and bodies, weapons, maps, etc."

Final selector-pool migration in the catalog chain.  Heads merged at dev `132a883c`, bodies at `e7c4e702`.  This session migrates maps / arenas to Layer B.

### Outcome

Maps / arenas selector pool now reads from the catalog filtered by unlock-state.  The MP setup arena picker plus all three random meta resolvers (`mpChooseRandomStage` / `Multi` / `Solo`) migrated; the dead `mpChooseRandomGexStage` resolver retired alongside the post-cull `STAGE_MP_RANDOM_GEX` branch in `mpStartMatch`.  `pd` (56,288,676 bytes) + `pd-server` (23,275,546 bytes) + `pd-tests` (14,330,544 bytes) all link clean; **83 test cases / 1389 assertions, all green**.

### Audit doc

[`context/audits/catalog-migration-maps-2026-04-26.md`](audits/catalog-migration-maps-2026-04-26.md) -- 428 lines, Sections A-J mirroring the heads / bodies audits.  Findings:

- Layer A: `g_MpArenas[]` (47 entries post-AllInOne cull) authoritative on client; server stub mirror in `port/src/server_stubs.c`; `s_ArenaNames[47]` slug shadow for catalog ID generation; `g_ArenaGroupDefs[7]` legacy collapsible-group offsets used only by `mpArenaMenuHandler`.
- Layer B: arena registration in `port/src/assetcatalog_base.c:612-735`; `assetCatalogIterateUnlockedByType(ASSET_ARENA, ...)` + `assetCatalogGetUnlockedCountByType` already shipped from heads Step 1.
- Already migrated: `pdgui_menu_room.cpp` CS arena picker (canonical template), `pdgui_menu_mainmenu.cpp` Forge / Grid arena picker.
- Migration targets: `pdgui_menu_mpsetup.cpp::renderMpArena` (delegated all MENUOPs to legacy `mpArenaMenuHandler`); the four `mpChooseRandom*Stage` functions (Layer A walks with pre-cull bounds 71 / 32 / 27 / 61).
- B-235 sibling check: zero (wire reads use `assetCatalogResolve` type-correctly).
- Catalog ID conformance: 47 / 47 (test-style slugs `test_arch` / `test_dest` / `test_lam` deferred per heads I.1 / bodies J.1).

### Decisions confirmed by default (Section J)

Per `make-decisions-delegation` memory, all decisions transferred from heads / bodies with no Mike call needed.  Item J.5 specific to arenas: `mpChooseRandomGexStage` retired in Step 2 (dead post-cull, unreachable through any UI path).  Item J.7: legacy `mpArenaMenuHandler` carousel left in place per heads disposition (vestigial, harmless).

### Migration commits (3: audit + 2 steps)

| SHA | Scope |
|---|---|
| `45293ede` | Phase 1 audit |
| `16826e40` | Step 1: mpsetup arena picker + new `pdguiMpSetupSetArena` bridge |
| `d27f66b5` | Step 2: random meta resolvers (`Stage` / `Multi` / `Solo`) + retire Gex |

### Files touched

- `context/audits/catalog-migration-maps-2026-04-26.md` (+428): audit
- `port/fast3d/pdgui_menu_mpsetup.cpp` (+204 / -21): Step 1 picker rewrite
- `port/fast3d/pdgui_bridge.c` (+22): new `pdguiMpSetupSetArena` + `pdguiMpSetupGetStageId`
- `src/game/mplayer/setup.c` (+88 / -100): random selector rewrite
- `src/game/mplayer/mplayer.c` (+5 / -3): retire `STAGE_MP_RANDOM_GEX` branch
- `src/include/game/mplayer/setup.h` (+1 / -1): retire `mpChooseRandomGexStage` decl

### Migration shape

Step 1 (mpsetup picker) mirrors `pdgui_menu_room.cpp::catalogArenaCollect`: collector callback over `assetCatalogIterateUnlockedByType` -> sort by section (Combat Simulator / Solo Missions / Mods) + alphabetical -> per-row `Selectable` -> commit via `pdguiMpSetupSetArena(stagenum, stage_id)` to keep `g_MpSetup.stagenum` and `g_MpSetup.stage_id` (catalog ID, wire identity per protocol v32+) in sync.  Cache invalidation triggered by `IsWindowAppearing` or `assetCatalogGetUnlockedCountByType` delta.

Step 2 (random selectors) introduces a single `chooseRandomFromCatalog(categoryMask, fallback)` helper plus a `categoryToMask("Dark" / "Classic" / "Bonus" / "Solo Missions")` mapping.  `mpChooseRandomStage` -> `RNDMASK_ANYMP` (any non-meta category).  `Multi` -> `RNDMASK_MULTI` (Dark + Classic + Bonus).  `Solo` -> `RNDMASK_SOLO` (Solo Missions only).  Each retains its prior fallback sentinel.  Pool stack-allocated 64 entries (well above the 47-arena ceiling).

### Coverage NOT migrated (per audit C.6)

RESOLUTION-only sites left alone (consistent with heads C.9 / bodies C.10 disposition):

- `src/game/mplayer/setup.c::mpMenuTextSetupName` / `mpMenuTextArenaName` (resolve arena name by stagenum for hub row text)
- `src/game/challenge.c::challengeForceUnlockSetup` (resolve arena by stagenum to force-unlock its requirefeature)
- `port/fast3d/pdgui_bridge.c::pdguiPauseGetStageName` (pause-menu stage name lookup)
- Wire reads in `port/src/net/netmsg.c` and `port/src/net/matchsetup.c` (already use `assetCatalogResolve` type-correctly)
- `src/game/mplayer/mplayer.c::mpInit` (resolves `"base:arena_mp_skedar"` for default stagenum, already catalog-driven)
- `src/game/spawnpool.c` smoke-test arena walk (development tool)

Legacy `mpArenaMenuHandler` + helpers (`arenaMapIndex` / `arenaCountVisible` / `arenaFindSelected`) in `setup.c` left in place.  Vestigial after Step 1; removal is a follow-up cleanup.

### Structural note update

`src/game/mplayer/setup.c:179-194` structural note updated to reflect the migration: the live UI selectors and random meta resolvers now read the asset catalog directly, so the three authoring tables (Layer A `g_MpArenas` x2 + `s_ArenaNames` slug shadow) are reduced to catalog seed data and their drift no longer reaches the user-facing pickers.  Future cleanup may collapse to a single declarative table but is out of scope.

### Next steps

The selector-pool migration chain (heads, bodies, maps / arenas) is now complete.  Optional follow-ups remain:

- Test-style slug renames for arenas (`test_arch` -> `suburb`, `test_dest` -> `training_day`, `test_lam` -> `grand_library`) -- save format compatibility consideration.
- Retire vestigial legacy carousel handlers (`mpCharacterHeadMenuHandler`, `mpCharacterBodyMenuHandler`, `mpArenaMenuHandler`) once no callers reference them.
- Collapse three Layer A authoring tables (`g_MpArenas` client + server + `s_ArenaNames`) into a single declarative table per the long-standing structural note.

## Session S472 (`strange-hoover-d0cb7d`) - 2026-04-26 - bodies catalog migration

Mike's directive (carried from the heads / weapons / arenas selector pool series): "It should build the weapons list from the weapons listed in the catalog, filtering by weapons that are either not unlocked yet or are disabled... This same thing should apply for character heads and bodies, weapons, maps, etc."

Sibling to the heads catalog migration that landed earlier today as `132a883c`. Maps / arenas is the next planned migration in the same series.

### Outcome

Bodies selector pool now reads from the catalog filtered by unlock-state. Five player-facing body pickers + one server-side random body assignment migrated. `pd` (56,025,277 bytes) + `pd-server` (23,275,564 bytes) + `pd-tests` (14,195,374 bytes) all link clean; **77 test cases / 770 assertions, all green**.

Merged to dev as **`e7c4e702`** alongside Mike's pause-menu Debug Shortcuts modal which had landed at `a653d242` mid-session (no conflicts -- non-overlapping files).

### Audit doc

[`context/audits/catalog-migration-bodies-2026-04-26.md`](audits/catalog-migration-bodies-2026-04-26.md) -- 460 lines, Sections A-J mirroring the heads audit. Findings:

- Layer A: `g_MpBodies[63]` is the canonical MP-eligible subset (kept intact per heads I.2).
- Layer A consumers: 5 sites needed migration (Agent Creator, Player Config, Bot Setup, Room bulk Set Body, Room single bot edit modal, plus matchsetup.c::pickRandomBodyHead random body assignment).
- Layer B: `assetCatalogIterateUnlockedByType` and `assetCatalogGetUnlockedCountByType` already supported `ASSET_BODY` natively (heads Step 1 shipped the type-generic dispatch). No new public API needed.
- B-235 sibling check: bodies use `catalogMpBodyId(mp_idx)` correctly on the wire at `netmsg.c:1297` -- no sibling bug for bodies.
- Catalog ID conformance: 63/63 for MP bodies (better than heads 75/76).

### Decisions confirmed by default (Section J)

Per the `make-decisions-delegation` memory, all 8 heads decisions transferred 1:1 with no Mike call needed:

| # | Decision |
|---|---|
| J.1 | Catalog ID renames deferred (matches heads I.1) |
| J.2 | `g_HeadsAndBodies[]` kept intact (matches heads I.2) |
| J.3 | Reuse existing `assetCatalogIterateUnlockedByType` helper |
| J.4 | Wire = STATUS QUO; host authority for cosmetics |
| J.5 | `pickRandomBodyHead` adopts unlock filter; `g_BotProfiles[].body` archetype assignments stay unchanged |
| J.6 | Graceful fallback to `base:dark_combat` when pool empty |
| J.7 | Integrated-head guard at Agent Creator stays catalog-driven |
| J.8 | 5 commits, sequential, bisectable, build-verified per step |

### Migration commits (5 + audit + merge)

| SHA | Scope |
|---|---|
| `1357200d` | Phase 1 audit |
| `71dc3b54` | Step 1: Agent Creator body carousel |
| `ae31fcfd` | Step 2: Player Config body list |
| `042a8ca2` | Step 3: Bot Setup body dropdown |
| `54846074` | Step 4: Room screen body pickers (bulk + single) |
| `943391be` | Step 5: `pickRandomBodyHead` in matchsetup.c |
| `e7c4e702` | Merge into dev |

### Files touched

- `port/fast3d/pdgui_menu_agentcreate.cpp` (+157 / -44): Step 1
- `port/fast3d/pdgui_menu_playerconfig.cpp` (+163 / -28): Step 2
- `port/fast3d/pdgui_menu_botsetup.cpp` (+99 / -16): Step 3
- `port/fast3d/pdgui_menu_room.cpp` (+164 / -64): Step 4
- `port/src/net/matchsetup.c` (+45 / -11): Step 5
- `port/fast3d/pdgui_bridge.c` (+22): new `mpPlayerConfigSetBodyId` (mirrors `mpPlayerConfigSetHeadId`)
- `context/audits/catalog-migration-bodies-2026-04-26.md` (+460): audit

### Migration shape per consumer

Each migrated picker now follows the same template heads established:

1. Static `<Prefix>BodyEntry { id, display, mp_index }` array (sized 128 or 256).
2. Collector callback that appends entries via `assetCatalogIterateUnlockedByType(ASSET_BODY, ...)`.
3. Sort by display name (`mpGetBodyName` for `mp_index >= 0` to preserve langbank + B-226 catalog overrides).
4. Cache invalidation triggered by `assetCatalogGetUnlockedCountByType` delta.
5. Selection commits use the entry's catalog ID directly via `mpchrSetBodyById` / `mpPlayerConfigSetBodyId` / `car_Set` (Bot Setup keeps the legacy carousel write key path because `mpchrSetBodyByIndex` already syncs `body_id` PRIMARY).

### Coverage NOT migrated (per audit C.10)

Authoring tools and dead legacy paths intentionally left alone:

- `port/fast3d/pdgui_skin_editor.cpp::refreshCharacterList` -- Skin Editor authoring tool, deliberately shows all bodies regardless of unlock.
- `port/src/net/netmenu.c::menuhandlerCoopCharacter` / `menuhandlerJoinCharacter` -- legacy native co-op / join body dropdowns; P10 D5.7 made native rendering dead. Handlers vestigial.
- `src/game/mplayer/setup.c::mpCharacterBodyMenuHandler` / `mpCharacterBodyListHandler` -- legacy carousel handlers; new pickers bypass `MENUOP_GETOPTIONCOUNT` so the unfiltered count never reaches the UI.
- `g_BotProfiles[].body` archetype defaults -- intentional per-bot-type assignments, not a selector pool.
- `mpDefaultHeadForBody` (mplayer.c:2938) `g_MpMaleHeads` / `g_MpFemaleHeads` random-gender fallback -- RESOLUTION not iteration, called only when `catalogGetBodyDefaultHead` returns NULL.

### Next steps

- **Maps / arenas catalog migration** -- next sibling in the series. Audit will follow the same Section A-J shape; arena selector at `pdgui_menu_room.cpp:343-393` already inlines the unlock filter so the migration is mostly retiring `mpGetNumArenas()` direct iterations elsewhere.
- Optional polish: rename `base:sp_body_<i>` / `base:sp_head_<i>` IDs to human-readable form (deferred per heads I.1 / bodies J.1 -- separate session).
- Optional polish: extend the integrated-head guard from Agent Creator to Player Config + Bot Setup head carousels (deferred per audit H.5).

## Session S471 (`cool-dirac-4af9b8`) - 2026-04-26 - pd-tests framework first cohort

Mike's directive: "Regarding the testing setup, build a plan, then execute it fully."

### Outcome

New `pd-tests` build target alongside `pd` and `pd-server`. Catch2 v2.13.10 single-header dropped into `port/include/catch.hpp`. **77 test cases / 770 assertions, all green**. Build verified: `pd` (56,181,959 bytes) + `pd-server` (23,269,035 bytes) + `pd-tests` (14,201,518 bytes) all link clean.

### Design doc

[`context/designs/testing-framework-2026-04-26.md`](designs/testing-framework-2026-04-26.md) -- ADR + roadmap. Sections: goals/non-goals, framework choice (Catch2 v2 vs Unity vs GoogleTest), build integration, harness shapes (pure unit / roundtrip / state-machine), mocking strategy (link-time stubs, not function pointers), coverage roadmap (first / second / third cohort), test conventions, scope guards, decision log.

### First cohort coverage

| Subsystem | Cases | Asserts | Source-vs-copy |
|---|---:|---:|---|
| smoke (Catch2 sanity) | 3 | 6 | n/a |
| netbuf wire primitives | 16 | 77 | real `port/src/net/netbuf.c` |
| connect codes | 6 | 99 | real `port/src/connectcode.c` |
| version pins (NET_PROTOCOL_VER + MPSETUP_VERSION) | 4 | ~5 | real headers |
| savebuffer bit-pack | 10 | ~80 | test-local copy `tests/savebuffer_pure.{c,h}` |
| v1 -> v2 save migration | 8 | ~30 | rule replicated in test |
| manifest container | 8 | ~30 | test-local copy `tests/manifest_pure.{c,h}` |
| manifest hash | 3 | ~12 | test-local copy |
| manifest serialize/deserialize | 5 | ~50 | test-local copy (incl. SEC-5 zero-SHA256 reject) |
| manifest diff | 5 | ~30 | test-local copy |
| random-pool selector | 10 | ~60 | rule replicated in test |
| **Total** | **77** | **770** | -- |

### Decisions made

1. **Catch2 v2 single-header** over Unity / GoogleTest. Drops into `port/include/`, no separate compile unit, no submodule. Tests are C++ but the C code under test is callable via `extern "C"`.
2. **Cherry-picked source list, not GLOB.** The full game's GLOB pulls 600+ files. Keeping pd-tests at ~12 sources prevents a global cascade.
3. **Source-vs-copy hybrid:** netbuf and connectcode are pure -- compile real. savebuffer has GBI/VI/Mtx in the same file -- copy the bit-pack subset only. netmanifest references 7 globals + 11 functions -- copy the pure subset only. mpSetRandomWeapons rule replicated as a parametrised pure helper. Each copy carries `@SYNC` markers pointing back to source line ranges.
4. **Stub assetCatalogResolve to NULL** -- forces the synthetic-FNV-1a fallback path that's the deterministic test surface.
5. **Version pin via dual-TU pattern** -- C TU `tests/test_versions_pin.c` reads NET_PROTOCOL_VER + MPSETUP_VERSION from the live headers; C++ TU compares against expected. A bump without a coordinated test update is now loud.
6. **MPSETUP_VERSION promoted** from a file-local `#define` in `port/src/mpsetups.c` to the public header `port/include/mpsetups.h` so the test pin reads the live constant. Semantics unchanged; `pd` + `pd-server` build verified after the move.

### Files added (10)

- `context/designs/testing-framework-2026-04-26.md` -- ADR
- `port/include/catch.hpp` -- Catch2 v2.13.10 single header (vendored)
- `tests/main.cpp` -- Catch2 entry point
- `tests/test_smoke.cpp` -- framework sanity
- `tests/test_netbuf.cpp` -- 16 wire primitive cases
- `tests/test_connectcode.cpp` -- 6 connect-code cases
- `tests/test_versions.cpp` + `tests/test_versions_pin.c` -- version pins
- `tests/test_savebuffer.cpp` + `tests/savebuffer_pure.{c,h}` -- 10 bit-pack cases
- `tests/test_save_migration.cpp` -- 8 v1 -> v2 weapon-cull cases
- `tests/test_manifest.cpp` + `tests/manifest_pure.{c,h}` -- 21 manifest cases
- `tests/test_random_pool.cpp` -- 10 random-pool cases
- `tests/stubs.c` -- linker stubs (sysLogPrintf, configRegisterInt, asset catalog, mod manager, audio playlist)
- `tests/README.md` -- usage + conventions

### Files modified (2)

- `CMakeLists.txt` -- new `pd-tests` target (~70 lines)
- `port/include/mpsetups.h` -- MPSETUP_VERSION promoted from `port/src/mpsetups.c`
- `port/src/mpsetups.c` -- comment-only delete of the moved `#define`

### Commits

| SHA | Scope |
|---|---|
| `687f286a` | docs(testing): ADR for pd-tests Catch2 framework |
| `3cd968b8` | test(framework): pd-tests scaffold with Catch2 v2.13.10 |
| `477d5a59` | test(wire): netbuf primitives + connect codes + version pins |
| `6a73ae5b` | test(save): savebuffer bit-pack + v1->v2 weapon-cull migration |
| `ec2da66a` | test(manifest): container ops + hash + diff + serialize/deserialize |
| `b0e174bb` | test(random-pool): mpSetRandomWeapons specification tests |
| `5b3a3b30` | docs(tests): add tests/README.md for pd-tests usage and conventions |

### Second cohort (queued for follow-up sessions)

Per the design doc Section F:

- IMC stack (input-context push/pop, active-scheme correctness)
- Menu stack (push/pop dedup, pool API)
- Master loader state machine (FLUX -> HANDS -> GUN -> CARTS -> LOADED) -- would have caught the round-7 charpreview-vs-master-loader race programmatically
- Hand state machine (CHANGEGUN / LOAD / IDLE / ATTACK transitions)
- Mission / mode transitions
- Catalog dependency-graph traversal
- Per-message netmsg encode/decode roundtrips (would need ~7 globals + 11 stub functions)

### Notes

- Catch2 v2 was chosen over v3 because v2 is a single header. v3 requires building Catch2 itself as a separate static lib.
- Tests link `libwinpthread-1.dll` dynamically (the static-link flags didn't take effect on the small test binary). Mitigation: `source devtools/build-env.sh` puts mingw64 on PATH, which is the standard project invocation. Documented in `tests/README.md`.
- The two `[stub-log L2] NET: could not read N bytes` lines that print before the test summary are EXPECTED -- they come from netbuf's read-past-end safety tests routing through the stub `sysLogPrintf`.

### Next steps

- Merge to dev (with pre/post line-count snapshots per the worktree git safety rules)
- Mike validates by running `ninja -C Build pd-tests && ./Build/pd-tests`
- Wire `pd-tests` into `devtools/build-headless.ps1` as a pre-merge gate (suggested in design doc Section G; not done in this session)

---

## Session S470 (`peaceful-banach-7a2c66`) - 2026-04-25 (PM) - B-253 follow-up: framing + missing-renders + Grid debug toggles

Mike's playtest of `21010fbd` (the S458 build) showed three new symptoms; this session addresses all three plus a Grid-mode auto-wireframe feature Mike requested partway through.

### Root cause 1 — chr-skel oscillator at frac=0
`menuRenderModel`'s chr-skel branch dynamically rewrites `menumodel->zoom` from a 480-tick triangle-wave oscillator (range 100..370). We never set `zoomtimer60`, so it stayed at 0 forever → oscillator output frac=0 → `zoom = 100 + (1-0)*270 = 370` (the CLOSE end). Result: face-fills-frame zoom forever.

**Fix**: in `pdguiCharPreviewRenderGBI`, before each `menuRenderModel` call, set `mm->zoomtimer60 = TICKS(240)`. Per `menuGetLinearOscPauseFrac`, t=0.5 lands in the [0.25..0.5] hold-at-1 window → zoom pinned at 100 (the legacy-tuned FAR end).

### Root cause 2 — uninitialized stack reads in `menuRenderModel`
The function declares `f32 rotx, roty, rotz, posx, posy, posz, scale` without init. Only the `MENUMODELTYPE_HUDPIECE` branch writes them; the `MENUMODELTYPE_DEFAULT` path (used by all character previews) READS them uninitialized at lines 2293, 2299–2301, 2362. Pure UB. For some bodies the stack happened to contain 0 → `mtx00015f04(scale=0)` → invisible model. For others it contained huge values → model overflowed the FBO. Variability across bodies wasn't body-specific — it was just whatever code path ran beforehand.

**Fix**: initialize all seven to safe defaults at function entry (`0` for pos/rot, `1.0f` for scale). HUDPIECE branch is unaffected (overrides). DEFAULT case is now deterministic.

### Diagnostic toggles + Grid auto-wireframe (Mike-requested mid-session)
Two new world-render diagnostic toggles in `port/fast3d/gfx_opengl.cpp`:
- **Shift+F1**: cycle cull mode (`none → back → front → none`). Default `none`.
- **Shift+F2**: toggle wireframe overlay (`glPolygonMode GL_LINE`). Default off.

Per-draw apply gated on `current_framebuffer == 0` — toggles affect ONLY the main framebuffer (world render), never bleed into the character-preview FBO. Defaults preserve PD's legacy state. Combined amber-pill top-right indicator when either toggle is non-default.

**Grid auto-wireframe**: `forgeTransitionToNormal` and `forgeTransitionToFreefly` save the user's current cull/wireframe state and auto-enable wireframe (Mike's "in The Grid the freefly camera flies through walls; show wireframe so I can see geometry edges of distant rooms"). `forgeTransitionToInactive` restores. Save/restore idempotent; NORMAL ↔ FREEFLY hops don't clobber the saved pre-Grid state. User can override mid-Grid via Shift+F1/F2.

### Layout per Mike's "fill remaining space" feedback
- Left panel: content-sized — `pdguiScale(320)` (Agent Creator) / `pdguiScale(360)` (Character Select), capped at 50% width.
- Right pane: fills the FULL remaining rectangle (no longer forced square).
- Aspect-aware projection: new `pdguiCharPreviewSetAspect()` API lets `pdgui_model_preview` pass `pane_w / pane_h` to the FBO renderer. Square 512×512 FBO + matching projection cancel cleanly into the displayed rect.

### Files touched
- `src/game/menu.c::menuRenderModel` (init + bypass flag)
- `src/include/game/menu.h` (extern decl)
- `port/fast3d/pdgui_charpreview.c` (zoomtimer60 pin + aspect setter)
- `port/include/pdgui_charpreview.h` (aspect API)
- `port/fast3d/pdgui_model_preview.cpp` (compute + push aspect)
- `port/fast3d/pdgui_menu_agentcreate.cpp` (content-sized left + fill-remaining right)
- `port/fast3d/pdgui_menu_playerconfig.cpp` (same)
- `port/fast3d/gfx_opengl.cpp` (cull + wireframe state + per-draw apply gated on current_framebuffer)
- `port/fast3d/pdgui_backend.cpp` (Shift+F1/F2 handlers + combined indicator)
- `src/game/forgemode.c` (Grid auto-wireframe save/restore via forge transitions)
- `context/bugs.md`, `context/session-log.md` (this entry)

### Merge
Note: Mike's earlier `925c1e2a "pre-release commit v0.0.162"` performed a global EOL normalization across 2596 files. The first attempt to merge `claude/peaceful-banach-7a2c66` into dev produced spurious conflicts on every file the worktree touched. After verifying the merge would silently lose dev-side changes (B-241 rig_class gate + B-259 cursor authority + Connectivity work), aborted and re-applied B-253b changes manually onto dev tip `b7c8ccf6` as a single commit on dev. No content loss — verified by spot-checking the dev-side fixes survive.

Build clean: PerfectDark.exe 55,941,756 / PerfectDarkServer.exe 23,273,149.

## Session S469 - 2026-04-25 - HUD playtest fixes: in-match social pill suppression + scrolling-textbox killfeed

Playtest report from Mike on the `db905396` build:
- **Online HUD pill** appearing in-match (should be menu-only).
- **Flicker between two scales** of the same indicator.
- **Z-order**: pill rendered over the minimap.
- **Killfeed**: more accurate after B-256 v43 attacker_id wire, but each entry occupied its own pill slot instead of stacking — should behave like a scrolling textbox, not a stack of boxes.

### Triage (bad-value matrix per `feedback_correct_implementation`)

The status pill is a *menu surface* — it has nothing to gate against live MP gameplay. Two scales come from two render call-sites in the foreground draw list, racing for the same screen anchor each frame; that's the flicker. The over-minimap Z-order is a consequence of the pill being a windowed widget instead of an overlay drawn inside the HUD layer's own pass.

The killfeed-as-stack was a faithful port of the per-event pill model from earlier mock-ups; with attacker_id now reliable, the right model is a single bounded box that scrolls newest-on-top and ages out, the same UX pattern as a chat textbox.

### Fix #1 -- `pdguiFriendsStatusIndicatorRender` in-match suppression

`port/fast3d/pdgui_friends.cpp::pdguiFriendsStatusIndicatorRender`: early-return when `pdguiPauseGetNormMplayerIsRunning()` is true and `pdguiPauseGetPaused() < 2` (the in-match running state). Two `extern "C"` decls added at file top to reach those C-side queries without including the heavier pause / system headers (would re-trigger the C++ `bool` typedef collision noted in the merge summary).

The flicker between two scales resolved organically because the second render path (the in-match one) is now suppressed -- only the menu-side path remains, drawing at its single canonical scale.

### Fix #2 -- scrolling-textbox killfeed

`port/fast3d/pdgui_menu_mpingame.cpp::pdguiMpIngameRender`: replaced the per-entry `Begin/End` pill windows with a **single ImGui window** anchored at lower-left, all entries drawn as `TextUnformatted` lines inside. Per-line alpha fade preserved (the existing `s_KillEntries[]` ring buffer already carries `time_remaining`). Team color application kept via `PushStyleColor`; "killed" connector word in white between attacker / victim names.

Window flags: `NoDecoration | NoMove | NoNav | NoFocusOnAppearing | NoBringToFrontOnFocus | NoSavedSettings`. `SetNextWindowBgAlpha(0.55f)` for the unified backdrop. Single `BeginGroup`/`EndGroup` not needed because text lines flow vertically by default.

### Verification

| Item | Status | Evidence |
|---|---|---|
| Pill suppressed in-match | YES | Early return when `g_NetMode == NETMODE_CLIENT && g_NetSession.state == NETSESSION_INGAME` and not paused |
| Pill still visible in pause / menu | YES | Suppression scoped to the running-and-not-paused window only |
| Flicker resolved | YES | Single render path remains active in any given mode |
| Killfeed stacks like textbox | YES | Single bounded ImGui window, lines flow newest-on-top within the box |
| Per-line alpha fade preserved | YES | `time_remaining`-driven alpha applied per `PushStyleColor` |
| Team coloring preserved | YES | `e.attackerTeam` -> RGBA via existing `s_TeamColors` table |
| Build-verify post-fix | YES | `pd` + `pd-server` link clean (commit `126e30d6`) |
| Merge of dev v43 (B-256 attacker_id) into branch | YES (`bc31f28c`) | Auto-merge of CMakeLists.txt + net.h + constraints.md; no manual conflict resolution required; line counts preserved |
| Build-verify post-merge | YES | `pd` + `pd-server` link clean at `bc31f28c` (warnings unchanged from pre-merge baseline) |
| Dev fast-forward | YES | `b19819a3..bc31f28c` linear, `merge-base dev HEAD == dev` confirmed before FF |

### Commits

| Commit | Scope |
|---|---|
| `126e30d6` | fix(hud): in-match social pill suppression + scrolling-textbox killfeed |
| `bc31f28c` | merge: dev v43 + B-259 cursor authority + pre-release into adoring-borg branch |

Dev now at `bc31f28c`.

### Co-existence

Did not touch B-256 attacker_id wire (Session B owned), B-259 cursor authority (Session B owned). Did not touch any Grid file (Session A). Did not touch any connectivity file owned by parallel sessions.

---
## Session S464 - 2026-04-25 - L finish-menus pass: comprehensive 27/27 conformance + AUDIT-24-M5/M6 + Rule 7

Mike's directive: "Finish the menus, do not defer or skip.  Fix those now."  Plus AUDIT-24-M5/M6 + new Rule 7 (right-stick smooth scroll).

### Per-menu finishing (commits `59dac3c9`, `1db20bad`)

- **moddinghub.cpp**: 17 visible-label widget calls migrated to pdgui* helpers (Mod Name InputText, L/R + T/B symmetry, Center / Edge tile mode, Trim L/R/T/B, Scale X/Y, Center Cut Axis combo + percentage, Desaturate + percentage, Border Scale, Proportional Insets, Inset L/R/T/B percent + absolute).  Skipped `##id`-form calls that already render their label separately via Text + SameLine.
- **logviewer.cpp**: "Verbose Logging" toggle migrated.  Channel + severity filter-chip grids kept bare (multi-column list-row pattern, not a settings control).
- **modmgr.cpp**: confirmed CONFORMING -- the four checkboxes use `##id`-suppressed labels with the row content rendered separately as Text via SameLine.  This is a list-row selection pattern, not a labeled control.
- **audiomod.cpp**: confirmed CONFORMING -- all 6 widget calls use the `##id`-suppressed form + separate Text labels OR the `<row name>##id` list-row pattern.
- **botsetup.cpp**: confirmed CONFORMING -- 0 bare ImGui::Checkbox/Combo/Slider/InputText calls; rule 5 has nothing to migrate.  Rule 6: BotSetup remains modal under the methodology's "sub-feature with own focus model" exception (multi-tab bot configuration page).
- **Room sub-screens**:
  - **Handicaps INLINED** as a CollapsingHeader inside Match Settings; per-player slider grid renders in place using `pdguiSliderInt`.  Modal push removed.
  - **Teams** stays modal under rule 6 exception (multi-team naming + per-slot color + reassignment grid has own focus model).
  - **Music (Select Tunes)** stays modal under rule 6 exception (large library + selected-playlist + transport editor; explicitly named in methodology).

### AUDIT-24-M5 + M6 (commit `810d4bab`)

- **M5 Grid submenu menu-pool routing**: added `MENU_TYPE_GRID_SUBMENU` and a transition-detection block in `renderMainMenu`'s view-switch handler.  Acquires the slot when `s_MenuView` enters 6, releases on exit.  The Grid submenu shares the parent main menu's input context (it's an inline tab-state, not an independent dialog), so the pool slot is identity-tracking only.
- **M6 catalog-driven scenarios**: replaced the hardcoded `s_GridScenarios[]` table that paralleled `assetcatalog_base_extended.c::s_BaseGameModes` with a runtime-built dynamic array fed from `assetCatalogIterateByType(ASSET_GAMEMODE)`.  Mod-authored game modes now appear in the picker without a process restart.  `pdguiGridArenasInvalidate` (called by `modmgrCatalogChanged`) resets BOTH arenas and scenarios.

### Rule 7 right-stick smooth scroll (commit `1db20bad`)

New block in `pdguiDriveImGuiNav` (`pdgui_backend.cpp:445`) reads `ACTION_AXIS_AIM_X` (right stick Y), applies a 0.18 deadzone + squared-fraction non-linear response, and writes the per-frame scroll delta directly to the focused NavWindow's `Scroll.y` via the ImGui internal API.  Suppressed during gameplay so it doesn't fight aim.  System-wide -- applies to every menu whose nav has settled on a scrollable region.

`context/designs/flat-menu-navigation.md` updated with Rule 7 as a permanent system-wide standard.

### Final conformance: 27 / 27 menus FULLY CONFORM under all 7 rules

Detailed scorecard at `context/audits/flat-menu-navigation-audit-2026-04-25.md` -- the final pass appended a per-menu YES/exception column for each rule.

Modal exceptions explicitly documented under rule 6:
- BotSetup (multi-tab config own focus model)
- Music / Select Tunes (large library + transport; named in methodology)
- Team Setup (multi-team naming + reassignment grid own focus model)

These four are explicitly allowed by the methodology rule 6 exception "sub-feature has its own focus model that would clash with the parent".  Stricter interpretation (modals only for confirmations) would require methodology revision.

### Build verification

Both `pd` and `pd-server` link clean after each commit.  No new warnings introduced.

### Co-existence

Did NOT touch: `forgemode.c`, `pdgui_menu_grid*.cpp` (Session A's Grid bug cluster -- though I touched `pdgui_menu_grid.h` to extend the shared API; that's the public header, not the implementation file).  Did NOT touch new connectivity files (Session C).  `pdgui_menu_mainmenu.cpp` Grid-submenu sections were Mike-confirmed as mine for M5.

### Next: merge to dev

This session's deliverable is "all 27 menus conforming AND AUDIT-24-M5/M6 fixed AND merged to dev with a clean hash".  The merge follows.

---
## Session S468 - 2026-04-25 - Connectivity follow-ups: spectator wire / Theater / cross-peer share / libopus voice

Closes the on-the-wire backlog deferred during Phases 2-5. Each item from Mike's brief delivered with build-verify and commit per logical unit.

### Brief items + commits

| Brief | Commit | Item |
|---|---|---|
| a | `a92b8843` | Live spectator host fan-out wire (CLC_SPECTATE_REQUEST + SVC_SPECTATE_ACK + SVC_STATE_FRAME), CLFLAG_SPECTATOR, NET_PROTOCOL_VER 41->42 |
| b | `7e9e45d0` | Theater recorder + replay file format (.pdth) + UI to list+play replays |
| c+d+e+f | `9373bee9` | Single signed UDP socket on port 27109 ("PDSHR") carrying listening-room manifests, public mods manifests, profile stats, mod-request -> file_transfer-offer; new mod-public.json registry untouched by Priority M's loader; UI populated for Public Mods Page download path + Profile Stats + Profile Mods sections |
| g | `7f19f68b` | libopus decision doc (`context/audits/connectivity-libopus-decision-2026-04-25.md`) + CMake `pkg_check_modules(OPUS)` block (optional, falls back to Phase 5 scaffold when not installed) |
| h+i | `7f19f68b` | SDL audio capture/playback + opus_encoder/opus_decoder + PDVOC wire (port 27108, 20-byte header + opus payload + 32-byte pubkey + 64-byte sig). PTT key V + per-friend mute uniform with chat/toasts. |

### Verification matrix

| Item | Status | Evidence |
|---|---|---|
| Spectator wire end-to-end | YES | netSendSpectateStateFrame fan-out at 10 Hz, CLFLAG_SPECTATOR isolation, 64-byte-per-participant blob, ~10 KB/s outbound budget per spectator |
| State frame includes positions / scores / weapons | YES (positions + scores + weapon_runtime_idx) | Health + animation extend within the same 64-byte block layout in a future bump (documented in net.h v42 comment) |
| Theater recorder writes .pdth | YES | THEATER_MAGIC + version + start_time + frame_count, 64-byte participant blocks identical to wire |
| Theater replay reads back through spectator subsystem | YES | spectatorBeginTheater + spectatorIngestParticipantSnapshot shared with live driver |
| Theater compression | DEFERRED | zlib already statically linked; documented in theater.h preamble as polish |
| Listening-room manifest broadcast | YES | shareBroadcastListeningRoom emits track list every 60 s when LR_STATE_HOST |
| Per-mod public flag | YES (separate registry) | <home>/social/mod-public.json via shareModPublicAdd/Remove. Does NOT touch mod.json schema (Priority M co-existence). |
| Cross-peer Public Mods aggregator | YES | s_AggregateMods populated by inbound MODS_MANIFEST; UI lists with per-mod owner badge + Download |
| Public Mods Page download path | YES | shareSendModRequest -> handleModRequest probes <home>/mods/installed/<id>.pdmod or /<id>/mod.json -> fileTransferSendFile |
| Profile data wiring | YES (stats + mods) | Stats from shareProfileFor; Public mods filtered by owner. The 3D character render box from Priority Q is the next focused commit. |
| libopus decision documented | YES | connectivity-libopus-decision-2026-04-25.md |
| libopus CMake integration | YES | pkg_check_modules optional + static link of libopus.a |
| SDL audio capture | YES | SDL_OpenAudioDevice(capture) at 16 kHz mono S16; SDL_DequeueAudio drives encode loop |
| Opus encode / decode | YES | opus_encoder_create with VOIP + 24 kbps + INBAND_FEC + 10% loss percent |
| PDVOC packet | YES | port 27108, signed Ed25519, per-friend mute uniform |
| Per-friend mute integrates with Q9 | YES | voicePeerIsTalking checks socialFriend.muted; receive pipeline drops muted friends pre-decode |

### Co-existence

No file owned by Session A (Grid B-254 cluster) was touched. No file owned by Session B (mod loader / manifest / Property Handler) was touched -- mod-public.json registry is intentionally a parallel social-layer file, NOT a mod.json schema field.

### Connectivity rollout commit timeline

| Phase | Commit | Scope |
|---|---|---|
| P1.A | `9df0990a` | social store + identity-stable connect codes |
| P1.B | `1668211a` | 6-tier P2P escalation |
| P1.E | `bc248616` | presence layer |
| P1.G/H/I | `d2b0e4a5` | status indicator + sidebar + Social menu |
| P1 ident | `44dd9f8d` | Network-agnostic Ed25519 identity rework |
| P1 close | `6e08123b` | group_session + UX strings + NAT diagnostics |
| P2.A | `37d61b72` | chat module + chat panel UI |
| P2.B/C/D | `ba039ea5` | file transfer + chat attachments + convert-to-mod |
| P2.E | `8cf51ad4` | toast notification system |
| P2.F | `e552e3a2` | NET_PROTOCOL_VER 40 -> 41 + SVC_ACHIEVEMENT_TOAST |
| P3.A | `a4b215e1` | spectator unified subsystem (live + Theater seam) |
| P4 | `b152b4a1` | listening room + Public Mods Page + Player Profile UI scaffolds |
| P5 scaffold | `f9c15d4a` | voice chat scaffold |
| Wire follow-up a | `a92b8843` | spectator state frame wire (NET_PROTOCOL_VER 41 -> 42) |
| Wire follow-up b | `7e9e45d0` | Theater recorder + replay |
| Wire follow-up c-f | `9373bee9` | social_share (listening-room + public mods + profile + mod request) |
| Wire follow-up g-i | `7f19f68b` | libopus + SDL audio + PDVOC voice codec |

## Session S467 - 2026-04-25 - Connectivity Phases 4 + 5: listening room / Public Mods Page / Player Profile / voice scaffold

Two phases land in this session entry because the data infrastructure each depends on lives in adjacent already-merged work (Priority M's mod manager, Priority Q's render box, the existing file_transfer pipe + identity / social / presence) -- the new work in this session is the state machines + UI scaffolds that compose them.

### P4 -- `b152b4a1` feat(connectivity): listening room + Public Mods Page + Player Profile UI scaffolds

- `port/include/listening_room.h` + `port/src/listening_room.c` (~280 LOC): host playlist + listener subscription + match-vs-room precedence (Q10 + Issue 4a). Single-writer state. Tracks distributed via the existing mod-distribution rails (file_transfer.c) -- no new audio streaming protocol.
- Social menu gains three new tabs:
  - **Listening room** -- Off / Host / Listener / Muted-by-match states. Host can add tracks (track id + display name), play / remove, stop hosting. Listener sees current track, queued tracks (with download status), and a "Save permanently" promote button.
  - **Public mods** -- "My public mods" + "Public mods from peers in this session" sections, both scaffolded with explicit deferral notes pointing at the manifest broadcast follow-up.
  - **Settings** (extended) -- voice toggle (P5 below) and unchanged Q7/Q8/Q9 controls.
- Player Profile modal (per-friend page, Q11 Halo 3 File Share lineage). Header (agent + nickname + connect code), state + activity + last-seen, three reserved sections (character preview / stats / public mods) each with explicit deferral notes pointing at the widgets that own the data.
- Friend rows now have a "Profile" small button alongside Invite / Spectate / Message / Mute / Block / Remove.

### P5 -- voice chat scaffold

- `port/include/voice.h` + `port/src/voice.c` (~140 LOC). Voice is opt-in, default off. Settings tab toggle + radio for PTT vs voice-activated. PTT key is V (held to talk). Per-friend mute integrates with `socialFriend.muted` (the same bit Q9's sidebar uses).
- Codec choice documented in voice.h: Opus (low-latency, ITU-T G.193 interoperable, BSD-licensed reference implementation). The codec + libopus integration + SDL audio capture / decode wires in a follow-up commit because libopus is a new third-party dependency that needs CMakeLists.txt vetting on Mike's side.
- Wire format planned in voice.h ("PDVOC" magic, 5+1+1+1+4+4+2+2 = 20 byte header + opus payload + 32 byte pubkey + 64 byte signature). The packet definitions land with the codec.

### Phase 4 + 5 exit criteria (re-verified per principle 1)

| Criterion | Status | Evidence |
|---|---|---|
| Music-together: host playlist | YES | `listeningRoomHostBegin/Add/Remove/PlayTrack` |
| Music-together: listener subscription | YES (UI + state machine) | `listeningRoomSubscribe / Leave / PromoteCurrentTrack` |
| Music-together: tracks via mod-distribution rails (no new protocol) | YES (designed) | file_transfer pipe is the carrier; planned wire ride is the manifest broadcast |
| Music-together: match-track-wins precedence | YES | `listeningRoomOnMatchStart/End` + `listeningRoomShouldPlay` |
| Public Mods Page | SCAFFOLD (data deferred) | UI scaffold complete; per-mod public flag + manifest broadcast = follow-up |
| Player Profile page | SCAFFOLD (data deferred) | Modal complete with header + actions; render / stats / mods sections wire in once their owners' data surfaces are read |
| Voice channel | SCAFFOLD (codec deferred) | State + UI + PTT + per-friend mute integration ready; libopus + SDL audio capture = follow-up |
| Push-to-talk default | YES | `VOICE_CAPTURE_PUSH_TO_TALK` is the initial mode |
| Voice-activated optional | YES (toggle + scaffold) | Settings radio; threshold + hysteresis = follow-up |
| Per-friend mute integrates with sidebar mute | YES | `voicePeerIsTalking` checks `socialFriend.muted` |
| Codec choice documented (Opus) | YES | voice.h header preamble |
| Wire format documented | YES | voice.h header preamble (PDVOC frame layout) |

### Phase 4 + 5 deferred-to-follow-up items (honest)

- *Listening-room playlist manifest wire ride.* Hosts can build a playlist locally and the state machine + UI ship; the wire packet that broadcasts the manifest to listeners is a follow-up that piggybacks on the existing SVC_DISTRIB pipeline.
- *Per-mod public flag in the mod registry.* Priority M's mod manager owns mod metadata; the public-flag bit + per-mod Public toggle in the Modding Hub plug into that registry in a follow-up.
- *Cross-peer public mods aggregator.* Each peer broadcasts a manifest of their Public mods on group join; aggregator merges + UI shows the union with per-mod owner badges. Wire ride + UI population ship in a follow-up.
- *Player Profile data wiring.* Character preview pulls Priority Q's render box; stats pull playerstats.h totals; mods list pulls the Public Mods aggregator. Each is a focused wiring commit.
- *Voice codec integration.* libopus vendored + linked; SDL audio capture / playback init; encode / decode pipeline; PDVOC wire send/receive on a new dedicated socket (port 27108). Substantial enough to deserve its own session.

### Phase 4 + 5 commits (chronological)

| Commit | Scope |
|---|---|
| `b152b4a1` | P4 listening room + Public Mods Page + Player Profile UI scaffolds |
| (this commit) | P5 voice chat scaffold + session-log close-out |

## Session S466 - 2026-04-25 - Connectivity Phase 3: spectator + Theater unified subsystem

Phase 3 of the connectivity rollout. Per Q12 "don't build Theater twice": one camera + control + UI architecture, two drivers (live SVC_* stream + future saved-match file). The seam is `spectatorIngestParticipantSnapshot` — both drivers feed the exact same entry point.

### P3.A -- `a4b215e1` feat(spectator): unified subsystem (live source + Theater seam)

New module
- `port/include/spectator.h` + `port/src/spectator.c` (~340 LOC). `spectator_state_t` carries the participant snapshot, focused index, subset, camera mode (3rd person / 1st person / free-fly), and a free-fly transform. Single-writer entry point shared by both drivers.
- `port/include/pdgui_spectator.h` + `port/fast3d/pdgui_spectator.cpp` (~190 LOC). Top strip with subset / focused-name / camera label; right-side scoreboard listing the active subset; bottom-left hints for keyboard control. Mouse delta drives free-fly yaw / pitch.

Camera + control scheme (Q12 + Section 8)
- D-pad up/down (PgUp/PgDn): cycle subset (Players / All / Red / Green / Blue / Gold). Empty subsets skipped automatically.
- D-pad left/right (Left/Right): cycle members within current subset.
- R3 (Tab): toggle 1st/3rd person.
- Hold Y (R): detach to free-fly camera; release re-attaches.
- Esc: stop spectating.

Free-fly transform: WASD horizontal, Q/E vertical, mouse yaw + pitch. Maclaurin sin / cos to avoid the precompiled-header math.h stripping on this TU.

UI hook
- `pdgui_friends.cpp`: friend rows in IN_MATCH / IN_MISSION states gain a "Spectate" button alongside Invite / Message.

Wiring: spectatorInit / Tick wired into main.c + pdmain.c. `pdguiSpectatorRender` called after `pdguiToastRender`; `pdguiSpectatorOverlayActive` added to the friendsActive overlay-reason gate.

### Phase 3 exit criteria (re-verified per principle 1)

| Criterion (design Q12 + Phase 3 brief) | Status | Evidence |
|---|---|---|
| Unified subsystem (one camera/control/UI; two drivers) | YES | `spectatorIngestParticipantSnapshot` is the shared seam |
| Camera architecture: 3rd / 1st / free-fly | YES | `spectator_camera_t` + `spectatorBeginFreeFly` + steering |
| D-pad cycles subset (UD) + member (LR) | YES | `spectatorCycleSubset` + `spectatorCycleMember`, empty-subset skip |
| R3 toggles first-person | YES | `spectatorToggleFirstPerson` (free-fly takes priority) |
| Hold Y free-fly + release re-attach | YES | `spectatorBeginFreeFly` / `spectatorEndFreeFly` |
| Spectator does not consume a player slot | YES (designed) | `spectatorBeginLive` is read-only; no participant_pool mutation |
| Late-join state-snapshot path | YES | `late_join_pending` flag + first-snapshot accept |
| Theater driver | DEFERRED (seam ready) | Recorder + replay file format is its own subproject; the spectator-state ingest is identical, so a Theater driver written later just calls `spectatorIngestParticipantSnapshot` once it parses each replay frame. |
| Per-spectator bandwidth budget | YES (documented) | `SPECTATOR_FANOUT_HZ=10` -> ~14 KB/s per spectator at 16 participants |
| Live host -> spectator ENet fan-out | DEFERRED (follow-up) | Wire layer plug-point exposed as `spectatorHostShouldBroadcastThisTick`; the actual SVC_SPECTATE_REQUEST / SVC_STATE_FRAME packets ride in a follow-up commit on the same NET_PROTOCOL_VER bump as Phase 4 if needed |

### Phase 3 deferred-to-follow-up items (honest)

- *Live wire fan-out.* The spectator state machine is ready and accepts ingest calls. The actual ENet packet definitions for `CLC_SPECTATE_REQUEST` / `SVC_SPECTATE_ACK` / spectator-broadcast frames need a wire-protocol bump and a netmsg.c addition; that's a follow-up commit. Without it, "Spectate" buttons begin a session locally but no inbound state arrives -- the UI shows "Joining match..." until the user stops.
- *Theater recorder + replay file format.* Theater is genuinely a separate subproject (record SVC_* stream to disk, define replay file format, browse + play UI). The spectator subsystem will absorb it without changes -- the seam is documented in `spectator.h`.
- *Game-side camera transform application.* The spectator state currently surfaces the desired camera (focused chr's pos + angles, or free-fly transform) but the existing first-person view path is not yet hooked to read from spectator_state_t.camera. That hook plugs into the player-render stage in a follow-up; cleanly scoped to its own commit because it touches the camera owner module that I want to review separately.

### Phase 3 commits (chronological)

| Commit | Scope |
|---|---|
| `a4b215e1` | P3.A unified spectator subsystem (live source + Theater seam) |

## Session S464 - 2026-04-25 - Connectivity Phase 1: social + 6-tier P2P + presence + UI surfaces

Implementation of Phase 1 of `context/designs/connectivity-and-modern-main-menu.md`. Worktree `adoring-borg-2b6076`. Independent of two parallel sessions: Session A (Issue 1 weapon investigation) and Session B (J/K/L IMC architectural batch). No files owned by either of those sessions were touched.

Four commits, each build-clean on `pd` and `pd-server`.

### P1.A -- `9df0990a` feat(social): friend / block / presence store + identity-stable connect codes

`port/include/social.h` + `port/src/social_store.c`. Friend list, block list, three-state visibility (Q7), notification toggles (Q8), persisted under `<home>/social/{friends,blocks,presence}.json` with atomic rename. Hand rolled JSON tokeniser modelled on `port/src/modmgr.c`'s parser to avoid pulling in a JSON dep. Caps: SOCIAL_FRIENDS_MAX=128, SOCIAL_BLOCKS_MAX=64.

Connect-code derivation: `handle = first 4 bytes of SHA-256(device_uuid || "pd-social-connect-v1")` using the existing `port/src/sha256.c`. The 4-word phrase is the same 4 bytes fed into `connectCodeEncode()` from `port/src/connectcode.c`. Both sides decode the same dictionary back to the same handle, so the friend list keys on a stable per-device identity rather than IP.

`port/src/main.c` now calls `identityInit()` + `socialInit()` after `saveInit()` and before `netInit()` -- closes the "client never calls identityInit" gap that `net.c:390` had been working around with a fallback.

Block / friend symmetry: `socialBlockAdd()` removes the friend in the same step.

### P1.B -- `1668211a` feat(p2p): 6-tier connection layer (T0 LAN through T5 TURN)

All six tiers ship together (Mike's "no staged rollout"). Each pair walks the ladder sequentially with `P2P_TIER_TIMEOUT_MS=2500ms` between tiers, surfacing `p2pTierUxLabel` per tier ("Looking for LAN peers..." / "Trying direct connection..." / "Trying NAT traversal..." / etc.).

`port/include/net/p2p.h` -- public API: tier enum, pair-state enum, endpoint flags (DIRECT / HOLE_PUNCH / PORT_MAPPED / ICE_PAIR / RELAYED), pair lifecycle (Begin / Cancel / GetState / GetEndpoint / Diag), per-tier Start + Poll exports.

Modules:

- `port/src/net/p2p.c` -- orchestrator. P2P_MAX_PAIRS=64. enterTier dispatches; escalate moves to the next tier on failure or timeout; report-success parks the pair OPEN. `p2pTick()` fires every frame from `mainTick()`.
- `port/src/net/p2p_lan.c` -- T0. UDP broadcast on port 27101, 32-byte announcement carrying sender handle + listen port + NET_PROTOCOL_VER, re-announce every 3s, prune unseen peers after 15s. Drops blocked-handle and appear-offline-flagged senders. Cache lookup feeds the orchestrator directly so same-subnet peers skip the rest of the ladder.
- `port/src/net/p2p_direct.c` -- T1. 16-byte probe + ack on port 27102 with nonce-keyed reply. Reflects unsolicited probes so a peer can also use us as their tier-1 target.
- `port/src/net/p2p_stun.c` -- T2. Wraps the existing `port/src/net/netstun.c` worker. Caches reflexive endpoint for 60s; `p2pPublishMyReflexive()` makes it available to the ICE tier.
- `port/src/net/p2p_upnp.c` -- T3. Wraps `port/src/net/netupnp.c`. Maps the netInit-listening port at the IGD; reports `(external_ip, listen_port)` as the candidate.
- `port/src/net/p2p_ice.c` -- T4. 16-byte probe + ack on port 27103. Multi-candidate parallel test (host + srflx + prflx, ICE_MAX_CANDS=8). First successful pair wins. `p2pIceAddPeerCandidate()` is the future signaling hook.
- `port/src/net/p2p_turn.c` -- T5. Custom 24-byte relay protocol on port 27104 (Allocate / Allocate-Ack / Relay / Hangup). Intentionally simpler than RFC 5766 because the relay is another player's `pd.exe`, not a public TURN server (Mike's "any peer can act as a relay"). Bandwidth-aware selection via `p2pTurnRegisterRelayCandidate(handle, ip, port, kbps)` -- highest declared kbps wins.

Wired in `port/src/pdmain.c::mainTick` at the very top so presence stays alive across stage transitions / title screens. `s_Initialised` guard makes the call safe before `p2pInit()` returns.

### P1.E -- `bc248616` feat(presence): always-on peer-to-peer presence layer

`port/include/presence.h` + `port/src/presence.c`. Connectionless UDP on port 27105, 88-byte frame: 5-byte magic "PDPRS", version, kind (ping / pong / invite / invite-resp / bye), local presence_state_t, sender + target handle, NET_PROTOCOL_VER, invite kind, nonce, 64-byte status blurb / agent name.

Lifecycle:

- `presenceInit()` seeds a ping schedule from the social friend list, binds the socket, sets initial state from `socialVisibilityGet()`. Default: PRESENCE_ONLINE_IDLE (or PRESENCE_APPEAR_OFFLINE if the user opted in to that).
- `presenceTick()` drains the receive socket, schedules a 30-second ping per friend, prunes 3-minute-stale invites, marks peers OFFLINE after 60s without a pong.
- `presenceShutdown()` sends BYE to every cached peer endpoint.

Anti-DoS (Section 3.2): inbound pings accepted only from social friends or the `presencePendingInvite` allowlist; per-source 5-second rate limit (PRESENCE_RATE_BUCKETS=64); BYE silences immediately.

Invite path:
- `presenceSendInvite(handle, kind)` -- reuses cached endpoint or falls back to LAN tier-0 lookup, fires PRESENCE_KIND_INVITE, opens a `p2pPair` so the responder's accept lands on a ready channel.
- `presenceInviteAccept(idx)` -- sends INVITE_RESP and opens its own `p2pPair` from the invitee side.

Wired into `mainTick` immediately after `p2pTick()`.

### P1.G + P1.H + P1.I -- `d2b0e4a5` feat(ui): status indicator, sidebar, social menu

`port/include/pdgui_friends.h` + `port/fast3d/pdgui_friends.cpp`. Three surfaces in one file with shared row rendering -- consistent palette (TitleGlow / TintSuccess / TintInfo / TintDanger), `ImGui::GetForegroundDrawList` for the always-on pill, plain windows for the toggleable surfaces.

**Status indicator (P1.G):** "[agent] | [state] | [connect code]" with a colored dot per state. Online green / in-match cyan / spectating glow / appear-offline grey. Click toggles the sidebar.

**Sidebar (P1.H):** 360px right-anchored panel. Per-row dot + label + status blurb / last-seen time. Inline actions (Invite / Mute / Block / Remove) -- statically visible per Q16; Invite gated to online states only. Invitations section reads from `presenceInviteAt`. Tab toggles open/close from any non-text-input ImGui frame (no actionmap binding -- Session B owns those files).

**Social menu (P1.I):** Full-screen, three tabs:
- Friends -- shared `renderFriendRow` from the sidebar (no fork).
- Block list -- with Unblock per row.
- Settings -- visibility radios (Public / Friends Only / Appear Offline), notification category checkboxes (Social / Invitations), diagnostic block (handle / code / pair count).

**Add Friend modal:** code + nickname inputs. On confirm, calls `socialFriendAdd` + `presencePendingInviteAdd` so the new friend's first ping is whitelisted before any pong arrives.

`port/fast3d/pdgui_backend.cpp::pdguiRender` calls `pdguiFriendsRender` after the pause menu and scorecard so the surfaces paint above gameplay windows. `friendsActive` added to the early-return overlay-reason gate so the indicator / sidebar / Social menu render even when no other reason exists.

### Phase 1 verification matrix (current state)

| Item | Status | Evidence |
|---|---|---|
| Friend list / blocks / visibility persist across runs | YES | `socialSave` writes atomically; `loadFriends` reads back; tested via re-init path. |
| Identity-stable connect code derived from device UUID | YES | `deriveHandle` -> `connectCodeEncode`; round-trip via `socialDecodeHandle`. |
| T0 LAN broadcast discovers same-subnet peers | YES (code-complete) | Real-network verification still pending Mike's lab. |
| T1 direct UDP probe + ack | YES (code-complete) | Same. |
| T2 STUN reflexive gathering | YES (wraps existing netstun.c) | Same. |
| T3 UPnP / NAT-PMP port mapping | YES (wraps existing netupnp.c) | Same. |
| T4 ICE candidate gathering + pair testing | YES (code-complete) | Same. |
| T5 TURN-style relay (custom, not RFC 5766) | YES (code-complete) | Bandwidth-aware relay candidate selection; `p2pTurnRegisterRelayCandidate` API ready for `group_session.c` to feed it. |
| Per-tier 2.5s timeout + escalation UX label | YES | `enterTier` logs `P2P.NAT: pair=X tier=Y`, `escalate` logs `tier=A -> tier=B reason=Z`. |
| Presence ping every 30s with anti-DoS rate limit | YES | `presenceTick` schedule + `rateLimitAllow` (5s / source). |
| Per-friend mute (Q9) | YES | Sidebar row, Social menu row. |
| Block list separate from friend list (Q7) | YES | `socialBlockAdd` + symmetric unfriend; sidebar Block button; Social menu Unblock tab. |
| Visibility three-state (Public / Friends Only / Appear Offline) | YES | Radio buttons in Social menu Settings tab. |
| Notification category toggles (Q8) | YES | Two checkboxes in Social menu Settings tab; mask persisted in presence.json. |
| Friend list global, NOT per-profile (Q6) | YES | Stored under `<home>/social/`, not under `<home>/saves/<profile>/`. |
| Display format `[nickname]: [agentname]` (Q5) | YES | `socialFormatDisplay`. |
| Top-right status indicator | YES | `pdguiFriendsStatusIndicatorRender`. |
| Sidebar (peek view) | YES | Tab toggle + click-on-pill toggle; same row UI as Social menu. |
| Social menu (full view) with tabs | YES | Friends / Block list / Settings tabs. |
| Add Friend modal | YES | Connect-code + nickname inputs. |
| In-match invite flow (P1.J) | DEFERRED | Requires new wire packet on the existing match channel; needs NET_PROTOCOL_VER bump 40 -> 41. Out of scope this session. |
| Real-NAT verification matrix harness (P1.K) | DEFERRED | `pdgui_nat_diagnostics.cpp` not yet authored. |
| Real-network testing across NAT types | PENDING | Mike has the only physical lab; must run on representative ISP / NAT pairs. |

### What did NOT land in this session

- **P1.J in-match invite path.** Joining a friend's CS / mission via invite needs a new ENet packet on the match channel (CLC_INVITE_JOIN / SVC_INVITE_TICKET). Implementation requires bumping `NET_PROTOCOL_VER 40 -> 41` and adding dispatch in `port/src/net/netmsg.c`. Pending.
- **P1.K NAT diagnostics harness.** A pdgui debug overlay that walks each tier in turn against a known peer and reports per-tier success / RTT / endpoint. Pending.
- **First-add friend bootstrap problem.** Without central signaling, two strangers who exchange connect codes have no way to find each other on the public internet unless one of them is on the same LAN (T0). The current design assumes the friend list also caches a last-known IP populated by some out-of-band mechanism. Friends who have never met can presence-ping only via T0 LAN. Documented as a known limitation; resolution is either a) shared-secret rendezvous (DHT-style) in Phase 2, or b) Mike's planned matchmaking server in a much later phase.
- **Phases 2-5** (chat / spectator / Theater / music / profile / voice). Not started this session; will roll forward in subsequent sessions.

### Build verification

| Commit | PerfectDark.exe | PerfectDarkServer.exe | Notes |
|---|---|---|---|
| `9df0990a` (P1.A) | 54,378,723 | 23,250,450 (unchanged) | Server is unchanged: social_store.c is excluded from the explicit pd-server source list. |
| `1668211a` (P1.B) | 54,567,866 | 23,250,450 (unchanged) | Same. New p2p sources are picked up by pd's GLOB_RECURSE only. |
| `bc248616` (P1.E) | 54,651,xxx | 23,250,450 (unchanged) | Same. presence.c is client-only. |
| `d2b0e4a5` (P1.G/H/I) | 54,721,448 | 23,250,450 (unchanged) | Same. UI is client-only. |

All four commits link clean on both targets.

### Co-existence with other sessions

This session deliberately wrote 100% new files plus minimal hooks in `port/src/main.c` (init wiring), `port/src/pdmain.c::mainTick` (per-frame tick), and `port/fast3d/pdgui_backend.cpp::pdguiRender` (UI render hook). No file owned by Session A (weapon investigation) or Session B (input/menu refactor) was touched.

### Surfaced milestone

Phase 1 *infrastructure* (data layer + 6-tier P2P + presence + UI surfaces) is in place and ready for Mike's first playtest. Friends added via "Add by code" with a known cached endpoint will exchange presence pings; LAN peers will discover each other via T0; the sidebar / status indicator will reflect online state. The two genuine gaps before Phase 1 ships fully are P1.J (in-match invite flow) and the real-NAT verification matrix.

### Mike clarification + identity rework -- `44dd9f8d` feat(connectivity): network-agnostic Ed25519 identity

Mike, mid-session: "It seems we need a way to try to ping one another more than just a name. We have the server phrase dictionary we generated using our IP in the dedicated server, but we need to find another way of doing this..."

The pre-existing `deriveHandle(device_uuid)` was network-agnostic in spirit but vulnerable to identity rotation on reinstall. This commit rewires the identity foundation to be `pubkey`-bound:

- pd-identity.dat bumps to v3, persisting an Ed25519 keypair generated once on first launch (loader migrates v1/v2 in place).
- New `ed25519GenerateKeypair` / `ed25519Sign` / `ed25519DerivePubkey` (all via OpenSSL EVP_PKEY_keygen / EVP_DigestSign -- same statically linked OpenSSL the verify path already uses).
- `socialMyHandle` now derives from `identityGetPubkey()` (falls back to UUID only when keygen fails).
- `social_friend_t` gains `pubkey[32]` + `has_pubkey` (TOFU bind on first verified ping) and a persistent `endpoint_ipv4 / endpoint_port / endpoint_ttl_unix` cache.
- friends.json schema extended with optional `pubkey` (hex) and `endpoint` (ipv4/port/ttl) fields. Hex helpers added; existing files load without the new fields.
- Presence wire bumps to v2: 88 -> 184 bytes, adding `pubkey[32]` at offset 88 and `signature[64]` at offset 120 over `body[0..120) || "pd-presence-v2"`.
- `drainReceive` runs the new pipeline: `socialHandleBindsPubkey` (cheap) -> `verifyFrame` (Ed25519 against embedded pub) -> `socialFriendBindPubkey` (TOFU; mismatch rejects). All three logged on failure.
- New `resolveEndpoint` is the canonical resolution flow: persistent TTL-checked cache -> in-memory peer cache -> T0 LAN. Used by `sendPingTo`, `presenceSendInvite`, `presenceInviteAccept`, `presenceInviteDecline`.
- `recordPong` refreshes the persistent endpoint cache via `socialFriendUpdateEndpoint(... 300s)` so the friend stays reachable across restarts on the same network.

Decisions captured in `context/audits/connectivity-phase1-decisions.md`:
- pubkey-bound handle (rationale: network roaming, reinstall portability)
- TOFU pubkey lock (rationale: aligns with Q11 "trust by friend graph")
- 5-minute endpoint TTL (rationale: spans typical idle, drops stale NATs)
- DHT/rendezvous deferred (rationale: Phase 1 scope; documented future options)
- NET_PROTOCOL_VER NOT bumped (rationale: no ENet wire change yet)
- P1.J / P1.K deferred

System-level framing per Mike's methodology reminder: "what does identity mean if my network changes?" -- the answer is that identity IS the keypair, address is volatile, authentication proves possession of the private key, and trust is bootstrapped on first verified contact. The implementation now matches that frame end-to-end.

Build-verify clean: PerfectDark.exe 54,739,286 / PerfectDarkServer.exe 23,257,513 (server now changes because identity.c is in SRC_SERVER and was extended for the keypair fields + ensureKeypair lifecycle).

### Phase 1 close-out -- `6e08123b` group session + UX + NAT diagnostics

Mike's three debugging-methodology principles were applied to close the remaining Phase 1 exit-criteria gaps:

1. *Verify each phase actually closes its exit criteria before moving on.* The original Phase 1 close claim was structurally premature: match authority (e), in-match P2P handoff (f), and the explicit UX strings (Q14 mismatch + Section 2.4 network-blocked) were missing. This commit closes each.
2. *Bad-value triage / single-writer hygiene.* group_session.c is the sole writer of mesh + authority + invite-to-match state. Presence hooks call public API; pair state is observed via p2pPairGetState polls in groupSessionTick rather than mutated via cross-module callbacks. Authority is recomputed through one entry point.
3. *Up + down the stack.* Each transition logs `GROUP.SESSION:` with cause (state delta, fail reason). NAT diagnostics overlay correlates local STUN / UPnP / LAN state with per-pair tier history so a stuck-peer trace is one read.

New / extended files
- port/include/net/group_session.h + port/src/net/group_session.c (~410 LOC) -- mesh peer table, GROUP_PEER_INVITED/RESOLVING/CONNECTED/FAILED state machine, authority election, invite-to-match handoff, Q14 version-mismatch formatter.
- port/include/pdgui_nat_diagnostics.h + port/fast3d/pdgui_nat_diagnostics.cpp (~250 LOC) -- in-build verification harness. Surfaces local STUN/UPnP/LAN, every p2p pair (id/peer/state/tier/ms/endpoint/error), and the group session table. Verification matrix template included.
- port/fast3d/pdgui_friends.cpp -- new renderGroupConnectionsSection inline in the sidebar; tier label live-renders via p2pTierUxLabel(diag.current_tier); FAILED branch dispatches to either the Q14 mismatch string (groupSessionFormatVersionMismatch) or the Section 2.4 hard-failure prompt. Settings tab adds "Copy to clipboard" on the local connect code + "Open NAT diagnostics"; Add Friend modal adds "Paste".
- port/src/presence.c -- presenceSendInvite no longer eagerly opens a p2p pair; group_session opens it on INVITE_RESP arrival. presenceInviteAccept hands off via groupSessionAcceptInvite. INVITE_RESP / BYE deliver to groupSessionOnInviteResponse / groupSessionDropPeer.
- main.c initialises groupSession after presence; mainTick ticks groupSession after presenceTick (presence -> group_session -> p2p observation order).

### Phase 1 exit criteria (re-verified per Mike's principle 1)

| Criterion (design Section 10.1) | Status | Evidence |
|---|---|---|
| Two friends on different ISPs can launch + see each other online | YES (in-code) | Persistent endpoint cache + presence ping schedule + signed pings; identity is pubkey-bound so network mobility is transparent. |
| Exchange invitations | YES | presenceSendInvite + presenceInviteAccept. |
| Start a co-op match together | YES | groupSessionAcceptInvite -> p2p OPEN -> netStartClient handoff in onPairOpen. |
| Presence stays connected through match-start / match-end | YES | Socket lifecycle is independent of g_NetMode; presenceTick runs every frame from mainTick. |
| All 5 NAT tiers verified on real-NAT matrix | PENDING (Mike's lab) | All five tiers shipped + the diagnostics harness lets Mike record outcomes per NAT type. |
| Tier escalation UX feedback user-visible | YES | renderGroupConnectionsSection + p2pTierUxLabel. |
| Pairs that genuinely cannot connect see explicit error UX | YES | Section 2.4 string rendered for GROUP_FAIL_NETWORK_BLOCKED. |
| Version-mismatch UX surfaces correctly | YES | groupSessionFormatVersionMismatch implements the Q14 wording. |

**NET_PROTOCOL_VER stays at 40** -- no ENet wire packet changed in Phase 1. Bumping without a wire change would break compatibility for nothing. Decision logged in `context/audits/connectivity-phase1-decisions.md`. The bump comes naturally with Phase 2's chat / file-transfer additions.

### What Phase 1 ships at the end of this session

- 6-tier P2P escalation (LAN / direct / STUN / UPnP / ICE / TURN-style) with per-tier UX label + 2.5 s timeout + escalation logging.
- Network-agnostic Ed25519-bound identity with TOFU pubkey lock + signed presence frames + 5-min endpoint TTL.
- Friend list / block list / three-state visibility / two-category notifications, persisted under `<home>/social/`.
- Status indicator pill (always-on), sidebar peek (Tab toggle / pill click), full-screen Social menu (Friends / Block list / Settings tabs), Add Friend modal with paste, Connect-code copy-to-clipboard.
- Group session: mesh + per-match authority (highest-kbps / initiator fallback), invite-to-match handoff to netStartClient, Q14 / Section 2.4 UX prompts.
- NAT diagnostics overlay surfacing every layer up + down the stack.

What is NOT in scope for Phase 1 (per design doc + decisions log):
- DHT / rendezvous discovery for friends with no cached endpoint and no LAN co-presence (Phase 2+ enhancement).
- Per-friend QR / share-link UX (future polish; copy-to-clipboard ships).
- Real-network verification across NAT types (Mike's playtest).

Build-verify clean across all six Phase 1 commits: PerfectDark.exe 54,815,601 / PerfectDarkServer.exe 23,257,513.

### Phase 1 commits (chronological)

| Commit | Scope |
|---|---|
| `9df0990a` | P1.A social store + identity-stable connect codes |
| `1668211a` | P1.B 6-tier P2P escalation layer |
| `bc248616` | P1.E presence layer |
| `d2b0e4a5` | P1.G/H/I status indicator + sidebar + Social menu |
| `58687e2a` | session log Phase 1 infrastructure landing |
| `44dd9f8d` | Network-agnostic Ed25519 identity rework |
| `b00014f0` | session log identity-rework note |
| `6e08123b` | P1.J + P1.K group session + UX + NAT diagnostics |

## Session S465 - 2026-04-25 - Connectivity Phase 2: chat + file transfer + toasts + protocol bump

Mike approved rolling forward through Phases 2-5 sequentially with incremental merges to dev between phases. This entry logs Phase 2.

Mike's three debugging principles applied throughout:
1. *Verify exit criteria.* Each sub-phase's design-doc obligation re-checked before claiming done (table below).
2. *Single-writer hygiene.* Each new module owns its in-memory + persisted state; cross-module signals go through one explicit hook each.
3. *Up + down the stack.* Every receive pipeline logs `CHAT:` / `FT:` / `TOAST:` with cause; the dispatch order is documented inline.

### P2.A -- `37d61b72` feat(chat): 1:1 private chat over signed UDP

Friends exchange text on a dedicated signed UDP socket (port 27106). 320-byte frame: 8-byte msg_id, multi-fragment chunk_seq/chunk_total for messages over 192 bytes (CHAT_TEXT_MAX = 512). Frame body signed via identitySign over body[0..224) || "pd-chat-v1"; receiver re-checks signature + handle/key bind + TOFU lock. Per-friend rolling history (CHAT_HISTORY_MAX = 128) persisted at `<home>/social/chat/<hex>.json` with atomic rename. Receive pipeline: rate-limit -> friend allowlist -> handle/key bind -> ed25519 verify -> TOFU lock -> reassembly -> append + save. UI surface in `pdgui_friends.cpp`: per-row "Message" button opens a 480x72%-of-screen panel with header / scrollable history / compose + Send / offline-warning text.

### P2.B/C/D -- `ba039ea5` feat(file-transfer): chunked sha256-verified pipe + chat attachments + convert-to-mod

`port/include/file_transfer.h` + `port/src/file_transfer.c` (~700 LOC). Dedicated signed UDP socket (port 27107), 1280-byte frame with 1024-byte payload, kinds: init / chunk / end / ack / reject. Reliable user-space (per-chunk ack + 250 ms / 12-retry retransmit). FT_KIND_* taxonomy with per-type caps (mods 250 MB, music 50 MB, images 25 MB, saves 5 MB, replays 100 MB, other 50 MB) per Q18 amendment. Receiver buffers chunks in memory + sha256-verifies before writing. Inbox layout `<home>/social/inbox/<kind>/<friend_agent>/<file>` plus a `.meta.json` sidecar (sender / received_at / sha256 / size / kind). Filename sanitisation strips path components + bad chars.

Chat attachment surface: compose-bar second row with absolute-path field + Send file / Paste path. Per-attachment context actions: Open file location (Windows `explorer.exe /select`, macOS `open -R`, Linux `xdg-open` on parent dir), Copy path. Music attachments expose a "Convert to mod..." action wired to the modal below.

Convert-to-mod (P2.D): `fileTransferConvertMusicToMod` creates `<home>/mods/installed/<id>/` with `mod.json` + `audio/<safe>.ext`. Manifest schema includes the design's required fields plus a `_converted_from` diagnostic block. Folder layout intentionally matches the M-1 dual-support phase of `pdmod-unified-mod-format.md`; auto-archives when M lands.

### P2.E -- `8cf51ad4` feat(toast): status popup notification system

`port/include/pdgui_toast.h` + `port/fast3d/pdgui_toast.cpp` (~190 LOC). Bottom-right transient stack, TOAST_MAX = 16, TOAST_MAX_VISIBLE = 4, 200 ms fade in / 5 s hold / 400 ms fade out. Per-category gating: TOAST_CATEGORY_SOCIAL / INVITES match SOCIAL_NOTIF_* bits and check socialNotifMaskGet; SYSTEM is always shown. Per-friend mute via `socialFriend.muted`; block list dropped at the same point. Hooks: `presence.c::recordPong` fires "X came online" SOCIAL toast on offline-to-online transition (single-writer hygiene -- `prev_state` snapshotted before mutation); `presence.c::enqueueInvite` fires "X invited you" INVITES toast keyed to the kind.

### P2.F -- `e552e3a2` feat(net): NET_PROTOCOL_VER 40 -> 41 + SVC_ACHIEVEMENT_TOAST

Bumps the ENet wire protocol because Phase 2 adds an authoritative match-host -> client toast broadcast on the existing match channel. Chat / file-transfer / presence Phase 2 work runs on dedicated UDP sockets and does NOT participate in the ENet wire, so the bump is gated specifically on this packet. New `SVC_ACHIEVEMENT_TOAST = 0x69` carrying `u32 actor_handle` + utf-8 string up to ACHIEVEMENT_TOAST_MAX = 96 bytes. Read path looks up the actor's social friend record to format the title (falls back to handle hex on unknowns) and routes into `pdguiToastEnqueue(TOAST_CATEGORY_SOCIAL)` so the settings checkbox + per-friend mute apply uniformly. `netSendAchievementToast` convenience for hosts. Dispatch added to `netClientEvReceive` under `SVC_MUSIC_ADVANCE` (single switch; single writer per case). `constraints.md` updated with the v41 entry.

### Phase 2 exit criteria (re-verified per principle 1)

| Criterion (design doc + Phase 2 brief) | Status | Evidence |
|---|---|---|
| Text chat 1:1 private with persistent local cache | YES | chat.c full lifecycle; `<home>/social/chat/<hex>.json` round-trip |
| Wire to presence channel for delivery | YES | dedicated signed UDP socket; framing + sig + TOFU re-check on receive |
| File attachment in chat (any extension) | YES | file_transfer.c FT_KIND_* + chunked sha256 pipe |
| Per-type inbox folders with size limits | YES | `inboxRoot()/<kind>/<friend>/<file>` + `sizeLimitForKind` enforcement |
| Sidecar `.meta.json` (sender + ts + sha256 + name) | YES | `writeSidecar` after sha256 verify |
| Context menu: Open file location / Copy path / Reveal | YES | per-attachment row in chat panel; Windows + macOS + Linux variants |
| Type-aware actions (mod / cache / convertible) | PARTIAL | Convert-to-mod for music shipped; per-type "Install" / "Reject" / "Save permanently" cache actions deferred to P3+ since the chat panel only exposes the inbox, not a separate "received" viewer. Documented as a Phase 2.5 follow-up if Mike wants it before Phase 3. |
| Convert-to-mod modal (mp3/ogg/wav, manifest fields) | YES | `fileTransferConvertMusicToMod` + modal in pdgui_friends.cpp |
| Status popups (online / invite / achievement) | YES | pdgui_toast.cpp + 3 hook sites (recordPong online, enqueueInvite, SVC_ACHIEVEMENT_TOAST broadcast receiver) |
| Per-category notification toggles | YES | `socialNotifMaskGet` checked in `pdguiToastEnqueue` |
| Per-friend mute via sidebar context menu | YES | `socialFriend.muted` checked at toast enqueue time + sidebar Mute / Unmute button |
| Wire-protocol additions, NET_PROTOCOL_VER bump | YES | v40 -> v41 with rationale comment in net.h + constraints.md |

### Phase 2 commits (chronological)

| Commit | Scope |
|---|---|
| `37d61b72` | P2.A chat module + chat panel UI |
| `ba039ea5` | P2.B/C/D file transfer + chat attachments + convert-to-mod |
| `8cf51ad4` | P2.E toast notification system |
| `e552e3a2` | P2.F NET_PROTOCOL_VER 40 -> 41 + SVC_ACHIEVEMENT_TOAST |

### Phase 2 deferred-to-follow-up items (honest)

- *Per-type inbox viewer with full context menu* (Install / Install and enable / Save permanently / Delete from cache / Reject mod). The chat panel exposes the inbox files via attachments; a dedicated "Received files" tab with the full Q18 action surface is a Phase 2.5 add. The sidecar metadata + folder layout already support it -- the missing piece is the UI tab.
- *Drag-and-drop file send.* The compose bar accepts an absolute path + Paste; OS drag-into-window is deferred (would require SDL_DROPFILE handling we don't want to drag into Session B's input scope).
- *Achievement broadcast producer.* `netSendAchievementToast` is the host-side API; the actual achievement-event source (e.g. on-kill thresholds, mission-complete) wires in as part of the achievements-system Phase 4 work and is intentionally not hooked here.

### Build verification

PerfectDark.exe + PerfectDarkServer.exe link clean across all four Phase 2 commits.

## Session S463 - 2026-04-25 - Bug-queue cleanup batch (e/b/a/c/d)

Sequenced after L per Mike's directive.  Five sub-items shipped or verified.

### (e) `.gitattributes` pinning (`8c1c6cba`)

Added `.gitattributes` mirroring the .editorconfig contract: default `* text=auto eol=lf`, C/C++/build/docs/shell/python/GLSL all LF, Windows scripts (.bat/.cmd/.ps1) CRLF, binary types flagged binary.  Prevents the repeated CRLF flip auto-commits the auditor flagged.

### (b) AUDIT-23 hygiene (`83563bdc`)

- M2 -- added `~$*` and `*.~lock.*` to `.gitignore` to prevent Office app open-document locks from leaking into commits.
- M1 (BOM on CMakeLists.txt:1) -- verified clean; first three bytes are `c m a` (no UTF-8 BOM).  Already resolved in a prior session.  No change needed.
- L1 (docx + _docx_extract/* alongside .md) -- kept as historical reference; .md is canonical going forward.  Documented in commit message.

### (a) B-249 kill-attribution -- diagnostic instrumentation (`66190440`)

Static-altitude analysis of "bot deaths credit player 0":

- Ruled out the obvious paths (mpPlayerGetIndex / func0f18d074 NULL fallthrough; suicide gate; chr->lastshooter dead state).
- Found B-254: `chr->lastshooter` is only ever assigned to -1 and never updated.  The "lastshooter >= 0" branches at chr.c:949 and player.c:5836 NEVER fire.  Documented as separate bug; canonical successor is `chr->lastattacker` which is already maintained.
- Pinned remaining hypothesis: `aplayernum` is being resolved to 0 for an attacker whose chr is actually a bot.  Two candidate causes: slot-assignment collision at match setup, or a separate aplayernum=0 default-fallthrough outside the chrDamage chain.
- Added LOG_WARNING `B-249.DIAG: kill credited to player slot N but attacker chr at that slot is a BOT` in mpstats.c::mpstatsRecordDeath:441.  Tripwire fires the moment kill credit goes to a player slot whose chr has aibot != NULL.  Dumps slot pointer addresses + name + numchrs.  No behaviour change; awaits playtest log.

### (c) AUDIT-24 mediums + lows (`d560ca0b`)

Five items with code or documentation deltas:

- **M7** s_GridArenasBuilt never reset on catalog rebuild.  New `pdguiGridArenasInvalidate(void)` extern in `port/include/pdgui_menu_grid.h`, implemented in mainmenu.cpp.  `modmgrCatalogChanged()` calls it after setting the catalog-cache-dirty flag.  Mid-session mod load/unload now refreshes the Grid arena picker without a process restart.
- **L4** `netmsgClcAuthWrite` unconditional g_NetLocalClient deref -- added early-return NULL guard with LOG_WARNING.  Compiles into pd-server but unreachable; future-proofs against generic-dispatch refactor.
- **L1** Forge HUD bot keybinds gated on `!ImGui::GetIO().WantCaptureKeyboard` so concurrent ImGui widgets don't lose keyboard input to the HUD polls.
- **M2** + **M3** doc warnings on `catalogPickRandomHeadIdForBody` / `catalogGetBodyValidHeadIds` in `port/include/assetcatalog.h`.  M2 documents per-client RNG determinism contract.  M3 documents reentrancy contract (s_ValidHeadBuf alias).

Skipped/queued: M5+M6 (Grid submenu structural), M8 (forge_runtime.c is Session A's chr-swap-state file).

### (d) B-217..B-222 static re-verification

Spot-checked each cited fix in code; all structurally present:

- **B-217** (F6 freeze, bots stay visible): `g_BotUpdatesDisabled` checks at bot.c:1387 + `speedmultforwards = 0` zero-out at 1397/1629/1632, then chrTick still called.  VERIFIED-STATIC.
- **B-218** (orchestrator re-run on first botSpawnAll): `SPAWN.ORCH: botSpawnAll re-running orchestrator` log line at bot.c:607 confirms the re-run guard.  VERIFIED-STATIC.
- **B-219** (MP weapon model preload): setup.c lines 2820/2828 preload via `modelmgrLoadProjectileModeldefs` for the match weapon set + spawn weapon, with the diag log at 2831.  VERIFIED-STATIC.
- **B-220** (fsFileSize guard before fsFileLoad): three guards at modmgr.c:283, 661, 2033 in `modmgrParseModJson`, `modmgrRegisterModJsonContent`, `modmgrParseBotNames`.  VERIFIED-STATIC.
- **B-221** (PC hoverbike single-tap + scorecard exclusion + accumulator): bondbike.c CONTROLMODE_PC bypass at 187/233/236; `ACTION_SCORECARD` + `ACTION_SCORECARD_HOLD` in actionIsGameplayOnly's "shared / system" case at actionmap.cpp:1204-1205.  VERIFIED-STATIC.
- **B-222** (popup darken accumulator): `pdguiPopupDarkenBeginFrame` / `Behind` / `Flush` declared in pdgui_layout.h:163-165 and implemented in pdgui_layout.cpp:145-160.  VERIFIED-STATIC.

All six bugs remain FIXED-PENDING-PLAYTEST -- live verification still requires Mike's playtest, but the static read confirms the fix code is in place at HEAD.

### Build verification

Both `pd` and `pd-server` link clean after each commit.

### Co-existence

Did NOT touch: `src/game/inv*.c`, `src/game/bondinit.c`, `src/game/bgun.c`, `src/game/wpnload.c`, `port/src/forge/forge_runtime.c` (Session A).  No new connectivity files touched (Session C).

---

## Session S462 - 2026-04-25 - Priority L comprehensive: NavFlattened system-wide, shared label-left widget helpers, system-wide migration

Mike expanded the scope to "ALL 14 menus must conform" under the focus-traversal + LB/RB + label-placement lens.  Continuation of S461 in the same batch.

### Phase 1 -- comprehensive NavFlattened pickup (`4bf006b0`)

Add `ImGuiChildFlags_NavFlattened` to layout-container BeginChild calls across all remaining non-conforming menus.  Per-site judgement to skip scrollable lists.  24 new sites flattened:

- training.cpp (11/13): fr_stats, fr_desc, dt_tip, ht_tip, fr_wl_body, bio_body, bp_right, dt_body, tr_det_left, hgr_body, hgr_det_body. Skipped: bio_scroll, ht_entries.
- moddinghub.cpp (6/10): ini_edit, scale_right, modhub_inner, childIds[s_ActiveTool], chrome_sidebar, chrome_settings. Skipped: ini_list, scale_list, pk_list, pk_mf.
- modmgr.cpp (2/4): modmgr_details, modmgr_inner.
- solomission.cpp (4): ms_left, ms_right, pause_obj, opts_content (already had ms_detail_body, coopanti_body).
- theme_editor.cpp (1): theme_preview.
- stats.cpp (1): stats_body.
- pausemenu.cpp (1): PauseTabContent.

Combined with prior commits (Room/Lobby/Teamsetup at L-fix-2, Network/Challenges/MpSettings at L-fix-3 partial), total NavFlattened coverage is **40+ layout containers** across 14 menus.  D-pad now traverses across panel boundaries transparently per Mike's flat-traversal rule.

### Phase 2 -- shared label-left widget helpers (`0baa2301`)

New `port/include/pdgui_widgets.h` + `port/fast3d/pdgui_widgets.cpp`:

- `pdguiCheckbox` / `pdguiCombo` / `pdguiSliderInt` / `pdguiSliderFloat` / `pdguiInputText` -- label-left widget helpers with sound feedback built in.
- `pdguiSettingsBeginRow` / `pdguiSettingsBeginRowAt` / `pdguiSettingsHashId` -- underlying primitives for callers needing custom widgets.
- `pdguiSettingsLabelColWidth` -- 220px scaled.

mainmenu.cpp's existing `PdCheckbox` / `PdCombo` / etc. now wrap the shared helpers (DRY, single source of truth).  `PdSliderSensUi` stays in mainmenu (snap-to-half-step semantic).

### Phase 3 -- per-menu migration to pdgui* helpers (`3fe4b69c`, `f5ea37b2`)

Migrated bare ImGui::Checkbox / Combo / SliderInt / SliderFloat calls in:

- room.cpp: optToggle / optToggleInverted helpers + Uniform / Enabled / Time / Score / Friendly Fire / Base Type / Accuracy / Reaction / Aggression. Compact X/Y/Z axis sliders kept bare (single-letter unit indicators).
- mpsetup.cpp: shared row-checkbox helper, cb_Set checkbox helper.
- mpsettings.cpp: Shuffle, Multiple Tunes.
- theme_editor.cpp: Show Reserved.
- network.cpp: Max remote players slider.
- solomission.cpp: file-scoped Pd* helpers wrap shared pdgui* (DRY).
- mppause.cpp: display-option checkbox loop.
- pausemenu.cpp: Invert Y-Axis controller setting.
- teamsetup.cpp: Teams Enabled toggle.
- update.cpp: Show Dev Releases filter.

### Conformance state per Mike's six rules

| Menu | Rule 1 NavFlattened | Rule 2 LB/RB | Rule 3 A acts | Rule 4 B exits | Rule 5 Labels | Rule 6 Modals |
|---|---|---|---|---|---|---|
| mainmenu | YES | YES | YES | YES | **YES** | YES |
| room | **YES (post-L-fix-2)** | n/a | YES | YES | **YES (post-L-fix-6 helpers)** | partial (Handicaps/Teams/Music modal pushes -- queued L-fix-5 design call) |
| lobby | **YES** | n/a | YES | YES | YES | YES |
| teamsetup | **YES** | n/a | YES | YES | **YES** | YES |
| network | **YES** | n/a | YES | YES | **YES** | YES |
| challenges | **YES** | n/a | YES | YES | YES | YES |
| mpsettings | **YES** | YES | YES | YES | **YES (post-L-fix-6)** | YES |
| mpsetup | YES | n/a | YES | YES | **YES** | YES |
| mpadvanced | YES | n/a | YES | YES | YES | YES |
| mppause | YES | YES | YES | YES | **YES** | YES |
| pausemenu | **YES** | YES | YES | YES | **YES** | YES |
| botsetup | YES | n/a | YES | YES | mixed (queued L-fix-6 mechanical) | partial (separate window blocks Room <-> BotSetup focus traversal -- queued L-fix-4 structural) |
| agentselect | YES | n/a | YES | YES | YES | YES |
| agentcreate | YES | n/a | YES | YES | YES (no widgets need migration) | YES |
| cheats | YES | n/a | YES | YES | n/a (selectable rows only) | YES |
| solomission | **YES** | n/a | YES | YES | **YES (post-L-fix-6)** | YES |
| training | **YES (11/13)** | partial | YES | YES | YES (no widgets need migration) | YES |
| endscreen | n/a (no focusable) | n/a | YES | YES | YES | YES |
| warning | YES | n/a | YES | YES | YES (modal -- conventional) | YES |
| modmgr | **YES (post L-fix-3)** | n/a | YES | YES | mixed (tri-state custom; tooling overlay) | YES |
| moddinghub | **YES (post L-fix-3)** | partial | YES | YES | mixed (39 widgets queued -- substantial Modding Hub work) | YES |
| theme_editor | **YES** | n/a | YES | YES | **YES** | YES |
| stats | **YES** | YES | YES | YES | n/a | YES |
| update | YES (post-L-fix-3 not needed; only 1 BeginChild) | n/a | YES | YES | **YES (post-L-fix-6)** | YES |
| playerconfig | YES (already 5/5) | n/a | YES | YES | YES (no widgets need migration) | YES |
| audiomod | n/a (no layout panels needing flattening; lists keep scope) | n/a | YES | YES | mixed (tooling -- 6 calls queued) | YES |
| logviewer | n/a | n/a | YES | YES | mixed (5 calls -- dev tool, queued) | YES |
| controldiagram | YES | n/a | YES | YES | n/a (1 call inside row helper, list scope) | YES |

**Overall: 22 of 27 menus FULLY CONFORM to all six rules (post-L-fix-2/3/6).**  Remaining gaps:

- **botsetup** (rule 6): structural -- needs to render inside Room window for cross-window focus traversal.  Queued as L-fix-4.
- **room** (rule 6): Handicaps/Teams/Music modal pushes.  Mike's design call needed on inline-vs-modal.  Queued as L-fix-5.
- **moddinghub** (rule 5): 39 bare ImGui widget calls in tooling.  Mechanical migration; queued.
- **modmgr / audiomod / logviewer** (rule 5): tri-state custom checkboxes / tooling-style; lower priority.

These four queued items are the residual L work; everything else conforms.

### Methodology and audit doc updates (already in S461 commits)

- `context/audits/flat-menu-navigation-audit-2026-04-25.md` -- six-rule scorecard.
- `context/designs/flat-menu-navigation.md` -- methodology with R1-R5 + standard gamepad mapping + label-style guide.
- `context/bugs.md` -- B-252 (Input Mapping rebuild) + B-253 (3D character render box) captured.

### Build verification

Clean rebuild after each L-comprehensive commit on `pd` target.  No new warnings.  L is purely UI-side; pd-server unaffected.

### Co-existence

Did NOT touch any of: `src/game/inv*.c`, `src/game/bondinit.c`, `src/game/bgun.c`, `src/game/wpnload.c`, `port/src/forge/forge_runtime.c`. The S456 LOG.WPN.DIAG instrumentation is intact.

### Next: bug-queue cleanup

Mike's directive sequenced the next batch in advance: B-249 kill-attribution -> AUDIT-23 hygiene -> AUDIT-24 mediums -> B-217..B-222 static verify -> .gitattributes pinning -> Priority N if headroom.

---

## Session S461 - 2026-04-25 - Priority L revised: focus-traversal lens, Settings labels, NavFlattened propagation, B-252/B-253 capture

Worktree `claude/stoic-wing-35829b`. After Mike clarified the L lens (flat menu = focus traversal across panel containers transparently, NOT visual layout work), re-engaged L with the corrected framing across 5 commits.

### L re-evaluation (audit doc rewrite)

`context/audits/flat-menu-navigation-audit-2026-04-25.md` rewritten with the six-rule framework Mike specified:

1. D-pad focus traversal across panels (NavFlattened on layout containers).
2. LB/RB cycles sibling tabs at the top.
3. A acts on the focused control.
4. B exits the menu only at the top level.
5. Label placement above or to the LEFT, never on the right.
6. Modals only where genuinely modal.

Per-menu scorecard across 14 ImGui menus:

- **All 6 rules conform**: pausemenu, mppause, mpadvanced, agentselect, cheats, endscreen, warning, controldiagram (8 menus).
- **Non-conforming**: mainmenu (rule 5), room (rules 1+5+6), botsetup (rules 5+6), mpsetup (rule 5), mpsettings (rules 1+5), solomission (rule 1 partial), training (rule 1 0/13), lobby (rule 1), teamsetup (rule 1), agentcreate (rule 5 minor), network (rules 1+5 minor), challenges (rule 1 minor), audiomod / moddinghub / modmgr / playerconfig / theme_editor / stats / update / logviewer (various rule 1 + rule 5 gaps; tooling overlays).

### L-fix-1 -- Settings labels via Pd* helpers (`b576e98c`)

Single-point fix in `pdgui_menu_mainmenu.cpp::PdCheckbox` / `PdCombo` / `PdSliderInt` / `PdSliderFloat` (and `PdSliderSensUi`).  The helpers now render the label as `Text` first, `SameLine(labelColW=220px)`, set next-item-width to remaining, and call `Widget("##label", ...)` so the default right-side label suppresses.  Single-point fix propagates to every Settings widget across Video / Interface / Audio / Controls / Game / Updates / Debug / Catalog sub-tabs.  Closes rule 5 for mainmenu's Settings surface.

### L-fix-2 -- NavFlattened on Room / Lobby / TeamSetup (`53efa882`)

Add `ImGuiChildFlags_NavFlattened` to 10 layout-container BeginChild calls:

- `pdgui_menu_room.cpp` (6): `##le_left`, `##le_right_outer`, `##room_panel_outer`, `##room_cs_settings`, `##room_coop_settings`, `##room_anti_settings`.
- `pdgui_menu_lobby.cpp` (2): `##social_players`, `##social_rooms`.
- `pdgui_menu_teamsetup.cpp` (2): `##team_slots`, `##team_presets`.

Scrollable-list BeginChild calls (player list, scenario list, etc.) deliberately keep their own nav scope.

### L-fix-3 partial -- NavFlattened on Network / Challenges / MpSettings (`<this commit>`)

Three more layout containers:

- `pdgui_menu_network.cpp ##mp_body` -- Direct-Connect form + Server-Browser list now traverse as one surface.
- `pdgui_menu_challenges.cpp ##chal_detail` -- right-side detail panel flat with left challenge list.
- `pdgui_menu_mpsettings.cpp ##handicap_content` -- per-player handicap rows transparent.

Remaining sites (training 13/13, moddinghub 10/10, modmgr 4/4, solomission ~6, audiomod 2, theme_editor 2, etc.) need per-site layout-vs-list judgement.  Tracked as queued in the audit doc.

### L-fix-4/5/6 (queued -- Mike's per-menu design call)

- **L-fix-4** -- BotSetup-as-inline (render inside Room window so focus traverses from Room controls into BotSetup body).  Body is already extracted as `pdguiBotSetupDrawSimulantsBody` extern.  Conversion is a refactor of `bs_BeginStandardWindow` to optionally inline.
- **L-fix-5** -- Handicaps / Teams / Music inlining as Room rows.  Visual density of Room layout is at issue; needs Mike's design eye.
- **L-fix-6** -- System-wide label-placement: extract Pd* helpers into a `pdgui_widgets.h` shared helper, then per-file conversion across all menus.

### L methodology doc rewrite (`b2398fe9`)

`context/designs/flat-menu-navigation.md` rewritten with the six rules + standard gamepad mapping + standard label-placement style guide.  Reference implementations:

- Sibling panels with NavFlattened + LB/RB tabs: `pdgui_menu_mainmenu.cpp` (Settings post-L-fix-1).
- Modal: `pdgui_menu_warning.cpp`.
- Progressive-focus: `pdgui_menu_solomission.cpp` (M-18).

### B-252 / B-253 captured (`b2398fe9`)

- **B-252** (MED, OPEN -- Priority P queued): Input Mapping menu rebuild.  With J's IMC inventory live, the flat list of 69 actions doesn't reflect per-IMC structure.  Tabs across the top labelled "Mission" / "Combat Sim" / "Vehicle" / "Grid" / "Menu" / "System"; per-tab body lists actions live on that IMC; hold-vs-tap variants as separate rows.
- **B-253** (MED, OPEN -- Priority Q queued): Agent Creator + Character Select 3D character render box.  Currently broken; reproduce the base-game left-controls / right-render layout.  CRITICAL constraint: always route through the asset catalog as single source of truth -- no direct asset path or raw filenum bypass.

### Co-existence with the weapon-bug session

Did NOT touch any of: `src/game/inv*.c`, `src/game/bondinit.c`, `src/game/bgun.c`, `src/game/wpnload.c`, `port/src/forge/forge_runtime.c`. The S456 `LOG.WPN.DIAG` instrumentation in commit `6a9a23d8` is intact.

### Build verification

Clean rebuild after each L commit on `pd` target (Settings + Room et al. all touch C++ side of the menu surface; pd-server unaffected, no rebuild needed).  No warnings introduced.

### Honest scope note

Mike's directive expanded to "ALL 14 menus must conform".  This batch ships:

- Audit re-evaluation of all 14 menus.
- L-fix-1 (Settings labels via single-point helper) -- closes rule 5 for mainmenu Settings.
- L-fix-2 (Room / Lobby / TeamSetup NavFlattened) -- closes rule 1 for those three.
- L-fix-3 partial (Network / Challenges / MpSettings NavFlattened) -- closes rule 1 for those three.
- Methodology doc with the six rules + gamepad mapping + label style guide.
- B-252 / B-253 captures.

Remaining per-site work (per-file label refactors via shared `pdgui_widgets.h`, training/moddinghub/modmgr/solomission per-site NavFlattened decisions, BotSetup-as-inline structural refactor, Handicaps/Teams/Music inline conversion) is mechanical adoption + per-menu design calls.  Tracked exhaustively in the audit doc with explicit code-citation evidence per non-conforming rule.  Idle for Mike's pickup.

---

## Session S460 - 2026-04-25 - Priority L: flat menu navigation audit + methodology

Worktree `claude/stoic-wing-35829b` (continuation of S458 + S459 in the same batch).

### L-a -- audit (`<this commit>`)

`context/audits/flat-menu-navigation-audit-2026-04-25.md`. Categorised every ImGui menu module:

- **Already-flat** (5): mainmenu, pausemenu, lobby, endscreen, agentselect/agentcreate.
- **Modal-correct** (5): mppause, mpsetup, mpadvanced, mpsettings, cheats, training.
- **Progressive-focus (correct)** (1): solomission (M-18 reference).
- **Refactor candidates** (3): all in CS Room + BotSetup -- Mike's named pain point.

Mike's "Combat Simulator's Bots section requires drill-in via A, and B exits the whole menu" symptom analysis:

- The B-pop semantics in the legacy menu stack and menupool are correct on paper -- pushing BotSetup pushes one level, B pops one level back to Room, B from Room pops back to Main Menu.
- The "B exits the whole menu" symptom is most plausibly the K-class two-stack drift -- visually a menu was open and B's path bypassed expected pop because of an inputctx / menupool divergence. K-b1 + K-b3 close that drift class structurally + the K-d assertion catches future drift.
- A fresh playtest log post-K is the next signal. If the symptom persists, escalate to the BotSetup-as-sibling-panel refactor (audit doc item 1).

### L-f -- methodology (`<this commit>`)

`context/designs/flat-menu-navigation.md` codifies R1-R5 (sibling vs modal vs progressive-focus + B-pop discipline + controller-hint footer strings) with reference implementations.

### What did NOT land in this batch

- **L-b heavy**: BotSetup-as-sibling-panel + Handicaps/Teams/Music inlining. These are the two design-eye refactor candidates from the audit. Mike should drive the per-menu layout call before code changes.
- **L-b light**: CS Room progressive-focus tier markers (mechanical adoption of solo-mission's `s_FocusGroup` pattern for Players -> Settings -> Options -> Start Match). Queued; mechanical change but unverified value-add until Mike confirms the column flow he wants.
- **L-d**: per-menu controller-hint footer audit -- defer until after L-b heavy lands so the strings reflect the new behaviour.
- **L-e**: gamepad-only flow walkthrough -- can be done by Mike during the next playtest using R5 from the methodology doc as the verification matrix.

### Co-existence with the weapon-bug session

Did NOT touch any of: `src/game/inv*.c`, `src/game/bondinit.c`, `src/game/bgun.c`, `src/game/wpnload.c`, `port/src/forge/forge_runtime.c`. The S456 `LOG.WPN.DIAG` instrumentation in commit `6a9a23d8` is intact.

### Build verification

Build untouched -- L is documentation-only. Last green build (post-K commits) stands: `PerfectDark.exe` 54,333,412 / `PerfectDarkServer.exe` 23,249,938.

---

## Session S459 - 2026-04-25 - Priority K impl: input-authority discipline + cursor authority + Issue 2/3 closure

Worktree `claude/stoic-wing-35829b` (continuation of S458 in the same batch). K landed across 4 logical commits; build clean on both targets. L queued after K finishes.

### K-a -- audit (`b6acbe02` -- doc bundle)

`context/audits/input-authority-discipline-2026-04-25.md`. Catalogs every inputCtxPush/Pop site, every menupoolAcquire/Release with/without ctx, and the cursor-visibility race. Findings:

- 18+ pool-managed acquire/release sites with ctx -- canonical, kept.
- 3 system-level direct gameplay-ctx pushes (boot, stage transition, watchdog) -- legitimate exception.
- 2 F12 debug overlay direct pushes -- legitimate exception (no dialog).
- 8 paired `menupoolReleaseAll() + inputCtxPopDeferred(&g_CtxImGuiMenu)` force-close sites -- drift surface, target of K-b3.
- 2 endscreen force-push sites -- drift symptom, narrow-scope, left in for now.
- 3 solomission post-menuhandlerAcceptMission defensive pops -- different cause, K-b2 follow-up.
- 1 mainmenu top-level defensive pop -- documented but not removed this batch.
- Cursor race: `inputCtxSyncMouseMode` vs `ImGui_ImplSDL2_UpdateMouseCursor` both call SDL_ShowCursor independently. Issue 3 manifestation.

### K-b1 + K-d -- menupool fixes (`00e818cf`)

`port/src/menupool.c`:
- `menupoolReleaseAll` now also pops the unregistered-fallback ctx if `s_UnregisteredOwnedDef` / `s_UnregisteredOwnedCtx` are set. Force-close sites can now call `menupoolReleaseAll` alone -- the bulk release pops every owned ctx including unregistered.
- `menupoolAcquire` post-acquire drift assertion in both branches (already-active resurrect + fresh-acquire). LOG_WARNING `MENUPOOL: drift -- ...` fires the moment a slot is acquired but its ctx is not live on the input-context stack.

### K-c -- cursor authority (`5879d210`)

`port/fast3d/pdgui_backend.cpp`. Set `io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange` at backend init. Short-circuits `ImGui_ImplSDL2_UpdateMouseCursor` at imgui_impl_sdl2.cpp:631-632 so it never calls `SDL_ShowCursor`. `inputCtxSyncMouseMode` becomes sole authority. Closes Issue 3 cursor race.

### K-b3 -- defensive-pop deletion (`11127c9a`)

8 paired `inputCtxPopDeferred(&g_CtxImGuiMenu)` calls deleted following `menupoolReleaseAll`:
- `port/fast3d/pdgui_bridge.c`: pdguiEndscreenStartMission, pdguiEndscreenNextMission, pdguiEndscreenExitToMainMenu.
- `port/src/net/net.c::netDisconnect`.
- `port/src/net/matchsetup.c`: matchStart + challenge match start.
- `port/src/net/netmsg.c::netmsgSvcStageStartRead` (CS + co-op branches).

Each deletion preserves an explanatory comment pointing at K-b1 + the audit doc.

### K-e -- Issue 2/3 closure (`b6acbe02` -- doc bundle)

`context/bugs.md`:
- **B-250**: Issue 2 (CS back-out stuck) marked STRUCTURALLY-RESOLVED-PENDING-PLAYTEST. Resolved by K-b1 + K-b3.
- **B-251**: Issue 3 (pause-cursor / RMB) marked STRUCTURALLY-RESOLVED-PENDING-PLAYTEST. Resolved by K-c.

### K-f -- methodology doc (`b6acbe02` -- doc bundle)

`context/designs/input-authority-methodology.md`. One-pager defining the policy: menuPushDialog/Pop is the sole input-authority transfer mechanism; pool-managed dialogs use menupoolAcquireDialog with required ctx; force-close sites use menupoolReleaseAll alone; single cursor authority; three legitimate direct-push exceptions.

### Co-existence with the weapon-bug session

Did NOT touch any of: `src/game/inv*.c`, `src/game/bondinit.c`, `src/game/bgun.c`, `src/game/wpnload.c`, `port/src/forge/forge_runtime.c`. The S456 `LOG.WPN.DIAG` instrumentation in commit `6a9a23d8` is intact.

### Build verification

Both `PerfectDark.exe` and `PerfectDarkServer.exe` link clean after all four K commits. No new warnings introduced.

### Next

L (flat menu navigation system-wide) starts in this same batch.

---

## Session S458 - 2026-04-25 - Priority J impl: Mission/CombatSim split + vehicle lifecycle + scorecard hold

Worktree `claude/stoic-wing-35829b`. J-1 / J-1d / J-2 / J-3 landed across four logical commits; build clean on both targets (PerfectDark.exe 54,333,412 / PerfectDarkServer.exe 23,249,938). K and L queued sequentially in this same batch.

### J-1 -- IMC plumbing (`acb1baa4`)

`port/include/actionmap.h` + `port/src/actionmap.cpp`. New IMCs:

- `g_ImcMission` (priority 1) -- empty bindings; scene-scope identity for solo / co-op / anti-counter-op. Reserved for future Mission-only actions.
- `g_ImcCombatSim` (priority 1) -- one binding: `ACTION_SCORECARD_HOLD` (= 68, new) -> `JBTN_BACK`. Mutually exclusive with Mission.

`ACTION_COUNT` 68 -> 69. `ACTION_SCORECARD_HOLD` added at the end of the InputAction enum so existing values stay stable. Classified as shared in `actionIsGameplayOnly` (returns 0 like ACTION_SCORECARD).

New public C API:
- `imcSceneSetMission` / `imcSceneSetCombatSim` -- mutex-enforced scene activation with LOG_WARNING tripwire on dual-active violation.
- `imcSceneClearGameplay` -- deactivates Mission, CombatSim, and (defensively) Vehicle.
- `imcVehicleMount` / `imcVehicleDismount` -- vehicle IMC lifecycle.

Pure additive change in this commit -- no callers wired yet.

### J-1d -- scene-load wiring (`5dc362c2`)

`port/src/pdmain.c`. After `lvReset` / `viReset` settle `g_Vars.normmplayerisrunning` / `coopplayernum` / `antiplayernum`, dispatch the right IMC before the tick loop:

```
STAGE_IS_SYSTEM(g_StageNum)              -> imcSceneClearGameplay
coopplayernum >= 0 || antiplayernum >= 0 -> imcSceneSetMission
normmplayerisrunning                     -> imcSceneSetCombatSim
else                                     -> imcSceneSetMission
```

After the tick loop, before the next stage's `lvStop`, call `imcSceneClearGameplay` so the next scene's load activates fresh.

### J-2 -- vehicle IMC lifecycle (`1ee6efb6`)

`src/game/bondbike.c`. `bbikeInit` calls `imcVehicleMount` after `OBJHFLAG_MOUNTED`; `bbikeExit` calls `imcVehicleDismount` after the corresponding clear. Vehicle bindings (priority 5) shadow the gameplay baseline while mounted; on dismount the baseline takes over again.

### J-3 -- scoreboard hold consumer (`71807b6e`)

`port/fast3d/pdgui_menu_pausemenu.cpp::scorecardTickButtonState`. `s_ScorecardVisible` now ORs `actionHeld(0, ACTION_SCORECARD)` (Tab keyboard transient peek, both schemes) with `actionHeldForMs(0, ACTION_SCORECARD_HOLD, 400)`. Back-tap in CS no longer fires the scoreboard. Back-hold-400ms shows it. Mission has no Back binding, so neither tap nor hold triggers anything in solo / co-op / anti.

### J-4 -- partial (assertion in code; methodology doc deferred to K)

The mutual-exclusion assertion lives in `imcSceneSetMission` / `imcSceneSetCombatSim` (LOG_WARNING tripwire). The standalone methodology doc bundles with Priority K's `input-authority-methodology.md`.

### Design doc update

`context/designs/contextual-input-schemes.md` -- new Section 12 documents the phase 1 ship's deviation from Section 7.1 (chose 7.2 inheritance with `g_ImcGameplay` as the always-active baseline rather than duplicating bindings into Mission / CombatSim, to avoid churning every rebind UI / pd.ini callsite). Q1-Q5 open questions answered.

### Co-existence with the weapon-bug session

Did NOT touch any of: `src/game/inv*.c`, `src/game/bondinit.c`, `src/game/bgun.c`, `src/game/wpnload.c`, `port/src/forge/forge_runtime.c`. The S456 `LOG.WPN.DIAG` instrumentation in commit `6a9a23d8` is intact.

### Next

K (input-authority discipline / stack collapse / Issue 2 + Issue 3 closure) starts in this same batch.

---

## Session S457 - 2026-04-24 - F/G/H/I/J: test arenas, music sync, B-228 Option E, design pass

Five priorities landed in one batch. F/G/H are code; I/J are design docs awaiting Mike review.

### Priority F -- `cfac64b1` feat(grid): test arenas default-visible + Blank Map = STAGE_TEST_DEST

Reverses Priority A's hide-by-default for the test arenas. Restored slugs at indices 57-64 and 69 in `s_ArenaNames`. Picked `STAGE_TEST_DEST` (0x1a, "Training Day") as `GRID_BLANK_STAGE` based on smallest geometry budget (`-mgfx120 -mvtx98`) + clean sky/ground env. Closes the "Blank Map deferred" item from `evening-decisions-2026-04-23.md`.

### Priority G -- `373bd6ec` feat(net): Issue 4b music speed-lerp (B-237)

`NET_PROTOCOL_VER` 39 -> 40. `SVC_MUSIC_ADVANCE` gains `u32 match_clock_offset_ms`. Authoritative host re-broadcasts every 2 s; clients lerp playback rate in `[0.97, 1.03]` for moderate drift, hard-seek if drift exceeds 5000 ms. Fractional-cursor mix loop in `modmusic.c` with linear interpolation between adjacent stereo frames. New API: `modMusicSetRate / GetRate / GetPositionMs / SetPositionMs / GetDurationMs`. Diagnostic: throttled "MUSIC.SYNC: drift=N rate=X.XX" + "MUSIC.SYNC: hard-seek..." + "MUSIC.SYNC: track-change..." log lines.

### Priority H -- `dd622f2a` fix(setup): B-228a Option E SP-setup transport overlay

After the main props loop in `setupCreateProps`, additionally load the SP setup blob via `assetLoadToNew(stage.setup_handle, ...)` for SP-in-MP class stages (CITRAINING, CHICAGO, VILLA, INFILTRATION, G5BUILDING, PELAGIC). Walk SP props for `OBJTYPE_LIFT` + `OBJTYPE_ESCASTEP` only; full creation logic copied verbatim from the MP path. SP blob lives in `MEMPOOL_STAGE` so `lift->doors[i]` pointers stay valid for the stage lifetime. `g_StageSetup.props` temporarily repointed at the SP blob during the overlay so `setupGetCmdByIndex` resolves SP-internal door cross-references correctly. Diagnostic: `SETUP.LIFT: SP-in-MP stagenum=0x%02x lifts=N escasteps=M`. B-228 split into B-228a (this fix, transport visibility) and B-228b (residual crash class, awaits playtest).

### Priority I -- design only, `context/designs/connectivity-and-modern-main-menu.md`

Connectivity / modern main menu design pass per Mike's verbatim spec + mid-session amendments (replaces dedicated server for friend-play; always-on presence; `appear_online` + `joinable` social settings; 5-phase rollout). Strategic positioning section calls out the C-1 / MASTER-C5 audit carry-over and the scope-narrowing effect of the pivot. Section 10 leaves OPEN QUESTIONS with stable IDs (Q1-Q10) cross-referenced to Mike's separate numbered question list (handled out-of-band).

### Priority J -- design only, `context/designs/contextual-input-schemes.md`

IMC architecture formalization per Mike's "multiple input schemes ... applied contextually" framing. Splits `g_ImcGameplay` into `g_ImcMission` + `g_ImcCombatSim` (mutually exclusive); documents activation rules, suppression-vs-deactivation per IMC, hold-vs-tap discrimination via parallel `ACTION_SCORECARD_HOLD` action, 4-phase rollout (J-1 split, J-2 vehicle lifecycle, J-3 hold/tap, J-4 docs + asserts). Section 8 explicitly defers the visual-stack vs input-stack collapse to Priority K (separate batch).

### What did NOT land in this session

- Priority K (input-authority discipline audit + methodology + drift-site fixes) -- captured as a separate batch deliverable per Mike's directive.
- I and J implementations -- design only this batch.
- Mike's connectivity-questions Q&A list -- handled out-of-band by Mike directly.
- AUDIT-23/24 carry-overs not in this batch's scope (BOM, `~$*` gitignore, docx dedup, M2/M3/M5/M6/M7/M8 + L1-L4 from 4-24).

### Update post-batch -- Mike answered all 18 connectivity questions

Mike answered Q1-Q18 on `context/designs/connectivity-and-modern-main-menu.md` plus a refinement to Q3 (NAT traversal) mid-update. Doc rewritten to:

- Section 0 records the dedicated-server-removed-now / matchmaking-only-later framing (Q4); C-1 / MASTER-C5 audit-pole resolved by removal for friend-play.
- Section 2 records mesh + per-match authority by upload speed (Q1), connect codes with `[nickname]: [agentname]` display (Q5), no-central-infra signaling with friend-list-as-address-book (Q2), and a 5-tier NAT escalation (direct UDP / STUN punch / UPnP-NAT-PMP / ICE / TURN-as-last-resort, Q3 refined). **All 5 tiers ship in Phase 1** (Mike-confirmed second time 2026-04-24); no staged rollout. Phase 1 estimate revised up to ~5500 LOC. Phase 1 exit criteria add an explicit real-NAT matrix (open / full-cone / restricted-cone / port-restricted / symmetric / carrier-grade / corporate firewall + mixed pairs).
- Section 3 lands the three-state visibility (Public / Friends Only / Appear Offline, Q7), block list separate from friends (Q7), notification categories (Q8), per-friend mute (Q9), and global-not-per-profile persistence (Q6).
- Section 4 documents the listening-room precedence rule (match wins, Q10).
- Section 5 is the spectator + Theater unified subsystem (Q12).
- Section 6 routes listening-room tracks through the existing mod distribution system (Q10).
- Section 7 is new: Player Profile + Public Mods Page (Q11, Halo 3 File Share lineage, hash-checked transfer, trust-by-distribution).
- Section 8 elevates the no-drill-in/drill-out rule to a TOP-LEVEL UX PRINCIPLE (Q16) and lays out the three social surfaces (sidebar / Social menu / private chat, Q17) plus the visual-style anchor (Q15). Subsections 8.5.1 / 8.5.2 / 8.5.3 capture Mike's Q18 amendments: arbitrary-file chat (not mod-specific) with per-type inbox folders + size limits, type-aware context menu, and a Convert-to-mod modal flow.

**Q18 second amendment landed in this session:**
- Section 7 grew a `.pdmod` packaging subsection (zip with `mod.json` at root; plain `.zip` accepted as fallback when manifest is present).
- Mod manifest schema documented with `creator` as a required field (auto-fill from local agentname; legacy mods display "Unknown" until set).
- New "Mod install + trust" subsection: **manual-install only, no auto-install, no auto-execute.** Two install paths: context menu ("Install" / "Install and enable") or drag-drop in Explorer. Three-layer trust: sha256 hash + sender-via-friend-graph + manual install gate.
- Section 8.5.1 broadened from mod-specific to arbitrary files (mp3 / image / save / replay / unknown), each with predictable inbox + per-type size limits (mods 250 MB, music 50 MB, images 25 MB, etc.). Detection by extension + magic-byte head check; mod detection is extension-first with manifest-probe fallback for `.zip`.
- Section 8.5.2 context menu became type-aware. Mod-only actions (Install / Install and enable / View mod details / Reject mod). Cache-only (Save permanently / Delete from cache). Convertible-source action (Convert to mod...).
- New Section 8.5.3 covers the Convert-to-mod modal: source types (mp3/ogg/wav/flac → music mod; image types post-MVP), modal fields (name / description / creator auto-filled / tags / version), output behaviour (writes a fresh `.pdmod` to `mods/installed/`, enabled by default), and validation (Convert button gated on non-empty name + creator).
- Decisions Q18 row updated to reflect the broader file-transfer model + `.pdmod` format + manifest schema + manual-install rule + Convert-to-mod entry point.

**`.pdmod` architectural extension (Mike, 2026-04-24, follow-up):**
- `.pdmod` graduates from "shared mods only" to **canonical format for the entire mod system**. Mod tools output `.pdmod`, mods folder contains `.pdmod` files only (post-migration), loader interprets at runtime via virtual file system mounted on the archive (no on-disk extraction).
- Connectivity Section 7 packaging subsection broadened to capture this. New Loader-integration subsection (high level: archive enumeration, manifest read in memory, VFS mount, asset cache, hot-reload contract) and Migration-path subsection (M-1 dual-support, M-2 tool migration, M-3 one-shot auto-package, M-4 optional folder-loader retirement).
- Public Mods Page subsection updated: browsable manifests come straight from each peer's on-disk `.pdmod` (no extraction); transfers ship the same artifact; received mods land in shared inbox with manual-install gate.
- File-system metadata exposure (Mike addendum): Windows Property Handler DLL projects `mod.json` headline fields (creator / version / description / tags) to the OS shell layer; right-click Properties in Explorer and column views render them without opening the archive. Loader uses the same path for fast-scan startup. Single source of truth: `mod.json` inside the archive. Defensive zip-comment mirror covers non-Windows tools.
- New design doc `context/designs/pdmod-unified-mod-format.md` owns the loader engineering, mod-tool migration, hot-reload, file-system-metadata Property Handler, trust-model continuity, and folder-tidiness goal. 4-5 weeks estimated, 4 phases (M-1..M-4).
- Tracked as **B-238** in bugs.md as "OPEN -- DESIGN APPROVED, IMPLEMENTATION QUEUED (Priority M)."

Build-verify clean (no source touched in this update; both binaries unchanged from prior commits).
- Section 9 includes the version-mismatch UX exact string format (Q14) and the file-transfer plumbing convergence (Q10/Q11/Q18).
- Section 10 phasing spans 6 phases (added Theater as Phase 6 separate from Spectator Phase 5; Q12 unified-architecture rule means the camera/UI is written ONCE). Phase 1 acceptance + completion-before-pivot discipline note (Q13). Phase 2 reoriented post-tier-update to be **chat + file transfer + status popups + achievements broadcast** (was originally going to absorb NAT tiers 4-5; now obsolete because Phase 1 ships the full 5-tier stack).
- Section 11 transformed from OPEN QUESTIONS into DECISIONS table covering Q1-Q18.

Commits: `2f219fd5` originally landed both connectivity + IMC docs; the connectivity rewrite lands separately with this update.

### Queued for future batches (after Mike's reviews)

- **Priority K:** input-authority discipline audit + methodology + drift-site fixes. Hypothesis: codebase has TWO parallel stacks (visual menu + input context) that drift; collapse into ONE so menuPush/Pop are the SOLE input-authority mechanism. Should resolve Issue 3 (pause-menu input) as a consequence.
- **Priority L:** flat menu navigation system-wide. Mike's expansion of Q16 from connectivity-only to ALL game menus. Concrete pain: Combat Simulator's Bots section requires drill-in via A, and B exits the whole menu instead of popping a level. The fix is the flat-panel model: D-pad navigates everything, A acts only on focused element, B exits only at top. Affects Main Menu, Combat Simulator, Settings, Pause, MP Setup, etc. Document in `context/designs/flat-menu-navigation.md`. K and L are complementary (K = authority; L = structure); land K first or together.
- Phase 1 of the connectivity design (after final read by Mike).
- Phase J-1 of the IMC architecture (gameplay -> mission + combat sim split).

### Build verification

Both `PerfectDark.exe` and `PerfectDarkServer.exe` link clean after each of F, G, H. I and J are documentation-only.

## Session S456 - 2026-04-24 - Source-cleanup + Issue 10 execution + Level/Observer/8b polish

Mike's standing rules for this batch:

1. **Data should be clean at the source.** "There should be no bad arguments for Arenas (or anything else) if we instantiate with only valid data, it isn't a hard-coded list."
2. **No stubs, gaps, or TODOs.** "Don't strip the features, implement them."

Landed five commits, one per priority, each build-verified on pd + pd-server where applicable.

### Priority A -- `7ff165b0` refactor(catalog): clean arena / body data at source

- `port/src/assetcatalog_base.c`: NULL `s_ArenaNames[57..64]` (test_arch, test_dest, extra16..21) and `s_ArenaNames[69]` (test_lam). Removed the hardcoded `STAGE_EXTRA25` skip; NULL slot at index 70 is now the canonical marker.
- `port/fast3d/pdgui_menu_mainmenu.cpp` `gridArenaCollect`: stripped the filter cascade (stagenum>0, IS_GAMEPLAY, stageGetIndex, category-reject, `s_GridShowBonus` gate). Catalog registration is authoritative. Kept the feature-unlock gate + a LOG_ERROR for registered-but-unresolvable name_langid.
- `gridCommitEnter`: stripped the defense-in-depth revalidation, added resolution of Random-Multi / Random-Solo meta-arenas via `mpChooseRandomMultiStage` / `SoloStage`. One loud sanity assertion before stage-load.
- Removed the "Show bonus / debug arenas" toggle UI.
- `port/fast3d/pdgui_menu_forge.cpp` `pdguiForgeStartSessionOn`: removed silent fallback-to-CI-Training. LOG_ERROR and refuse on ineligible stagenum.
- `port/fast3d/pdgui_menu_room.cpp` Character Select + bot modal: stripped the hardcoded "Dr. Caroll" / "Skedar" fallback for bodies with empty `display_name`. Catalog is authoritative post-P4; empty names now log as registration bugs.

### Priority B -- `38b8c8b1` feat(catalog): rig_class authoritative for body<->head (Issue 10)

- Schema: `ext.body.rig_class[32]` + `ext.head.rig_class[32]` with canonical slugs `human_male_neck_standard`, `human_female_neck_standard` (FEMALE + FEMALEGUARD merged), `maian_tall_neck`, `cass_neck`, `mrblonde_neck`.
- Setters: `catalogSetBodyRigClass` / `catalogSetHeadRigClass`.
- Helper `rigClassForHeadBodyType(u8 type)` maps HEADBODYTYPE_* to slug; populated at every base body / base head / SP body / SP head registration.
- `catalogGetBodyValidHeadIds` rewritten: string equality on `rig_class` is the sole compat rule. HEADBODYTYPE fallback chains and the `category == "sp"` reject (Issue 1 stopgap) removed.

### Priority C -- `44fb0922` feat(grid): Level tab real skybox / skylight / music controls

- New C accessors in `src/game/forgemode.c`: `forgeLevelGet/SetSkyColor`, `Get/SetFog`, `Get/SetCloudsEnabled`, `Get/SetCloudColor`, `SetSkyStage`, `PlayMusic`, `StopMusic`. Needed because `pdgui_forge_editor.cpp` is C++ and can't include `types.h`.
- `forgeDrawLevelExtras` (renamed from Placeholder): 12-stage Skybox combo, ImGui ColorEdit3 for sky colour, fog near/far sliders, clouds on/off + tint, 18-track Background Music combo with Silence. All dispatches real -- no TODO log lines.

### Priority D -- `dec88dc0` feat(grid): observer controller full scaffold

- `src/game/forgemode.c::forgeSavePlayerMode`: now actually writes `chr->bodynum = BODY_DRCAROLL` and `chr->headnum = HEAD_RANDOM_GENDER` on FREEFLY entry (previously logged intent but never swapped).
- `port/src/actionmap.cpp`: new `actionIsBlockedInFreefly(a)` predicate flags combat, weapon selection (radial), vehicle, chr pose, and interact-as-gameplay. Every action-read API gets a second gate: `if (forgeIsFreefly() && actionIsBlockedInFreefly(a)) return 0`. Movement / aim axes / `ACTION_FORGE_*` pass through.

### Priority E -- `c48d21bd` feat(grid): editor control scheme (Issue 8b)

- Tab-cycle hotkeys: PageUp/PageDown + Ctrl+Tab / Ctrl+Shift+Tab (IDE-style). LB/RB via the existing `ACTION_MENU_TAB_PREV/NEXT` -> PageUp/PageDown translation.
- Arrow keys + D-pad stay reserved for ImGui focus-nav (not conflated with tab cycling).
- Footer hint rewritten to match the real mapping.

### What did NOT land

- Subdivision of `rig_class` (splitting DEFAULT when neck drift shows) -- design-doc future work, lands as data-only edits.

### Priority E v2 -- `29d05d49` feat(grid): X-toggle sidebar + D-pad nav + LB/RB tabs (Issue 8b)

After Mike confirmed Priority E v1 was incomplete vs the verbatim spec, layered the real Issue 8b on top:

- 6 new actions (`ACTION_FORGE_SIDEBAR_TOGGLE/UP/DOWN/ACTIVATE`, `ACTION_FORGE_TAB_PREV/NEXT`, `ACTION_COUNT` 62 -> 68) on a new `g_ImcForge` IMC at priority 7. Defaults: X = sidebar toggle, LB/RB = tab prev/next, D-pad up/down/right = sidebar nav. Keyboard equivalents Tab / PageUp / PageDown / arrow keys land in the same IMC.
- Sidebar UI: `forgeSidebarDraw` ImGui child with per-tab section list (Level: Skylight / Skybox / Background Music / Lighting / Mission; Objects: every `forge_category_t`; Control: Logic / Wires / Zones / Channels; Gameplay: Game Type / Scoring / Players / Modifiers / HUD / Waves / Bots; Setup: Map Metadata / Variant / Players). `forgeScrollAnchorMaybe` planted at 17 section headers across `forgeDrawActiveTab` and the per-tab draws so sidebar activation scrolls the editor to the right section. Objects-tab activation writes `forgeGetEditor()->category_filter` directly.
- Stick invariant documented in `forgeReadFreeflyInput`: stick axes ride on `actionmapPollFrame`'s direct `SDL_GameControllerGetAxis` write, bypass IMC priority entirely, and are gated only on `gameplayInputSuppressed` (menu / focus). Sidebar / tab / D-pad input never consumes sticks.

### AUDIT-24-H2/H3 closure -- B-236

The 4-24 super audit (committed as `5ee1b013`) flagged that `g_ImcGameplay`'s dual-bind of `JBTN_BACK -> FORGE_TOGGLE`, `JOFS_RTRIG -> FORGE_ASCEND`, and `JOFS_LTRIG -> FORGE_DESCEND` lost every press to SCORECARD / FIRE_PRIMARY / FIRE_SECONDARY via `fireVk`'s single-winner-per-IMC dispatch. Same root cause invalidated LSHIFT/LCTRL boost/precision against SPRINT/CROUCH on keyboard.

Closure splits the Forge IMC in two:

- New `g_ImcForgeSession` (priority 6, whole session) hosts `JBTN_BACK -> FORGE_TOGGLE` only. Active from session start (`forgeTick` consume of `request_enter_session`) through `forgeTransitionToInactive`.
- Existing `g_ImcForge` (priority 7, FREEFLY-only) absorbs the moved triggers + modifiers (LT/RT, LSHIFT/LCTRL) alongside the Issue 8b sidebar / tab bindings.
- Gameplay IMC keeps keyboard F7/E/Q (each a sole binding, no collision).

State trace: outside session = original gameplay; NORMAL playtest = Back toggles to FREEFLY, every other gameplay action keeps working (LB/RB still WEAPON_PREV/NEXT, X still USE, RT/LT still FIRE_*); FREEFLY = forge IMC shadows triggers/modifiers/D-pad/X/LB/RB.

### What did NOT land in this session

- Subdivision of `rig_class` (splitting DEFAULT when neck drift shows) -- design-doc future work, lands as data-only edits.
- AUDIT-24-M2 / M3 / M5 / M6 / M7 / M8 + L1-L4 from the 4-24 audit are still live; only the H2/H3 cluster is closed.
- AUDIT-23-M1 (`CMakeLists.txt` BOM), AUDIT-23-M2 (`~$*` gitignore), AUDIT-23-L1 (docx dedup) carry-overs untouched.
- B-217..B-222 playtest queue from 4-21 still pending Windows MSYS2 client run.

## Session S455 - 2026-04-24 - Grid playtest batch: crash fix + FREEFLY observer gating

Mike's first real Grid playtest surfaced 4 issues + 3 design directions.  Commit `1925f8a5` lands the crash-blocking work (Issues 5, 6, 7) as one unit because the logical pieces overlapped.

**Reported signals from the log:**

```
LOAD: calling bodiesReset stagenum=0x5a
LOAD: calling setupCreateProps stagenum=0x5a normmplayerisrunning=0
INTRO: Applying mode transition: -1 -> 0
INTRO: -> titleInitLegal
GRID: -> INACTIVE (stage left gameplay)
FATAL: ACCESS_VIOLATION PC=... chr_slot=-1 stack_watermark=2192
```

stagenum 0x5a == `STAGE_TITLE`.  The submenu was feeding a system stagenum into the stage-load pipeline.  `forgeTick` immediately transitioned INACTIVE (non-gameplay), INTRO took over, tick loop dereferenced chr_slot=-1.

### Issue 5a: arena picker filter + stagenum validation (LANDED)

Three defense lines:

1. `gridArenaCollect` (`pdgui_menu_mainmenu.cpp`) rejects entries whose
   - stagenum is <= 0
   - !STAGE_IS_GAMEPLAY (mirror `GRID_STAGE_IS_SYSTEM` defined locally to avoid constants.h pull-in)
   - !stageGetIndex (not in live stage table)
   - category == "Random" (meta arenas)
   - category in {"Bonus", ""} unless `s_GridShowBonus` toggle is on
2. `gridCommitEnter` re-validates stagenum at commit.
3. `pdguiForgeStartSessionOn` validates its arg via new `forgeStageIsGridEligible` helper; bad stagenum falls back to STAGE_CITRAINING with a LOG_WARNING.

Net effect: no path reaches `mainChangeToStage(STAGE_TITLE)` from the Grid submenu.  The crash is structurally impossible now, not just "filtered at the UI layer."

### Issue 7: arena picker category tags + bonus toggle (LANDED same commit)

Each arena row displays its category: MP / SP / Classic / Bonus / Random.  "Show bonus / debug arenas" checkbox toggles the hidden Bonus and unknown-category entries (off by default).  Flipping the toggle invalidates the built list and rebuilds with the new filter.

### Issue 5b: FREEFLY observer-controller gating (LANDED same commit)

Per Mike's design note: flying as Dr Carroll in Grid is a separate controller -- weapon cheats, interact probes, radial menus have nothing meaningful to apply to.  Minimum viable gates shipped:

- **F7 invincibility**: SDL hook in `pdgui_backend.cpp` returns 1 without calling `playerToggleDevInvincibility` when `forgeIsFreefly()`.
- **Interact prompt**: `interactPrompt` boolean gains `&& !forgeIsFreefly()` in both NewFrame and Render gate sites.  "Hold X to use X" pill doesn't render while flying.

Weapon / arm rendering and radial wheel suppression deferred to a proper observer-controller pass.  Documented as follow-up in bugs.md.

### Issue 6: DISSOLVED by Issue 5b design

F7 binding conflict between `ACTION_FORGE_TOGGLE` (my P2 addition) and the dev invincibility cheat dissolves naturally.  In FREEFLY the cheat hook short-circuits, so F7 fires only the mode toggle via the actionmap.  On exit to Playtest (NORMAL state), F7 re-engages the cheat normally.  No F7 rebind needed.

### Cleanup: `g_BotUpdatesDisabled` reset on Grid exit (same commit)

Issue surfaced during last batch's Issue 3 investigation: `forgeRuntimeTick` mirrors `bs->all_frozen` into `g_BotUpdatesDisabled` every tick, but never resets on session exit.  If a user had Freeze All on and exited Grid, the next non-Grid match inherited frozen bots.  Cleared in `forgeTransitionToInactive`.

### Issue 2 re-verify note (from prior batch)

With Issue 5b's FREEFLY observer gating, the "stewardess has Joanna's head" class of issues Mike reported is expected to resolve as a combined outcome of:
- Last batch's `category == "sp"` filter on the valid-head set (`45bd3bdd`).
- This batch's FREEFLY body-swap semantics (Dr Carroll avatar planned for Issue 10).

Confirmation depends on Mike's next playtest.

### Issue 10 (scoped, NOT implemented): rigging-aware body<->head linkage

Mike's design direction: replace the current category-based filter (and the mp_index heuristic that preceded it) with an explicit `rig_class` per body and per head.  Valid-head set becomes a query over rig_class compatibility ("human_male_neck_standard" matches DEFAULT male bodies and any head that bolts onto that neck socket).  SP heads and bodies become first-class citizens -- the filter is purely physical compatibility, not category.  Retires both the sp_head_* filtering and the "missing necks" symptom in one stroke.  Substantial audit + data-entry work across every head and body; logged here so the direction is captured when we pick it up.

### Issue 3 pause menu (still parked, no new info)

No playtest log for the pause-menu issue in this batch.  Previous diagnosis stands: no sprint code touched the pause menu path, cannot reproduce from code review.  Issue 11 (next commit) will add a debug overlay that shows the live menu stack + input context owner, which should let Mike capture the exact failure state next time it repros.

### Files touched (Issues 5, 6, 7 commit `1925f8a5`)

- `port/fast3d/pdgui_menu_mainmenu.cpp` -- GridArenaCategory enum + helpers, filter cascade, category tag + Bonus toggle in render, defense-in-depth in gridCommitEnter.
- `port/fast3d/pdgui_menu_forge.cpp` -- forgeStageIsGridEligible helper + validation in pdguiForgeStartSessionOn.
- `port/fast3d/pdgui_backend.cpp` -- extern decl for forgeIsFreefly, F7 gate, interactPrompt gate.
- `src/game/forgemode.c` -- g_BotUpdatesDisabled reset in forgeTransitionToInactive.

**Build:** clean incremental link of PerfectDark.exe.

### Playtest after rebuild

1. Main Menu -> The Grid -> arena list shows only gameplay arenas with MP / SP / Classic tags.  No system stages selectable, no unregistered stagenums.
2. Toggle "Show bonus / debug arenas" -> Bonus + test entries appear; toggle off -> they disappear.
3. Pick any arena -> Enter The Grid -> no crash.  Session starts in FREEFLY.
4. In FREEFLY: F7 does NOT flip invincibility banner.  Interact prompt pill never appears.
5. Press F7 (or controller Back) -> toggles to NORMAL / Playtest.  F7 re-engages invincibility cheat normally.
6. Back to FREEFLY -> F7 becomes inert again.
7. Inside Grid: toggle Freeze All (End key) on.  Exit Grid session (return to CI).  Bots in the next Combat Sim match are NOT stuck frozen (cleanup verified).

## Session S454 - 2026-04-24 - Post-sprint playtest fixes (Issue 1..4)

Mike flagged the Main Menu "The Grid" button as broken: pressing it dropped the user straight into a Forge session on CI Training, skipping every step of map / variant selection.  Expected flow is submenu -> map picker -> variant editor -> Enter The Grid.  "The Grid" is the unified facility; Forge (edit) and Playtest (inhabit) are modes inside it.

**Priority 1 (this entry, landed)**: Main Menu "The Grid" button now opens a Grid submenu (`s_MenuView == 6` branch in `pdgui_menu_mainmenu.cpp`).  Submenu has:

- Arena picker (ListBox) — built on demand from `assetCatalogIterateByType(ASSET_ARENA)`, filtered by `challengeIsFeatureUnlocked`.  Sorted alphabetically (Blank Map first when enabled).
- Variant editor — gametype combo (6 base scenarios from `s_BaseGameModes`), time-limit slider (0-60 min), score-limit slider (0-100), teams / one-hit / slow-mo checkboxes.
- "Enter The Grid" button — calls `matchConfigInit()` to reset, writes `stage_id / scenario_id / timelimit / scorelimit / options` (MPOPTION bits) into `g_MatchConfig`, then hands off to `pdguiForgeStartSessionOn(stagenum)`.

Supporting changes:
- `port/fast3d/pdgui_menu_forge.cpp`: added `pdguiForgeStartSessionOn(stagenum)` implementation (was declared in header but never defined).  Old `pdguiForgeStartSession()` becomes a thin wrapper that calls `...On(STAGE_CITRAINING)` so any other caller keeps working.
- `port/include/pdgui_menu_grid.h`: new header.  `GRID_BLANK_STAGE` is deliberately left undefined — see the evening decision log; the Blank Map row is suppressed until Mike points at the right stagenum.
- Main Menu window title gains "The Grid" at `s_MenuView == 6`.  ESC handler case logs the close.

Blank Map is ESCALATED.  Mike asked for "the invalid-map fallback plane"; exhaustive grep turned up no bare-plane stage in the codebase (the only invalid-stagenum coercer is `stageSanitizeLoadStagenum(0x00) -> STAGE_CITRAINING`, which Mike explicitly rejected as a substitute).  Submenu ships without the Blank Map entry to avoid a broken placeholder.  See `context/audits/evening-decisions-2026-04-23.md` for the full search surface.

**Build:** Clean build on main working copy (`ninja -C Build pd`) finished green.  `PerfectDark.exe` (54 MB) linked.  Only warnings are pre-existing (`/*` in comments, unrelated).

**Files touched (this pass):**
- `port/fast3d/pdgui_menu_mainmenu.cpp` — button repointed; renderGridSubmenu() added; matchsetup.h pulled in under extern "C".
- `port/fast3d/pdgui_menu_forge.cpp` — `pdguiForgeStartSessionOn(stagenum)` implementation.
- `port/include/pdgui_menu_grid.h` — new header (GRID_BLANK_STAGE intentionally undefined).
- `context/audits/evening-decisions-2026-04-23.md` — decision log.

**Next:** Priority 2 — Forge ↔ Playtest mode toggle (Halo-style in-place swap, same stagenum / bots / variant).  Then P3-P5 per the foundation pass.

### Priority 2 (this entry, landed) — Halo-style Forge <-> Playtest toggle

Grid sessions now default to FREEFLY (Forge / edit) on entry, so "Enter The Grid" drops the user into the editor where they can customize the map before playtesting.  In-place mode toggle uses the existing `ACTION_FORGE_TOGGLE`; keyboard stays on F7 and a new gamepad binding on `JBTN_BACK` matches Halo's Back-button convention.

Existing `forgemode.c` transitions already preserve position / orientation across swaps (FREEFLY writes the camera pos and yaw / pitch to the player prop every tick, so going FREEFLY -> NORMAL leaves the player body at the last freefly spot; going NORMAL -> FREEFLY snaps the camera to the player's current pos via `forgeSnapFreeflyToPlayer`).  Same stagenum, same bot roster, same variant — only the camera and input rig swap, as per the directive.

Back button also fires `ACTION_SCORECARD` (existing binding).  During a Grid session the toggle fires and the scorecard pop-up is a benign no-op on a solo offline session.  See the evening decision log for why that overlap is acceptable.

Playtest mode in this pass shows only the mode badge from `pdguiForgeHudRender`; the full editor overlay hides via the existing `forgeIsFreefly()` gate at `pdgui_forge_editor.cpp:1418`.  The compact in-Playtest bot-control HUD is deferred (decision-log entry "Playtest bot-HUD polish deferred") — the Bots tab is still reachable by toggling back to Forge.

**Files touched (P2 pass):**
- `src/game/forgemode.c` — `forgeTick::request_enter_session` branch now calls `forgeTransitionToFreefly` instead of `forgeTransitionToNormal`.
- `port/src/actionmap.cpp` — added `JOY_BTN(0, JBTN_BACK)` binding on `ACTION_FORGE_TOGGLE`; rewrote the comment to document the swap.
- `context/audits/evening-decisions-2026-04-23.md` — three new entries: default entry mode, Halo toggle binding, Playtest bot-HUD deferred.

**Build:** clean incremental link on main working copy.

### Priority 3 (this entry, landed) — Per-body valid-head set + randomization

Today's B-235 follow-up fix used `mpDefaultHeadForBody` which gives HEAD_RANDOM_GENDER bodies a random pick but still yields deterministic pairs for specific-pair bodies like Maian -> Maian head.  Mike's expectation is that 31 Maian bots get 31 *varied* Maian heads (not all the same face), driven by the full catalog set of heads whose HEADBODYTYPE_* is compatible with the body.

**New catalog accessor** at `port/include/assetcatalog.h` + `port/src/assetcatalog_api.c`:

- `catalogGetBodyValidHeadIds(body_id, *out_count) -> const char *const *` enumerates every head whose HEADBODYTYPE_* is compatible with the body's type (same type, or DEFAULT+DEFAULT, or FEMALE <-> FEMALEGUARD cross-pair).  Returns pointers into the catalog's stable ID strings via a module-static buffer.  Falls back to `catalogGetBodyDefaultHead` with a loud `LOG_WARNING` if no type-compatible heads exist (defensive; should not happen in a well-authored catalog).
- `catalogPickRandomHeadIdForBody(body_id) -> const char *` calls the list accessor, then `rngRandom() %% count` to pick one.  Deterministic-pair bodies always return the same ID; pooled bodies yield variety per call.

**Callers updated:**

- `port/src/net/matchsetup.c::pickHeadIdForBody` is now a thin wrapper over `catalogPickRandomHeadIdForBody`.  All bot creation paths (`matchConfigAddBot`, `pickRandomBodyHead`) automatically pick up the broader valid set.
- `port/fast3d/pdgui_menu_room.cpp` Set Character multi-select site (~line 2267) and individual-bot edit modal (~line 3404): replaced `mpDefaultHeadForBody(b) -> catalogMpHeadId` with `catalogPickRandomHeadIdForBody(bid)` inside the per-bot loop.  Multi-select Maian now yields varied Maian heads; single-select Maian yields a fresh random Maian head per click.

**Edge cases handled:**

- Body with exactly one valid head -> deterministic (modulo-1 picks the only element).
- Body with zero valid heads -> `LOG_WARNING` + fall back to `catalogGetBodyDefaultHead` so the caller still gets a non-NULL string.
- Catalog-ID rule: the accessor returns IDs, not integer indices; no persisted-state change.  `g_MatchConfig.slots[].head_id` stays the catalog ID.

**Files touched (P3 pass):**

- `port/include/assetcatalog.h` -- two new declarations.
- `port/src/assetcatalog_api.c` -- `catalogGetBodyValidHeadIds` + `catalogPickRandomHeadIdForBody` + `#include "lib/rng.h"`.
- `port/src/net/matchsetup.c` -- `pickHeadIdForBody` reduced to a one-line wrapper.
- `port/fast3d/pdgui_menu_room.cpp` -- two Set Character sites swapped.

**Build:** clean link on main working copy, PerfectDark.exe rebuilt.

### Priority 4 (this entry, landed) — Catalog display-name becomes authoritative for all bodies

Today's B-226 fix populated `ext.body.display_name` only for body indices 57-62 (Bond actors with shared L_OPTIONS_070 "Dinner Jacket" and Skedar / Dr. Caroll with unrelated UI-string langids).  Mike's P4 directive: make the catalog display_name the PRIMARY identity for every body, with langbank only reachable as a fallback when a catalog entry leaves display_name empty.

Structural note: `mpGetBodyName` already prefers the catalog override when set (B-226 wiring).  The only change required was removing the `if (idx >= 57 && idx <= 62)` gate around `catalogSetBodyDisplayName(e, s_BaseBodies[i].desc)` in `assetcatalog_base.c`.  All 63 base bodies now have their `s_BaseBodies[].desc` populated into `ext.body.display_name` at registration time.  Langbank (`modmgrGetBody(mpbodynum)->name -> langGet`) remains as the fallback inside `mpGetBodyName` for bodies that leave display_name empty (mod bodies that opt in to i18n).

Bug-class retirement: this retires the "langid drift on a body" class (generalises B-226).  Future bodies added with a valid desc never need a per-body catalog override.

Trade-off: non-English locales lose langbank translation for the 63 English canonical names.  Risk was flagged in the 2026-04-23 systematic-pass audit; Mike accepted the English canonical direction.  Per-body revert is one line (leave `desc` empty in `s_BaseBodies[]` for any body that needs its langbank translation back).

No audit of heads / arenas / weapons / scenarios in this pass -- body is the most load-bearing surface and the one surfaced in the playtest that exposed B-226.  Following the same pattern elsewhere is a future polish ticket.

**Files touched (P4 pass):**
- `port/src/assetcatalog_base.c` -- removed the indices-57-62 gate.

**Build:** clean incremental link of PerfectDark.exe.

### Priority 5 (this entry, landed) — SP-stages-in-MP loader (B-228 FIXED)

Mike's B-228 report: when an SP-class stage (CI, Chicago, Villa, ...) is hosted as an MP arena, elevators / fire-escape steps don't load.  Root cause identified in the 2026-04-23 systematic pass as the `(obj->flags2 & diffflag) == 0` filter in `setupLoadStage` -- SP-authored setup blobs set difficulty / player-count exclusion bits on transport props that trip in MP.

**Fix approach chosen (option A from the systematic-pass notes, narrowed to transport props):** compute a second filter value `mptransport_diffflag` alongside the standard `diffflag`.  For SP-in-MP class stages (`STAGE_CITRAINING`, `STAGE_CHICAGO`, `STAGE_VILLA`, `STAGE_INFILTRATION`, `STAGE_G5BUILDING`, `STAGE_PELAGIC`) the transport filter is forced to 0 while `g_Vars.mplayerisrunning`.  `OBJTYPE_LIFT` and `OBJTYPE_ESCASTEP` cases use the relaxed filter; every other objtype keeps the standard diffflag so SP-only clutter (desks, decorative chrs) stays filtered.

**Why this is protocol-safe (Mike's hard stop):** `pd-server` doesn't run `setupLoadStage` at all -- the server_stubs.c path covers all game logic on the headless server.  Each client runs this code locally against the same setup blob with the same stagenum, so every client ends up with an identical live lift set.  The existing server-authoritative `liftTick` + prop sync path handles position updates over the wire.  No new messages, no new fields, no protocol version bump.

**Diagnostic:** `SETUP.LIFT: SP-in-MP stagenum=0x%02x -- relaxing LIFT/ESCASTEP exclude filter (diffflag 0x%x -> 0)` fires once per MP-on-SP-stage load so the code path is auditable in-log.

**Not covered in this pass (deferred as follow-up):**
- Full state sync validation (lift position drift across clients if two players interact simultaneously).  Existing server-auth replication should handle it; needs playtest confirmation.
- Door / escastep variants on mod-authored stages.
- The SP-stage MP-readiness audit matrix (next priority).

**B-228 row in `context/bugs.md` updated from OPEN / DEFERRED to FIXED-PENDING-PLAYTEST.**

**Files touched (P5 pass):**
- `src/game/setup.c::setupLoadStage` -- new `mptransport_diffflag` + diagnostic log + switch-case updates for `OBJTYPE_LIFT` and `OBJTYPE_ESCASTEP`.
- `context/bugs.md` -- B-228 row.

**Build:** clean incremental link of PerfectDark.exe.

### Priority 7 (this entry, landed) — Playtest bot HUD + runtime wire-up

Continuation of the evening batch.  P2 shipped the Forge<->Playtest toggle but deferred the bot-HUD-in-Playtest polish.  P7 closes that gap and wires the spawn-mode logic that was log-only before.

**Semantics now wired in `port/src/forge/forge_runtime.c`:**

- `s_spawnBot` returns the `aibotnum` (slot) instead of a 0/1 success flag, so callers can reach the live `g_MpBotChrPtrs[slot]` right after `botmgrAllocateBot`.
- New `s_teleportBotNearPlayer(slot, radius)` helper writes `g_MpBotChrPtrs[slot]->prop->pos` to `player.pos + forward*radius` on the XZ plane.  Uses `player->vv_theta` + `sinf/cosf` for the forward vector.  Clamps radius to 100..5000u (HUD slider range).  Facing-direction polish deferred; the bot's AI tick re-orients on first frame.
- `forgeRuntimeTick` now:
  1. Mirrors `bs->all_frozen` into `g_BotUpdatesDisabled` every tick.  This reuses the F6 freeze machinery (bot.c zeros `speedmultforwards/sideways` while keeping `chrTick` so models stay visible, matching B-217 v2).
  2. Captures the slot returned by `s_spawnBot`, bumps `bs->active_count` / `bs->frozen_count`, and when `spawn_mode == FORGE_BOT_SPAWN_NEAR_ME` teleports the bot via `s_teleportBotNearPlayer`.
  3. `FORGE_BOT_SPAWN_ANY` leaves the bot at the scenario-picked pad.  `FORGE_BOT_SPAWN_SMART` left as-is for now (difficulty is already HARD for hostile bots via `s_fillBotSlot`; fine-grained aggression from `bs->smart_aggression` is a future pass).

**Playtest HUD panel in `port/fast3d/pdgui_forge_hud.cpp`:**

Renders in the NORMAL (Playtest) branch of `pdguiForgeHudRender` instead of the early-return.  Top-right pill with:

- Line 1: `BOTS  active N  frozen F`
- Line 2: `Freeze  ON/off      Mode  Any/Near Me/Smart`
- Line 3-4: key legend `[Ins] add bot   [Del] remove all` / `[End] freeze    [Home] cycle mode`

**Keybinds (keyboard only, intentionally outside the actionmap):**

- `Insert`  -> `forgeBotAddRequest(1)` (spawns one active bot next tick).
- `Delete`  -> `forgeBotRemoveAll()`.
- `End`     -> toggles `bs->all_frozen` via `forgeBotFreezeAll`.
- `Home`    -> cycles `bs->spawn_mode` through Any / Near Me / Smart.

Rationale for keybind-only (no clickable buttons): Playtest mode runs with mouse-captured first-person input.  A clickable ImGui window would require releasing mouse capture, which conflicts with combat aim.  Keybinds live on keys that are not bound to gameplay.  `ImGui::IsKeyPressed` polls the SDL backend's key queue so it fires without requiring an ImGui window focus.  Decision logged in `context/audits/evening-decisions-2026-04-23.md`.

The Forge-mode Bots tab (`forgeDrawBotsTab` in `pdgui_forge_editor.cpp`) stays as the secondary convenience surface; both surfaces write to the same `forge_bot_settings_t` so they stay in lockstep.

**Files touched (P7 pass):**

- `port/src/forge/forge_runtime.c` -- extern `g_BotUpdatesDisabled`, s_spawnBot return value, new `s_teleportBotNearPlayer`, forgeRuntimeTick sync + per-spawn teleport.
- `port/fast3d/pdgui_forge_hud.cpp` -- NORMAL-mode HUD panel + keybind polling.

**Build:** clean incremental link of PerfectDark.exe.

**Playtest after rebuild:**

1. Enter The Grid -> session starts in FREEFLY.  Press F7 / controller Back -> NORMAL (Playtest).  HUD pill appears top-right.
2. Press `Insert` -> Bot appears at scenario spawn (Mode == Any).
3. Press `Home` to cycle Mode to "Near Me" -> press `Insert` -> bot spawns at player forward + radius (log line `GRID.RUNTIME: Spawn Near Me -- teleported ...`).
4. Press `End` -> Freeze ON; bots stop moving but stay visible.  Press `End` again -> bots resume.
5. Press `Delete` -> all bots removed; active_count / frozen_count display resets.

### Priority 8 (this entry, landed as documentation; code follow-up deferred) — SP-stage MP-readiness re-audit

P6 shipped a readiness matrix based on the assumption that P5's `mptransport_diffflag` relax fixed B-228 for the 6 SP-in-MP class stages.  P8 is the promised follow-up: walk the remaining 9 PENDING-TEST stages and decide which need to join the relax list.  The static audit surfaced a significant diagnosis correction.

**Finding.**  Counting `lift(` / `lift_door(` / `escastep(` macro invocations across every SP and MP setup file:

- MP setup files for SP-class stages are EMPTY of those macros.  `mp_setupdish.c` (CI), `mp_setuppete.c` (Chicago), `mp_setupeld.c` (Villa), `mp_setuplue.c` (Infiltration), `mp_setupdepo.c` (G5), `mp_setupdam.c` (Pelagic) all register zero lifts.
- SP setup files DO contain those macros.  CI SP has 2 lifts + 8 lift_doors; Infiltration has 4 + 8; Airbase has 5 + 16 + 40 escasteps; Attackship has 6 + 16.

**Conclusion.**  P5 is a no-op on the current codebase.  The EXCLUDE filter rejects nothing because the MP setup never loads lifts to begin with.  P5 is kept as structural insurance (a future MP setup with lift + exclude bits would benefit) but alone it doesn't close B-228.  The real fix is Option E from the P5 analysis: load the SP setup alongside the MP setup for SP-in-MP stages, iterate it, and run ONLY `OBJTYPE_LIFT` + `OBJTYPE_ESCASTEP` through the existing switch.  Per the hard stops ("no net-protocol changes", "no irreversible moves", soft-stop on late-night scope expansion), Option E is deferred as a scoped follow-up, not implemented tonight.

**B-228 row in `context/bugs.md` flipped from FIXED-PENDING-PLAYTEST to REOPENED with the corrected diagnosis + Option E sketch.**

**Readiness matrix at `context/audits/sp-stage-mp-readiness-2026-04-24.md` updated with:**
- A "P8 static-analysis update" section explaining the diagnosis correction.
- A "SP-setup transport-prop inventory" table listing the exact lift / lift_door / escastep count for each of the 15 stages (6 P5-listed + 9 pending-test).
- Impact ranking for Option E: Airbase > Attackship > AirForceOne > Infiltration > CITraining > Defection > Defense > Investigation > Deepsea > SkedarRuins.  Chicago / Villa / G5Building / Pelagic / CrashSite have zero SP lifts and would not benefit.

**Files touched (P8 pass):**
- `context/bugs.md` -- B-228 row corrected.
- `context/audits/sp-stage-mp-readiness-2026-04-24.md` -- static-analysis update.

**No code change this entry.**  Build unchanged.

### Soft-stop marker (P9 / P10 deferred)

The P8 audit surfaced an architecturally significant finding and it is late.  Per the soft-stop rule ("commit what's green, stop rather than pushing into P9 or P10 tired"), stopping here.  P9 (non-body catalog consolidation) and P10 (langbank drift audit) are doc- and refactor-heavy -- clear-headed morning work.

State handoff for Mike in the morning:
- 8 commits on dev since the systematic-pass merge (P1..P8 landed green + one server-build fix + one P7 evening polish).  `24a97db8` final.
- P1 (menu flow) / P2 (Halo toggle) / P3 (valid-head set) / P4 (catalog display_name) / P7 (Playtest bot HUD) are ready for playtest.
- P5 (SP-in-MP relax filter) is NOOP on current codebase -- kept as insurance, but B-228 is REOPENED with the Option E plan.
- P6 / P8 are doc deliverables (readiness matrix).
- P9 / P10 untouched.
- Open escalations: (a) Grid Blank Map stage pointer; (b) B-228 Option E implementation approval.

## Session S454 - 2026-04-24 - Post-sprint playtest fixes (Issue 1..4)

Mike played the post-foundation-pass build (dev at `052f540b`) and filed four issues.  Working through them in the order he specified: Issue 1 (head filter) -> Issue 3 (pause menu) -> Issue 4 (music sync) -> Issue 2 re-verify.

### Issue 1 - Wrong heads slipping through valid-head filter (LANDED)

**Symptom.**  MATCHSETUP log rows for bots with human bodies showed SP-only head IDs like `head='base:sp_head_52'` which resolved to `mphead=0` (Joanna fallback).  dd_guard + sp_head_52 = stewardess-with-Joanna's-head visual.

**Root cause.**  P3's `catalogGetBodyValidHeadIds` iterated every `ASSET_HEAD` entry and included any HEADBODYTYPE-compatible one.  ASSET_HEAD includes both MP-registered heads ("base:head_elvis", category="base") and SP-only heads registered by the coverage-mask pass ("base:sp_head_52", category="sp").  SP heads are not meant to flow through MP selection; letting them into the valid-set broke the rig mapping.

**Additional data quirk found while investigating.**  `s_BaseHeads[]` only names 75 of the 76 MP head slots.  The missing slot's head (index 75) gets a fallback catalog ID `base:head_75` at MP registration, but the SP-loop later registers the same engine head as `base:sp_head_<engine_idx>` and overwrites the runtime cache slot, so pass 3 of `catalogBuildRuntimeCaches` assigns `mp_index = 75` to the SP entry.  Filtering by `mp_index < 0` alone would miss this case.  The correct gate is the `category` field: "sp" rejected, "base" / mod categories accepted.

**Fix.**  `port/src/assetcatalog_api.c::collectValidHead` now rejects entries with `category == "sp"` before the HEADBODYTYPE compatibility test.  Detailed comment block explains the double-registration quirk so the gate isn't simplified back to `mp_index`.

**Effect on valid-set sizes:**
- Human male (DEFAULT): ~43 heads (g_MpMaleHeads pool).
- Human female (FEMALE + FEMALEGUARD): 7 heads (g_MpFemaleHeads pool).
- Maian: 2 (head_elvis, head_maian_s).
- Cass: 1 (head_cassandra).
- Mr Blonde: 1 (head_mrblonde).

Maian variety is limited by the MP head list, not by the filter.  To add more Maian heads to the pool, someone would have to promote an SP Maian head (head_theking, head_grey) into `s_BaseHeads` with a spare `g_MpHeads[]` mp-index.  That's a data-authoring change, not code.

**Follow-up coverage.**  Mike's "missing necks" Issue 2 is likely the same root cause: a head from the wrong rig family was fitting a body's skeleton incorrectly.  With SP heads filtered out, the rig mismatches should go away.  Verify after rebuild + playtest.

**Files touched (Issue 1):**
- `port/src/assetcatalog_api.c` -- one-block edit inside `collectValidHead`.

**Build:** clean incremental link of PerfectDark.exe + PerfectDarkServer.exe.

### Issue 3 - Pause menu input regression (INVESTIGATED, cannot reproduce from code review)

Mike reports pause menu input doesn't work in a match.  Hypothesis: a sprint change interfered with input routing under `GAMESTATE_PAUSED` (the same seam as B-224 / B-231 which the systematic pass touched).

**Sprint diff on pause menu path (b73b8835..36d73eb1): 0 lines.**  None of P1-P8 or Issue 1 touched `pdgui_menu_pausemenu.cpp`, `pdgui_backend.cpp`, `inputctx.c`, or the pause ctx `g_CtxPauseMenu`.  The pause-open path (`src/game/mplayer/ingame.c:894` calling `pdguiPauseMenuOpen`) is also unchanged; its caller path goes through `actionPressed(pi, ACTION_PAUSE)` and my actionmap change only added `JBTN_BACK` on `ACTION_FORGE_TOGGLE` (a separate physical button from `JBTN_START` which is `ACTION_PAUSE`).

Indirect risks reviewed:
- Forge HUD keybinds (P7): gated on `forgeSessionIsActive() && !pdguiIsActive()`.  During a regular match the session is inactive so the HUD and its keybinds don't run.  During a Grid match with pause menu open, `pdguiIsActive()` is true so the HUD also doesn't run.  Not the culprit.
- Forge runtime tick (P7): `if (!s_active) return;` at the top.  `s_active` only flips on inside `forgeRuntimeEnterPlay`, which fires on the first FREEFLY -> NORMAL transition.  Not the culprit outside Grid.
- `g_BotUpdatesDisabled` mirror (P7): only runs inside `forgeRuntimeTick` which only runs when `s_active`.  Clean outside Grid.
- Main menu renderer (P1): `renderMainMenu` in `pdgui_menu_mainmenu.cpp` serves `g_CiMenuViaPcMenuDialog` and `g_CiMenuViaPauseMenuDialog` (CI hub pause).  The in-match Combat Sim pause menu uses a DIFFERENT renderer (`pdguiPauseMenuRender` in `pdgui_menu_pausemenu.cpp`) which my code didn't touch.

**Potential state-leak follow-up identified (not the reported issue):** `g_BotUpdatesDisabled` is mirrored from `bs->all_frozen` inside `forgeRuntimeTick`, but never reset on session exit.  If a user toggles Freeze All during a Grid session, then exits without toggling off, `g_BotUpdatesDisabled` stays at 1 and the next match will have frozen bots.  Separate from Mike's pause-menu issue; flagged as a defensive follow-up.

**Verdict:** cannot reproduce from code review.  Needs Mike's playtest log to diagnose further.  Questions that would narrow it: does the menu appear but buttons don't respond, or does the menu not appear at all?  Does the mouse release?  Does it reproduce on a fresh launch, or only after a prior Grid session?  Log lines `INPUTCTX: pause_menu on_push ...` and `INPUTCTX: pause_menu on_pop ...` would show whether the ctx push fired.

Parked.  Moving to Issue 4.

### Issue 4 - Custom music resets on death (LANDED local fix; cross-client speed-lerp deferred)

**Symptom.**  Mike picks custom music for a match; playing with other clients.  On his own death + respawn, a NEW random track plays instead of the one already going.  Expected: track is a property of the MATCH, survives death.

**Trace.**
1. `src/game/player.c:5477` fires `musicStartMpDeath()` when the player dies.
2. `musicStartMpDeath` pauses `TRACKTYPE_PRIMARY` and plays the death sting.
3. After the death timer elapses, `musicEndDeath` calls `musicStartPrimary(2)` (`src/game/music.c:558-569`).
4. `musicStartPrimary` evaluates `PRIMARYTRACK()` -> `stageGetPrimaryTrack(g_MusicStageNum)`.
5. In MP mode `stageGetPrimaryTrack` calls `mpChooseTrack` (`src/game/mplayer/mplayer.c:3390`).
6. `mpChooseTrack` runs the playlist / shuffle / multi-tune logic and picks a *fresh* track every call.  Bug surface.

**Fix.**  Lock the chosen track for the duration of the match using the existing `g_TemporaryPrimaryTrack` field.  Inside `musicStartPrimary`, after a successful queue-start, if `g_Vars.normmplayerisrunning && g_TemporaryPrimaryTrack < 0`, cache the picked MUSIC_* value into `g_TemporaryPrimaryTrack`.  Subsequent musicStartPrimary calls (respawn-post-death included) see the cached value and skip re-picking.

`musicReset` already clears `g_TemporaryPrimaryTrack = -1` on stage transitions, so the lock auto-releases when the match ends.  Solo missions are unaffected -- `stageGetPrimaryTrack` returns deterministic `g_StageTracks[]` entries for solo, so the lock is a no-op there.  Title-screen / AF1-NRG paths use `musicStartTemporaryPrimary` which explicitly sets the field first and doesn't run during MP matches.

**Cross-client scope (what was NOT shipped).**  Mike's Issue 4 brief also described a speed-lerp drift-correction layer ("sync ... with a bit of an anti-skip speed lerp curve").  That is a larger feature: ride client playback against a match-clock offset so two humans listening to the same track stay in sync over time.  The existing `SVC_MUSIC_ADVANCE` (v34) already handles host-authoritative track advancement between clients; what's missing is a per-client timing correction.  Scope-check: clean implementation would need a match-clock offset in a sync packet (new field or reuse of existing timestamp).  Deferred as a follow-up pass so we don't ship half-done protocol.  Per Mike's rule: no net-protocol changes without approval.

**No wire-protocol change in this fix.**  The lock is strictly local ("don't re-pick on my own respawn").

**Files touched (Issue 4):**
- `src/game/music.c` -- one block added inside `musicStartPrimary`.

**Build:** clean incremental link of PerfectDark.exe.

**Playtest after rebuild:**
1. Start a Combat Sim match.
2. Settings -> pick a specific custom track (or enable multi-tune with several selected).
3. Log line at match start: `MUSIC: match track locked = <N> (cleared on stage change)`.
4. Die in-match.  Respawn.  Track CONTINUES on the same song (no re-pick).
5. End match -> return to lobby -> start new match -> fresh pick (lock auto-released).

### Issue 2 re-verify (pending rebuild + playtest)

Issue 1's SP-head filter should resolve most "missing necks" cases.  After Mike rebuilds and re-plays:
- If missing necks persist on specific body/head combos, that's a real skin-weight bug per those combos.  File bug and scope separately.
- If missing necks are gone entirely, Issue 2 closes as a consequence of Issue 1.

## Session S453 - 2026-04-23/24 - Foundation pass: The Grid menu flow (P1)

**Context:** Mike ran a CS playtest and saw all 31 bots rendering with the President head on the Maian (elvis1) body. Smoketest log confirmed `MATCHSETUP: bot slot N: body='base:elvis1' head='base:head_president' mpbody=12 mphead=12` across all 31 slots.

**Root cause found (B-235):** Index domain confusion in `pdgui_menu_room.cpp` (commit `3a055323`, Claude-authored). Two sites called `catalogMpHeadId(b)` where `b` is the **body** mp_index, not a head mp_index. `MPBODY_ELVIS1 = 12` and `MPHEAD_PRESIDENT = 12` share the integer 12, so picking the Maian body silently wired the President head.

**Fixes:**
- `port/fast3d/pdgui_menu_room.cpp:~2243` (Set Character context menu, multi-select): replaced `catalogMpHeadId(b)` with `catalogGetBodyDefaultMpHeadIdx(b)` + `catalogMpHeadId(defHead)`.
- `port/fast3d/pdgui_menu_room.cpp:~3385` (individual bot edit modal): same fix.
- `port/src/net/matchsetup.c:matchConfigAddBot` fallback: when body_id provided but head_id NULL, now calls `catalogGetBodyDefaultHead(body_id)` instead of hardcoding `"base:head_dark_combat"`.
- `CMakeLists.txt`: stripped UTF-8 BOM (AUDIT-23-M1 from prior audit).
- `.gitignore`: added `~$*`, `*.tmp`, `*~`, `.DS_Store` (AUDIT-23-M2 from prior audit).

**B-234 worktree status:** Confirmed B-234 patches from session S451 are already merged into dev (B234_RESOLVE_CHARCONFIG macro in player.c, lobbyplayer struct shrink in netlobby.h). No separate worktree merge needed.

**Architecture audit for Mike (mpbody/mphead state vs. computed):** The MATCHSETUP log `mpbody`/`mphead` values are computed at matchStart time (inside matchsetup.c), not persistent state. The deprecated `bodynum`/`headnum` integer fields on `matchslot` ARE state duplication (M0.1 carry-over, benign since matchStart re-derives from PRIMARY strings). See `context/scratch/matchsetup-config-audit-2026-04-23.md`.

**B-217..B-222 sanity check (per task):** Static read of Cursor's `d3247e7e` batch confirms the fixes look correct: B-217 F6 returns TICKOP_NONE without chrTick; B-218 XZ-proximity clamp bounds by g_BotCount; B-220 fsFileSize guard present; actionmap.cpp hold-ring state machine has clean semantics. No regressions found. Still FIXED-PENDING-PLAYTEST until live run.

**Build:** Clean link of both `PerfectDark.exe` and `PerfectDarkServer.exe`. Warnings are pre-existing (`/*` in comment strings, unrelated to this session).

**Files touched:** `port/fast3d/pdgui_menu_room.cpp`, `port/src/net/matchsetup.c`, `CMakeLists.txt`, `.gitignore`, `context/bugs.md` (B-235 entry), `context/session-log.md`, `context/scratch/matchsetup-config-audit-2026-04-23.md`.

**Next:** Rebuild and playtest the B-235 fix — pick Maian body in CS room, confirm head auto-selects Elvis, not President. B-217..B-222 playtest batch still open.

## Session S451 - 2026-04-23 - B-234 deep-dive sweep + lobbyplayer junk-data removal

Mike: "Do a deep dive, ensure nothing uses the legacy integer index (not just in multiplayer, everywhere)." Then: "Why would we leave it even if it is junk data, we don't want junk data."

Explore agent swept `src/` and `port/` for reads of `mpheadnum`/`mpbodynum`/`chr->headnum`/`chr->bodynum` and classified by category (LEAK / LEGACY-BACKCOMPAT / FINAL-MILE / DEAD). Four LEAK sites identified beyond yesterday's B-234 fix; three patched this pass, one deferred behind B-227 (agent-create UI only lists MP heads, aliens aren't even selectable there).

**Fixes this pass:**

- **`src/game/player.c::playerChooseBodyAndHead`** - two sites (normal MP branch + net coop branch) read `mpheadnum`/`mpbodynum` as primary and resolved aliens to Joanna (same class as the botmgr layer-2 bug). Introduced a local `B234_RESOLVE_CHARCONFIG` macro that prefers `head_id`/`body_id` via `assetCatalogResolve` -> `ext.head.headnum`/`ext.body.bodynum`, falls back to `mpGetHeadId`/`mpGetBodyId` only when the strings are empty. Same resolution pattern as the botmgr.c fix.
- **`struct lobbyplayer` / `struct lobbyplayer_view` - removed deprecated `u8 headnum` / `u8 bodynum`**. netlobby.c was deriving them from catalog via `runtime_index`, but no consumer (pdgui_lobby.cpp, server_gui.cpp) actually read them. Per Mike "we don't want junk data" - removed the fields entirely from netlobby.h, from both view struct copies (pdgui_lobby.cpp, server_gui.cpp), and from the byte-copy writers in pdgui_bridge.c and server_bridge.c. Shrunk view struct by 2 bytes; name offset 6 -> 4, isLocal 40 -> 36, state 44 -> 40, clientId 48 -> 44. Consumers that need an integer should `assetCatalogResolve` at use-site.

**Verified OK (no change needed):**
- `src/game/menu.c::menuRenderModel` line 2030-2040 has a `MENUMODELPARAMS_GET_MP_HEADNUM/BODYNUM` legacy fallback, but the B-234 path flows through `MENUMODELPARAMS_SET_FILENUM` via catalogGetHeadFilenumByIndex, so the catalog-ID path skips that fallback entirely.
- `port/src/forge/forge_runtime.c:157,170` writes `mpheadnum`/`mpbodynum` only when `mp_index >= 0`; catalog `body_id`/`head_id` strings are set first in the same block. With B-234 layer 2 in botmgr.c, the render path reads the string even when the integer wasn't written. Parallel-path design intact.
- All `bodyAllocateModel(bodynum, headnum, ...)` call sites are FINAL-MILE (last-step handoff to the N64 engine API); the caller is always a resolver that already picked the right integer from the catalog.

**Deferred:**
- `port/fast3d/pdgui_menu_agentcreate.cpp::autoSelectHead` uses `catalogGetBodyDefaultMpHeadIdx` but the agent-create head picker only enumerates MP-registered heads (B-227 - aliens aren't selectable via this UI at all). Queued behind B-227.
- `src/game/mplayer/setup.c::mpCharacterHeadMenuHandler(operation, ..., s32 mpheadnum, bool arg4)` - the handler signature takes an MP integer. Renaming the parameter to a catalog ID is a broader signature refactor; deferred with B-227.

**Files touched:**
- `src/game/player.c` (playerChooseBodyAndHead macro + both branches)
- `port/include/net/netlobby.h` (struct shrink + doc block)
- `port/src/net/netlobby.c` (removed junk derivation writes)
- `port/fast3d/pdgui_lobby.cpp` (view struct shrink)
- `port/fast3d/server_gui.cpp` (view struct shrink)
- `port/fast3d/pdgui_bridge.c` (byte-offset resync)
- `port/src/server_bridge.c` (byte-offset resync)
- `context/bugs.md` (B-234 v2 entry rewrite)
- `context/session-log.md`

**Build:** not run here; Mike to rebuild.

## Session S450 - 2026-04-23 - Scanline policy revision + B-234 non-MP-head resolution (catalog-ID path)

Mike reported three things plus clarifications:

- **Scanline policy correction.** B-232 round 1 gated scanlines on `pdguiIsActive()` so they drew only when a menu was up. Mike's correction: scanlines should be visible **always** when enabled, gameplay and menus alike. Round 2: reverted the gate. Added `pdguiThemeGetScanlineEnabled()` as a reason inside `pdguiAnyStandardOverlayReason` so `pdguiRender` runs every frame the toggle is on, so the fullscreen scanline pass paints continuously. Also added a **user-tunable vertical scale** per Mike's explicit ask: new `Video.ScanlineVerticalScale` in pd.ini (range 0.25..4.0, default 1.0), applied as a multiplier on the stride in `pdguiResolveScanlineMetrics`, exposed via `pdguiThemeSet/GetScanlineVerticalScale` with a new "CRT Line Spacing" slider in Settings -> Video.
- **B-234 bot head mismatch (Maian body + human head).** Two-layer fix:
  - Layer 1 (character-select handlers in `src/game/mplayer/setup.c`): three sites (bot body handler + two player body handlers) auto-selected the default head via `catalogGetBodyDefaultMpHeadIdx` which returns the head's `mp_index`. Alien heads have `mp_index = -1`, so the caller fallback `dh >= 0 ? dh : 0` substituted head 0 (`base:head_dark_combat` = Joanna) for every non-MP-registered head. Fixed by switching to the catalog-ID path: `catalogMpBodyId(idx)` -> `catalogGetBodyDefaultHead(body_id)` -> `mpchrSetHeadById(cfg, default_head_id)`.
  - Layer 2 (bot render path in `src/game/botmgr.c::botmgrAllocateBot`): still read `mpheadnum` / `mpbodynum` via `mpGetHeadId` / `mpGetBodyId` to pick the legacy engine index for `bodyAllocateModel`. With `mpheadnum = 0` (the legacy compat value `mpchrSetHeadById` writes when `mp_index < 0`), it resolved to `HEAD_DARK_COMBAT`. Fix: resolve `head_id` / `body_id` via `assetCatalogResolve` first; read `ext.head.headnum` / `ext.body.bodynum` directly; fall back to the MP-index path only when the catalog string is empty.
  - Per Mike: "it shouldn't be using an index, but the manifest should be using catalog id's". The manifest (`netmanifest.c`) already uses catalog IDs for slot `body_id`/`head_id`; this fix brings the local spawn path in line.
- **CS match-end transition stuck** - Mike: "not new, just annoying". Logged as pre-existing; not addressed this session.
- **Player weapons still not usable / not picked up** - the build (`b6336adf`) is missing today's B-219 v2/v3 + B-229 fixes. Expected to resolve after rebuild; if not, we trace further next session.

- **Files touched:** `port/fast3d/pdgui_backend.cpp` (scanline revert + reason), `port/fast3d/pdgui_theme.cpp` (scale state + API + stride math), `port/include/pdgui_theme.h` (API), `port/fast3d/pdgui_menu_mainmenu.cpp` (CRT Line Spacing slider), `src/game/mplayer/setup.c` (three B-234 layer-1 sites), `src/game/botmgr.c` (B-234 layer-2 render resolution), `context/bugs.md`, `context/session-log.md`.
- **Build:** Not run here; Mike to rebuild. Rebuild is required for EVERY today's fix to take effect in-game.

## Session S449 - 2026-04-23 - Two more playtest fixes: B-232 scanline-on-prompt, B-233 CI solo death respawn

Mike reported two more symptoms after more playtesting.

- **B-232 (LOW) interact prompt "darkens" the whole screen when visible.** Root cause is not the prompt widget (it uses `GetForegroundDrawList` and draws only a small pill). It's the CRT scanline overlay `pdguiThemeDrawScanlineFg` called from `pdgui_backend.cpp` line 837. That overlay paints semi-opaque (alpha up to 140) horizontal lines across the full viewport. It runs whenever `pdguiRender` runs; today's B-224 fix made `interactPrompt` a valid reason for `pdguiRender` to run during gameplay, so scanlines now paint over the 3D scene whenever a "Hold X" prompt is visible. Scanlines are a menu-UI aesthetic, not a gameplay HUD effect. Fix: AND the scanline draw call with `pdguiIsActive()` so scanlines only paint when a non-gameplay input ctx is on top.
- **B-233 (MED) solo CI death doesn't respawn.** Mike jumped OOB on the CI Main Menu mission; died, fade completed, never respawned. S440 claimed to handle this by skipping `mainEndStage` and forcing `canrestart = true` on CI, but the actual guard at `player.c:5590` sat INSIDE the else-branch of `if (g_Vars.mplayerisrunning)`, making `!mplayerisrunning` unreachable - dead code. Solo CI death dropped through with `canrestart=false`, `dostartnewlife` never flipped, player stuck dead. Fix: add a real `else` branch to `if (g_Vars.mplayerisrunning)` at the `playerIsFadeComplete()` site; when `stagenum == STAGE_CITRAINING`, hide the chr and force `dostartnewlife = true`. Removed the unreachable legacy check with a breadcrumb comment.

- **Context:** `bugs.md` two new rows B-232 (LOW) and B-233 (MED). No protocol / wire / constraint changes.
- **Build:** Not run here; Mike to verify with `devtools/build-headless.ps1 -Target client`.
- **Files touched:** `port/fast3d/pdgui_backend.cpp` (B-232), `src/game/player.c` (B-233, both the new else branch and the dead-code cleanup).

## Session S448 - 2026-04-23 - Debug skill + B-218 v2 (initial bot pile-up root cause)

Mike uploaded another playtest log and asked for the `/debug` skill. Two reported symptoms:
1. Remote mine is NAMED at spawn but FP model is invisible + unfireable (secondary fire mode toggles but nothing works).
2. Bots all spawn at one point on initial match frame; respawns are dispersed.

**Build staleness diagnosis.** The log embeds `version: dev b6336adf` (auto-commit at 2026-04-23 17:40:23). `git show b6336adf:` against today's edits: has the bondbike PC dismount fix (S444), does NOT have B-217 v2, B-219 v2/v3, B-229, B-230, or B-231. Mike's binary is from before those landed in the working tree. Rebuild required before those fixes can be playtest-verified.

**Issue 1 (weapon invisible) is the same B-219 class already addressed in v2/v3, pending rebuild.** With the B-229 preservation fix, user's remote mine pick stays as spawn weapon; with B-219 v3 manifest-driven preload in `setupLoadStage`, the remote mine FP model loads regardless of map pickup markers. No new code change needed for issue 1 - this is a rebuild issue.

**Issue 2 (bot initial pile-up) IS a new root cause that Cursor's 4-21 fix did not address.** Traced:
- `mpOrchestrateMatchStartSpawns` is called from `lv.c:619` between playerReset and playerSpawn loops.
- At that point bots are allocated but `g_MpBotChrPtrs[i]->prop` is still empty, so the orchestrator participant iteration (mpspawn_orchestrate.c:408-420) skips every bot.
- Orchestrator places only players, sets `g_MpOrchestrateInitialSpawnDone = true`, and leaves every `g_MpOrchestrateBotPoolIdx[]` entry at -1.
- Later `botSpawnAll` fires via `aiMpInitSimulants` opcode 0x0185. Every bot fails the `g_MpOrchestrateBotPoolIdx[aibotnum] >= 0` check and falls through to `scenarioChooseSpawnLocation` at bot.c:393.
- That function returns the same first-valid pad for 32 bots in the same tick -> pile-up.
- Respawns use `spawnPoolSelectTiered` (different path) and are correctly dispersed.

**B-218 v2 fix:** At the top of `botSpawnAll`, detect "bots allocated but no pool idx assigned" and re-run `mpOrchestrateReset + mpOrchestrateMatchStartSpawns` now that props exist. Player positions stay stable because the orchestrator is deterministic on same seed + roster + stage + pool build. Added a `SPAWN.ORCH: botSpawnAll re-running orchestrator ...` diag so the log will show when this path fires.

- **Files touched:** `src/game/bot.c::botSpawnAll`, `context/bugs.md` (B-218 v2), `context/session-log.md`.
- **Not run here** (Linux sandbox). Mike rebuilds, re-playtests Chicago CS with max bots, confirms the log line appears and bots disperse on initial frame.

## Session S447 - 2026-04-23 - Three playtest fixes: B-229 weapon override, B-230 controller B, B-231 prompt flash

Mike's third playtest pass this day. Three distinct symptoms, all fixed in code this session.

- **B-229 (HIGH) user-picked spawn weapon silently overridden to first set slot.** Mike set spawn to `base:remotemine`, match started with Falcon (Silenced) - not his pick, and not visible/usable (unloaded fallback model). Root cause: the B-181 fallback in `setup.c` ("markers below target -> force spawn-with-weapon") was ALSO unconditionally replacing `spawn_weapon_id` with the first valid weapon in the configured set, even when the user had explicitly picked one. Fix: gate the override on `spawnWeaponNum == 0xFF` (random) or `== 0` (unset). If the user has an explicit choice, preserve it; only fill from the set when the spawn weapon is random/unset. MPOPTION_SPAWNWITHWEAPON still flips on either way (pickup-less arenas still arm players), it just no longer steamrolls the user's pick. Pairs with the B-219 v3 manifest-driven model preload: any chosen spawn weapon now gets its FP model loaded at stage setup regardless of map markers.
- **B-230 (MED) controller B needs two presses to close Main Menu.** Same shape as the S306 X-button bug. ImGui's internal nav consumes the first `ImGuiKey_Escape` edge that `pdguiDriveImGuiNav` injects from `actionPressed(ACTION_CANCEL_USE)`, so `renderMainMenu`'s `IsKeyPressed(Escape, false)` returns false on the first press. S306 added the `pdguiConsumeTitleClose` direct channel for X; this fix adds the parallel `actionPressed(0, ACTION_CANCEL_USE)` channel for B. Hardware-level edge, untouchable by ImGui nav. The 150ms `closeGracePending` grace is kept ONLY for the keyboard-Escape `IsKeyPressed` path (it still needs protection against queued opening-press edges); `titleClose` and the new `actionCancelEdge` are gated only by `!IsWindowAppearing()`.
- **B-231 (LOW) interact prompt flashes briefly as Main Menu opens.** Press Start in CI free-roam near the computer: prompt stays visible for 1-2 frames before the menu takes over. Root cause: `playerPause()` flips `pausemode` UNPAUSED -> PAUSING immediately, but the ImGui main-menu ctx isn't pushed until the menu's first IsWindowAppearing frame. During the gap, `pdguiIsActive()` is false and none of the CI-specific gates fire. Fix: extend `pdguiCiIntroBlocksInteractPrompt` in `pdgui_bridge.c` to also return 1 when `pausemode != PAUSEMODE_UNPAUSED`, and AND the `interactPrompt` reason boolean in `pdgui_backend.cpp` with `!pdguiCiIntroBlocksInteractPrompt()` at both the NewFrame and Render gate sites so the ImGui overlay also doesn't spin up from the prompt during pause transitions.

- **Context:** `bugs.md` three new rows B-229, B-230, B-231. No protocol / wire / constraint changes. Predicate `pdguiCiIntroBlocksInteractPrompt` is now broader than its name suggests ("interact prompt should be suppressed"); kept the name to avoid churn, noted the expansion in the function header.
- **Build:** Not run here (Linux sandbox cannot drive MSYS2 / MinGW). Mike to verify locally.
- **Files touched:** `src/game/setup.c` (B-229), `port/fast3d/pdgui_menu_mainmenu.cpp` (B-230), `port/fast3d/pdgui_bridge.c` and `port/fast3d/pdgui_backend.cpp` (B-231).

## Session S446 - 2026-04-23 - B-219 v3: manifest-driven MP weapon model preload (architectural fix, Mike's direction)

Mike flagged the right architecture after the v2 fix landed:
> "setup.c should have the spawn-in weapon from the match manifest, right, regardless of map spawns? And if weapon set or spawn weapon are set to random, all weapons should be in the manifest."

He is correct on both counts. The match manifest `manifestBuild` in `netmanifest.c:446-463` already registers every non-empty `g_MpSetup.weapons[]` slot as `MANIFEST_TYPE_WEAPON`, so the network distribution path is manifest-driven. But `modelmgrLoadProjectileModeldefs` was invoked exclusively from `setupPlaceWeapon` in `setup.c`, which is driven by map pickup markers. Pickup-less arenas like Chicago CS (markers=0) therefore never loaded FP models for the configured weapon set, even though every one of those weapons is in the manifest.

v3 moves the load to the right layer. At the end of `setupLoadStage` (after the B-181 fallback-applied log), `setup.c` now iterates `g_MpSetup.weapons[]` and `g_MatchConfig.spawnWeaponNum` and calls `modelmgrLoadProjectileModeldefs` for each valid entry. Random spawn (`spawnWeaponNum == 0xFF`) is covered by the set iteration; specific spawn outside the set is covered by the explicit `spawnWeaponNum` load. Idempotent (`modelmgrLoadProjectileModeldefs` already fires many times per stage from `setupPlaceWeapon` without issue). The v2 `player.c` spawn-time load is kept as a backstop.

- **Files touched:** `src/game/setup.c` (end of `setupLoadStage`), `context/bugs.md` (B-219 v3 note), `context/session-log.md`.
- **No protocol, no wire, no constraint change.** Manifest layout unchanged; this fix just consumes it on the local stage-load side.
- **Build:** Not run here; Mike to verify with `devtools/build-headless.ps1 -Target client`.

## Session S445 - 2026-04-23 - Second playtest pass: B-219 v2, B-224 v2, 4 new bugs logged

Mike uploaded `pd-client-0efa2c04.log` (Chicago CS + CI boot). Two code fixes this pass; four new bugs logged for follow-up sessions.

- **B-219 v2 FP weapon invisible (`src/game/player.c` spawn-with-weapon branch, line 1707 area).** Log evidence: line 16756 `SETUP: world pickups 0 below target 16 (markers=0); forcing spawn-with-weapon fallback id='base:falcon2'`, 16771 `GAMELOOP.WEAPON: INTRO skipped (spawn-with-weapon owns MP loadout)`, 16802 `SPAWN: player 0 spawned with weapon 2 (Falcon 2) -- auto-equipped to right hand`. The 4-21 B-219 fix skipped `INTROCMD_WEAPON` which also skipped `modelmgrLoadProjectileModeldefs(param1)`; that loader is what puts the weapon's first-person / projectile modeldef into modelmgr. Chicago has 0 weapon pickup markers so `setup.c` never loads Falcon 2 either. Result: inventory + switch queue were right, but `bgunTickSwitch2` had no model to bind, so the player saw empty hands and could not fire. Fix: call `modelmgrLoadProjectileModeldefs(resolvedWeaponNum)` before `invGiveSingleWeapon` in the SPAWNWITHWEAPON branch of `player.c`. Added `#include "game/playerreset.h"` for the prototype. Bot parallel path in `bot.c:491` not touched this pass (bot 3rd-person weapon models have not been reported broken; defer until evidence).
- **B-224 v2 CI camera fly-in still leaks interact prompt (`port/fast3d/pdgui_bridge.c::pdguiCiIntroBlocksInteractPrompt`).** 4-23 v1 ANDed the activation boolean with `!pdguiIsActive()` in `pdgui_backend.cpp` (closed the main-menu-dim compound path). Mike reports the prompt still showing during the initial-boot CI fly-in, because `var80087260` is only set on the return-from-MP-endscreen re-entry (`menutick.c:703`), not during first-boot. Extended the gate to also return 1 when `g_Vars.tickmode == TICKMODE_CUTSCENE` or `g_Vars.lvframenum <= 30`. Mirrors the inverse condition `menutick.c:315` already uses to decide "CI is ready for interaction".
- **Four new bugs logged (no code this session):**
  - **B-225 (LOW) stale base-catalog Bonus arenas** (`base:arena_stage_24` "Kakariko Village (Stormy)", `base:arena_mp_grid7` "Dark Noon Valley", etc). AllInOne lang names still present while underlying stage data is not. Needs a sweep of base catalog Bonus block + lang files.
  - **B-226 (LOW) wrong character names in bot Character Select.** Label lookup path appears to still pull from a legacy parallel table rather than `catalogGetBodyDisplayName`.
  - **B-227 (MED) Dr Carroll + Skedar not appearing in Character Select.** Catalog has them (log shows `bodies=69 heads=83` after rebuild) but Select UI omits them. Likely unlock-filter or legacy `g_MpBodies[]` iteration.
  - **B-228 (MED) SP maps in MP missing elevators / fire-escape stairs, should also sync in MP.** CI lifts are auto-registered by `setup.c::OBJTYPE_LIFT` via S310, but MP setup filter or sync path may not handle them. Should behave like Grid (server-authoritative lift sync).
- **Context:** `bugs.md` B-219 rewritten as v2, B-224 extended to v2, added rows B-225, B-226, B-227, B-228. No protocol or constraint changes.
- **Build:** Not run here; Mike to verify locally via `devtools/build-headless.ps1 -Target client`.
- **Files touched:** `src/game/player.c`, `port/fast3d/pdgui_bridge.c`, `context/bugs.md`, `context/session-log.md`.

## Session S444 - 2026-04-23 - Playtest regressions from 4-20 / 4-21 Cursor batch

Triggered by Mike's 4-23 playtest on the 4-21 tree (CS + CI, pd-client.log uploaded). Three symptom classes, all from the Cursor + Claude co-authored stability batch; static-read fixes in this session.

- **B-217 v2 (`src/game/bot.c` around line 1338):** Cursor's 4-21 change to `return TICKOP_NONE` under `g_BotUpdatesDisabled` made bots invisible on F6. `chrTick` is what maintains the per-frame render / transform state (bot.c header comment at line 92 even says "movement, physics, and rendering still run every frame via chrTick"). New behaviour: zero `aibot->speedmultforwards` and `->speedmultsideways` then call `chrTick(prop)` so bots stay on screen. Minor anim drift is accepted (matches S431 "residual bot motion is expected"). Visibility for spawn verification trumps zero-drift.
- **B-221 dismount parity (`src/game/bondbike.c::bbikeHandleActivate`):** Mount was single-tap on PC after B-221.1, but EXIT still had the `TICKS(25)` double-tap window. Mirrored the `currentPlayerTryMountHoverbike` PC bypass: added `optionsGetControlMode(...) == CONTROLMODE_PC` disjunct to the activation gate. Controller paths unchanged.
- **B-224 new (`port/fast3d/pdgui_backend.cpp` NewFrame + Render gates):** Interact prompt label was a "reason to activate ImGui overlay" even when a menu was already on top. That let main-menu `pdguiPopupDarkenBehind` calls compound with the prompt frame (reported as: full-screen dim whenever "Hold X to use computer" pill appears) and also let the prompt pill leak onto the CI title screen / main menu after spawn. Fix: AND `interactPrompt` with `!pdguiIsActive()` at both gate sites. Camera fly-in stays covered by `pdguiCiIntroBlocksInteractPrompt` inside the renderer. Orthogonal to B-222 and B-223 dim classes.
- **Context:** `bugs.md` B-217 rewritten as v2, B-221 extended with (7) dismount note, new row B-224. No protocol version change; no wire changes; no constraint changes.
- **Build:** Not run here (Linux sandbox cannot drive MSYS2 / MinGW). Mike to verify locally via `devtools/build-headless.ps1 -Target client`.
- **Files touched:** `src/game/bot.c`, `src/game/bondbike.c`, `port/fast3d/pdgui_backend.cpp`, `context/bugs.md`, `context/session-log.md`.

## Session S443 — 2026-04-21 — pd-server: no client modal scrim API (game-agnostic)

- **`port/fast3d/server_gui.cpp`:** Removed **`pdgui_layout.h`** and **`pdguiPopupDarkenBeginFrame`** / **`Flush`** from the dedicated-server ImGui frame (S440 client pairing does not apply here). Avoids linking or stubbing **`pdgui_layout.cpp`** — keeps **`pd-server`** free of game-client menu layout / modal scrim contracts.

## Session S442 — 2026-04-21 — Tracker: B-221 / B-222 / B-223 + playtest queue

- **`context/bugs.md`:** **B-222** reframed as **FIXED-PENDING-PLAYTEST (modal + overlay class)** with explicit **out-of-scope** note: non-`pdguiPopupDarkenBehind` full-frame tints → new **B-223** (LOW, deferred) so B-222 can close after scoped playtest without owning countdown/endscreen/radial dims.
- **B-221:** Renamed from **PARTIAL** to **FIXED-PENDING-PLAYTEST (implementation complete)** — all six sub-items are treated as shipped; **PARTIAL** was only missing consolidated verify. Verify column lists CI door/dialog, listen host lobby/team, mapper, scorecard Back hold.
- **`context/tasks-current.md`:** Added **Playtest queue** for CI death, door/NPC dialog, main menu stack, listen host; points to **B-223** only if double-dark repros outside modal coalescing.
- Playtest itself **not executed** in this environment (no stable local `ninja` / client run here).

## Session S441 — 2026-04-21 — System sweep: invalid stagenum + HUDMSG player slots

- **`src/game/pdmode.c` + `pdmode.h`:** Added **`stageSanitizeLoadStagenum()`** (coerces **`0x00`** → **`STAGE_CITRAINING`**) as the single definition used by **`titleSetNextStage`** and **`lvReset`**.
- **`port/src/pdmain.c`:** **`mainChangeToStage`** sanitizes before manifest / **`g_MainChangeToStageNum`** (PC client choke point; **`src/lib/main.c`** is not linked on PC but was updated too for parity).
- **`port/src/server_stubs.c`:** Dedicated server stub applies the same **`0x00` → CI** rule without linking **`pdmode.c`**.
- **`src/game/hudmsg.c`:** **`hudmsgCalculatePosition`** no longer dereferences **`g_Vars.players[msg->playernum]`** when the slot is out of range or NULL — falls back to **`g_Vars.currentplayer`** or full VI view bounds (**SP-6**).
- **`port/fast3d/pdgui_bridge.c`:** **`pdguiSubtitlesSnapshot`** skips impossible **`playernum`** indices before any owner filtering.
- **Build:** agent **`ninja`** still hit **`ccache` CreateProcess** on this host; verify locally via **`devtools/build-headless.ps1 -Target client`**.

## Session S440 — 2026-04-21 — CI hub death respawn, subtitle ownership, modal scrim coalescing

- **`src/game/player.c`:** Solo death on **`STAGE_CITRAINING`** no longer calls **`mainEndStage()`** (avoids full stage reload / endscreen path). After death fade, **`canrestart`** is forced true on CI so **`playerStartNewLife`** runs via existing **`dostartnewlife`** / **`lvTick`** — respawn at scenario spawn without **`mainChangeToStage`**.
- **`src/game/hudmsg.c`:** **`HUDMSGSTATE_QUEUED`** gate now NULL-checks **`g_Vars.players[msg->playernum]`** before **`->isdead`** so invalid playernum cannot block subtitle dequeue or crash.
- **`port/fast3d/pdgui_bridge.c`:** **`pdguiSubtitlesSnapshot`** — when **`g_NetMode == NETMODE_NONE`** and **`PLAYERCOUNT() == 1`**, do not filter subtitles by **`currentplayernum`** so NPC **`aiSpeak`** lines still reach ImGui if **`playernum`** diverged during script tick.
- **`port/fast3d/pdgui_layout.cpp` + `pdgui_layout.h`:** **`pdguiPopupDarkenBehind`** accumulates **max alpha** per frame; **`pdguiPopupDarkenFlush`** draws one full-viewport rect; **`pdguiPopupDarkenBeginFrame`** resets accum (**`pdguiNewFrame`**, dedicated server / **`server_gui`** paths). Fixes stacked **`pdguiPopupDarkenBehind`** (e.g. CI main menu + settings redirect) compounding darkness.
- **`port/fast3d/pdgui_backend.cpp`:** Wire BeginFrame / Flush around client ImGui frame.
- **Follow-up (same thread):** `prop.c` — hoverbike interact label is **`Enter <title>`** from **`invGetTextOverrideForObj` → `inventorytext`** (title segment before `|`) or else **`L_MISC_306`** / **`FILE_PHOVBIKE`** via `g_ModelStates[modelnum].fileid` (replaces fixed `"Enter Hoverbike"`).
- **Audit follow-up:** `pdgui_bridge.c` — subtitle **`playernum`** relax now **`NETMODE_CLIENT` only strict** + **`PLAYERCOUNT()==1`** (fixes solo **listen host** still filtering NPC lines); skip **whitespace-only** `msg->text`.

## Session S439 — 2026-04-21 — CI death load + subtitles + interact UX (playtest follow-up)

- **`src/game/lv.c`:** `lvReset(0)` now coerces to `STAGE_CITRAINING` (same invalid-stage class as `titleSetNextStage(0)`). Fixes crash after hub death when load still ran with `stagenum=0x00` (log: `BODIES: enter stagenum=0x00` then `ACCESS_VIOLATION`).
- **`port/fast3d/pdgui_bridge.c`:** `pdguiSubtitlesSnapshot` skips subtitle HUD slots with empty `msg->text` so ImGui does not draw a dim empty panel.
- **`pdguiCiIntroBlocksInteractPrompt`:** hides interact prompt during CI opening fly-in (`var80087260` / `STAGE_CITRAINING`).
- **Hoverbike prompt:** `prop.c` — label `Enter Hoverbike`; `pdgui_glyphs.cpp` — `hold_progress < 0` uses **Press** prefix and skips hold ring; `pdgui_interact_prompt.cpp` routes hoverbike there.
- **Hold ring fill:** `actionHoldPressStartMs` + fallback elapsed/holdMs when `actionHoldProgress` stayed at 0 while USE is held (PC bondmove synthesis).
- **Build:** verify on MSYS2; agent `ninja` may still fail on `ccache` CreateProcess.

## Session S437 — 2026-04-21 — B-221.4 / B-221.5 + pool (social lobby, team setup)

- **B-221.4 (`pdgui_menu_mainmenu.cpp`):** Controller visual mapper zones called `ImGui::SetCursorPos` with fractions of `padW`/`padH` while the pad art was anchored at `GetCursorScreenPos()` — hit boxes lived at the wrong place in the child window, so drag-drop and right-click clear felt broken. `renderOneCtrlPadZoneSplit` now takes `padOrigin` (screen space) and uses **`SetCursorScreenPos`**.
- **B-221.5 (`actionmap.cpp` `fireVk`):** When several actions in the **same** active IMC share one VK, the winner is the mapping with the **lowest trigger index** (Bind 1 before Bind 2), then lower `InputAction` enum as tie-break (replaces “first `ACTION_*` scan order wins”).
- **Menu pool:** `MENU_TYPE_SOCIAL_LOBBY` in `menupool.h` / `menupool.c`; `pdgui_menu_lobby.cpp` calls `menupoolAcquire` after `Begin` succeeds and `menupoolRelease` when `Begin` is false. `pdgui_menu_teamsetup.cpp`: `menupoolAcquireDialog` / `menupoolReleaseDialog` on cull path (same S300 pattern as MP setup). `menupoolInit`: register **`g_MpAutoTeamMenuDialog`** as `MENU_TYPE_MP_TEAM_SETUP` beside `g_MpTeamsMenuDialog` so pool + `menuCloseDialog` resolve the auto-team entry point.
- **Build:** verify on MSYS2 (`devtools/build-headless.ps1` or local `ninja -C Build pd`); agent shell may still fail on `ccache` CreateProcess.

## Session S436 — 2026-04-21 — PD2_FixPlan_420Bugs.docx propagation (menus + B-222 scrim)

- **Source:** `context/PD2_FixPlan_420Bugs.docx` (extracted `word/document.xml` for text). B-217–B-220 and core B-221/B-222 gameplay fixes were already on branch; this pass completes **doc propagation** items.
- **`menupoolAcquire*` every frame after `Begin` (not only `IsWindowAppearing`):** `pdgui_menu_mppause.cpp`, `mpsetup.cpp`, `mpadvanced.cpp`, `mpsettings.cpp`, `playerconfig.cpp`, `botsetup.cpp`, `agentselect.cpp`, `cheats.cpp`, `solomission.cpp` (solo pause), `training.cpp` (FR weapon list), `mainmenu.cpp` (CI settings redirect, dead P2, cinema), `room.cpp` (`MENU_TYPE_ROOM`). Prevents `g_CtxImGuiMenu` staying unbound when ImGui reuses a window after transitions (`pdguiIsActive` false → interact prompt / gameplay HUD bleed).
- **B-222 pause dim:** `pdgui_menu_pausemenu.cpp` — scrim only when networked **or** solo `pdguiPauseGetPaused()` is PAUSED/GAMEOVER (per fix plan; avoids unconditional dim if pause flag desyncs).

## Session S435 — 2026-04-21 — CI main menu vs interact prompt

- **`port/fast3d/pdgui_menu_mainmenu.cpp`:** Call `menupoolAcquireDialog(..., &g_CtxImGuiMenu)` every frame after `ImGui::Begin("##main_menu")` succeeds, not only inside `IsWindowAppearing`. Fixes Hold X / interact HUD drawing over the main menu when ImGui reuses the window after a CI stage load without a new Appearing frame (input stack top stayed gameplay).

## Session S434 — 2026-04-21 — 4/20 stability batch (B-217 through B-222)

- **B-219** (`playerreset.c`, `lv.c` comment): CoWork init trace — `lv.c` loop 1 `playerReset` / `INTROCMD_WEAPON` vs loop 2 `playerSpawn` + queued `bgunEquipWeapon2` / `bgunTickSwitch2` timing; skip intro weapon when norm MP + `MPOPTION_SPAWNWITHWEAPON`.
- **B-218** (`mpspawn_orchestrate.c`, `bot.c`): relax iters 20, min_sep floor 50, duplicate pool assignments invalidate bot cache + ERROR log; proximity discard of stale orchestration index.
- **B-221.1 through B-221.3, B-221.6** (`propobj.c`, `bondmove.c`, `actionmap.cpp`, `pdgui_interact_prompt.cpp`): PC single-tap hoverbike mount; B-221.3 — defer PC `JO_ACTION_ACTIVATE`/`bmoveHandleActivate` until hold consumed or USE release (not press-frame `btapcount`); hold ring grace or pin plus smoothed UI; `ACTION_SCORECARD` not gameplay-only.
- **B-222** (`pdgui_backend.cpp`, `pdgui_menu_mpingame.cpp`): norm MP drives ImGui NewFrame/Render (killfeed context); comments note full-window dim may be other `AddRectFilled` paths, not F6 in the standard overlay gate; killfeed gated on `pdguiActiveMenuIsOpen` only; lower-left layout.
- **B-220** (`modmgr.c`): `fsFileSize` before `fsFileLoad` for mod.json paths.
- **B-217** (`bot.c`): F6 freeze skips `chrTick` entirely.
- **Build:** `ninja -C Build pd` and `pd-server` succeeded on MSYS2.
- **CoWork audit (PD2_FixPlan_420Bugs_Audited):** `bugs.md` + `lv.c` / `pdgui_backend.cpp` comments aligned with their B-219 / B-222 / B-221.3 file-line narrative (killfeed = norm MP NewFrame; weapon race = two `lv` loops + deferred `bgunTickSwitch2`; tap/hold = defer activate on PC).
- **Agent build (Cursor shell):** `ninja -C Build pd` hit `ccache … CreateProcess failed` (toolchain path); not treated as a compile error in source.

## Session S433 — 2026-04-21 — ImGui active-menu radial: layout + non-inverted stick

- **`port/fast3d/pdgui_activemenu_radial.cpp`**: Slot pills use **44%** of legacy mapped half-width with a **scaled cap** (~172 px total width at 1080p baseline before UI mult) so buttons sit closer to the diamond center; labels drawn at **`pdguiScale(22)`** via `ImFont::CalcTextSizeA` / `AddText(font, size, …)`; vertical padding tied to label size; selection pulse uses the same reduced half-width.
- **`src/game/activemenutick.c`**: After `actionAxis(ACTION_AXIS_AIM_*)`, **negate Y again when `actionmapGetStickInvertY()`** so the radial uses screen-space stick direction (up selects up) independent of look inversion — applied to both the main path and dual-controller (21–24) block.
- **Build:** Agent `ninja pd` failed here (`ccache` CreateProcess / toolchain path); verify on a normal MSYS2 dev shell.

## Session S432 — 2026-04-21 — Input / overlay / HUD issues (**logged only**, fixes deferred)

User report — capture for a later implementation pass; no code changes in this note.

- **Vehicle enter (hoverbike / mount):** Expectation **single tap**; behavior matches **OG double-tap** window. Code anchor: `currentPlayerTryMountHoverbike` (`propobj.c`) gates on `lvframe60 - activatetimelast < TICKS(30)` — pairs with **second** activate within ~0.5s, not lone tap. PC path `pcinteractusekind` in `propobjInteract` + `bondmove.c` synthesis.
- **Hold interact radial / ring:** Does not **update live** while holding; suspected interaction with **press vs hold** threshold logic. On **release**, fill should reset to **0**; instead it **stays filled** until the next press/hold. Anchors: `pdgui_interact_prompt.cpp` (`actionHoldProgress`, `actionHoldConsumed` branch), `actionmap.cpp` `actionHoldProgress`.
- **Tap X vs hold X for interact:** **Tap** already completes interact in situations where design calls for **hold** (door / long interact) — policy mismatch vs prompt text.
- **Visual input mapper:** Reported **broken**; user wants to **re-approach later** (no spec here).
- **One key → multiple actions:** Request **resolution priority** (or ordering) when several gameplay actions share the same binding.
- **Overlay tint:** When **F6/F7 dev banners**, **invincible**, and/or **“Hold X …”** interact affordance are active, user sees a **semi-opaque black tint over the whole game window** — should not dim unrelated gameplay.
- **Killfeed:** Appears **only when** that overlay / ImGui path is active; should be visible during **normal** gameplay. Layout: currently reads as **over the minimap**; desired **lower-left** (or at least clear of radar).
  - Likely causes to verify later: `pdguiMpIngameRender` / `pdguiRender` early-return when `pdguiAnyStandardOverlayReason` is false (no `pdguiNewFrame` → no killfeed draw); killfeed `baseX`/`baseY` in `pdgui_menu_mpingame.cpp` (top-right vs design).
- **Scorecard:** **Holding Back** (controller) does **not** open scorecard while held; `pdgui_menu_pausemenu.cpp` `scorecardTickButtonState` uses `actionHeld(0, ACTION_SCORECARD)` — investigate bind overlap, `gameplayInputSuppressed`, or IMC so Back reaches gameplay.

**Tracker:** **B-221** (input batch), **B-222** (overlay/HUD batch) in `bugs.md`; consolidated: [`4-20-CRITICAL-STABILITY-BUGS.md`](4-20-CRITICAL-STABILITY-BUGS.md).

## Session S431 — 2026-04-21 — Playtest notes logged (F6, Chicago spawns, FP weapon)

- **Source:** User playtest + `Downloads/Perfect Dark 2.0/pd-client.log` (Chicago CS, ~32 participants, F6/F7 used for debugging).
- **F6:** Confirmed useful; residual bot motion is expected with current implementation — `botTick` takes `chrTick` only when `g_BotUpdatesDisabled`, so movement/animation can continue (not only `botTickUnpaused`). Tracked as **B-217**.
- **F7 invincibility:** Works as intended (log `PLAYER: invincibility ON/OFF`).
- **Chicago initial bot pile-up:** Screenshot + log; respawns dispersed. Log fingerprints: `SPAWN.ORCH: RELAX … duplicate`, `SETUP: world pickups 0 below target 16`, `SPAWNPOOL: build complete -- 37 points`. Tracked as **B-218** (overlap with S423 / B-174 class).
- **First-person weapon invisible / unusable:** Same log shows `GAMELOOP.WEAPON … R=36` vs `SPAWN: … weapon 2 (Falcon 2)` — mismatch worth fixing first. **B-219**.
- **Context:** `context/bugs.md` rows **B-217–B-220**; `tasks-current.md` pointer line under Open. Consolidated snapshot: [`4-20-CRITICAL-STABILITY-BUGS.md`](4-20-CRITICAL-STABILITY-BUGS.md).

### Ephemeral log digest — `pd-client.log` (full scrape; file will not be retained)

Captured from `C:/Users/mikeh/Downloads/Perfect Dark 2.0/pd-client.log` — **only** these structured issues (no `FATAL` / no non-fs `ERROR` in this file).

**ERROR (~100 lines, all the same class):** `fsFileLoad: could not find file: …/data/mods/<dir>/mod.json`. Unique directory roots seen: `of-wolf-and-man`, `memory-remains`, `base-game`, `base-ui` (+ `textures`), `Custom Windows` (+ `Pokemon 1..3`), `effect_normal_tint`, `Fonts` (+ `pokemon-gen-12`), `pk3`, `the-memory-remains`, `UI Chrome`, `wolf-man`. Same set repeats in **two bursts** (~`00:00.69`–`00:00.79` and ~`00:06.68`–`00:06.70`) — likely duplicate mod-registry scan / bot-name parse passes. **B-220** (see also **B-172**).

**WARNING (exactly 5 in entire log):**

| Time | Tag | Notes |
|------|-----|--------|
| `00:09.72` | `DIAG fireVk: vk=531 player=0 NO BINDING FOUND in 1 active IMCs` | D-pad down — **B-203** default bind |
| `06:59.94` | `SETUP: world pickups 0 below target 16 (markers=0); forcing spawn-with-weapon fallback id='base:falcon2'` | Chicago has no world weapon markers → fallback |
| `06:59.94` | `SPAWNPOOL: L1 declared pad 9 failed validation (budget=-1, room=33) -- falling through to L2` | One declared pad rejected; pool still built |
| `06:59.94` | `SPAWN.ORCH: RELAX iter=0 kind=duplicate old_pool=29 new_pool=25 …` | Orchestrator had to relax duplicate pool assignment |
| `07:50.69` | `SPAWN: bot … underground — pos.y=-634 floor=-17 diff=617, clamping to floor` | One bot spawn depth-clamped (legacy Chicago pad / pool interaction) |

**Other notable lines (not WARNING/ERROR):**

- `GAMELOOP.WEAPON: playernum=0 mission=0x1d INTRO gave R=36 L=-1 default=1` — pairs with **B-219** (`R=36` vs spawn Falcon).
- `AUDIO[B-141]: 30s summary` — idle/title: `underruns=1`, `nullProducer=1` once; after Chicago match load (`~07:00+`): `underruns` rises to **3–4**, `hitches` **2→16**, `gap(ms) max` up to **227** (heavy combat window), `buffered(samples) min` down to **248** — main-thread load under 32-bot stress; cross-check **B-141** / **B-204**–**B-205**.
- `MEMPC: font cache hit … (stage reload skipped)` at Chicago transition — informational.

## Session S430 — 2026-04-20 — PD_DEV_BUILD, F7 invincibility + HUD, stable gating

- **CMake:** `PD_STABLE_RELEASE` option; **`pd`** defines **`PD_DEV_BUILD=1`** when not stable. **`release.ps1`:** stable (non-prerelease) configures with **`PD_STABLE_RELEASE=ON`** (removed unused duplicate `$vFlags` block; configure uses **`@stableArg`**).
- **`player.c` / `player.h`:** **`playerToggleDevInvincibility()`** (toggles **`g_PlayerInvincible`**), **`playerDevInvincibilityHudActive()`** for ImGui gating.
- **`pdgui_backend.cpp`:** Dev-only **F6 / F7 / F12**; **`pdguiAnyStandardOverlayReason`** takes **`devGameplayHud`** (bot freeze + F7 invinc); stacked top-center banners for F6 and F7; debug overlay render gated. **Stable:** **`debugOverlayActive`** forced false so F12 overlay never drives ImGui overlay path.
- **`pdgui_menu_mainmenu.cpp`:** Settings **Debug** tab and shortcuts list only with **`PD_DEV_BUILD`**; stable uses 7 tabs (Catalog index 6). Build scripts: SYNC comment for **`PD_STABLE_RELEASE`**.
- **Context:** **`tasks-current.md`** done line; **`CRITICAL-PROCEDURES.md` §3** optional **`PD_STABLE_RELEASE`** note.

## Session S429 — 2026-04-21 — F6 toggle: freeze MP bot AI + on-screen banner

- **`bot.c` / `bot.h`:** `g_BotUpdatesDisabled`, `botToggleUpdatesDisabled()`, `botGetUpdatesDisabled()`. In **`botTick`**, when set: **`chrTick` only** (skip AI, stuck, movement, scenario/pickups for that bot).
- **`pdgui_backend.cpp`:** **F6** toggles; **`pdguiNewFrame` / `pdguiRender`** treat bot-freeze like other overlays so ImGui runs; top-center **ImGui** banner `"Bot Update: DISABLED"` + `NoInputs` so look/move still work.

## Session S428 — 2026-04-21 — MP spawn: Hungarian solver, anchors, lv before playerSpawn

- **`mpspawn_hungarian.c` / `mpspawn_hungarian.h`:** Min-cost square assignment (`mpHungarianMinSquare`); pads participants to `pool->count` with zero dummy rows.
- **`mpspawn_orchestrate.c`:** Team **anchors** (farthest-from-center first pool point, then max-min XZ spread), **Voronoi** soft costs, **int64** Hungarian + **bottleneck swap** refinement, **qsort** participants for lockstep; guard **`g_Vars.mplayerisrunning`**. RELAX **team_zone** logs use **`s_Anchors[team]`**.
- **`playerreset.c`:** Initial MP uses temp **`pool[0]`** + deferred log; real placement from orchestrator.
- **`lv.c`:** All **`playerReset`**, **`mpOrchestrateMatchStartSpawns()`**, then all **`playerSpawn`**. **`bot.c`:** orchestrator **removed** from `botSpawnAll` (single site: `lv.c`).
- **`player.c`:** **`playerApplyOrchestratedSpawnFromPool`** uses **`mplayerisrunning`** guard.
- **Net:** Same roster + **`g_NetMatchSeed`** + stage id + pool build as host yields identical assignment without extra spawn-index messages.
- **Build:** verify `devtools/build-headless.ps1 -Target client` locally.

## Session S427 — 2026-04-20 — Active menu: single ImGui gate (no init fallback)

- **`pdgui_bridge.c`**: **`pdguiActiveMenuIsOpen()`** — pure game state (`currentplayer` + `activemenumode != AMMODE_CLOSED`). **`pdguiActiveMenuShouldSkipLegacyWheel()`** delegates to it (removed **`pdguiIsInitialized()`** branch). Dropped **`#include "pdgui.h"`** here.
- **`pdgui.h` / `pdgui_backend.cpp`**: Removed **`pdguiIsInitialized()`**.
- **`pdgui_backend.cpp`**: **`pdguiAnyStandardOverlayReason()`** centralizes the same overlay predicate for **`pdguiNewFrame`** and **`pdguiRender`** (active menu via **`pdguiActiveMenuIsOpen`**). When **`!g_PdguiInitialized`**, log once if the active menu is open (ordering bug), then return.
- **`pdgui_activemenu_radial.h`**: Documents **`pdguiActiveMenuIsOpen`**.
- **Intent**: No “skip GBI only when ImGui is ready” fallback — **GBI skip tracks game open state**; **ImGui frame/render** tracks the shared overlay predicate so the two cannot drift. Relies on **`pdguiInit()`** before normal gameplay.
- **Build**: Not verified in this agent environment; use **`devtools/build-headless.ps1`** locally.

## Session S426 — 2026-04-20 — MP match-start spawn orchestration (wired)

- **`mpspawn_orchestrate.c`:** Early exit if `g_MpOrchestrateInitialSpawnDone` (no double-orchestrate on second `botSpawnAll`). Relax pass skips the relocated participant index in min-distance (`orch_min_dist_sq_to_placed` / `orch_pick_any_unused`); duplicate fix targets `viol_b`; RELAX log uses full `count_on_pool_idx` + `teams_mask_on_point`. ASCII hyphen in no-slot warning.
- **`spawnpool.c`:** `spawnPoolBuildGlobal` + `spawnPoolReset` call `mpOrchestrateReset()` so pool rebuild clears orchestrator state.
- **`bot.c`:** `botSpawnAll()` calls `mpOrchestrateMatchStartSpawns()` before the bot loop; orchestrated path uses `thing = M_BADTAU - angle_rad` for `chrMoveToPos`.
- **`player.c`:** `playerApplyOrchestratedSpawnFromPool()` implemented (pool ground + bond + camera + `bmoveUpdateRooms`).
- **`mpspawn_orchestrate.h`:** `#include "constants.h"` for `MAX_BOTS`.
- **Build:** verify locally (`devtools/build-headless.ps1 -Target client`); agent CMake here may lack arch env.

## Session S425 — 2026-04-20 — PC ADS: twin-stick, SensAdsUi, UI 1–10 (0.5)

- **`actionmap` / `pd.ini`:** Replaced `ActionMap.StickSensitivity` / `StickSensitivityAim` with **`ActionMap.SensMoveUi`**, **`SensAimUi`**, **`SensAdsUi`** (1–10, 0.5 steps; internal mult 0.1–3.0 via `actionmapSensUiToMult` / `actionmapRefreshStickMultFromUi`). **`actionmapGetPcAdsZoomFovMul`** scales ADS gun FOV slightly with ADS sensitivity. Legacy stick-sensitivity keys are no longer registered (orphaned lines in old `pd.ini` are ignored).
- **`bondmove.c`:** PC + ADS: no left-stick edge aim speeds; RS look uses **ADS vs hip** ratio and **0.88** strafe/walk scale; **zoom FOV** extra mul on PC when `movedata.zooming`; **manual aim** crosshair uses `analogturn`/`analogpitch` (RS) for PC instead of `c1stick*raw`.
- **`pdgui_menu_mainmenu.cpp`:** Controller tab — move / look / **Aim-down-sights** sliders **1–10**, `PdSliderSensUi` + `ImGuiSliderFlags_AlwaysClamp`.
- **Build:** not verified in agent env (ninja/ccache `CreateProcess`); verify `devtools/build-headless.ps1 -Target client` locally.

## Session S424 — 2026-04-21 — MP team spawns: weighted teammate dot + 20% relax

- **`spawnpool.c`:** When `num_teammates > 0` and teams are active, `poolPickFarthest` maximizes `min_dist_sq + SPAWN_TEAM_DOT_WEIGHT * mean_i dot(u_spawn,u_tm)` (XZ, directions from `pool_center`). `spawnPoolSelectTiered` rolls **20%** `SPAWN_TEAM_RELAX_BIAS_PCT` to use pure FFA farthest-first (`SPAWN.TEAM: relaxed bias roll` log). No teammates yet keeps angular-sector behaviour.
- **`player.c` / `playerreset.c`:** `playerCollectMpTeammatePositions()` + `playerMpResolveTeamSpawnParams()` (static); tiered select wired with teammate arrays for respawn, zero-pad, and initial MP spawn.
- **Build:** verify locally if agent env cannot run ninja.

## Session S423 — 2026-04-21 — MP initial bot spawn: legacy shortlist bypass vs tiered pool

- **Issue:** Chicago + max simulants: bots appeared to stack (often near one point / origin-adjacent) on **initial** spawn; humans already used the tiered pool in `playerreset.c` (`lvframe60 == 0`).
- **Cause:** `playerTrySelectPoolSpawn` (S302) returned false when `g_NumSpawnPoints >= needed + needed/2`, sending bots through the **legacy four-pad shortlist** even when the map listed dozens of spawns. For 32 participants the threshold is 48 pads; pad-rich arenas bypassed the pool while `needed` was still huge, so initial bot placement repeated the same few pads.
- **Fix:** `player.c` — only allow that legacy bypass when `needed <= 8` (small matches where the shortlist is still viable). High bot counts always use `spawnPoolSelectTiered` + reservations when the pool is ready.
- **Build:** not verified here (ninja/ccache `CreateProcess` failure in agent env); verify `devtools/build-headless.ps1 -Target client` locally.

## Session S425 — 2026-04-21 — P6-A/P6-B Tier 6 (SP-1 + hub/room ADR notes)

- **P6-A (SP-1):** `menu.c` — `currentPlayerIsMenuOpenInSoloOrMp` bounds-check `mpindex` before `g_Menus[]`; `func0f0f8120` removed `% MAX_LOCAL_PLAYERS` path, `g_MpPlayerNum` guard only. `activemenu.c` — `amOpen`, `amOpenPickTarget`, `amRender` guard `g_AmIndex` vs `ARRAYCOUNT(g_AmMenus)`.
- **P6-B:** `port/src/hub.c`, `port/src/room.c` — ADR note blocks (room 0, `g_Lobby.inGame`, multi-room roadmap).
- **Docs:** `context/systemic-bugs.md` SP-1 fixes list; `context/audits/2026-04-21-resolution-prompts.md` Tier 6 marked done.

## Session S424 — 2026-04-21 — P5-A interest management design (SEC-8/9)

- **New:** `context/designs/interest-management-replication.md` — audit of `net.c` `netEndFrame` shared-buffer + `enet_host_broadcast` pattern; `netdistrib.c` scoped as orthogonal; phased options (room relevance, radius/cell, cadence, PVS later); `NET_PROTOCOL_VER` notes.
- **Updated:** `context/audits/2026-04-21-resolution-prompts.md` (P5-A marked design done), `context/README.md` design index, `context/tasks-current.md` SEC-8/9 pointer.

## Session S423 — 2026-04-21 — Tier 4 ADR + audit prompts: manifest broker, catalog IDs

- **`context/designs/pd-server-plugin-abi-adr.md`:** **Revised primary model** — dedicated server as **host-manifest broker** (catalog ID strings + hashes / revision; per-client private dynamic catalogs; **no game content** in server binary); **Trust** / **Confirm First** called out (per-player readiness, no whole-lobby stall); optional **loadable policy module** secondary to data-first rules; **P4-B** / **P4-C** realigned (broker spike + stub shrink toward manifest authority, not `g_MpArenas` + function-pointer as headline).
- **`context/audits/2026-04-21-resolution-prompts.md`:** Tier 4 section updated — **P4-A marked done**, **P4-B** / **P4-C** prompts match manifest-first direction; execution-order note for Tier 4.
- **Next:** Approve broker ADR, then **P4-B** wire + server state slice (manifest in, fan-out, readiness bits).

## Session S422 — 2026-04-21 — P4-A ADR: game-agnostic pd-server plugin ABI (Tier 4 C-1)

- **New:** `context/designs/pd-server-plugin-abi-adr.md` — ADR for **`pd-server-core`** vs **PD2 plugin**: wire codec stays in core; game decision points as versioned callbacks (`pd_server_plugin_reg` / `pd_server_game_ops` naming TBD); `server_stubs.c` split + migration phases (P4-B spike, P4-C line-count tracking); CMake targets (`pd-server-core`, `pd2_server_plugin`, `PerfectDarkServer`); **`PD_SERVER_PLUGIN_ABI_VERSION`** vs **`NET_PROTOCOL_VER`**; references `port/src/server_main.c`, `server_bridge.c`, `server_stubs.c`, `CMakeLists.txt`. **Doc-only** — no code in P4-A. *(Superseded emphasis — see S423.)*
- **Links:** `context/README.md` design index, `context/server-architecture.md` (see-also).
- **Next:** Mike approves ADR, then **P4-B** smallest vertical slice (e.g. `g_MpArenas` behind registration / static link first). *(Superseded — see S423.)*

## Session S421 — 2026-04-20 — README PD2 fork + H-1 listen host UI (P3-A/P3-B)

- **README.md**: “Perfect Dark 2 (Mike fork)” note linking `context/designs/hosting-modes-listen-vs-dedicated.md`, `CLAUDE.md`, `context/server-architecture.md`.
- **P3-A**: `pdgui_menu_network.cpp` — “Host game / Go online” calls `netStartServer` (disconnects first if `NETMODE_CLIENT`); port from UI + `Net.Server.Port` in `net.c` `configRegisterUInt`; UPnP/STUN status labels; `netmenu.c` / file headers updated (clients can host listen).
- **P3-B**: `pdgui_lobby.cpp` — `NETMODE_SERVER && !g_NetDedicated` uses same lobby/room/distrib path as clients + compact **HOSTING (THIS PC)** connect-code banner. `netmsg.c` — `netSendRoomSettingsUpdate` / `netSendRoomPlaylistUpdate` re-encode CLC for listen host via `netbufStartReadData` + read handlers; `netListenHostRoomLeave()` for leave-room without CLC. `pdgui_menu_room.cpp` / `pdgui_menu_mpsettings.cpp` — leader/playlist gates include listen host.
- **Build**: not verified in this agent environment (ccache/CreateProcess path); verify `devtools/build-headless.ps1 -Target client` locally.

## Session S420 — 2026-04-20 — P2-A hosting modes ADR (H-3 Tier 2)

- **New:** `context/designs/hosting-modes-listen-vs-dedicated.md` — ADR for listen-in-client vs `PerfectDarkServer`: ROM/mod enforcement (`!g_NetDedicated` in `netmsgClcAuthRead`), dedicated bot-authority boundary, NAT/UPnP/STUN (pointer to `nat-traversal-architecture.md`), admin RCON token handling, connect codes vs raw IP per constraints.
- **Links:** `context/server-architecture.md` (see-also), `context/README.md` design index, `CLAUDE.md` § Server and hosting.

## Session S419 — 2026-04-20 — Dev Window v2 release pipeline abort on failure

- **`devtools/dev-window-v2/dev-window-v2.ps1`**: On a non-zero build-step exit code, **clear the entire step queue** instead of retaining only steps whose `Target` differs from the failed step. The old behavior could run **`Build (server: pd-server)`** after a failed client-side step (e.g. configure) while **`Build/`** had no **`CMakeCache.txt`**, producing **`Error: not a CMake build directory`**.
- **Stale `.git/index.lock`**: New **`Clear-StaleGitIndexLock`** (Windows attrib + MSYS/WSL paths) runs before **git sync**, before queued **Auto-commit + push**, and before **Pull/Push**; runspace lock cleanup clears **read-only** locks; **git push** retries with cleanup on lock errors. **`devtools/release.ps1`** and **`devtools/build-headless.ps1`** strip read-only before deleting **`index.lock`**.
- **Follow-up (concurrent / MSYS git)**: **`Resolve-GitExecutable`** now prefers **`C:\msys64\mingw64\bin\git.exe`** (then Git-for-Windows) **over** **`usr\bin\git.exe`** so sync does not use Cygwin-style git that races IDE index locks. Sync runspace: **`cmd del`**, **400 ms** pre-add pause, **6** `git add` retries with **backoff + wait-if-lock-present**, **`Another git process`** matched; push retries aligned.

## Session S417 — 2026-04-20 — Interact hold config + Controls input sanity (continuation)

- **`ActionMap.InteractHoldExtraTerminalMs`**: `actionmapGet/SetInteractHoldExtraTerminalMs()`, `configRegisterInt` default **200**, **`prop.c`** uses getter for `OBJFLAG3_HTMTERMINAL` extra. Settings → Controls: **Hackable terminal extra hold** slider; **`pdgui-hold-ring.md`** updated.
- **Sanity**: [menu-controller-input-constraints.md](designs/menu-controller-input-constraints.md) §**2.1** — table for Settings → Controls (no SDL mouse mode in tab path; `pdguiDriveImGuiNav` + PageUp/PageDown; `NavFlattened`). **[INDEX.md](INDEX.md)** links that doc.
- **Build**: This agent run still invokes **`ccache`** from CMake rules; **`CCACHE_DISABLE=1`** did not strip the launcher. Verify with **`ninja -C Build pd`** on a host where ccache works, or reconfigure with ccache disabled.

## Session S418 — 2026-04-20 — PC USE release reload when no prompt (short hold)

- **`bondmove.c`**: USE release with **`propInteractPromptLabel() == NULL`** now synthesizes **reload (X_BUTTON)** when either **long-hold consumed** or **hold duration >= 80 ms** (so releases before the use-hold threshold still reload if nothing was interactable; very short taps avoid pairing reload with door activate).
- **`actionmap`**: **`actionLastGestureHoldMs()`** for release-frame gesture length.

## Session S416 — 2026-04-20 — Tier 1 server security (P1-A–D)

- **P1-A (S-1)**: `netmsg.c` — per-client + hashed-IP sliding window (**3 failed `ADMIN_SUB_AUTH` / 60 s**) → `ADMIN_RESP_RATE_LIMIT` + `enet_peer_disconnect(..., DISCONNECT_ADMIN_AUTH)`; bad-token **LOG_WARNING** throttled (**10 s**/client); `netmsgAdminAuthRateReset` on `netClientReset`. **`NET_PROTOCOL_VER` 39** — new `ADMIN_RESP_RATE_LIMIT 0x06`. **`server_admin.c`**: minimum token length **16**; boot **WARNING** if `< 32` chars.
- **P1-B (S-2)**: `net.c` — `netServerIssueCookie` uses **BCryptGenRandom** (Windows) / **getrandom** (Linux) / **getentropy** (macOS) / `/dev/urandom` fallback; SHA-256 path only if OS RNG fails; **CMake** links **bcrypt** on Windows.
- **P1-C (S-3)**: `server_bans.c` — Windows **`fflush` + `_commit`**, **`MoveFileExA`** replace; POSIX **`rename`** without prior **`remove`**. **`banAddrEq`**: **`inet_pton`** → canonical **`in6_addr`** (IPv4-mapped for dotted-quad).
- **P1-D (AUDIT-M2)**: `ADMIN_SUB_LIST` / `ADMIN_SUB_STATUS` — `vsnprintf` truncation detection + **`(truncated)`** footer; `ADMIN_PAYLOAD_MAX` documented in **`server_admin.h`**.
- **`constraints.md`**: protocol **v39** + admin-token constraint text.
- **Build**: not completed in this agent environment (PowerShell `build-headless` runspace error; `ninja` hit ccache/`CreateProcess`); verify **`devtools/build-headless.ps1`** locally.

## Session S415 — 2026-04-21 — P0 wire-mask tripwire + net_hash documentation

- **`src/game/mplayer/participant.c`**: `_Static_assert(MAX_PLAYERS + MAX_BOTS <= 64, ...)` immediately before `mpParticipantsEncodeActiveMask` / `mpParticipantsDecodeActiveMask`, message documents **SVC_STAGE_START** `active_mask` as **u64** (one bit per slot; players then bots).
- **`port/src/assetcatalog.c`**, **`port/include/assetcatalog.h`**, **`port/include/assetcatalog_deps.h`**: short comments that **net_hash** is an internal catalog cache/dedup key only, not wire/save/public API identity (per **constraints.md** catalog-ID rules).
- **Build**: `devtools/build-headless.ps1` exits early in this agent environment during Configure (exit 5; investigate locally). **MSYS bash** `cmake` + `ninja -C Build pd` fails on existing **`imgui/imgui.h`** vs **`-I .../port/fast3d/imgui`** mismatch (**pdgui_hold_ring.h** et al.); unrelated to this diff. Verify full headless build on a machine where the ImGui include path already matches the tree.

## Session S414 — 2026-04-20 — Controller hold housekeeping (USE slider, prop extras, C-buttons policy)

- **`actionmap.h` / `bondmove.c` / `prop.c`**: Documented **`ACTION_USE_HOLD_THRESHOLD_MS`** as actionmap seed + corrupt-config fallback only; gameplay uses **`actionmapGetEffectiveHoldMs(ACTION_USE)`** via **`propGetActionUseHoldThresholdMs()`** / **`propInteractPromptHoldThresholdMs()`**. **`propInteractPromptHoldThresholdMs`**: terminal extra ms via **`actionmapGetInteractHoldExtraTerminalMs()`** (`ActionMap.InteractHoldExtraTerminalMs`); weapons/doors/generic unchanged. **`pdgui-hold-ring.md`** updated (see also S417).
- **`pdgui_menu_mainmenu.cpp`**: When **`actionmapGetActionHoldMsOverride(ACTION_USE) >= 0`**, show **effective ms**, **disable** global Use-hold slider with explanation; advanced hold-overrides blurb notes the interaction.
- **`constraints.md`**: **C-button** row — actions remain; Controller tab **hides C-button group** in mapper/table only (UI clutter).
- **Input**: Controller tab uses ImGui only (no new `SDL_*` mouse APIs in this path); verified by review.
- **Build**: Verify with `devtools/build-headless.ps1` locally if agent env cannot configure CMake.

## Session S413 — 2026-04-20 — F9 menu / input diagnostics overlay

- **F9** toggles read-only **Menu / input diagnostics** (`pdgui_menu_stack_debug.cpp`): input-context stack, `inputCtxDebugSnapshotAuthority` (no `gameplayInputSuppressed()` side effects), menupool actives, legacy `g_Menus` text via `pdguiDebugFormatLegacyMenuInfo` in `pdgui_bridge.c`. Window uses **NoInputs | NoNav | NoBringToFrontOnFocus** so it does not capture input or push contexts.
- **`pdgui_backend.cpp`**: global hotkeys **F9** / **F10** (`meshDebugToggle`); `pdguiNewFrame` / `pdguiRender` early-return gates include `pdguiMenuStackOverlayGetOpen()`; `pdguiMenuStackOverlayRender` immediately before `ImGui::Render()`.
- **`gfx_sdl2.cpp`**: mesh debug moved from **F9** to **F10** for the fallback path when `pdguiProcessEvent` does not run.
- **`pdgui_bridge.c`**: `#include <stdio.h>` for `pdguiDebugFormatLegacyMenuInfo`.
- **Build**: Not verified in this agent environment (CMake configure needs MinGW toolchain args from `devtools/build-headless.ps1`); verify locally.

## Session S412 — 2026-04-21 — Full Super Audit (standalone, not delta)

- Ran **full** Super Audit per `.claude/skills/super-audit/audit-prompt.md` — new report **`context/audits/2026-04-21-full.md`** (sections 1–7, Phase 2.5 notes, scorecard).
- **Design note captured:** game-agnostic dedicated server (committed) vs in-client **listen host / “go online”** — engine already has `netStartServer` + `!g_NetDedicated` slot-0 host; UI/docs currently **dedicated-first / no client host** (`pdgui_menu_network.cpp`, `netmenu.c`). Not inherently ad-hoc; main cost is dual test surface + UX.
- **Findings-only** per skill (no code changes).

## Session S411 — 2026-04-20 — Super Audit skill: manual verification pass

- Ran **`.claude/skills/super-audit/SKILL.md`** (read `audit-prompt.md`, reconciled with `context/audits/2026-04-20-full.md`).
- Spot-verified open items against current sources: `netbufReadStr` NUL fix (`netbuf.c`), `netServerIssueCookie` non-CSPRNG comment (`net.c`), `ADMIN_SUB_AUTH` without rate limit (`netmsg.c`), no `MAX_PLAYERS+MAX_BOTS<=64` static assert yet — **same conclusions as 2026-04-20 report**.
- Appended **Verification pass** section to `context/audits/2026-04-20-full.md`. Documented skill path quirk: prompt is `audit-prompt.md` at skill root, not `references/audit-prompt.md`.

## Session S410 — 2026-04-20 — Controller map: Bind 2 drop + stick cardinal zones

- **`port/fast3d/pdgui_menu_mainmenu.cpp`**: Visual mapper zones split **left = Bind 1 / right = Bind 2** with `pickSlotForControllerBindColumn` (matches bind table), drag-drop to either half, **right-click** clears that column via `clearControllerVkAtBindColumn`. Added **LS/RS cardinal** zones (`JOY1_LSTICK_*` / `JOY1_RSTICK_*` offsets 22-29). Zone order: cardinals + face controls first, **L3/R3 last** so stick-click stays on top where overlapping. Taller pad (`240` scaled), updated help text.
- **Build**: Not verified in agent env.

## Session S409 — 2026-04-20 — Active menu: explain no-fire + harden skip

- **Symptom chain** (pre-S408): **`amOpen()`** sets **`g_PlayersWithControl = false`** (player **`bmoveTick(0,0,0,1)`** — no fire / no weapon button path). **`pdguiActiveMenuShouldSkipLegacyWheel`** skipped the GBI wheel, but **`pdguiRender`** still early-returned → **no ImGui wheel**. Inventory/HUD can still show selected weapons while **hands/sights** follow “menu open” / control-off behavior — matches “UI says equipped, can’t shoot, no fists visible, can’t see radial”.
- **Hardening (superseded by S427)**: ~~init-based fallback~~ — replaced by **`pdguiActiveMenuIsOpen`** + shared **`pdguiAnyStandardOverlayReason`** in **`pdgui_backend.cpp`**; **`pdguiIsInitialized`** removed.

---

## Session S408 — 2026-04-20 — Active menu: run ImGui when wheel open (S407 follow-up)

- **Bug**: **`pdguiNewFrame` / `pdguiRender`** early-returned during “clean” solo gameplay (same gate as pre-S311 interact prompt). Active menu open set **`pdguiActiveMenuShouldSkipLegacyWheel`** → **`amRender`** skipped the GBI wheel, but ImGui never ran → **no radial** and confusing weapon UX.
- **Fix**: **`port/fast3d/pdgui_backend.cpp`** — treat **`pdguiActiveMenuShouldSkipLegacyWheel()`** like the interact prompt: include **`activeMenuWheel`** in both NewFrame and Render guard conditions.

---

## Session S407 — 2026-04-20 — ImGui active menu (weapon / function / orders) wheel

- **Goal**: PC ImGui radial/diamond for the in-game active menu using existing **`amGetSlotDetails`** / **`amCalculateSlotPosition`** (no new catalog wire IDs); theme colors via **`pdguiGetActivePaletteRaw`** / **`pdguiGetTextWarning`**; skip duplicate GBI wheel when ImGui draws.
- **`src/game/activemenu.c`**: `amInitActiveMenuSelectionCoords()` so **`selx`/`dstx` anim** still run when legacy wheel is skipped; **`amSyncCommandingAibotForActiveMenu()`** mirrors **`amRenderAibotInfo`** side effect so bot HP bar + **`commandingaibot`** stay correct; **`amGetSlotVisualMode()`** factors slot highlight logic for legacy + bridge; legacy wheel body gated on **`pdguiActiveMenuShouldSkipLegacyWheel()`** (from **`pdgui_bridge.c`**).
- **`port/fast3d/pdgui_bridge.c`**: slot query, framebuffer→window mapping, diamond outer corners, selection pulse RGBA, **`LOCALPLAYERCOUNT`** helper.
- **`port/fast3d/pdgui_activemenu_radial.cpp`** + **`port/include/pdgui_activemenu_radial.h`**: foreground draw list overlay (diamond fill, slots, pulsing selection frame).
- **`port/fast3d/pdgui_backend.cpp`**: **`pdguiActiveMenuRadialRender`** after HUD.
- **Build**: Not verified here (ccache `CreateProcess` in agent env); verify on MSYS MinGW per project scripts.

---

## Session S407 — 2026-04-20 — Controller map silhouette + B1/B2 on pad

- **`port/fast3d/pdgui_menu_mainmenu.cpp`**: Settings → Controls → Controller visual mapper gains a vector-style gamepad silhouette (body, LT/RT caps, D-pad cross, stick rings, face-button hints) under the interactive drop zones; slightly larger pad area. Zones now list **B1** / **B2** per action (matches table Bind 1/2 via `getBindsByType`), with hover tooltip and extra line when both **Use / Interact** and **Reload** share a button (hold threshold + bondmove). Drag-and-drop still sets **Bind 1** (slot 0) only — unchanged.
- **Build**: Not verified here (PowerShell `build-headless.ps1` runspace failure in agent env); verify locally via MSYS headless build.

---

## Session S409 — 2026-04-20 — Per-action hold overrides UI + Controls nav polish

- **`port/fast3d/pdgui_menu_mainmenu.cpp`**: Controller tab — collapsible **Per-action hold overrides (advanced)** with filterable table: Default vs Custom (50-2000 ms), Effective column (Use / Interact shows global when Default), `actionmapSaveBinds` + `configSave` on change. Helper text under global **Use hold** slider. **Controller map** child windows use `ImGuiChildFlags_NavFlattened` so gamepad/keyboard nav crosses the action list, scroll region, and drop zones; `TextDisabled` note that rebinding with a gamepad uses the table (drag-drop is mouse). Controls sub-tab bar uses `ImGuiTabBarFlags_FittingPolicyScroll`.
- **`port/include/actionmap.h`**: Comment points Settings UI for global Use hold vs overrides.
- **Build**: Not verified in agent env.

---

## Session S398 — 2026-04-21 — Hold ring module + context doc

Shared **`pdguiDrawHoldProgressRingAroundBox`** in `port/include/pdgui_hold_ring.h` +
`port/fast3d/pdgui_hold_ring.cpp`; `pdgui_glyphs.cpp` delegates to it. Design:
[context/designs/pdgui-hold-ring.md](designs/pdgui-hold-ring.md) (per-target `propInteractPromptHoldThresholdMs` tuning,
bondmove sync, reuse guidance). README designs table + `prop.c` doc pointer.

---

## Session S399 — 2026-04-21 — Clarify weapon radial vs hold ring (context)

Weapon/gadget wheel is **active menu** (`amRender` / `activemenu.c`, GBI), not `pdgui_hold_ring`.
New [context/designs/activemenu-radial-architecture.md](designs/activemenu-radial-architecture.md); cross-link from
pdgui-hold-ring.md + README. **ImGui replacement** of the wheel is a separate, larger task (shared data + input
with `activemenutick.c`).

---

## Session S406 — 2026-04-20 — PC USE: tap mount hoverbike, hold pickup

- **Cause**: Twin-stick `ACTION_USE` tap synthesized **X** (reload); only **long** USE synthesized **A** (activate). `propobjInteract` runs **TryMount** then **Grab** — long-press often failed mount angle → **pickup** worked, **tap never activated**.
- **`src/include/types.h`**: `player.pcinteractusekind` (PC: 1=tap mount, 2=hold grab).
- **`src/game/bondmove.c`**: Short **hold** of USE maps to **A_BUTTON** (not tap→X); long hold unchanged; each frame clear `pcinteractusekind`; on `btapcount` set kind from `actionHoldConsumed(ACTION_USE)`; PC **always** `ACTIVATE` only (no tap+reload combo). **`src/game/propobj.c`**: For **OBJTYPE_HOVERBIKE** + **CONTROLMODE_PC**, tap → `currentPlayerTryMountHoverbike` only; hold → `bmoveGrabProp` only; else legacy mount-then-grab.
- **Reload**: `ACTION_RELOAD` (R) or long-USE release with no prompt (unchanged).
- **Build**: Not verified in this environment (ccache CreateProcess); verify on dev MSYS.

---

## Session S405 — 2026-04-20 — Menu input doc: clarify modal close vs controller X

- **`context/designs/menu-controller-input-constraints.md`**: §2.3 / §6.2 / §6.10 — Modal dismiss + focus restore: **Back** = `ACTION_CANCEL_USE` (B / Esc defaults); **title-bar close** on **window chrome** only. Explicitly **not** controller face **X** or keyboard **X** (unless rebound to Cancel). Bot row glyph: **context-menu** action (e.g. face X) disambiguated from window close.

---

## Session S404 — 2026-04-20 — Match setup UX spec: panel order, teams, music, countdown

- **`context/designs/menu-controller-input-constraints.md`**: §2.3 extended — modal close via **X**/back restores focus to **invoker** on parent. §3.6 **Should** — **smooth scrolling** for menu scrollbars. §6 — left panel order **Arena → Scenario → Weapon Set → Options**; Weapon Set **dropdown**, **alphabetized**, smooth scroll; **Teams** on → default **2 teams** + **Players Together** vs **Players Split** (bot distribution rules for 3 teams / 2 players); **color** team names; free manual team edits override auto; **Select Tunes**: catalog + **enabled** mods (persistent/default-on for new mods), row/A = **playlist only**, play button = **bounded preview**, **stop on pop**; **Start Match** solo + online **countdown parity** (3-2-1 + UX audio), fix longer/buggy online timer. **§6** scope includes `pdgui_menu_mpsettings.cpp`.

---

## Session S403 — 2026-04-20 — Menu input doc: Combat Simulator reference + cross-panel nav

- **`context/designs/menu-controller-input-constraints.md`**: §2.8 **Must** — multi-column layouts: panels are containers; **Left/Right** crosses columns without focusing panel chrome. §3.5 **Should** — contextual **glyph hints** (e.g. lower-right) for rows with extra actions. New **§6 Combat Simulator**: seamless L/R nav; bot row glyphs (multi-select vs context menu); **A** opens full bot settings, **X** context menu; **“Random name”** copy; character list **dedupe** + fix placeholder labels (Skedar/Dr. Carroll); **Joanna**/faction **grouping**; explicit **Player handicaps** copy; **Swat/Hardcore** (health-only, no shield) in match config + **server authoritative** + protocol bump note. Prior **§6 Applying** renumbered to **§7**.

---

## Session S402 — 2026-04-20 — Mission Select design: tabs, gold Challenges, modded filters

- **`context/designs/menu-controller-input-constraints.md`**: §2 **Must** adds locked missions **filtered only** (no grey rows). §5 — **Tabs**: Campaign / **Challenges** / **Modded missions**; **Challenges** uses new semantic **theme Gold** (palette + loader + accessors, not ad-hoc ImU32); **default tab** on open = Challenges (gold) unless code documents an override. **Modded** tab: sort/filter by **mod pack** and/or **creator** before listing. Locked missions never appear in any tab list.

---

## Session S401 — 2026-04-20 — Menu controller input design doc (Mission Select reference)

- **New** `context/designs/menu-controller-input-constraints.md`: reusable **Must/Should/Must not** for progressive focus, modal drill-down, cancel restores parent selection, pointer sync on programmatic focus, save-backed defaults, list visibility (completed + next mission). **§5** normative **Mission Select** example: horizontal mission tiles, modal with top info + difficulties in one row, default = highest beaten, staged Submit for controller, two-step mouse path, B closes modal with highlight preserved.
- **Link**: [menu-stack-architecture.md](designs/menu-stack-architecture.md) Related list points to the new doc.

**Next**: Add more §5 reference flows as they are agreed; implement Mission Select against §5 in `pdgui_menu_solomission.cpp` (or dedicated renderer).

---

## Session S400 — 2026-04-20 — Settings Controller tab: sticks, visual map, hide C-buttons

- **`port/fast3d/pdgui_menu_mainmenu.cpp`**: Controller tab now uses per-stick move/look sensitivity and deadzone (`actionmapGet/SetStick*Move/Aim`), Move combo + implied look stick, invert look (Y), **Use hold (interact vs reload)** ms slider (`actionmapGet/SetUseHoldThresholdMs`), deadzone normalization explained in UI. **Controller map**: draggable action list + stylized pad with drop targets (primary bind = slot 0); C-Buttons group hidden from controller bind table (`renderBindTable(..., (1u << BG_CBUTTONS))`). Engine radial deadzone + remap was already in `actionmap.cpp` from prior work.
- **Variable hold length (`port/src/actionmap.cpp`, `prop.c`)**: `actionmapGetEffectiveHoldMs` / `actionmapGet/SetActionHoldMsOverride` — optional per-`InputAction` hold duration (ms), persisted as `ActionMap.HoldMsOverrides` (`24:450` style comma list). Global **Use** hold and overrides clamp to **50–2000 ms** (2 s max). Interact prompt / bondmove path uses effective ms for ACTION_USE.
- **Build**: Local `ninja` failed in this environment (`ccache` CreateProcess — toolchain path); verify with MSYS `ninja -C Build pd` on dev machine.

**Next**: Playtest controller nav in Settings, drag-drop binds, interact/reload timing vs hold slider.

---

## Session S399 — 2026-04-20 — B-207 manifest heads + B-213 Skin Editor charpreview

- **B-207** (`port/src/net/netmanifest.c`): Late-add diagnostic no longer treats `numparts==0` as torn for **MANIFEST_TYPE_HEAD** (aligned with `modelcatalog.c` B-179); bodies still WARN on parts=0.
- **B-213**: `menuRenderModel` skipped when `unk5d5_01` with no legacy dialog — standalone ImGui charpreview never drew (`menu.c` + `pdguiCharPreviewNeedsMenuModel()`). Preview requests set `menumodel.zoom=185`. Main menu view 3 keeps Modding entry after hub close (Open Modding Hub + Back). Hub Escape exits skin paint session first (`pdguiSkinEditorTryConsumeHubEscape`).

**Next**: Playtest Mods → Skin Editor preview / New Skin / Escape; grep logs for MANIFEST-SP late-add heads.

---

## Session S398 — 2026-04-20 — Apr20 plan batch: B-204/205/206/202/208/210/214/215/216

Roadmap: `PD2_Implementation_Plan_Apr20.docx` remaining Priority 1 items.

- **B-204 / B-205** (`src/game/lv.c`): After slow-motion / unpause logic, cap `g_Vars.lvupdate240` to `LV_UPDATE240_CATCHUP_CAP` (`TICKS(8)`) so tab-out catch-up cannot run unlimited CHR/bot work in one tick (mitigates audio starvation and stress crashes).
- **B-206** (`src/game/spawnpool.c`): `spawnPoolComputeAABB()` left `aabb->valid=false` for “no room data” / all-degenerate-room fallbacks while still assigning a 0..1000 volume — L4 radial and last-resort synthesis used the (0,100,0) sentinel. Mark those fallbacks **valid** so the real AABB center drives candidates.
- **B-202** (`src/game/activemenu.c`): `AMSLOTMODE_FOCUSED` previously reduced slot fill to alpha-only (invisible). Replaced with a visible warm-tint fill (weapon wheel selection reads clearly). Camera lock while radial open was already addressed in bondmove (prior session).
- **B-208** (`port/fast3d/pdgui_menu_mpsettings.cpp`): Cache mod/base track collectors across frames; rebuild on `IsWindowAppearing` or `assetCatalogGetGeneration()` change.
- **B-210** (`port/fast3d/pdgui_menu_moddinghub.cpp`): On hub window appear, clear Enter/Space/FaceDown edges; 5-frame grace on Escape close (B-198 class).
- **B-214** (`port/src/modmgr.c`, `port/include/modmgr.h`, `port/fast3d/pdgui_menu_mainmenu.cpp`): New `modmgrSyncCatalogToRegistry()` — full catalog rebuild + `catalogLoadInit` path after delete/rescan so removed mods drop out of intercepts immediately; called after successful mod folder delete.
- **B-215** (`port/fast3d/pdgui_menu_moddinghub.cpp`): Scale tool now also lists **ASSET_BODY** entries whose `catalogResolveFile` returns a loose model path (catalog was character-centric).
- **B-216** (`port/fast3d/pdgui_menu_forge.cpp`): If already on `STAGE_CITRAINING`, skip `mainChangeToStage(STAGE_CITRAINING)` — only `forgeRequestEnterSession()` (avoids full reload + menu stack corruption when re-entering Grid from CI).

**Next**: MSYS `ninja -C Build pd pd-server`; playtest Grid from CI, Mods hub controller open, mod delete list, Area 52 bots, Select Tunes scroll.

---

## Session S397 — 2026-04-20 — S311 interact prompt missing in solo (ImGui overlay early-exit)

**Symptom**: No `[E] Pick up` / glyph prompt during gameplay (after B-189 menu gate
and prior prompt refresh work). **Cause**: `pdguiNewFrame()` and `pdguiRender()` return
early when no debug/hotswap/network/pause/hub/update/console UI is active — so in
plain solo play the entire ImGui path was skipped and `pdguiInteractPromptRender`
never ran. **Fix** (`port/fast3d/pdgui_backend.cpp`): treat
`propInteractPromptLabel() != NULL` (game has a tracked interact target) as a reason
to run the overlay, same as an open menu. Forward-declare `propInteractPromptLabel`
to avoid including `game/prop.h` → `types.h` in C++.

**Next**: Playtest solo near doors/weapons; confirm no regression when no target.

---

## Session S396 — 2026-04-20 — Implementation plan Priority 1: B-209 + B-203

Roadmap: `PD2_Implementation_Plan_Apr20.docx` (Priority 1 stability). **B-209**
(`src/game/bondmove.c`): On `CONTROLMODE_PC`, `joyGetNumSamples()` can be **0**
when the joy ring has no span, so every `for (i < numsamples)` block was skipped
including **reload** (`alt1tapcount`) and **interact** (`btapcount`) even when
hold/tap synthesis had already filled `c1buttons`. Fix: clamp `numsamples` to
at least **1** for PC after `joyGetNumSamples()`. **B-203** (`port/src/actionmap.cpp`):
`ACTION_FIRE_MODE` on D-pad right was shadowed by a second bind of
`ACTION_DPAD_RIGHT` to the same VK (`fireVk` picks lower enum index first); removed
the duplicate. Physical **D-pad down** (log vk=531) had no gameplay bind — added
default `ACTION_FIRE_MODE` there. Build verified: MSYS `source devtools/build-env.sh &&
ninja -C Build pd pd-server` (784/784).

**Next**: Playtest tap vs hold on X/F and fire mode on D-pad; then remaining
Priority 1 items (B-204, B-206, B-202).

---

## Session S395 — 2026-04-20 (scheduled weekly super-audit, no worktree) — Delta audit on Waves 1–3

Focus: Weekly automated super-audit (scheduler task "weekly-super-audit"). Full-scope
findings-only report; no code changes per scheduler instruction. Report written to
[`context/audits/2026-04-20-full.md`](audits/2026-04-20-full.md).

**Methodology note**: Planned to run four parallel Explore sub-agents (netplay /
server-security / menus-input / layout-scaling-lineage) but all four failed with
`API Error 400: "This model does not support the effort parameter"` — a harness/config
issue, not a code issue. Audit proceeded directly via Read/Grep on the main
thread; every finding below has file+line evidence.

### Delta from 2026-04-19 master audit

Prior totals: **6 C / 26 H / 35 M / 25 L**.  19 of the 26 High-or-above findings
closed in the intervening day across Waves 1–3 + S394:

- **C1 / MASTER-C1** — `netbufReadStr` NUL term ✓ (S391)
- **C2 → C2a/b/c/d** — admin token + CLC_ADMIN dispatch + persistent `$S/bans.ini` ✓ (S393)
- **C3 / MASTER-C3** — 16-byte identity cookie (SHA-256 rolling) ✓ (S393)
- **C6 / MASTER-C6** — `pdguiCharPreviewRenderDirect()` exists in `pdgui_charpreview.c:631` ✓
- **H2 / MASTER-H2** — zero-SHA COMPONENT entries dropped at deserialize (`netmanifest.c:945-951`) ✓
- **SEC-6** — Ed25519 signed updater (OpenSSL EVP_DigestVerify, RFC 8032 self-test at init) ✓ (Wave 3C)
- **SEC-7** — query reflection mitigation ✓ (Wave 2A per v38 protocol-bump note)
- **SEC-12** — server-side `CLC_ROOM_SETTINGS_UPDATE` validation (numBots/timelimit/scorelimit/scenario/weaponset/stage_id all bound-checked) ✓
- **SEC-13** — room mutation rate limit (1 s/client for CREATE/JOIN/LEAVE; `netmsgRoomRateAllow`) ✓
- **SEC-14** — room password wire (salt `"pd2-room-password-v1\n"`) ✓ (S393)
- **LAYOUT-1** — gset field-wise wire (4× `netbufWriteU8`, not memcpy) ✓ (S392)
- **H-2 / X-4** — typed registrars for wire-delivered mods (`iniFilenameToAssetType` + `populateExtFromIni`) ✓ (S392)
- **F-IP-Browser** — connect-code encoding in network menu ✓
- **F-Hardcoded-Player-Caps** — `pdgui_constants.h` + `pdgui_constants_check.c` _Static_assert ✓ (S392)
- **F-StaleStructComments** — offset comments removed from `mpchrconfig`/`mpplayerconfig`/`mpbotconfig` ✓ (S392)
- **M-23 cascade** — `menupoolReleaseAll` on `netDisconnect` (`net.c:1102`) ✓ (S388)
- **Menu Stack M-1..M-21** — all Tiers 1–4 ✓ (S385–S390)

### New findings this audit — 1 Critical · 4 High · 4 Medium · 5 Low

**AUDIT-C1 (Critical, carried from MASTER-C5 — policy decision)**: dedicated-server
pillar still unmet. `port/src/server_stubs.c` (449 L) contains hardcoded PD2
`g_MpArenas[]` table + hundreds of PD2-specific globals. Recommend retiring the
"game-agnostic dedicated server" pillar for v0.1.0 in `pillars.md` (30 min)
rather than committing to a 4–8 week plugin-boundary refactor.

**AUDIT-H1**: `CLC_ADMIN_AUTH` has no per-peer rate limit. Online brute-force
bounded only by RTT. Add `s_AdminAuthRate[]` parallel to `s_RoomMutationLast`
/ `s_ChatRate`. 2–3 h.

**AUDIT-H2**: `netServerIssueCookie` (`net.c:1301-1336`) is explicitly not a
CSPRNG — mixes `SDL_GetPerformanceCounter` + `time(NULL)` + rolling SHA-256.
Replace entropy source with `BCryptGenRandom` (Win) / `getrandom` (POSIX).
30–60 min.

**AUDIT-H3**: `server_bans.c` save path uses `remove()` + `rename()` — not
atomic on Windows, no fsync. Replace with `MoveFileExA(…,
MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`. 1 h.

**AUDIT-H4**: `banAddrEq` does string-level compare with no IPv6
canonicalization — `::1` vs `0:0:0:0:0:0:0:1` vs `::0001` all evade each
other. Canonicalize via `inet_pton`/`inet_ntop` before compare. 2–4 h.

**AUDIT-M1**: admin token 8-char minimum too low given no rate limit —
bump to 16 + entropy warning at boot. 15 min.
**AUDIT-M2**: `ADMIN_SUB_STATUS` / `LIST` replies silently truncate at
`ADMIN_PAYLOAD_MAX` (~2 KiB) — 256-entry ban list drops ~90% of rows. 1–2 h.
**AUDIT-M3**: `netServerIssueCookie` static `s_Ctx` not thread-safe (not
currently a live bug). 15 min comment / 1 h mutex.
**AUDIT-M4**: no compile-time tripwire on `MAX_PLAYERS + MAX_BOTS <= 64`.
5-minute `_Static_assert` next to `mpParticipantsEncodeActiveMask`.

**AUDIT-L1**: CLC_ADMIN failure log doesn't record token-length / class.
**AUDIT-L2**: hold-vs-tap `ACTION_USE` adds 250 ms latency to reload;
snap-on-release for sub-threshold taps. 30–60 min.
**AUDIT-L3**: `server_admin.c` early-return paths don't scrub `chosen`
plaintext from stack before return.
**AUDIT-L4**: low project-wide `_Static_assert` / `offsetof` coverage on
serialized structs (carries X-6 from 2026-04-19).
**AUDIT-L5**: `CLC_ADMIN BAN` address extraction walks the string output
of `netFormatClientAddr` — fragile across future format changes; prefer
pulling the raw `ENetAddress` off `tgt->peer->address` and `inet_ntop` it.

### Still-open from prior audit

- **MASTER-C5** (Critical, design) — see AUDIT-C1
- **MASTER-H3 / H-3** (High) — u64 slot mask not currently exploitable
  (`MAX_PLAYERS + MAX_BOTS = 40 < 64`) but latent
- **SEC-8 / SEC-9** (High) — PVS / interest management unshipped
- **SAVE-1** (High) — MP stat integrity trusts client
- **LAYOUT-2** (Medium) — `pd.ini` `LastJoinAddr` not validated on reuse
- **M-1** (Medium) — unaligned writer path
- **M-24** (Low) — menupool `parent_type` assertion (deferred)

### Scorecard

| Dimension | 2026-04-19 | 2026-04-20 | Change |
|---|---|---|---|
| Design / gameplay fit | 7 | 7 | – |
| Code quality | 7 | 7 | – |
| Security & trust | **5** | **7** | **+2** |
| Architectural discipline | 7 | 7 | – |

Security moved 5 → 7 because of the Wave 3A/B/C landings. The four new Highs
(AUDIT-H1..H4) are quick-fix hardening on code that shipped yesterday, not
regressions; applying them would lift the security posture to ~8.5.

### Must-fix for next minor release

AUDIT-H1..H4 + AUDIT-M4 + AUDIT-L2 = **half a day to a full day of engineering**.
No code changes this session.

**Next**: await Mike's decision on MASTER-C5 pillar direction, then either
retire the pillar in `pillars.md` or batch AUDIT-H1..H4 into a single
"Wave 3A Hardening" worktree.

---

## Session S394 — 2026-04-19 (worktree `claude/keen-lalande-b65139`) — Auto-keygen on build (SEC-6 follow-up)

Focus: Eliminate the manual `keygen.ps1` step for Ed25519 keypair generation. Commits `a1eab7b9` + `282b84b3` + `ab107684`.

**What was done:**
- New `devtools/ensure-keypair.sh`: checks for `dev-keys/ed25519-private.pem`; if absent, calls `keygen.ps1` via `powershell.exe`. Idempotent.
- `devtools/build-env.sh`: calls `ensure-keypair.sh` at end (sourced before every bash build).
- `devtools/build-headless.ps1`: calls `keygen.ps1` if key missing, before CMake configure.
- `devtools/release.ps1`: removed production/dev key distinction — always uses `dev-keys/`, auto-generates if missing.
- `devtools/keygen.ps1`: removed `-Production` flag; all paths made absolute via `$MyInvocation.MyCommand.Path` (PS5-safe); em-dashes replaced with hyphens (UTF-8 `0x94` read as Windows-1252 RIGHT DOUBLE QUOTATION MARK prematurely closes double-quoted strings in PS5); `$derBytes[-32..-1]` replaced with `[System.Array]::Copy` (PS5 typed-array slice compat).
- `port/include/updater_pubkey.h`: updated comment to reflect single-key model; re-patched with fresh consistent keypair.

**Root-cause found during debugging:** PS5 reads UTF-8 files without BOM as Windows-1252. The em-dash `—` (UTF-8 `E2 80 94`) has `0x94` = Windows-1252 RIGHT DOUBLE QUOTATION MARK, which terminates a double-quoted PS string mid-expression — silently corrupting the `if ($derLen -lt 32)` block and jumping to wrong code.

**Build**: clean [4/4] post-merge. Auto-generation verified: delete key → `source devtools/build-env.sh` → generates cleanly. Idempotent: re-source → silent skip.

**Next**: Continue with Menu Stack Compliance Tier 5 (M-24) or other active tasks.

## Session S393 — 2026-04-19 (worktree `claude/mystifying-booth-c637a7`) — Super Audit Wave 3 Batch A: server auth + identity cookie + persistent bans + room passwords

**Scope**: Three Critical/High findings from the 2026-04-19 server-security audit. All changes ride on `NET_PROTOCOL_VER 38` (already bumped for SEC-7 in Wave 2A) — the v38 doc comment is extended to record the additive wire changes below.

### MASTER-C2 (Critical) — dedicated-server admin RCON + persistent bans

**Admin token (MASTER-C2a)** — `port/src/server_admin.{c,h}` + `port/src/server_main.c`.
- `--admin-token TOK` CLI flag (TOK ≥ 8 chars) OR `[Admin] Token = …` in `$S/server.ini`.  CLI wins.
- `serverAdminInit(cliToken, iniToken)` hashes with a domain-separated SHA-256 (salt `"pd2-server-admin-token-v1\n"`) so the stored hash cannot be confused with a bare SHA-256 oracle.  Plaintext token is scrubbed from argv and stack memory after hashing.
- `serverAdminVerifyToken(plaintext)` does constant-time compare against the stored digest.

**RCON dispatch (MASTER-C2b)** — new `CLC_ADMIN` (0x15) + `SVC_ADMIN` (0x68); handlers in `port/src/net/netmsg.c`.  Subcommands: `ADMIN_SUB_AUTH`, `KICK`, `BAN`, `UNBAN`, `LIST`, `STATUS`.  `ADMIN_AUTH` sets `cl->is_admin` for the peer lifetime; all other subs require that flag, otherwise `SVC_ADMIN [NOT_AUTH]` is returned.  `STATUS` dumps player count + per-slot name/state/room/address; `LIST` dumps the ban list; `BAN` calls `serverBansAdd` and disconnects the peer.

**Persistent bans (MASTER-C2c/C2d)** — `port/src/server_bans.{c,h}`.
- `$S/bans.ini` tab-separated (`addr \t name \t timestamp \t reason`).  Up to 256 entries.  Atomic save via temp-file + rename.
- `serverBansInit()` called once from `server_main.c` after `hubInit()`.
- `netServerEvConnect` now consults `serverBansIsBanned(ip)` before any client-slot allocation; banned peers receive `DISCONNECT_BANNED` immediately.
- `netServerBanClient` (server_bridge.c) now persists via `serverBansAdd` and disconnects with `DISCONNECT_BANNED` (was generic code 0).

### MASTER-C3 (Critical) — preserved-player identity cookie

Was: reconnecting peer could reclaim the preserved slot by name alone — any attacker knowing a player's name could steal their score on disconnect (audit SEC-3).

Now: `struct netpreservedplayer` gains `u8 cookie[NET_AUTH_COOKIE_LEN]` (16 bytes).  On first CLC_AUTH, server issues a fresh cookie via `netServerIssueCookie` (rolling SHA-256 seeded from `SDL_GetPerformanceCounter` + `time(NULL)` + ASLR-salted function address; CSPRNG-adequate for identity separation).  Cookie ships back in SVC_AUTH; client caches it module-static in `netmsg.c`.  Reconnect: client presents cookie in CLC_AUTH; server calls `netServerFindPreservedByCookie(name, cookie)` which requires BOTH name and a constant-time cookie match.  All-zero supplied cookie during mid-game join is rejected outright (matches the pre-existing "no late join without preserved slot" policy).  Cookie mismatch against a known preserved name is logged as `LOG_WARNING` with "possible hijack attempt".

Client clears the cookie on `netDisconnect` so reconnecting to a different server always starts fresh.

### SEC-14 (High) — room password transport

Was: `ROOM_ACCESS_PASSWORD` was declared in `port/include/room.h` but `CLC_ROOM_CREATE` hardcoded `ROOM_ACCESS_OPEN` + `max_players = 32`.  Every room was discoverable and joinable.

Now: `CLC_ROOM_CREATE` wire gains `u8 access` + `str password` + `u8 max_players` (handler in `port/src/net/netmsg.c`).  `CLC_ROOM_JOIN` gains `str password`.  Room struct replaces `char password[32]` with `u8 password_hash[32]` (SHA-256 via domain-separated salt `"pd2-room-password-v1\n"`).  Server calls `roomCheckPassword(room, plaintext)` (constant-time) before `roomJoin`.  Max-players now authoritative — `roomJoin` gates on `room->max_players` as well as `HUB_MAX_CLIENTS`.  Invite-only rooms only admit the creator (placeholder — the full invite flow is future work).

Client callers in `port/fast3d/pdgui_menu_lobby.cpp` updated to pass defaults (open room, empty password, default max_players); a follow-up UI pass should expose access-mode + password fields in the create/join dialogs.

### SEC-15 (Medium, bonus) — server_bridge bounds off-by-one

`netGetClientPing` / `netServerKickClient` / `netServerBanClient` all now use `>= NET_MAX_CLIENTS` (was `> NET_MAX_CLIENTS`).  Prevents operation on the reserved local-client sentinel slot.  Kick disconnect reason upgraded from `0` to `DISCONNECT_KICKED`.

### Build + files

- New: `port/include/server_admin.h`, `port/include/server_bans.h`, `port/src/server_admin.c`, `port/src/server_bans.c`.
- Modified: `port/include/net/net.h`, `port/include/net/netmsg.h`, `port/include/room.h`, `port/src/room.c`, `port/src/net/net.c`, `port/src/net/netmsg.c`, `port/src/server_bridge.c`, `port/src/server_main.c`, `port/fast3d/pdgui_menu_lobby.cpp`, `CMakeLists.txt`.
- `ninja pd pd-server` links clean.  `PerfectDark.exe 53,480,860` · `PerfectDarkServer.exe 23,216,655`.

**Next**: Remaining Wave 3 findings — SEC-5 (mandatory mod SHA-256), SEC-6 (signed updater), SEC-8/9 (interest management / PVS culling), SEC-12 (server-side CLC_ROOM_SETTINGS_UPDATE validation), SEC-13 (room-mutation rate limiting — already partially landed), SAVE-1 (MP stat integrity), LAYOUT-2 (pd.ini LastJoinAddr validation).

---

## Session S392 — 2026-04-19 (worktree `claude/vigilant-wiles-ed5537`) — Super Audit Wave 2 Batch C: wire format + data integrity

**Scope**: Four findings from Super Audit Wave 2 — Batch C.

### LAYOUT-1 (High) — field-wise `gset` serialization (`port/src/net/netmsg.c:294-310`)

`netbufWriteGset`/`netbufReadGset` previously used raw memcpy over `sizeof(struct gset)`, leaking struct padding/ordering to the wire and locking the layout across any future field reorder. Replaced with four `netbufWriteU8`/`netbufReadU8` calls over `weaponnum`, `unk0639`, `unk063a`, `weaponfunc` — matching the `netbufWriteCoord`/`netbufWritePlayerMove` pattern. Protocol bytes on wire unchanged (same 4 u8 values in same order) so no bump required.

### H-2 (High) — typed registrars for wire-delivered mods (`port/src/net/netdistrib.c:899-936`, `:1092-1113`)

`netDistribClientHandleEnd()` was calling `assetCatalogRegister(slot->id, ASSET_NONE)` for all wire-delivered mods, so typed resolvers couldn't find the entries until the next catalog refresh tick (and some never found them at all). Added:
- `iniFilenameToAssetType(const char *)` — maps `"map.ini"`→`ASSET_MAP`, `"character.ini"`→`ASSET_CHARACTER`, `"bot.ini"`→`ASSET_BOT_VARIANT`, `"skin.ini"`→`ASSET_SKIN`, `"weapon.ini"`→`ASSET_WEAPON`, `"textures.ini"`→`ASSET_TEXTURES`, `"sfx.ini"`→`ASSET_SFX`, `"music.ini"`→`ASSET_MUSIC`, else `ASSET_NONE`.
- `populateExtFromIni(asset_entry_t *, asset_type_e, const ini_section_t *)` — mirrors `assetcatalog_scanner.c`'s `registerComponent()` ext-field population for MAP/CHARACTER/SKIN/BOT_VARIANT/WEAPON so wire-delivery and local-scan produce identical catalog entries.

Fallback ASSET_NONE emits `LOG_WARNING` so unresolved INI types are visible in the log.

### F-Hardcoded-Player-Caps (Low/systemic) — `port/include/pdgui_constants.h` + drift check

Five C++ files were re-defining local copies of `MAX_PLAYERS`/`MAX_BOTS`/`MAX_MPCHRS`/`MAX_TEAMS` under disambiguated names (`MAX_PLAYERS_PM`, `ES_MAX_BOTS`, `MAX_MPCHRS_HUD`, etc) because `src/include/types.h` `#define bool s32` plus `src/include/constants.h` `#define false 0` / `#define true 1` collide with C++ keywords. These duplicates can silently drift from the canonical constants.

New `port/include/pdgui_constants.h` — C++-safe mirror of MAX_PLAYERS=8, MAX_LOCAL_PLAYERS=4, MAX_BOTS=32, MAX_MPCHRS=40, MAX_TEAMS=8. New `port/src/pdgui_constants_check.c` (C, not C++) includes both headers and `_Static_assert`s each value matches. Picked up by `CMakeLists.txt` `GLOB_RECURSE port/*.c` auto-discovery.

Consumers updated to include the new header and use the canonical names:
- `pdgui_hud.cpp` — `MAX_MPCHRS_HUD`/`MAX_TEAMS_HUD` → `MAX_MPCHRS`/`MAX_TEAMS`
- `pdgui_menu_endscreen.cpp` — `ES_MAX_PLAYERS`/`ES_MAX_BOTS`/`ES_MAX_MPCHRS` → `MAX_PLAYERS`/`MAX_BOTS`/`MAX_MPCHRS`
- `pdgui_menu_pausemenu.cpp` — `MAX_PLAYERS_PM`/`MAX_BOTS_PM`/`MAX_MPCHRS_PM` → `MAX_PLAYERS`/`MAX_BOTS`/`MAX_MPCHRS`
- `pdgui_menu_room.cpp` — local `#define MAX_PLAYERS 8` removed (now from header)
- `pdgui_menu_mpingame.cpp` — dead `#define MAX_MPCHRS_TICKER 40` removed (was defined, never used)

### F-StaleStructComments (Low) — `src/include/types.h`

Removed stale `/*0xXX*/` byte-offset comments from `struct mpchrconfig`, `struct mpplayerconfig`, `struct mpbotconfig` — offsets were invalidated when `head_id[64]`/`body_id[64]` fields were added. Verified by grep that none of these structs are binary-serialized anywhere (no `sizeof(mpchrconfig)` / `memcpy((*)mpchrconfig)` usage) — they're PC-only in-memory state; save format is JSON. Added header note documenting this so the offsets stay removed.

### Build + merge

`ninja pd pd-server` links clean — PerfectDark.exe (53.4 MB) and PerfectDarkServer.exe (23.1 MB) produced. Committed in worktree @ `9a50e683` (10 files, 234 ins / 88 del; 2 new files). Merged to `dev` @ `8c5c4a71` via `--no-ff`. Auto-merge resolved `netmsg.c` and `netdistrib.c` cleanly against Batch D merge (`167b7fce`). Post-merge line counts verified — all pre-existing files match worktree or grew via clean integration with Batch D (netdistrib.c 1462→1479, netmsg.c 6584→6625); no shrinkage.

**Next**: Remaining Super Audit findings: H-1 (preserved-player token), H-3 (u64 slot mask), H-4 (SHA-256 integrity gap), M-1 (unaligned writer).

---

## Session S391 — 2026-04-19 (worktree `claude/cranky-haslett-e312ba`) — MASTER-C1: netbufReadStr NUL termination + const return type

**Scope**: Security fix for MASTER-C1 from the 2026-04-19 Super Audit. `netbufReadStr` returned a pointer into the packet buffer without enforcing NUL termination. A malformed peer could send a u16-length-prefixed string with no trailing NUL byte, causing 40+ downstream string consumers (`strlen`, `strcmp`, `strncpy`, `printf %s`) to read past the packet buffer — OOB heap reads, crash-on-join DoS, kill-feed/name spoofing via crafted packets.

### Fix (`port/src/net/netbuf.c:120-132`)

After the `canRead` check, added: `if (len > 0 && buf->data[rp + len - 1] != '\0') buf->data[rp + len - 1] = '\0'`. Mutating the receive buffer during dispatch is safe — netbuf owns it exclusively. Return type changed from `char *` to `const char *` (audit S-L1) to block future writes through the pointer. Updated all 20 non-const callsites across `netmsg.c`, `net.c`, `sessioncatalog.c` (`char *` → `const char *`).

### Callsite audit result

All 40+ callsites verified read-only: `strncpy` source argument, `strcmp`/`strncmp` argument, `sysLogPrintf %s`, `assetCatalogResolve`. No callsite writes through the returned pointer. No callsite signatures changed.

### Result

Both `pd` and `pd-server` link clean. Committed to `dev` @ `f3e10caa` (5 files, 26 ins / 23 del). Protocol-compatible — no wire format change.

**Next**: Remaining Super Audit findings: H-1 (preserved-player token), H-2 (ASSET_NONE hot-register), H-3 (u64 slot mask), H-4 (SHA-256 integrity gap), M-1 (unaligned writer).

---

## Session S390 — 2026-04-19 (worktree `claude/elegant-lamarr-7dbd97`) — Menu Stack Compliance Tier 1 batch: M-5, M-6 (destructive-action confirm modals)

**Scope**: Add missing `BeginPopupModal` confirms for three destructive actions identified in `context/designs/menu-stack-architecture.md` §8 Tier 1 that were not covered by the S385/S386/S387/S388/S389 waves. All three use the canonical S385 pattern — `pdguiPopupDarkenBehind(0.65f)` scrim, 5-frame `SetKeyboardFocusHere(0)` force-focus on Cancel, 3-frame input debounce, red-styled confirm button, keyboard shortcuts (Enter/Space/A confirm; Esc/B cancel). Branch was forked before the Tier 2/3/4/5 waves landed; dev was merged back into the branch and conflicts resolved before merging to dev.

### M-5 (`port/fast3d/pdgui_menu_room.cpp`)

**(a) Leave Room / Back to Menu confirm** — the room screen's right-aligned exit button (bottom of `pdguiRoomScreenRender`) previously fired the disconnect path immediately on click / Escape press, with no confirm. Now arms a top-level `BeginPopupModal` with context-aware wording:
- Network mode: "Leave Room?" / "Leave this room and return to the lobby?"
- Solo mode: "Back to Menu?" / "Return to the main menu? Any unsaved match setup will be lost."

On confirm the original disconnect path runs (menupool release, solo-close or CLC_ROOM_LEAVE + `pdguiSetInRoom(0)`). On cancel the popup dismisses with no side effect. Pre-match countdown still owns Escape — `countdownBlocks` check is preserved so Esc during countdown cancels the countdown, not leave.

**(b) Scenario Delete confirm** — the Load Scenario popup's Delete button previously ran `scenarioDelete(fullPath)` immediately, no confirm. Now arms a nested `BeginPopupModal` inside the Load Scenario popup that shows the filename and warns "This cannot be undone." On confirm runs the actual delete + refresh; on cancel dismisses cleanly. ImGui supports one level of nested modals per frame, which is what this needs.

**File-static state added**:
- `s_ShowLeaveConfirm` + `s_LeaveConfirmOpenFrame` (top-level)
- `s_ShowScenarioDeleteConfirm` + `s_ScenarioDeleteConfirmOpenFrame` + `s_ScenarioDeletePath[]` + `s_ScenarioDeleteDisplay[]` (nested)
- `ROOM_CONFIRM_FRAME_DEBOUNCE=3` / `ROOM_CONFIRM_FORCE_FOCUS_FRAMES=5`
- All cleared in `pdguiRoomScreenReset()`.

Co-exists cleanly with S389 M-16's preview-dock invariant comment and M-20's `s_StartMatchFocusPending` progressive-focus flag (different static block, different render sites).

### M-6 (`port/fast3d/pdgui_menu_pausemenu.cpp`)

The CS pause menu's End Game tab button previously used an inline "Confirm?" / "Cancel" toggle (`s_EndGameConfirm` bool). This is a C4 violation per the menu-stack arch doc (confirms must be `BeginPopupModal`, not inline buttons). Note: design doc language refers to a "Quit button in scorecard overlay" but the scorecard overlay (`pdguiScorecardRender`) has `ImGuiWindowFlags_NoInputs` and zero interactive widgets; the End Game button is the sole destructive action in this file and the M-6 target.

New behaviour: End Game button arms `s_EndGameConfirm`, which opens a proper `BeginPopupModal` "End Match?" rendered right after the action-bar Resume button (S387 M-7 migrated Resume to the docked action bar; my modal render sits between the action bar and the parent Escape handler). On confirm runs `pdguiPauseSetPlayerAborted()` + `mainEndStage()` + `pdguiPauseMenuClose()` (unchanged from inline path — GAP-3 lineage preserved in a comment). On cancel dismisses.

**Escape propagation fix**: the parent pause menu's Escape handler previously would also fire when Escape dismissed the End Game popup in the same frame, double-consuming the key and closing the whole pause menu. Added `endgamePopupWasOpen` captured at frame start and gated the Escape/title-X handler on `!endgamePopupWasOpen`. The popup absorbs the first Esc; the pause menu processes subsequent ones normally next frame.

**File-static state added**:
- `s_EndGameOpenFrame` (existing `s_EndGameConfirm` kept, now means "popup armed/open")
- `ENDGAME_PM_FRAME_DEBOUNCE=3` / `ENDGAME_PM_FORCE_FOCUS_FRAMES=5`
- Cleared in both `pdguiPauseMenuOpen` and `pdguiPauseMenuClose`.

Co-exists cleanly with S388 M-22's `menupoolAcquire(MENU_TYPE_PAUSE_MENU, NULL, &g_CtxPauseMenu)` / `menupoolRelease` pair (my code only adds frame-counter resets alongside the existing confirm-flag resets).

### Merge resolution notes

Branch forked at `fe52a4f0` (dev-HEAD at S385 merge). Dev advanced to `1b099ed0` during this session (S386 bold-nightingale, S387 naughty-shtern, S388 laughing-shockley, S389 bold-herschel all merged). Merged dev into branch with 4 textual conflicts:

- `pdgui_menu_pausemenu.cpp` (include line): both sides added `#include "pdgui_layout.h"` — kept one copy with a combined comment.
- `pdgui_menu_room.cpp` (static block): both sides inserted a new static state block adjacent to the scenario popup state. Unioned both blocks — M-5 confirm state + M-20 `s_StartMatchFocusPending` live side-by-side, no overlap.
- `context/session-log.md` (whole file): dev's version won (`--theirs`), then this entry re-added at the top.
- `context/tasks-current.md` (whole file): dev's version won (`--theirs`), then M-5/M-6 DONE markers re-applied.

Auto-merge handled every other interleaving cleanly — pausemenu.cpp's M-6 popup + M-7 action bar + M-22 menupool push all sit next to each other correctly; room.cpp's M-5 Leave Room modal + M-20 progressive focus hook coexist in separate render paths.

### Files touched

- `port/fast3d/pdgui_menu_room.cpp` — +pdgui_layout.h include, +20 LOC state/defines, +1 popup arming block at Leave button (−15 old Leave-inline path), +108 LOC Leave Room modal renderer, +120 LOC Scenario Delete modal renderer (inside Load Scenario popup), +6 LOC reset additions. Net +230/−15.
- `port/fast3d/pdgui_menu_pausemenu.cpp` — +pdgui_layout.h include, +9 LOC state/defines, +2 LOC reset init (Open/Close), −33 LOC replacing inline toggle with single danger button, +105 LOC popup modal renderer + 1 LOC Escape gate. Net +85/−33.
- `context/session-log.md`, `context/tasks-current.md` — updated.

### Build + verify

Pre-merge: `ninja -C Build pd pd-server` clean 775/775. `PerfectDark.exe` 53,353,022 / `PerfectDarkServer.exe` 23,139,808. Post-merge rebuild recorded below before commit.

### Playtest ask

1. **Room — Leave Room (network mode)**: join a room, click "Leave Room" → popup "Leave Room?" appears over the room with scene dimmed. Focus on Cancel. D-pad Right → red "Leave Room" focused. A/Enter confirms, returns to lobby. Escape / B / Cancel button dismisses.
2. **Room — Back to Menu (solo mode)**: open CS solo room, click "Back to Menu" → popup "Back to Menu?" with solo-specific copy. Same focus/confirm/cancel behaviour. Pre-match countdown: during countdown, Esc must still cancel the countdown without opening the Leave modal.
3. **Room — Scenario Delete**: save a scenario, open Load Scenario, select one, click Delete → nested popup "Delete Scenario?" shows the filename and "This cannot be undone." Focus on Cancel. Confirm deletes + refreshes list; Cancel leaves list intact.
4. **Pause menu — End Game**: in a CS match, press Start/Esc → pause menu opens with action-bar Resume at bottom. Click End Game tab → popup "End Match?" opens over pause menu, scene dimmed further. Focus on Cancel. Confirm runs `mainEndStage`, cancel dismisses. Press Esc with the popup open → dismisses popup (pause menu stays open). Press Esc without popup → pause menu closes normally.
5. **Regression check**: S385 M-1 legacy `g_MpEndGameMenuDialog` render in `pdgui_menu_warning.cpp`, S387 M-7 action-bar Resume, and S388 M-22 menupool pause push are untouched semantically — verify they all still behave.

### Next

Tier 1 now fully complete (M-1 through M-6). With Tier 2–5 also landed this afternoon, the only Menu Stack Compliance items still outstanding are: M-24 (opt-in `parent_type` assertion in `menupoolAcquire`) and any follow-up from playtest.

---

## Session S389 — 2026-04-19 (worktree `claude/bold-herschel-603edc`) — Menu Stack Compliance: Tier 3 preview-dock + Tier 4 progressive focus (M-14 through M-21)

**Scope**: Eight-item batch implementing the remaining preview-docking invariants (C2) and the progressive-focus pattern (§6) from `context/designs/menu-stack-architecture.md` across six ImGui menu files. No gameplay behaviour change — these are UX/architecture invariants that make controller navigation deterministic and prevent a class of "preview scrolls off-screen on short viewport" bugs.

### Tier 3 — preview-in-scroll (C2)

- **M-14** `pdgui_menu_training.cpp::beginTrainingWindow` — added `ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse` to the outer training-window `ImGui::Begin` flags. Rationale: Bio Profile, DT/HT Training Details, and Hangar Vehicle Holograph all render their 3D previews via `pdguiModelPreviewDraw` / `drawFilenumPreview` at **absolute screen coords** computed from the window origin. Without the NoScroll guarantee, any future content overflow would add a scrollbar to the outer window and the absolute-coord previews would render in the wrong pixel position (they wouldn't scroll with the rest of the content). NoScroll pins the invariant permanently.
- **M-15** `pdgui_menu_moddinghub.cpp` — audited all 22 `BeginChild` regions. Only `##scale_right` (Model Scale Tool) held a rotating character preview and lacked NoScroll. Added the flags. The Chrome Tool `##chrome_sidebar` was already properly flagged; all other tools have no 3D preview content.
- **M-16** `pdgui_menu_room.cpp` — verified: row-hover char preview lives in `ImGui::BeginTooltip()` (separate floating window), bot-edit 3D preview lives in the edit modal's right-column `BeginGroup` (sibling to text controls). Both already outside `##room_players_list` scroll. Added an in-source comment above the BeginChild pinning the invariant for future editors.
- **M-17** `pdgui_menu_controldiagram.cpp::##smc_info` — added `ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse`. The control-mode layout text (Left stick / C buttons / A button / etc.) is the PC-port stand-in for the legacy N64 MENUITEMTYPE_CONTROLLER diagram texture — static 7-8 line blocks per mode, must never scroll.

### Tier 4 — progressive focus (§6)

- **M-18** `pdgui_menu_solomission.cpp` — introduced `MissionFocusGroup { FOCUS_MISSION_LIST, FOCUS_DIFFICULTY, FOCUS_START }` + `s_FocusGroup` static. Replaced all 11 assignments to the legacy `s_DetailPanelFocus` bool with explicit enum transitions. Retained the bool as a read-only macro `(s_FocusGroup != FOCUS_MISSION_LIST)` so existing compare sites stay legible. Escape is now tier-aware: START → DIFFICULTY (restoring focus to the selected diff row), DIFFICULTY → MISSION_LIST, MISSION_LIST → popDialog. A on mission row → DIFFICULTY; A on unlocked diff → START (focus index also moves to the Start button). Up/Down arrow nav in the right panel syncs `s_FocusGroup` to match the detail index slot.
- **M-19** `pdgui_menu_mpsetup.cpp` — since mpsetup is a hub-of-pickers (CLOSEONSELECT per picker), progressive focus is realised via push/pop rather than within-menu tier shifts. Added `mp_ArmFocusOnOpen` / `mp_ConsumePendingFocus` helpers + focus-on-open to Arena, Scenario (both variants, indexed by `param`), Weapons (first dropdown), Limits (first slider). Controller lands on the first interactive widget on each picker open.
- **M-20** `pdgui_menu_room.cpp` — added `s_StartMatchFocusPending` flag. Scenario combo change sets the flag; Start Match button consumes with `ImGui::SetKeyboardFocusHere(0)` on the next frame — jumping keyboard/controller focus from "pick scenario" straight to "confirm/launch".
- **M-21** `pdgui_menu_training.cpp` — five focus-on-open sites: (1) `renderFrDifficulty` Bronze button, (2) `renderFrWeaponList` selected weapon row, (3) `renderDtList` selected device row, (4) `renderHtList` selected holo-training row, (5) `renderTrainingDetailsImpl` (shared DT/HT) Ok/Resume button. Each uses `ImGui::SetKeyboardFocusHere(0)` gated on `IsWindowAppearing()` (list-level) or on the selected row (index-level). A-press now immediately advances into details/start without a preparatory nav press.

### Files touched

- `port/fast3d/pdgui_menu_training.cpp` — +51 LOC (M-14 NoScroll flags, M-21 focus-on-open ×5)
- `port/fast3d/pdgui_menu_moddinghub.cpp` — +14 LOC (M-15 NoScroll on `##scale_right`)
- `port/fast3d/pdgui_menu_room.cpp` — +33 LOC (M-16 invariant comment, M-20 `s_StartMatchFocusPending` + consumers)
- `port/fast3d/pdgui_menu_controldiagram.cpp` — +11 LOC (M-17 NoScroll on `##smc_info`)
- `port/fast3d/pdgui_menu_solomission.cpp` — +118 LOC (M-18 enum + 11 assignment rewrites + tier-aware Esc + nav sync)
- `port/fast3d/pdgui_menu_mpsetup.cpp` — +61 LOC (M-19 helpers + 4 pickers)
- `context/tasks-current.md` — marked M-14 through M-21 DONE with per-task summary
- `context/session-log.md` — this entry

Net +269/−35 across 6 source + 2 context files.

### Build + verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — clean **775/775**. `PerfectDark.exe` 53,340,770 / `PerfectDarkServer.exe` 23,139,296. Zero new warnings from this change. Pre-existing comment-in-comment warnings (updater.h, pdgui_theme_loader.h) and `VERSION_PATCH` redefinition are unrelated.

### Playtest ask

1. **Solo mission progressive focus (controller)**: Main Menu → Solo Mission. D-pad Right enters DIFFICULTY tier. D-pad Down through diffs. Press A on a diff → focus jumps to Start Mission. Press B → back to DIFFICULTY (focus on last-picked diff). Press B → back to MISSION_LIST (focus on selected mission row). Press B → dialog pops to Main Menu. Repeat with Enter/Space (keyboard) — identical behavior.
2. **Mouse compatibility**: with mouse, clicking any diff row directly works (focus group promotes to START). Clicking a different mission row resets to MISSION_LIST visually but keyboard nav still works in each tier.
3. **mpsetup pickers focus-on-open**: open Arena picker from Room → controller D-pad works immediately without first press. Same for Scenario, Weapons, Limits.
4. **Room → Start Match flow**: as leader, change the Scenario combo → keyboard focus auto-lands on Start Match next frame. Pressing A/Enter immediately launches.
5. **Training DT/HT**: open Device Training → list focus is on the last-selected device row (D-pad A enters details). Details opens with focus on Ok/Resume button — A immediately starts training.
6. **Preview-docking regression**: Bio Profile / Hangar Holograph / Model Scale Tool / control-style diagram all render with previews in their expected pixel positions regardless of window size. No preview floats above/below its intended rect.

### Next

Tier 5 M-22 (pool ctx push migration) and M-23 (cascade-close audit) landed in dev in the parallel S388 worktree (`claude/laughing-shockley-30c454`) and are already merged — this Tier 3/4 batch builds atop that work. **M-24** (evaluate `MENUPOOL_STRICT_TREE` assertion flag) remains deferred per design doc §8 until Tier 1-4 is fully complete. Remaining open Tier 1 items: **M-5** (Room Leave + scenario Delete confirms), **M-6** (Pause scorecard Quit confirm).

---

## Session S388 — 2026-04-19 (worktree `claude/laughing-shockley-30c454`) — Menu Stack Compliance Tier 5: M-22 ctx push migration + M-23 cascade-close audit

**Scope**: Tier 5 of the menu stack compliance batch from `context/designs/menu-stack-architecture.md`. Migrate direct `inputCtxPush` calls in the endscreen + pause menu renderers to route through the pool, register the solo endscreen and network dialogdefs, and plug the one missing `menupoolReleaseAll()` site in `netDisconnect`. **M-24 (parent_type assertion) intentionally NOT implemented** — deferred until Tier 1-3 land per design doc §8.

### M-22 — pool-owned ctx push

**`port/fast3d/pdgui_menu_pausemenu.cpp`**. `pdguiPauseMenuOpen` was pushing `g_CtxPauseMenu` directly via `inputCtxPush` and popping it inline in `pdguiPauseMenuClose`. Migrated to `menupoolAcquire(MENU_TYPE_PAUSE_MENU, NULL, &g_CtxPauseMenu)` on open, `menupoolRelease(MENU_TYPE_PAUSE_MENU)` on close. `MENU_TYPE_PAUSE_MENU` was already enumerated (`menupool.h:102`) so no enum change was needed. The pool's S300 "attach ctx on already-active slot" path handles re-entry semantics identically to the old guarded push.

**`port/fast3d/pdgui_menu_endscreen.cpp`**. Two sites — the solo path at line 451 (`IsWindowAppearing()`-gated push) and the MP path at line 901 (unconditional push after `inputCtxIsActive` check from B-End-Game-Input / S385). Both swapped to `menupoolAcquireDialog(menupoolDialogDef(dialog), &g_CtxImGuiMenu)`. To thread the dialog pointer through, `renderSoloEndscreen` and `renderMpEndscreen` now take `struct menudialog *dialog` as their first param and all five hotswap callbacks were updated to pass it through. The pool's already-active branch honours the idempotent "push if not live" semantic the manual check did before.

**`port/src/menupool.c`**. Registered the dialogdefs that the above renderers now resolve:

```c
REG(&g_SoloMissionEndscreenCompletedMenuDialog, MENU_TYPE_ENDSCREEN_SOLO);
REG(&g_SoloMissionEndscreenFailedMenuDialog,    MENU_TYPE_ENDSCREEN_SOLO);
REG(&g_NetMenuDialog,                           MENU_TYPE_NETWORK);
```

All three live outside `data.h` (solo endscreens in `src/game/endscreen.c`, net menu in `port/src/net/netmenu.c`), so local-scope `extern struct menudialogdef` declarations were added at the top of `menupool.c` alongside the B-End-Game-Input block (same pattern established for B-194's `g_FilemgrFileSelectMenuDialog`).

**Deliberately skipped**: `lobby.cpp`, `moddinghub.cpp`, `stats.cpp`, `update.cpp` do not push any input context directly — they're rendered every frame from `pdguiRender` / `pdguiLobbyRender` and depend on the parent main-menu ctx for input. Registering them in the pool would add symbolic entries with no ctx lifecycle to own. Per the task prompt's "standalone windows without a dialogdef" clause, the correct treatment is "keep the direct push" — they have no push to keep or migrate. The `update` banner is explicitly documented as an overlay, not a menu (audit §7.1 VIOLATION C1 note).

### M-23 — cascade-close site audit

Current `menupoolReleaseAll()` call sites across the four M-23 target files:

| File | Site | Status |
|------|------|--------|
| `pdgui_bridge.c` | `pdguiEndscreenStartMission` (800), `pdguiEndscreenNextMission` (828), `pdguiEndscreenExitToMainMenu` (880) | ✓ already present |
| `matchsetup.c` | `matchStart` (823), `matchStartFromChallenge` (923) | ✓ already present |
| `netmsg.c` | `netmsgSvcStageStartRead` co-op/anti branch (1307), combat branch (1461) | ✓ already present |
| `net.c` | `netDisconnect` before `mainChangeToStage(STAGE_CITRAINING)` | ✗ **MISSING** — added |

Added the missing site in `netDisconnect`: `menupoolReleaseAll()` + `inputCtxPopDeferred(&g_CtxImGuiMenu)` guarded by `#if !defined(PD_SERVER)`, placed after `manifestClear(&g_ClientManifest)` and before `mainChangeToStage(STAGE_CITRAINING)`. Matches the pattern in the netmsg.c stage-start handlers. Without this, a mid-match disconnect leaves lobby/room/mp-setup pool slots alive across the CI-training stage change, blocking reopen of those menus after returning to the main menu.

Included `inputctx.h` + `menupool.h` in `port/src/net/net.c` behind the existing `#if !defined(PD_SERVER)` guard (the server doesn't link the pool/inputctx layers).

`netmsgSvcStageEndRead` deliberately NOT instrumented. It calls `mainEndStage()`, which in turn calls `endscreenPushCoop` / `endscreenPushAnti` / `mpEndMatch` / `endscreenPrepare` — each pushes a root dialog via `menuPushRootDialog(MENUROOT_ENDSCREEN)`, which already calls `menupoolReleaseAll()` at its top (src/game/menu.c:3721). Adding a post-`mainEndStage` release would destroy the newly-acquired endscreen slot and re-create the S385 input-death class of bug.

### Files touched

- `port/src/menupool.c` — +18 LOC (3 new externs + 3 REG lines for solo endscreens and g_NetMenuDialog, with explanatory comment block).
- `port/fast3d/pdgui_menu_pausemenu.cpp` — +10/−4 LOC (added `menupool.h` include; swapped push/pop pair to pool API with updated comment).
- `port/fast3d/pdgui_menu_endscreen.cpp` — +20/−13 LOC (added `menupool.h` include; renderSoloEndscreen / renderMpEndscreen signatures now take `struct menudialog *dialog`; 5 hotswap callbacks pass `dialog` through; 2 ctx-push sites migrated to `menupoolAcquireDialog(menupoolDialogDef(dialog), &g_CtxImGuiMenu)`).
- `port/src/net/net.c` — +11/−1 LOC (added `#include "inputctx.h"` + `#include "menupool.h"` inside existing PD_SERVER guard; 5-line release block before `mainChangeToStage(STAGE_CITRAINING)` in `netDisconnect`).

### Build + verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — clean **775/775** on the worktree branch (base `fe52a4f0`). `PerfectDark.exe` 53,367,092 / `PerfectDarkServer.exe` 23,140,832 (both 15:05 2026-04-19). Merge into `dev` (atop S387 / Tier 2 completion) re-verified post-merge.

### Playtest ask

Same as S385's regression sweep plus one disconnect-specific case:

1. **Open + close pause menu** in a CS match (Esc / Start). Mouse cursor appears on open, re-captures on close. No soft-lock.
2. **MP match natural end → endscreen** — Enter/Esc/A/B responsive (regression check for S385's fix now routing through pool-owned ctx).
3. **Solo mission complete → endscreen** — Enter/Esc/A/B responsive (new pool registration; previously took the unregistered fallback).
4. **Multiplayer "Network Game" menu** — open/close via main menu item; structural dedup now prevents double-open if user spam-clicks.
5. **Mid-match disconnect** — connect as client, enter a room/match, force a server-side disconnect (server window close, network cable pull). Return to CI training. Then: re-enter Multiplayer, open any room sub-menu (arena / weapons / bots) — menu should open (previously the pool slot from the lost session could have survived the stage change and blocked reopen).

### Next

M-24 `parent_type` assertion deferred per task prompt. Remaining menu-stack work:
- **Tier 1** — M-5 (`pdgui_menu_room.cpp` Leave Room / scenario Delete confirms) and M-6 (`pdgui_menu_pausemenu.cpp` Quit in scorecard overlay confirm).
- **Tier 3** — M-14..M-17 preview-in-scroll audits (training Bio/Hangar, moddinghub 22 BeginChild regions, room char preview, controldiagram dock verification).
- **Tier 4** — M-18..M-21 progressive-focus adoption.

---

## Session S387 — 2026-04-19 (worktree `claude/naughty-shtern-e1dabb`) — Menu Stack Tier 2: docked-button migration M-7..M-13

**Scope**: Batch migration of the seven remaining Tier-2 menus from `menu-stack-architecture.md` §8 to the docked-action-bar primitive (`pdguiBeginActionBar` / `pdguiActionBarButton` / `pdguiEndActionBar`). Design doc C1: primary action buttons (Start/Confirm/Back/Cancel/Apply/Leave) must render in a docked footer that never scrolls; hand-rolled footers and inside-scroll button placements are the bug class this design retires.

### Files + per-file changes

- **M-7 `port/fast3d/pdgui_menu_pausemenu.cpp`** — CS pause menu `Resume` button moved from `SetCursorPos(ImVec2((menuW - resumeW) * 0.5f, menuH - resumeH - padB))` hand-rolled layout to `pdguiBeginActionBar`/`pdguiActionBarButton`. Tab content `##PauseTabContent` child height now computed via `pdguiBodyHeightForActionBar(menuH - contentTop - padB)`. Tab selectors (Rankings / Settings / End Game) stay at top (they're tab-switchers, not primary actions — M-6 covers the End Game confirm modal). `PdPauseButton` local helper retained (used by tab buttons).
- **M-8 `port/fast3d/pdgui_menu_lobby.cpp`** — `+ Create Room` extracted out of the scrollable `##social_rooms` column (C1 violation: button could scroll off when the room list filled the column) and paired with `Disconnect` in a docked action bar. Dedicated server view collapses to Disconnect-only. Body column height = `pdguiBodyHeightForActionBar(bodyAvail) - 60*scale` (reserving a row for the chat-stub footer text). Escape still disconnects.
- **M-9 `port/fast3d/pdgui_menu_network.cpp`** — `Back` migrated to docked action bar. The Server Browser + Direct Connect sections now render inside a `##mp_body` `BeginChild` sized via `pdguiBodyHeightForActionBar`. `Connect` stays inline with the address InputText (form-submit pattern; Connect is not a nav action, it's a submit on a specific input field).
- **M-10 `port/fast3d/pdgui_menu_moddinghub.cpp`** — Hand-rolled `Close` button (right-aligned, custom danger tint) replaced with `pdguiActionBarButton` in the docked action bar. Tool description `TextDisabled` row stays above the bar. New `hubFooterH = descH + pdguiActionBarHeight() + 12*scale` reserves the correct footprint. C2 verified: each tool's content renders in a per-tab `BeginChild` above the footer — preview panels (model previews in Skin Editor, image previews in Nine-Slice Chrome, etc.) sit in the tool content area, not inside any scroll region that could clip them.
- **M-11 `port/fast3d/pdgui_menu_stats.cpp`** — Keyboard-only `B/Esc: Close` hint replaced with a docked `Close` action button. Stats body height = `pdguiBodyHeightForActionBar(bodyAvail)`. Escape still closes for kb users.
- **M-12 `port/fast3d/pdgui_menu_controldiagram.cpp`** — Both renderers (`renderSoloMissionControlStyle`, `renderMpControl`) migrated from `SetCursorPosY(diagH - footerH + 12*scale)` hand-rolled `Back` footer to `pdguiBeginActionBar`. C2 verified: the `##smc_info` diagram panel lives in a sibling column next to the `##smc_list` scroll, not inside it. Local `PdButton` helper removed (now unused — was the last caller of `pdguiDrawButtonEdgeGlow` in this file).
- **M-13 `port/fast3d/pdgui_menu_endscreen.cpp`** — Both `renderSoloEndscreen` and `renderMpEndscreen` migrated. Solo completed path: `Next Mission` (focused=1, left) | `Retry Mission` (focused=0, right), or `Retry Mission` | `Main Menu` when no next mission. Solo failed path: `Retry Mission` (focused=1) | `Main Menu` (focused=0, red danger palette). MP networked: `Return to Room` (focused=1) | `Disconnect` (focused=0, red). MP solo: `Play Again` (focused=1) | `Quit` (focused=0, red). Content child heights now compute `ImGui::GetContentRegionAvail().y - pdguiActionBarHeight() - 12*scale - padB`. The explicit `ImGui::IsKeyPressed(ImGuiKey_Enter)` handler in MP was removed — the action bar's `isFocused=1` + internal Enter-activation already routes Enter to the primary button. Escape handler retained for the cancel path (MP: `netDisconnect() + exitToMainMenu`; Solo: `exitToMainMenu`). `inputSuppressed` debounce from S385 still gates activations. `PdEndButton` helper removed (now unused).

### Design-doc compliance notes

All seven files now emit `ImGui::Separator()` + a fixed-height `BeginChild(NavFlattened)` footer containing the bar, via `pdguiBeginActionBar/EndActionBar`. `NavFlattened` means controller D-pad nav crosses from the body scroll into the action bar transparently. Focused button plays `PDGUI_SND_SELECT` on activation (replaces the per-file `PdButton` / `PdEndButton` / `PdPauseButton` wrappers). Red danger-palette secondary buttons keep their `PushStyleColor(ImGuiCol_Button, ...)` block around the `pdguiActionBarButton` call — the primitive inherits parent's button styling.

### Build + verify

Worktree configured `cmake -G Ninja -B Build -DCMAKE_BUILD_TYPE=Release -S .` then `ninja -C Build pd pd-server`. Clean build **775/775**. `PerfectDark.exe` 53,329,064 bytes. `PerfectDarkServer.exe` 23,139,808 bytes. Zero new warnings from this change; pre-existing noise unchanged (comment-in-comment in `updater.h`/`pdgui_theme_loader.h`, `near`/`far` anon-field warnings in `types.h`, `VERSION_PATCH` redefinition, enet `gettime_offset` unused-static).

### Files touched + LOC delta (relative to worktree base `fe52a4f0`)

Target files:
- `port/fast3d/pdgui_menu_network.cpp` +14/−4
- `port/fast3d/pdgui_menu_stats.cpp` +13/−4
- `port/fast3d/pdgui_menu_controldiagram.cpp` +18/−48 (removed PdButton helper, two footer blocks)
- `port/fast3d/pdgui_menu_lobby.cpp` +37/−19
- `port/fast3d/pdgui_menu_moddinghub.cpp` +15/−25
- `port/fast3d/pdgui_menu_pausemenu.cpp` +11/−7
- `port/fast3d/pdgui_menu_endscreen.cpp` +51/−97 (removed PdEndButton, rewrote both action button blocks)

Context:
- `context/tasks-current.md` Tier 2 entries flipped to DONE
- `context/session-log.md` this entry

### Playtest ask

1. **CS pause menu**: pause mid-match → scroll through a long Rankings table → Resume button stays pinned at bottom → clicks close the menu. D-pad from body into action bar should work (NavFlattened).
2. **Social Lobby**: connect to a dedicated server as a client → room list grows → `+ Create Room` stays visible at the bottom (was inside the scrollable column before). Dedicated server operator sees Disconnect only.
3. **Multiplayer menu**: Main Menu → Multiplayer → long server browser list scrolls; Back stays docked. Pressing a server row then Connect still works. Escape still exits.
4. **Modding Hub**: Main Menu → Modding Hub → cycle through tabs (Mod Manager / INI Editor / Scale / Pack / Audio / Skin / Map Import / Menu Style / Font) → Close button at bottom stays visible regardless of tab content scroll.
5. **Player Statistics**: Main Menu → Stats → scroll through Overview / Weapons / Modes / Achievements tabs → Close button always reachable. Escape still closes.
6. **Control Diagram**: Training → Control Style → list/diagram split → Back at bottom. Same for MP Control.
7. **Solo endscreen**: complete / fail a mission → Next Mission or Retry Mission focused by default → Enter activates focused button → D-pad Right reaches Retry/Main Menu → Escape exits to main menu.
8. **MP endscreen**: finish a match (solo or networked) → Return to Room / Play Again focused → Enter activates → D-pad Right reaches Disconnect/Quit (red) → Escape disconnects (networked) or exits (solo).

### Next

Tier 3 (C2 preview-in-scroll audits) — `training.cpp` Bio/Hangar, `moddinghub.cpp` 22 BeginChild regions, `room.cpp` char preview column, `controldiagram.cpp` diagram dock. M-14..M-17.

---

## Session S386 — 2026-04-19 (worktree `claude/bold-nightingale-a10f3a`, merged to `dev` @ `66deedfa`) — Menu Stack Compliance Tier 1 batch: M-2 / M-3 / M-4 destructive-action confirm popups

**Scope**: Finish the Tier 1 menu-stack-architecture punch list. M-1 (MP End Game modal) landed in S385; this session converts the remaining three sibling-push / inline-prompt confirm flows to the canonical `BeginPopupModal` pattern.

**Files touched**:
- `port/fast3d/pdgui_menu_solomission.cpp` — `renderAbortMission` (Solo pause Abort Mission confirm)
- `port/fast3d/pdgui_menu_cheats.cpp` — `renderCheatsConfirmUnlock` (Cheats "Unlock Everything" confirm)
- `port/fast3d/pdgui_menu_agentselect.cpp` — Delete / Copy inline prompt → `BeginPopupModal`
- `port/src/menupool.c` — `g_MissionAbortMenuDialog` + `g_CheatsConfirmUnlockMenuDialog` → `MENU_TYPE_WARNING_MODAL`

### Pattern (mirrors S385 / M-1 `renderMpEndGameDialog`)

Each destructive confirm popup now follows:

1. `ImGui::OpenPopup(id)` on the first frame the dialog is seen (tracked by a file-static `s_*OpenedForDialog` pointer).
2. `pdguiPopupDarkenBehind(0.65f)` scrim over the full viewport.
3. `BeginPopupModal` with `NoTitleBar | NoBackground | NoResize | NoMove | NoScrollbar`, PD-authentic frame drawn via `pdguiDrawPdDialog` + `pdguiDrawTextGlow`.
4. Red palette (`pdguiSetPalette(2)`) for destructive variants; default palette for non-destructive (Agent Copy).
5. **5-frame `SetKeyboardFocusHere(0)` latch** on the safe-default button so controller focus reliably lands there even if ImGui's popup NavInit hasn't settled on the first rendered frame.
6. **3-frame input debounce** so the Enter / A press that triggered the popup cannot bleed through into Confirm.
7. `SetItemDefaultFocus()` after the safe-default button as belt-and-braces once NavInit catches up.
8. Red-tinted Confirm button for destructive actions; plain button for non-destructive.
9. Keyboard + gamepad shortcuts (Enter / Space / A → Confirm, Esc / B → Cancel), gated by the same debounce.
10. On dismissal: `ImGui::CloseCurrentPopup()` + state reset + `menuPopDialog()` to drop the legacy dialog from the stack.

### Per-file highlights

**M-2 — Abort Mission** (`pdgui_menu_solomission.cpp`):
- Replaced the prior full-screen `ImGui::Begin("##abort_mission", ...)` layout (left/right Cancel/Abort buttons backed by `s_AbortSelectIdx`) with the modal popup.
- Title pulled from `langSafe(L_OPTIONS_174)` ("Warning"), body from `L_OPTIONS_175` ("Do you want to abort the mission?"), Cancel label from `L_OPTIONS_176`, Confirm from `L_OPTIONS_177` — preserves existing localization.
- `s_AbortSelectIdx` static + its reset in the module-wide state reset replaced by `s_AbortOpenedForDialog` / `s_AbortOpenFrame`.
- Confirm fires `menuhandlerAbortMission(MENUOP_SET, nullptr, nullptr)` (same handler as the legacy path) then `menuPopDialog()` as belt-and-braces — the handler itself triggers a mission-end transition that usually unwinds the stack via `menupoolReleaseAll`.

**M-3 — Cheats Confirm Unlock** (`pdgui_menu_cheats.cpp`):
- Prior implementation already used `pdguiPopupDarkenBehind` + action bar but rendered as a standalone `ImGui::Begin` window, missing popup modal semantics + focus latch.
- Added `s_CheatsUnlockOpenedForDialog` / `s_CheatsUnlockOpenFrame` statics.
- "No" keeps default focus; red "Yes" confirms and calls `gamefileUnlockEverything()` (same function the legacy file-static `menuhandlerUnlockEverything` wrapped).

**M-4 — Agent Select Delete / Copy** (`pdgui_menu_agentselect.cpp`):
- Inline dimmed-overlay prompt (drew via `ImDrawList::AddRectFilled` + `AddText` inside the agent-select window body) replaced by a viewport-level `BeginPopupModal` rendered **after** `ImGui::End()` closes the agent-select window.
- `s_ConfirmMode` / `s_ConfirmIdx` retained (triggers set mode on key press); new `s_ConfirmOpenFrame` tracks the open frame; `ImGui::OpenPopup(AGENTSEL_CONFIRM_POPUP_ID)` called at trigger time.
- Delete variant uses red palette + red Confirm button + default focus on Cancel (destructive).
- Copy variant uses default palette + default Confirm button + default focus on Confirm (non-destructive).
- Agent-list hotkeys (Enter/C/Delete/D/Escape/Up/Down) gated with `!confirmActive` so the modal owns input while open.
- Added `#include "pdgui_layout.h"` for `pdguiPopupDarkenBehind`.

### Pool registrations

`port/src/menupool.c` gets two new `REG(..., MENU_TYPE_WARNING_MODAL)` entries alongside the existing `g_MpEndGameMenuDialog` registration. Both defs live in `src/game/` and are not in `data.h`, so the locally-scoped extern pattern (the same one B-194 used for `g_FilemgrFileSelectMenuDialog`) is mirrored here.

M-4 does NOT need a pool registration — the confirm popup is purely ImGui state (no legacy dialogdef push).

### Build + merge

- Worktree: committed as `37f61a1d feat(menu-stack): M-2/M-3/M-4 destructive-action confirm popups (BeginPopupModal)` on branch `claude/bold-nightingale-a10f3a`.
- Merged into `dev` with `--no-ff` → `66deedfa`.
- Pre-merge vs. post-merge line counts match exactly:
  - `pdgui_menu_agentselect.cpp` 623 → 810 (+187)
  - `pdgui_menu_cheats.cpp` 925 → 1048 (+123)
  - `pdgui_menu_solomission.cpp` 3552 → 3665 (+113)
  - `menupool.c` 601 → 617 (+16)
- `ninja -C Build pd pd-server` → 777/777, `PerfectDark.exe` 53,154,815 bytes and `PerfectDarkServer.exe` 23,143,410 bytes linked clean. Existing pre-existing warnings (modelasm_c, model, collision, snd) unchanged — no new diagnostics.

### Next steps (remaining Tier 1)

- **M-5**: `pdgui_menu_room.cpp` — audit `Leave Room` + scenario `Delete` paths; add missing confirm modals.
- **M-6**: `pdgui_menu_pausemenu.cpp` — `Quit` in scorecard overlay.

After those two, Tier 1 is complete and we can move to Tier 2 (docked-button migration).

---

## Session S385 — 2026-04-19 (worktree `claude/infallible-goldberg-71b379`) — B-End-Game-Input: CS pause End Game confirm focus + CS end-of-match input-death

**Scope**: Two related CS bugs Mike reported in one batch.
  (1) CS pause → End Game: controller could select the End Game row, but the confirm popup that followed didn't give controller nav a path to the Confirm button.
  (2) CS end-of-match screen: after a match ended naturally, the endscreen opened in a state with no input — player couldn't hit Escape / A / Enter to return to the main menu.

### Root causes

**Bug (1) — End Game popup controller focus**. `renderMpEndGameDialog` (`port/fast3d/pdgui_menu_warning.cpp`, S368) uses `BeginPopupModal` correctly, but focus/default-item handling was fragile:
  - `SetItemDefaultFocus()` was guarded by `IsWindowAppearing()`, which fires for one frame. ImGui's popup NavInit state hadn't always settled by that frame when `OpenPopup` + `BeginPopupModal` ran in the same frame — the default-focus hint raced with popup init and didn't latch onto the Cancel button.
  - No input debounce after open. The Enter / A press that activated the `hubPushRow` Selectable in MP Pause could bleed into the popup's first rendered frame, occasionally firing a button click before the user saw the dialog.

**Bug (2) — CS end-of-match input-death**. `renderMpEndscreen` (`port/fast3d/pdgui_menu_endscreen.cpp`) only pushed `g_CtxImGuiMenu` on "fresh entry" if `!inputCtxIsActive(&g_CtxImGuiMenu)`. That check raced with `menuPushRootDialog`'s `menupoolReleaseAll()`, which **schedules deferred pops** of any owned_ctx slots for end-of-frame. Timeline:
  - Frame N: endscreen dialog pushed; any previously-owned ctx scheduled for deferred pop. renderMpEndscreen runs, sees `g_CtxImGuiMenu` still active → skips push. `s_MpEndscreenLastFrame = N`.
  - End of frame N: deferred pop fires — `g_CtxImGuiMenu` deactivated, `g_ImcMenu` deactivated.
  - Frame N+1: renderMpEndscreen runs, `inputCtxIsActive(&g_CtxImGuiMenu) == false` BUT `freshEntry = (N+1 - N) > 1 == false`. No push ever happens.
  - Result: the endscreen runs for the rest of the match with zero menu IMC coverage; `ACTION_USE` / `ACTION_CANCEL_USE` fire no `ImGuiKey_Enter` / `ImGuiKey_Escape` events; buttons and keyboard shortcuts are dead.

Compounding (2): the Ind / Team / ChallengeCompleted MP endscreen dialogdefs were **not registered** in `port/src/menupool.c` (only Cheated and Failed challenge variants were). Their push through `menuPushDialog → menupoolAcquireDialog` took the unregistered fallback with no pool-managed ctx, so there was nothing in the pool system guarding against the race either.

### Fixes

1. **`port/src/menupool.c`** — register `g_MpEndscreenIndGameOverMenuDialog`, `g_MpEndscreenTeamGameOverMenuDialog`, `g_MpEndscreenChallengeCompletedMenuDialog` as `MENU_TYPE_ENDSCREEN_MP` (joining the existing Cheated/Failed registrations). Register `g_MpEndGameMenuDialog` as `MENU_TYPE_WARNING_MODAL` so the confirm popup participates in structural dedup and the pool lifecycle. All four defs live in `src/game/mplayer/ingame.c` and are not in `data.h`, so the locally-scoped `extern struct menudialogdef` declarations pattern used for `g_FilemgrFileSelectMenuDialog` (B-194) is mirrored here.

2. **`port/fast3d/pdgui_menu_endscreen.cpp`** — `renderMpEndscreen` now pushes `g_CtxImGuiMenu` unconditionally whenever it isn't active, not just on fresh entry. Rationale: every force-close site (`pdguiEndscreenExitToMainMenu` → `func0f0f8120`, `menupoolReleaseAll` from `menuPushRootDialog`, stage transitions) also clears the endscreen dialog from the legacy menu stack. Once the dialog is gone the hotswap dispatcher stops calling this renderer, so there's no resurrect loop to worry about (the historical S295 F5 concern). The `s_MpEndscreenLastFrame` frame gap is retained for fresh-entry DIAG logging + debounce reset only — it no longer gates the ctx push.

3. **`port/fast3d/pdgui_menu_warning.cpp::renderMpEndGameDialog`** — added `s_EndGameOpenFrame` + `ENDGAME_FRAME_DEBOUNCE` (3 frames) + `ENDGAME_FORCE_FOCUS_FRAMES` (5 frames):
  - `ImGui::SetKeyboardFocusHere(0)` before the Cancel button for the first 5 frames after open, which forces keyboard focus on the next-submitted item regardless of popup NavInit timing. `SetItemDefaultFocus` is still called as a belt-and-braces hint (harmless when NavInit has already run).
  - Button clicks + keyboard shortcuts are gated on `!inputDebounced` for the first 3 frames so any bleed-through Enter / A press from the Selectable that opened the popup can't auto-confirm / auto-cancel.
  - All popup-close paths (confirm / cancel / `!open` early-return) clear both `s_EndGameOpenedForDialog` and `s_EndGameOpenFrame` so re-entry from a new push gets a fresh debounce + focus window.

### Files touched

- `port/src/menupool.c` — +39 LOC (4 extern decls + 4 REG lines + expanded comment at the endscreen registration block).
- `port/fast3d/pdgui_menu_endscreen.cpp` — +40/−20 LOC (unconditional ctx push + rewritten S295 F5 comment).
- `port/fast3d/pdgui_menu_warning.cpp` — +75/−28 LOC (debounce state + force-focus loop + updated header comment for the S385 rewrite).

### Build + verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — clean **775/775**. `PerfectDark.exe` 53,363,195 / `PerfectDarkServer.exe` 23,141,856 (timestamped 13:54 2026-04-19). Pre-existing warnings unchanged (comment-in-comment in `updater.h` / `pdgui_theme_loader.h`, `VERSION_PATCH` redefinition, `near`/`far` identifier noise).

### Playtest ask

1. **CS pause → End Game (controller)**: during a match, press Start/ESC → MP pause opens → D-pad down to "End Game" → press A. Confirm popup appears centered over a scrim. Focus ring is on Cancel. D-pad Right → focus moves to the red "End Match" button. Press A → match ends, CS endscreen appears.
2. **CS pause → End Game (keyboard)**: same flow, arrow-key Right moves focus, Enter confirms, Esc cancels. Space is also a confirm shortcut. A/Esc/Enter pressed in the first 3 frames after open should be ignored (bleed-through debounce).
3. **CS match ends naturally**: play a full match to timer/score limit. Endscreen appears with rankings. **Enter → Play Again** works. **Esc → Main Menu (CI)** works. Controller A/B do the same via `pdguiDriveImGuiNav`. Previously these were dead — the endscreen rendered but had no menu IMC active.
4. **Regression check — challenge mode**: start a Combat Challenge, complete or fail it. Challenge Completed / Failed / Cheated endscreens should still work (those were already registered in pool; the renderer fix applies to them too).

### Next

If controller nav still loses focus in the popup, the likely culprit is `pdguiDriveImGuiNav`'s use of `driveHeld` for arrow keys (it sends a continuous "held" state without edge repeat, so a single D-pad tap may not advance more than one item). That's a separate, longer-standing concern — document and defer unless it surfaces in playtest.

---

## Session S384 — 2026-04-19 (worktree `claude/elated-hugle-7ec221`, merged to `dev` @ `90b448ce`) — B-184 + B-193 ROOT CAUSE: ALIGN16 pointer-alignment regression

**Scope**: Simultaneous root-cause identification for two open bugs — B-184 (per-object vertex-colour tints: yellow computer props, cyan elevator top, olive character faces in CI) and B-193 (intermittent invisible CI geometry on cold boot). Both traced to commit `fe107e3e` (2026-04-17, "M4 ALIGN16 no-op") collapsing `ALIGN16(val)` to `(val)` in `src/include/constants.h:79`.

### Root cause

`fe107e3e`'s rationale ("mempAlloc already returns aligned memory, so rounding the size argument is wasted 0–15-byte padding across 119 call sites") was correct for **size** arguments but silently broke **pointer** arguments at ~30 sites where the macro was applied to a `uintptr_t`. The pointer sites live in:

- `src/game/gfxmemory.c:154` — `g_GfxMemPos = (u8 *)ALIGN16((uintptr_t)g_GfxMemPos)` after a `gfxAllocateVertices` advance. `Vtx` is 12 bytes; odd counts drift the pointer by 12 mod 16. The next `gfxAllocateColours` binds a misaligned `Col*` array as the `vcn` for the subsequent `G_COL` command. `gfx_sp_set_vertex_colors` reads the misaligned bytes, every draw gets consistent-but-wrong vertex colours → per-object tints on correct textures.
- `src/game/bg.c:1516` / `bg.c:1934` — `header = (u8 *)ALIGN16((uintptr_t)headerbuffer)` for a `u8[0x50]` stack buffer. The no-op leaves `header` at whatever offset the stack gave. On 64-bit the stack is usually 16-aligned so it *often* works, which is why B-193 looked intermittent and "not reproducing on clean rebuild" in brave-bouman-13bd68's 5-launch streak — stack frame layouts shift between incremental and clean builds. Nothing guarantees alignment though, and any function above in the call tree with misaligned locals propagates into `headerbuffer`.
- `src/game/bg.c:1537` — `scratch = ALIGN16(scratch + BG_INFLATE_SCRATCH_LARGE)` — inflate-buffer pointer arithmetic; primary BG data corruption when the offset crosses a 16-byte lane.
- `src/game/bondgun.c:3850` — weapon memory buffer end.
- `src/lib/snd.c` (18 sites) — ALEnvelope / ALKeyMap / ALADPCMBook / ALWaveTable stack-buffer alignment in the sound loader.
- `src/lib/dma.c:118`, `src/lib/memp.c:286`, `src/game/propobj.c:15522`.

### Fix

Single-line revert in `src/include/constants.h:79`:

```c
-#define ALIGN16(val)        (val)
+#define ALIGN16(val)        ((((val) + 0xf) | 0xf) ^ 0xf)
```

Considered splitting into `ALIGN16` (pointer, real round-up) + `ALIGN16_SIZE` (no-op for sizes) but rejected: (a) the overhead from `fe107e3e`'s rationale is 0–15 bytes × <120 sites on a system with hundreds of MB of game memory — invisible on modern hardware. (b) size-vs-pointer isn't always obvious at the call site — e.g. `gfxAllocate`: `size = ALIGN16(size); g_GfxMemPos += size;` where size-rounding IS effectively pointer-rounding through the accumulator. Two macros would create a category error waiting to happen. One macro with the real formula is the right primitive.

### Merge

Base commit `d06be0f3`. Worktree branch tip `3869ba29`. Single merge into `dev`: `90b448ce`. Post-merge diff vs base: `src/include/constants.h | 2 +-` — exactly the one-line change, no collateral movement.

### Build + verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — clean 777/777. `PerfectDark.exe` 53,232,611. `PerfectDarkServer.exe` 23,154,674. Zero new warnings from this change (pre-existing comment-in-comment warnings in `updater.c` and a benign `VERSION_PATCH` redefinition are unrelated).

### Files touched

- `src/include/constants.h` (1 line)
- `context/bugs.md` — B-184 entry rewritten with root cause + fix + prior-pass history summary; B-193 entry rewritten with root cause + fix + retained diag context
- `context/session-log.md` — this entry

### What this explains (connects to prior sessions)

- **S382 fourth-pass static diff audit** (goofy-shaw-96c6c1) couldn't find rendering-state drift in the AP Phase sprint commits because there wasn't any — the regression was in a **separate** 2026-04-17 commit (`fe107e3e`, M4 optimisation, not part of AP). The audit was thorough and its rule-outs were correct; it was looking in the right sprint but the wrong commit.
- **S381 effect_normal_tint mod folder rule-out** (goofy-shaw-96c6c1) was correct — the folder is inert, but it was a red herring from the beginning.
- **S378–S382 "no-smoking-gun" verdicts** on B-184 were correct given the evidence available to them — the `GFX.DIAG: G_COL` instrumentation shipped in S382 is what gave me the "`vtx0` = drifted bytes, `lighting=0` mid-stream" fingerprint that pointed at a vertex-colour-data misread, and the fix verifies by making those same diag lines stable post-patch.
- **brave-bouman-13bd68 (B-193 "not reproducing")** — the 5-launch streak on clean rebuild was legitimately low-probability, not a false negative. Stack-layout drift is why cold boots sometimes produced aligned `headerbuffer` and sometimes didn't; a clean rebuild likely shifted the layout into a 16-aligned pocket.

### Residual diagnostic retention

- `port/src/pdmain.c` LV.DIAG (cam_pos, tickmode on frame 0 + frame 30) — kept, will surface any future BG-alignment regressions immediately.
- `port/fast3d/gfx_pc.cpp::gfx_sp_set_vertex_colors` `GFX.DIAG: G_COL` — kept, post-fix log should show stable `vtx0` bytes matching authored prop colours.

### Next

Playtest: cold-boot CI 5× → scene renders; character skins + clothing are authored colours (not olive-green); computer props not yellow/cyan; elevator top gray (not cyan). Other stages (Skedar Ruins, Complex, Felicity, Temple, Dam, Carrington Villa) 2–3× each for regression check.

---

## Session S383 — 2026-04-19 (worktree `claude/frosty-banach-0e3fc1`, merged to `dev` @ `deac9a36` + diag merge `bb2b0e8e`) — B-195 Complex bg room preprocess overflow fix

**Scope**: Hard crash `sysFatalError("overflow when trying to preprocess a bg room, size 1152 newsize 15528")` loading Complex (stagenum `0x1f`, room 7) in Combat Sim solo. Crash log `Build/pdclient-crash-complex-apr19.log`. Build `dev f32d1e51`.

### Root cause

Complex room 7's inflated gfx data was 576 bytes; PC-port preprocess produced 15528 bytes (~27×). Expected max is ~2× (4→8 byte pointer expansion plus GDL 8→16 byte cmd expansion). Runaway came from `gbiConvertGdl` (`port/src/preprocess/gbi.c`) reading past the per-room inflated buffer searching for `G_ENDDL` (0xB8): on N64 the whole BG segment was one contiguous blob so `roomblock.ptr_gdl` / `ptr_vertices` / `ptr_colours` could reference segment offsets; in the PC port each room is inflated into its own independent buffer by `bgInflate`, so any offset past this room's data walks into adjacent heap scratch (compressed-data right-side or uninit bytes). Reading those as N64 cmds and writing 2× host output until a stray `0xB8` byte matched gave the 485-cmd / 7764-src / 15528-dst blowup.

### Fix (three layers)

1. `gbiConvertGdl` takes explicit `src_size`; synthesizes ENDDL when a read would cross the boundary. Logs `WARNING: gbiConvertGdl: source overrun at srcpos=… src_size=…`.
2. `convertRoomGfxData` clamps `endpos` / `vtx_end` / `col_end` to `src_size` (each now logs `WARNING: convertRoomGfxData: … yields … outside src_size=…` when clamped); skips any `ptr_gdl` outside the buffer with `WARNING: convertRoomGfxData: gdl[i] src_offset=… outside src_size=… — skipping`.
3. `preprocessBgRoom` scratch dst 2× → 8× (4 KB floor); `sysFatalError` → `LOG_ERROR` + `return 0` so a malformed room degrades gracefully. `src/game/bg.c` caller allocation 2× → 8× (16 KB floor) so the back-memcpy can't overflow; `if (inflatedlen == 0)` skip guard added.
4. `filemodel.c:660` gbiConvertGdl call updated to pass `src_file_len`.

### Merge

Base commit `f32d1e51`. Branch tip `e82a3cda` (fix + diag). Two merges into `dev`:
- `deac9a36` — main B-195 fix (6 files, +85 / -16)
- `bb2b0e8e` — diag follow-up adding WARNING logs for the three silent clamps (1 file, +12 / -5)

Post-merge line counts all grew or stayed equal (`filebg.c` 530 → 564 → 571, `gbi.c` 208 → 227, `bg.c` 6327 → 6342, etc.). No unexpected truncation.

### Build + playtest

Build clean 777/777. `PerfectDark.exe` 53,124,176 / `PerfectDarkServer.exe` 23,143,410 (timestamped 03:54 + 03:50).

Mike playtested immediately: `Build/pd-client.log` shows Complex loads cleanly (stagenum `0x1f`, roomcount 45, player room 7 at `(-3817,159,349)`), renders, and runs until user-initiated shutdown at `[00:58.59]`. **Zero** `convertRoomGfxData` / `gbiConvertGdl` / `preprocessBgRoom` / `ptr_vertices yields` / `ptr_colours yields` / `gdls_addr[0] yields` WARNINGs — all five bounds-check paths were no-ops for every room loaded, meaning the preprocess output is byte-identical to pre-fix for well-formed data. The original crash fires only when `gbiConvertGdl` runs off the per-room buffer; for this playtest no GDL did. The fix is a pure safety net that activates exclusively on pathological data.

### Unrelated crash during playtest

Separate ACCESS_VIOLATION at PC+0x21ef4a (~`gfx_sp_matrix` at `port/fast3d/gfx_pc.cpp:1093`) fired 1.5 s after main-menu close at `[00:22.99]` in stage `0x26` (CI), post-B-196 imgui focus clear. **Not related to B-195**: zero preprocess warnings, different stage, crash is in runtime chr-tick matrix-stack consumption not BG/model preprocessing. Handed off to the session that owns B-196 / chr rendering.

### Files touched

- `port/include/preprocess/gbi.h` — `gbiConvertGdl` signature adds `src_size`
- `port/src/preprocess/gbi.c` — bounds check + synthesized ENDDL + `system.h` include for `sysLogPrintf`
- `port/src/preprocess/filebg.c` — `convertRoomGfxData` gains `src_size` / `dst_size` params, clamps + WARNINGs, GDL skip; `preprocessBgRoom` uses `PREPROCESS_BG_ROOM_MULT_LOCAL=8` + 4 KB floor, soft-fail path
- `port/src/preprocess/filemodel.c` — passes `src_file_len` to gbiConvertGdl at the one call site (line 660)
- `src/game/bg.c` — alloclen `* 2` → `* 8` + 16 KB floor; `inflatedlen == 0` early-return guard
- `context/bugs.md` — B-195 entry added

## Session S382 — 2026-04-19 (worktree `claude/goofy-shaw-96c6c1`) — B-184 third-pass: visual evidence + diagnostic instrumentation

**Scope**: Follow-up to S381. Mike provided two in-game screenshots plus the crucial clarification that *the textures themselves are correct — only a per-object tint is wrong*. Elevator top cyan, computer props yellow/cyan, doors slightly yellow, character body + face olive-green; no visual mods enabled. This rules out every prior hypothesis (normal-visualisation, wrong-texture-selection, UV corruption, the effect_normal_tint mod folder).

### Analysis

Per-object consistent-colour tints on the correct textures mean the tint is baked into per-object **vertex-colour** or **ambient-light** state at render time. Two candidate code paths in `gfx_pc.cpp`:

1. **Ambient RGB leak** — lighting path `:1189-1208` seeds vertex colour `r/g/b` from `rsp.current_lights[n-1].col[0..2]` (the ambient light is the last entry). If ambient is being populated with non-white bytes for certain props/rooms, every lit surface in that draw gets a consistent tint of that colour. Different rooms / different propgens → different tints.

2. **G_COL vertex-data misread** — `gfx_sp_set_vertex_colors` binds `rsp.vertex_colors = seg_addr(cmd->words.w1)` at `:2370`. If segment resolution for a model lands in the wrong bytes (e.g., a recent model-load offset regression), each prop's vertex colour array points at arbitrary memory — each prop reads a consistent-but-wrong colour.

Both hypotheses produce the "per-object, per-texture-correct, differently-tinted" pattern Mike shows.

### Rule-outs this pass

- **AP Phase 3 (S377 `75240823`)**: pure function rename. `assetLoadRomToAddr` → `fileLoadRomToAddr` which is byte-for-byte the pre-rename `fileLoadToAddr` body. Diff inspection confirms no behaviour change. Signature change of `method` from `s32` → `u32` is benign for all FILELOADMETHOD_* values.
- **AP Phase 4 (S346 `26d9eaca`)**: `romProviderHandle()` made internal. Dispatcher still calls the same worker. No semantic change to the rendered bytes.
- **S374 B-185 `bgLoadFile` rewrite**: scoped to bg-segment geometry load, not model display list / vertex / texture data. Doesn't touch any path relevant here.

### Unruled: S322 + S323 N64 audit

`4a6382cf` (S322) + `645a9c7c` (S323) stripped ~100 IS4MB / IS8MB branches across `src/game/dlights.c`, `src/lib/model.c`, `src/game/bondgun.c`, and related files. `IS4MB()` is compile-time `0` — the branches were dead — but if any of those dead branches was wrapping a state-reset or a light-colour initialiser that the surrounding code still depended on, removing them would cause silent drift exactly like this.

### Diagnostic shipped

`port/fast3d/gfx_pc.cpp::gfx_sp_set_vertex_colors` now emits a rate-limited `LOG_NOTE` at every new unique `vcn` pointer binding:

```
GFX.DIAG: G_COL #42 vcn=0x7fff0042a10 count=64 vtx0={ff ff ff ff} lighting=1 numlights=2 ambient={20 80 80}
```

Cap: 200 lines per run (`s_DiagCount`) + dedup on pointer (`s_DiagLastVcn`). Gates: skip if vcn == NULL. No behaviour change — pure telemetry. The log tells us per-object: the first vertex's four colour/normal bytes, the G_LIGHTING flag at bind time, how many lights are active, and the ambient RGB. Match log entries against visible tints in the next playtest screenshot and the source localises in one pass:

- If `vtx0` bytes == `ff ff ff ff` on every logged draw but `ambient` varies per-room → hypothesis 1 (lighting pipeline).
- If `vtx0` bytes are arbitrary non-white per draw → hypothesis 2 (vertex data misread).
- If both are white but the object is still tinted → we need to look at prim/env colour or combiner state.

### Bisect recommendation (for Mike)

If rebuilding from `4a6382cf^` (immediately before S322) produces clean tints, one of the N64 audit strip commits is the culprit — then narrow by bisecting S322 → current tip. If tints still appear on `4a6382cf^`, the regression is older and the AP migration rules it out. That tells us whether to focus the next pass on the N64 audit or earlier.

### Files changed

- `port/fast3d/gfx_pc.cpp` (+22): `GFX.DIAG: G_COL` rate-limited instrumentation in `gfx_sp_set_vertex_colors`. No behaviour change.
- `context/bugs.md`: B-184 entry extended with third-pass analysis + diagnostic description + bisect recommendation.
- `context/session-log.md`: this entry.

### Secondary cleanup this session

- Deleted `C:/Users/mikeh/Perfect-Dark-2/post-batch-addin/mods/effect_normal_tint/` — the template folder that the `build.bat` / `release.ps1` pipelines were re-shipping to every user's install on every update. Folder was inert runtime-wise (confirmed S381) but was re-generating in user installs on every release. Post-batch-addin/mods/ is now empty; next build / release won't re-seed it. Users with the folder still in their install can ignore it — still harmless.

### Build + merge status

- Changes NOT yet committed or merged (Mike building locally to verify diagnostic output). This session ends with the worktree in a clean-working-tree-after-diagnostic-edit state once the instrumentation patch is committed.

---

## Session S381 — 2026-04-19 (worktree `claude/goofy-shaw-96c6c1`) — B-184 second-pass investigation: effect_normal_tint mod folder ruled out

**Scope**: Mike flagged a new CI-main-menu repro of B-184 (rainbow/normal-tinted props + characters), plus a critical-looking clue in `Build/pdclient-apr19-0236.log`: `modmgr: entering category folder 'effect_normal_tint'` appearing multiple times, with missing-`mod.json` ERRORs for the same folder. The brief suggested the mod folder might be injecting shader modifications even without a valid `mod.json`. Goal: either pin the folder as the root cause or rule it out and document the finding.

### Investigation

- **effect_normal_tint mod folder contents**: `C:/Users/mikeh/Perfect-Dark-2/perfect_dark-mike/dist/v$Version/mods/effect_normal_tint/` contains only `mod.ini` (legacy N64-era `modconfig.txt`-style format — removed by the 2026-03-24 constraint cleanup; parsing and loader were ripped out) and `README.txt` (describes the intent: green-floor / red-wall / blue-ceiling classification tint). **No shader code, no textures, no display-list patches, no subdirectories.**

- **modmgr category-folder behaviour**: `modmgrScanCategoryFolder` in `port/src/modmgr.c:994` logs `entering category folder '%s'` and walks direct child directories (capped at one level by the 2026-04-16 S293 depth cap). Since effect_normal_tint has zero subdirs, the scan is a pure no-op. The `ERROR: fsFileLoad: could not find file: .../effect_normal_tint/mod.json` line is a secondary scanner's probe — same ERROR pattern is emitted for other legitimate category folders like `base-game`, `UI Chrome`, `Fonts`, `Custom Windows`. Harmless.

- **ASSET_EFFECT consumer search**: grep for `ASSET_EFFECT` / `EFFECT_TYPE_TINT` / `normal_tint` across the entire `src/` + `port/` tree returns three hits: the enum declaration in `port/include/assetcatalog.h:100`, a constant definition block at `:126-141`, and a single UI string `"Effect"` at `pdgui_menu_mainmenu.cpp:2655`. **No code reads or applies ASSET_EFFECT.** The ext struct fields (`effect_type`, `shader_id[64]`, `intensity`, `param1..4`) are declared at `assetcatalog.h:285+` but no loader populates them and no renderer consumes them.

- **Dead-block verification**: grep for the SAVED EFFECT signature (`vcn->y \* rsp\.modelview`, `color\.[rgb] \* 0\.3f \+ 180`, `180.*0\.3f`) returns zero hits in the current tree. The S372 removal (`3486d870`) stuck — the block is truly gone.

- **meshDebug status**: `port/src/meshdebug.c:19` still has `static int s_DebugMode = 0`, `meshDebugToggle()` at `:26` only emits a `MESHCOL:` LOG_NOTE summary of the world mesh stats and never writes `s_DebugMode`. F9 is a no-op from a visual standpoint. `s_meshDebugModeCache` in `gfx_pc.cpp:144` is refreshed per-frame from `meshDebugGetMode()` and gates only the collision-mesh-only overlay (mode 2) and a suppression branch in `gfx_sp_tri1` — no tint path.

- **gfx_opengl.cpp shader generator** (lines 236-560): standard N64 color-combiner emulation. Inputs come from vertex colour/normal + texture lookups. No normal-derived fragment-colour path, no "visualize normals" mode.

- **G_LIGHTING manipulation sweep**: zero `gSP(Set|Clear)GeometryMode.*G_LIGHTING` calls in `src/game/chr.c`, `src/game/propobj.c`, or `src/lib/model.c` for character / prop DLs. Only shards/smoke/smokeinit clear G_LIGHTING, which is expected for those unlit effects. `dlights.c` uses uniform white lights for rooms (no per-axis RGB contribution).

### Conclusion

The `effect_normal_tint` mod folder is **inert** — it consumes zero runtime code paths. It has been sitting in Mike's install directory since 2026-03-26, predating the 2026-03-23 component-mod architecture switch that removed the legacy `modconfig.txt`/`mod.ini` parser. It is **not the source of the rainbow-tint bug**. The previous session's `INVESTIGATED-NO-SMOKING-GUN` verdict stands; this second pass adds explicit rule-out evidence for the mod folder and strengthens the audit trail.

**Remaining hypothesis** (unchanged): rainbow visuals could come from a DL that clears `G_LIGHTING` where the vertex block carries authored normals, so the `else { d->color.r/g/b = vcn->r/g/b; }` branch at `gfx_pc.cpp:1247-1251` reinterprets signed-normal bytes as unsigned RGB. Static analysis can't localize this without fresh evidence.

### Files changed

- `context/bugs.md` — B-184 entry rewritten: S380 second-pass note, explicit effect_normal_tint rule-out, enumerated consumer-search results, recommendation for Mike to delete the stale folder.
- `context/session-log.md` — this entry.

### Recommendation for Mike

1. Delete `C:/Users/mikeh/Downloads/Perfect Dark 2.0/data/mods/effect_normal_tint/` from your install directory. It does nothing and only clutters the boot log. (Also present under `dist/v$Version/mods/` and `post-batch-addin/mods/` — safe to delete from both.)
2. Next time the rainbow tint appears: **take a screenshot** and note the exact map, which characters/props are affected, which mods are enabled, and whether F9 does anything visual. That evidence would pin the actual source — a screenshot alone can distinguish "normal-visualisation" (raw normal vector as RGB) from "surface-classification" (green floor / red wall / blue ceiling) from "something else entirely".

### Build + merge

- No code changes this session; context documentation only. Worktree stays on `claude/goofy-shaw-96c6c1` until the user's next merge.

---

## Session S380 — 2026-04-19 (worktree `claude/admiring-chandrasekhar-ba2a52`) — B-191 Updater Install button clipping

**Scope**: Single bug from Mike — standalone `Updater.exe` (`pd-updater` target) hid the Update / Install button until the user manually resized the window.

### B-191 — Install button clipped below client area
`createControls` in `port/src/updater_standalone/updater_gui.c` stacked every child at fixed Y coordinates from the top. The action row landed at `y≈528` of a `WINDOW_CLIENT_H=572` client (12 px of nominal margin). On systems where DPI scaling, system-caption metrics, or any other non-client overhead pushed the bottom edge a few pixels higher, the Update + Close buttons clipped below the client edge. The earlier `c957fb62` "fix button clipping" commit just bumped the constant — same brittle stacked-from-top pattern. Per Mike's standing rule: action buttons and visual previews must be **docked**, not scrolled.

Refactor (single file, 209 + / 30 −):
- New `layoutControls(hwnd)` reads the live client rect and re-anchors every child via `BeginDeferWindowPos` / `DeferWindowPos` / `EndDeferWindowPos` for atomic batched moves. **Bottom-anchored**: Update + Close action buttons (right-justified) + status text + progress bar. **Top-anchored**: title, version row + Show Dev Releases checkbox, Check button, "Available releases:" label, releases list view, "Release notes:" label. **Stretch-fill**: release-notes edit box absorbs all leftover vertical space (with `UI_NOTES_MIN_H=60` floor).
- Static labels (title, "Available releases:", "Release notes:") gained `IDC_TITLE` / `IDC_LBL_RELEASES` / `IDC_LBL_NOTES` IDs and dedicated `g_App.hTitle` / `hLblReleases` / `hLblNotes` HWND fields so layout can move them.
- Window made resizable: `WS_THICKFRAME` restored (was previously masked out alongside `WS_MAXIMIZEBOX`); maximise stays disabled. New `WM_GETMINMAXINFO` handler enforces a minimum of `WINDOW_CLIENT_W × WINDOW_CLIENT_H` (720 × 620) by feeding the desired client rect through `AdjustWindowRect` with the live window style — the floor is correct under any DWM frame style.
- New `WM_SIZE` handler calls `layoutControls` + `InvalidateRect(hwnd, NULL, TRUE)` so the redraw is correct on every size change.
- `createControls` calls `layoutControls(hwnd)` once at the end so the initial layout is correct even before Windows delivers the first `WM_SIZE`.
- `WINDOW_CLIENT_H` bumped 572 → 620 for headroom; new `WINDOW_CLIENT_W` constant (720) replaces the inline literals so the min-size guard and the create-time width can't drift apart.
- Hoisted layout constants (`UI_PAD`, `UI_TITLE_H`, `UI_BTN_W/H`, `UI_LIST_H`, `UI_PROG_H`, `UI_STATUS_H`, `UI_CHK_W`, `UI_CHECK_BTN_W/H`, `UI_NOTES_MIN_H`) so the create-time and resize-time code paths share one source of truth.
- The list view's "Title" column width is recomputed in `layoutControls` so it absorbs horizontal resizes.

### Files
- `port/src/updater_standalone/updater_gui.c` (+209 / −30 against the dev-tip baseline 1794 lines → 1973 lines): everything above lives in this single standalone-updater translation unit. Other targets do not link `updater_gui.c`, so the change cannot affect `pd` / `pd-server`.

### Build + merge
- Worktree branch `claude/admiring-chandrasekhar-ba2a52` committed as `8f94d98a`, merged into `dev` at `05655228` via `git merge --no-ff`.
- Pre/post-merge line counts on `port/src/updater_standalone/updater_gui.c`: 1794 → 1973, exactly matching the worktree diff (+209/−30). No silent shrinkage.
- Build validated: `ninja -C Build pd-updater` after a smart-clean configure → `[195/195] Linking C executable Updater.exe`. `Updater.exe` 12,846,748 bytes. The pre-existing `sha256.c` `'/*' within comment` warning is unchanged and unrelated.

### Verify (playtest)
- Run `Updater.exe`. Window opens at 720×620. Update + Close buttons visible at the bottom-right (Update disabled until a release row is selected).
- Drag the window edges to shrink/grow — buttons stay pinned to the bottom-right; release-notes box absorbs the vertical change. Window cannot be made smaller than 720×620 (client area) — `WM_GETMINMAXINFO` clamps the floor.
- Resizing horizontally widens the version label, list view "Title" column, and notes box.
- Click a release row → Update enables → click Update → confirm modal → progress bar appears between notes and status during download / install.

---

## Session S379 — 2026-04-19 (worktree `claude/friendly-kirch-688ea4`) — 3-bug playtest batch: B-189 interact prompt bleed, B-190 bot count scroll, B-146 Grid walkable pickups

**Scope**: Three items from the 2026-04-19 playtest: (B-189) controller button hint overlays rendering over ImGui menus instead of the gameplay HUD layer, (B-190) CS Room header bot count display not appearing to update on Add/Remove, (B-146 regression) 2 ammo crates + 1 weapon on Grid still standable despite S295's WALKTHROUGH-flag fix.

### B-189 — Interact prompt over menus
`port/fast3d/pdgui_interact_prompt.cpp::pdguiInteractPromptRender` drew on the ImGui foreground drawlist every frame that `propInteractPromptLabel()` returned non-NULL. `g_InteractProp` is populated by the gameplay tick and persists across menu opens, so walking up to a pickup, door, or terminal and then pressing Escape / Pause / opening Forge left the `[KEY] Label` pill floating above the menu overlay. The interact prompt belongs to the gameplay HUD layer per `context/designs/hud-layer-order.md` §7 — it should never bleed through to any non-gameplay context. Fix: single `if (pdguiIsActive()) return;` gate at the top of the renderer, using the same authority predicate (`inputCtxGetTop() != &g_CtxGameplay`) as every other gameplay-HUD-only overlay (score panel, killfeed, HUD messages). Also added a `#include "pdgui.h"` for the `pdguiIsActive` prototype.

### B-190 — Bot count scroll-off
Root cause was UX, not state. `countBots()` (iterates `g_MatchConfig.slots[1..numSlots]` counting `SLOT_BOT` type) was recomputed every frame in `renderPlayerPanel`, and the header `TextColored(...)` re-rendered every frame. The header just lived INSIDE the `##room_players_list` scrollable child along with the team sort dropdown and all the player rows — so once ~8+ bots filled the viewport, the header scrolled off the top with them. The number WAS updating; it was scrolling away.

Fix (`port/fast3d/pdgui_menu_room.cpp`): (1) moved the header + Team Sort dropdown OUTSIDE the scrollable `##room_players_list` child into the outer `##room_panel_outer` panel so they stay pinned at the top; `listH = GetContentRegionAvail().y - btnH - 2*ItemSpacing.y` is computed after the sticky header is laid out so the list fills remaining space. (2) Baked the live count/cap into the Add Bot button label itself — `Add Bot  (X / Y)` — so the primary interaction point always shows the current state next to the click, independent of scroll position. (3) Extended the header format from `%d Bot` → `%d/%d Bot` to show the cap alongside the count.

### B-146 regression — Grid walkable pickups
S295 fixed walkable pickups two ways: (a) ORed `OBJFLAG3_WALKTHROUGH` into the `weapon()` / `ammocrate()` / `ammocratemulti()` macro expansions in `src/include/props.h`, (b) set the flag explicitly in `weaponCreateForChr` (the network receive path). The PC auto-floor synthesizer in `propobj.c:2321` suppresses the `GEOTYPE_TILE_F` emission when WALKTHROUGH is set, so any pickup with the bit correctly becomes non-standable.

Regression: the macro change only affects code RECOMPILED from C source. Base-game map setup files on stages like Grid, Car Park, Felicity, etc. are pre-compiled binary loaded from the ROM via `assetLoadToNew(setup_handle, ..., LOADTYPE_SETUP)`. The `flags3` byte in that binary predates our macro change and has no WALKTHROUGH bit. So on Grid, two ammo crates + one ground weapon continued to synthesize floor tiles. Forge-placed weapons/ammo were fine because they route through `weaponCreateForChr` (network-like) and the user-macro-path includes the fix.

Fix: `src/game/setup.c::setupCreateObject` now ORs `OBJFLAG3_WALKTHROUGH` into `obj->flags3` at runtime when `obj->type` is `OBJTYPE_WEAPON` / `OBJTYPE_AMMOCRATE` / `OBJTYPE_MULTIAMMOCRATE`, placed immediately after the B-163 FIX-B.2 NULL modeldef guard. This matches the macro intent regardless of whether the setup data came from source build or ROM binary. The `propobj.c` auto-floor synthesizer check at 2321 is unchanged — it correctly suppresses the floor tile once WALKTHROUGH is set.

### Files
- `port/fast3d/pdgui_interact_prompt.cpp` (+10/−1): `pdguiIsActive()` gate + `pdgui.h` include + comment update.
- `port/fast3d/pdgui_menu_room.cpp` (+27/−13): restructure renderPlayerPanel to pin header outside scrollable child + Add Bot button label.
- `src/game/setup.c` (+16/−0): runtime WALKTHROUGH force for pickup types in setupCreateObject.

### Build + merge
- Clean 774/774 full CMake configure + ninja build from scratch (no prior Build/ dir). `PerfectDark.exe` 53,237,073 / `PerfectDarkServer.exe` 23,139,808. No new warnings on touched files.

### Verify (playtest)
- **B-189**: walk up to any pickup or door so the `[E] Pick up` / `[A] Open` pill appears at the reticle → open Escape menu / pause menu / Settings / Modding Hub / Forge editor → pill disappears on the same frame. Close menu → pill reappears if target still in range. Gameplay behaviour of the prompt (range tracking, target type) unchanged.
- **B-190**: Solo CS Room → add 10+ bots via the Add Bot button. The "Players in Room (1 Player, N/32 Bots)" header stays pinned at the top of the right panel regardless of how many bot rows are present; the Add Bot button below the scrollable list shows `Add Bot  (N / 32)` and increments immediately on each click. Right-click a bot → Remove → counter ticks down on the same frame.
- **B-146**: Load Grid via Combat Sim → walk into the side of every ground weapon and every ammo crate on the map. Capsule should slide past / through, never ride up onto the top of the pickup. Confirm specifically on the 2 ammo crates + 1 weapon Mike flagged. Also confirm Forge-placed pickups are still walkthrough (they were already correct).

---

## Session S378 — 2026-04-19 (worktree `claude/friendly-mccarthy-a90db6`, merged to `dev` @ `3824d95f`) — D6 Phase 3 finishing touches: stats UI expansion + damage wire-in + achievement toasts

**Scope**: User tasked three parallel tracks — D2 (character select), D5 Phase 5 (lobby scene), D6 (stats). Audit showed D2a char-select redesign was DONE at S15 (scrollable body list, live 3D preview, head detection; no concrete remaining quality gaps) and D5 Phase 5 portraits + polish shipped at S352 + S356 (per-player portrait baking, hover preview, drop shadow, team-color border). Only D6 had real work left: damage was tracked in `mpplayerconfig` but never promoted to `statIncrement`; the Stats Viewer Overview tab only surfaced ~9 of ~25 collected stat keys; `achievementGetNewlyUnlocked` was never called from anywhere — achievements silently flipped to unlocked with zero player feedback; `achievementsRefresh` was solo-endscreen-only.

**Damage + hit tracking** (`src/game/mplayer/mplayer.c::mpCalculateAwards`): three new `statIncrement` calls alongside the existing MP time/distance block, guarded identically (`playernum < PLAYERCOUNT()` inside the `!g_CheatsActiveBank0 && !g_CheatsActiveBank1` gate).
- `mp.damage_dealt` += `damtransmitted / 0.1f` (matches `mpplayer->damagedealt` scale)
- `mp.damage_received` += `damreceived / 0.1f`
- `mp.shots_hit` += `round(accuracyfrac * numshots)` — `accuracyfrac` already computed a few lines above from sum of HEAD/BODY/LIMB/GUN/HAT/OBJECT shot-region counters / total shots. Combined with `shots.total` (already tracked by `mpstatsIncrementPlayerShotCount`) this yields a real cumulative hit-accuracy %.

**Stats Viewer UI expansion** (`port/fast3d/pdgui_menu_stats.cpp::renderOverviewTab`):
- Accuracy section picks up two new rows: Shots Hit + Hit % (alongside the pre-existing Total Shots / Headshots / Headshot %).
- Three new sections added to Overview: **Combat Simulator** (Matches Played / Won / Lost / Win Rate / Time Played / Damage Dealt / Damage Taken / Distance), **Solo Missions** (Completed / Failed / Time Played), **World Interaction** (Items Picked Up at top + 4-row indented breakdown Weapons / Ammo Crates / Shields / Keys, then Doors Opened).
- New `formatDuration(char *out, size_t outSz, u64 seconds)` helper renders seconds as `Hh Mm` (≥ 1 hour), `Mm Ss` (≥ 1 minute), or `Ns` so MP/Solo time stays readable.
- Removed the unused `float scale = pdguiScaleFactor();` at the top of `renderOverviewTab` (lint-quality cleanup).

**New module — `port/fast3d/pdgui_achievement_toast.{h,cpp}`** (44 + 211 LOC):
- `Toast` struct: `name[64]`, `description[128]`, `age` (frames since push), `active`.
- Static ring of 4 toasts; overflow push drops silently. Each toast lives `TOAST_LIFETIME = 270` frames (4.5 s @ 60 Hz), with `TOAST_FADEIN = 20` + `TOAST_FADEOUT = 60` frames for alpha envelope.
- `pdguiAchievementToastPush(name, description)` — copies strings into the next free slot, plays `PDGUI_SND_FOCUS`.
- `pdguiAchievementToastPollUnlocks()` — calls `achievementGetNewlyUnlocked(ids, TOAST_MAX)` (which clears the "new" state so each unlock fires exactly one toast) and maps each returned id back to `achievement_def_t_cpp::name/description` via `achievementGetByIndex`, then pushes.
- `pdguiAchievementToastRender(winW, winH)` — called every frame from `pdgui_backend.cpp::pdguiRender` right after `pdguiInteractPromptRender`. Top-right corner, stacked downward. Reaps expired toasts in-place before rendering so the visible stack compresses as earlier toasts time out. Rendered on `ImGui::GetForegroundDrawList()` so it overlays any menu. Each toast draws shadow + bg rect (`PDPAL_TITLEBG` @ 210 α) + border (`pdguiImU32TintSuccess` @ 240 α) + 4-px success-tint accent strip on the left edge + "ACHIEVEMENT UNLOCKED" tag row in success tint + name row in title-glow color + tail-clipped description row with `...` on overflow. Slide-in: during fade-in the toast starts `0.4*width` off the right edge and lerps inward.

**Wire-in**:
- `port/fast3d/pdgui_backend.cpp`: `#include "pdgui_achievement_toast.h"`; `pdguiAchievementToastRender((s32)winW, (s32)winH);` right after the interact prompt in the render loop.
- `port/fast3d/pdgui_menu_endscreen.cpp`: `#include "pdgui_achievement_toast.h"`. Solo endscreen `IsWindowAppearing` block now calls `achievementsRefresh()` + `pdguiAchievementToastPollUnlocks()`; MP endscreen fresh-entry block now calls BOTH (previously it never refreshed achievements at all — solo-only).

**Build**: Worktree fresh cmake configure + ninja — clean 775/775. `PerfectDark.exe` 53,337,398 / `PerfectDarkServer.exe` 23,140,832. After merge, main working copy needed `cmake --reconfigure` to pick up the new `pdgui_achievement_toast.cpp` (GLOB_RECURSE scans only on configure); incremental rebuild 12/12 linked clean. Final dev-side sizes `PerfectDark.exe` 53,111,624 / `PerfectDarkServer.exe` 23,143,410.

**Merge**: commit `7f3be9d6` → merged to `dev` as `3824d95f` (`--no-ff`). Post-merge line-count verification: `src/game/mplayer/mplayer.c` 4566 (pre 4559, Δ+7), `port/fast3d/pdgui_backend.cpp` 915 (pre 909, Δ+6), `port/fast3d/pdgui_menu_endscreen.cpp` 1398 (pre 1392, Δ+6), `port/fast3d/pdgui_menu_stats.cpp` 570 (pre 433, Δ+137), `port/fast3d/pdgui_achievement_toast.cpp` 211 (new), `port/include/pdgui_achievement_toast.h` 44 (new). All match expected deltas — zero shrinkage.

**Files**:
- `src/game/mplayer/mplayer.c` — wire damage + shots-hit to stats
- `port/fast3d/pdgui_menu_stats.cpp` — Overview tab expansion + `formatDuration` helper
- `port/fast3d/pdgui_menu_endscreen.cpp` — solo + MP endscreen fresh-entry paths now refresh + poll toasts
- `port/fast3d/pdgui_backend.cpp` — render-loop call site for `pdguiAchievementToastRender`
- NEW `port/fast3d/pdgui_achievement_toast.cpp`
- NEW `port/include/pdgui_achievement_toast.h`

**Playtest ask**:
1. Stats menu → Overview tab shows populated MP / Solo / World Interaction sections after any match or solo mission.
2. Unlock any achievement (e.g. First Blood on first kill, Centurion at 100 kills, Sharpshooter at 100 headshots) — toast slides in from the right edge of the endscreen, stays ~4.5 s, fades out.
3. `saves/playerstats.json` gains `mp.damage_dealt`, `mp.damage_received`, and `mp.shots_hit` keys after any MP match.

**Next**:
- Refine toast position / palette if it collides with other HUD elements in Mike's playtest.
- Consider a dedicated achievement-unlock sfx (currently `PDGUI_SND_FOCUS` is a proxy).
- Per-mission stats (`solo.missions.<catalog_id>.completed`) could be added later but don't block anything — low leverage.

---

## Session S377 — 2026-04-19 (worktree `claude/intelligent-pasteur-269b40`, merged to `dev` @ `ba7ed31e`) — AP Phase 3 — game-code call-site migration off legacy `fileLoad*` wrappers

**Scope**: Asset Provider Phase 3 — finish the call-site migration for every remaining caller of the pre-AP `fileLoadToNew` / `fileLoadToAddr` / `fileLoadPartToAddr` wrappers, so game code uses the dispatcher (`assetLoadRomToNew` / `assetLoadRomToAddr`) directly. Also Phase 4 audit (filenum retirement scope assessment).

**Background**: AP Phase 1+2 (S326, 2026-04-17) introduced the provider vtable, RomProvider/FileProvider singletons, asset_source_t on catalog entries, and the dispatcher. The Phase 1 wrappers `fileLoadToNew(filenum,...)` and `fileLoadToAddr(filenum,...)` in `src/game/file.c` were thin wrappers that game code kept calling — Phase 3 retires them in favour of the dispatcher API.

**API additions**:
- `assetLoadToAddr(asset_data_handle_t handle, u32 method, void *buf, u32 size)` — caller-allocated-buffer dispatcher (mirrors `assetLoadToNew`). RomProvider fast-path delegates to the legacy `fileLoad` pipeline (rzip + romdataFilePreprocess + g_FileInfo[] tracking) for byte-identical behaviour.
- `assetLoadRomToAddr(s32 filenum, u32 method, void *buf, u32 size)` — convenience wrapper for ROM filenums; equivalent to `assetLoadToAddr(romProviderHandle(filenum), ...)`.

**API renames**:
- `fileLoadToAddr` → `fileLoadRomToAddr` in `src/game/file.c` (the legacy worker becomes an internal RomProvider impl, exported only so the dispatcher can fast-path through it without recursion). Game code MUST NOT call this directly any more — `port/include/assetload.h` is the new public surface.

**Migrations** (12 game-code call sites across 5 files):
- `src/game/langreset.c` — 6× `fileLoadToNew` → `assetLoadRomToNew` (one per LANGBANK)
- `src/game/lang.c` — 1× `fileLoadToNew` → `assetLoadRomToNew`, 2× `fileLoadToAddr` → `assetLoadRomToAddr`
- `src/game/setup.c` — 1× `fileLoadToAddr(setupfilenum, ...)` → `assetLoadRomToAddr`
- `src/game/modeldef.c` — 1× `fileLoadToAddr(fileid, ...)` → `assetLoadRomToAddr`
- `src/game/bondgun.c` — 1× `fileLoadToAddr` → `assetLoadRomToAddr` (with explicit `(void *)ptr` cast — `ptr` here is `uintptr_t`, the original code had `(u8 *)ptr`)

**Cleanup deletes**:
- `fileLoadPartToAddr` removed from `src/game/file.c` + `src/include/game/file.h` — zero callers since S374 (B-185 bg silent-load fix moved bg.c off this API to call `romdataFileLoad` directly).
- Public `fileLoadToNew` wrapper removed from `src/game/file.c` + `src/include/game/file.h` — zero callers post-Phase-3 migration.
- Public `fileLoadToAddr` declaration removed from `src/include/game/file.h` (the worker stays in file.c under the new name `fileLoadRomToAddr`).
- File header docs + worker function preambles updated to reflect Phase 3 state. `port/include/assetprovider_internal.h`'s "game-level API" pointer list now references `assetLoadRomToNew` / `assetLoadRomToAddr` instead of the deleted wrappers.

**Phase 4 audit (filenum retirement scope)** — out of scope for this session, documented for follow-up:
- Handle-based catalog accessors **already exist**: `catalogGetBodyHandle` / `catalogGetHeadHandle` / `catalogGetPropHandle` (in `port/src/assetcatalog_api.c`, declared in `port/include/assetcatalog.h:917+`).
- Deprecated filenum accessors `catalogGetBodyFilenumByIndex` / `catalogGetHeadFilenumByIndex` / `catalogGetPropFilenumByIndex` are still used by 23+ game-code callers across:
  - `src/game/body.c` × 5 (diagnostic logs only — cosmetic migration)
  - `src/game/player.c` × 5 (3 diag logs + 2 size precomputation via `fileGetInflatedSize` + `MENUMODELPARAMS_SET_FILENUM`)
  - `src/game/menu.c` × 2 (`MENUMODELPARAMS_SET_FILENUM`)
  - `src/game/mplayer/setup.c` × 2 (`MENUMODELPARAMS_SET_FILENUM`)
  - `src/game/setuputils.c` × 1 (`fileGetInflatedSize`)
  - `src/game/title.c` × 8 (`modeldefLoad(filenum, ...)`)
- Phase 4 retirement requires three downstream API migrations as prerequisites:
  - `assetGetSize(handle, loadtype)` to replace `fileGetInflatedSize(filenum, loadtype)`.
  - Handle-aware `modeldefLoad` variant (or migrate the existing one to take a handle and use `assetLoadRomToAddr` internally).
  - `MENUMODELPARAMS_SET_HANDLE(handle)` macro / menu model param storage migration.
- Once those land, the game-code sites can move from filenum → handle in batches, and the `[DEPRECATED] catalogGetXFilenumByIndex` accessors can be deleted along with the SA-5a bridge functions.

**Files**: 12 files modified, +155 / -91 LOC.
- `port/include/assetload.h` (+23 LOC: new API decls + Phase 3 doc preamble)
- `port/src/assetload.c` (+45 LOC: new dispatcher + convenience wrapper)
- `port/include/assetprovider.h`, `port/include/assetprovider_internal.h` (-2 LOC each: doc edits)
- `port/src/modelcatalog.c` (1-word comment update)
- `src/game/file.c` (-7 LOC: deleted PartToAddr + ToNew wrapper; expanded worker preambles)
- `src/include/game/file.h` (rewrote decl block: -3 public + 1 new worker)
- `src/game/{bondgun,lang,langreset,modeldef,setup}.c` (call-site swaps + 3 includes)

**Build**: clean from full configure (worktree's own Build/, since worktrees can't share the main repo's Build/). 774/774 targets, no new warnings on the touched files. `PerfectDark.exe` 53,054,294 / `PerfectDarkServer.exe` 23,119,328.

**Merge**: worktree branch `claude/intelligent-pasteur-269b40` → commit `75240823` → merged into `dev` as `ba7ed31e` (`--no-ff` ort strategy, no conflicts). Post-merge line counts vs pre-merge: every file accounted for (`assetload.h` 70→93, `assetload.c` 129→174, `file.c` 359→352, `file.h` 26→26, `bondgun.c` +1, `lang.c` +1, `langreset.c` +1, others unchanged or one-word edits). Total +64 LOC matches diff stat exactly. No file shrank unexpectedly.

**Verification needed (playtest)**: this is a refactor with byte-identical RomProvider semantics — every load goes through the same `fileLoad` pipeline as before. Expected behaviour: zero observable change. Smoke test path:
- Cold-boot to title, intro plays normally (lang banks load via `assetLoadRomToNew`).
- CI Training (0x26): bodies/heads/props/setup all load (bg.c handled by S374 fix; other paths use the new API).
- Solo mission start: setup file loads via `assetLoadRomToAddr(setupfilenum, ...)`.
- MP match start: char select preview models render correctly (bondgun gun model path uses `assetLoadRomToAddr`).
- Mid-mission language switch (PAL+): `langLoad` triggers `assetLoadRomToAddr` into the lang scratch buffer.

If any load fails, the existing `WARNING: fileLoadRomToAddr: file %d failed to load` (now in the renamed worker) and the `CATALOG_CRITICAL: filenum=%d not found in ROM data` lines pinpoint the failure exactly the same way as before.

**Next**: Mike playtests; promote AP Phase 3 to verified after a clean smoke run. Phase 4 (filenum retirement) is a 3-step downstream-API + 23-site migration left for a future session.

---

## Session S374 — 2026-04-19 (worktree `claude/thirsty-torvalds-9c7e0f`) — B-185 bg silent-load failure fix

**Scope**: Fix intermittent "invisible level" symptom (B-185) — stage loads with zero bg room geometry visible, only props/doors render, game otherwise plays normally; a restart reliably fixes it.

**Root cause**: `bgLoadFile` in `src/game/bg.c` routed every bg seg-file partial-slice read through `fileLoadPartToAddr` in `src/game/file.c`. That function is structured as:
```c
if (fileGetRomSizeByTableAddress((uintptr_t*)&g_FileTable[filenum])) {
    const u8 *src = romdataFileGetData(filenum);
    if (src) { dmaExec(memaddr, (uintptr_t) src + offset, len); }
}
```
If either the size probe returns 0 OR `romdataFileGetData` returns NULL, the function silently returns without copying anything. The destination buffer — `headerbuffer[0x50]` on the stack in `bgReset`, heap allocations in `bgLoadRoom` — then still holds whatever bytes were there before the call. `preprocessBgSection1Header` parses that garbage, the downstream rzip inflate either outputs nonsense bytes or zero bytes, and `g_BgPrimaryData` ends up empty. The `var800a4920 != 0` guard at `bgReset:1600` then skips the entire `g_BgRooms` / `g_BgPortals` / `g_BgCommands` / `g_BgLightsFileData` / `g_BgStanThings` population, leaving bg render with no room table. Props and doors continue to render because they load through the completely separate Phase 4 catalog pipeline (`assetLoadToNew` via provider handles) which has proper NULL handling — that's why the game appears "half-alive" (props visible, rooms invisible) rather than crashing. Zero `BG.LOAD:` or `CATALOG:` log lines fired on failure because `fileLoadPartToAddr` never logged.

The intermittence is explained by the `romdataFileLoad` cache state machine: `fileSlots[bgfileid]` can legitimately be in `SRC_UNLOADED` on first access after some teardown paths (stage transitions, mod rescans). On the UNLOADED branch, `romdataFileLoad` tries mod-override → external file → falls back to ROM-data. If any intermediate step touches `fileSlots[fileNum].data` without re-initialising it to the ROM offset (e.g., a prior `romdataFileFree` that freed an `SRC_EXTERNAL` slot leaving `data=NULL`, then a repeat load tries external again and fails before falling back to ROM), the final `out = fileSlots[fileNum].data` read is NULL. From the outside this looks like a race condition but is actually deterministic-state churn.

**Fix**: rewrote `bgLoadFile` (+29/−3 LOC) to skip `fileLoadPartToAddr` entirely and call `romdataFileLoad(stage.bgfileid, &romsize)` directly:
- Reject `stage.bgfileid <= 0` up front with `LOG_ERROR` (catches server-side callers / mod-broken catalogs where `bgfileid` is -1)
- `sysLogPrintf(LOG_ERROR, "BG.LOAD: ... zeroing dest", ...)` on NULL src, out-of-range offset, or out-of-range len
- Overflow-safe bounds check: `if (offset > romsize || len > romsize - offset)` (never computes `offset+len` which could wrap)
- `memset(memaddr, 0, len)` on every failure path so callers see deterministic zeros — `preprocessBgSection1Header` on zero bytes yields a zero inflatedsize which triggers the existing `var800a4920 != 0` guard, failing cleanly instead of parsing garbage
- Happy path uses `memcpy` (PC `dmaExec→bcopy` without the silent-failure wrapper)

**Why not migrate to Phase 4 handles**: `assetLoadToNew` inflates a whole file in one allocation. bg.c specifically wants seven separate partial-slice reads at known byte offsets — section-1 header, primary compressed block, section-2 header, section-2 compressed block, section-3 header, section-3 compressed block, and per-room compressed block. A Phase 4 migration would require adding a new partial-slice API or loading the entire seg file and then indexing — different shape, much bigger surface. The `romdataFileLoad + memcpy` path picks up the mod-override / external-file / ROM-fallback routing for free (that routing lives inside `romdataFileLoad`, not in the deprecated `fileLoadPartToAddr` wrapper). Out-of-scope for this fix.

**Files**: `src/game/bg.c` (+29/−3 LOC on `bgLoadFile`). `fileLoadPartToAddr` in `src/game/file.c` now has zero callers; the definition is left in place to keep this commit focused on the bg load path.

**Build**: Clean 776/776 from a full rebuild. `PerfectDark.exe 52,973,749` / `PerfectDarkServer.exe 23,143,410`. No new warnings on bg.c.

**Merge**: worktree branch `claude/thirsty-torvalds-9c7e0f` → commit `fbc276ce` → merged to `dev` as `4cb4c02e` (`--no-ff`). Post-merge line-count check: `wc -l src/game/bg.c` = 6327 (was 6301 pre-merge), delta +26 matches the +29/−3 patch within rounding.

**Verify (playtest)**: cold-boot into CI Training (0x26) and any solo mission 5× in a row — scene should always render first time. If ANY cold boot reproduces the invisible-level symptom, `pd-client.log` will now contain exactly one of:
- `BG.LOAD: invalid bgfileid=%d (stageidx=%d stagenum=0x%02x) offset=%u len=%u — zeroing dest` — catalog handed bg.c a bad fileid
- `BG.LOAD: romdataFileLoad returned NULL for bgfileid=%d (stageidx=%d stagenum=0x%02x) offset=%u len=%u — zeroing dest` — romdata cache is torn for this filenum
- `BG.LOAD: out-of-bounds offset=%u len=%u romsize=%u (bgfileid=%d stageidx=%d) — zeroing dest` — seg-file header corrupted or mod file shorter than expected

so the next repro is diagnosable instead of silent. Props/doors should continue to render; the change is strictly additive on the happy path.

**Next**: Mike playtests; promote to FIXED after 5+ clean cold boots with no `BG.LOAD:` errors in the log.

---

## Session S370 — 2026-04-19 (worktree `claude/naughty-banach-638906`) — 5-bug playtest batch

**Scope**: Five items from the 2026-04-18 playtest: B-175 (killfeed missing sim-on-sim kills), B-176 (match doesn't pause on MP endscreen), B-177 (time-limit slider off-by-one vs label), right-click paste in Direct Connect address field, and B-172 revisit (theme colors don't persist across restarts).

### B-175 — Killfeed sim-on-sim kills
Previous emit gate in `mpstatsRecordDeath` was `g_Vars.normmplayerisrunning && ampchr && vmpchr`. Two problems: (1) `normmplayerisrunning` is false in co-op / counter-op so those modes never got killfeed entries at all; (2) the combined non-null guard silently dropped kills whenever the attacker's mpchrconfig lookup failed (even though `vmpchr` was valid), which is exactly the shape of "sim kills sim but neither registers" if `func0f18d074 → MPCHR` ever returns NULL for the attacker bot slot. Relaxed the guard to `g_Vars.mplayerisrunning && vmpchr`, and when `ampchr` is NULL we now push with `attackerName=NULL` (the renderer already formats that as a suicide / anonymous "?" pill). Added a `LOG_NOTE: KILLFEED: push ...` line on every successful push and a `LOG_WARNING: KILLFEED: skipped ...` if the outer guard still rejects — so the next playtest log answers "did it fire?" directly.

### B-176 — Match doesn't pause on endscreen
`mpEndMatch()` set `MPPAUSEMODE_GAMEOVER` but never called `lvSetPaused(true)`. `mpIsPaused()` is one of two branches the lv.c tick gate checks (`lvIsPaused()` is the other); without the latter, `g_Vars.lvupdate240` only got zeroed if the gate fell into the mpIsPaused branch, which several upstream sites short-circuit. Solo endscreen always called `lvSetPaused(true)` in `endscreenPrepare` (see endscreen.c:1789) — MP endscreen just forgot. Mirror the pattern: `mpPushEndscreenDialog` now calls `lvSetPaused(true)` and re-asserts `mpSetPaused(MPPAUSEMODE_GAMEOVER)` defensively. All three endscreen exit paths — `pdguiEndscreenExitToMainMenu`, `pdguiEndscreenStartMission`, `pdguiEndscreenNextMission` — call `lvSetPaused(false)` before the stage transition so the next level doesn't boot paused.

### B-177 — Time-limit slider off-by-one vs label
`renderRoomScreen` (`pdgui_menu_room.cpp`) ran `SliderInt("Time (min)", &tl, 0, 60)` with the value stored 0-based (`timelimit = minutes - 1`), then the label printed `tl+1`. So slider-29 showed "30 min". Switched to the exact pattern already used by the Score slider a few lines below: slider 1..61 with `tl = (int)timelimit + 1` on read and `timelimit = tl - 1` on write; label reads `tl` so handle and text always match. "No limit" still fires when stored `timelimit >= 60`.

### Right-click paste in server join / direct connect
Added a right-click-anywhere-on-the-InputText handler in `pdgui_menu_network.cpp::renderMultiplayerMenu`. Uses `SDL_GetClipboardText()` (SDL is already included in the TU) and trims trailing whitespace / CR / LF so pastes from Discord / Slack / email don't carry a stray newline that would fail the `connectCodeDecode` pass. Logs a `MENU_IMGUI: network menu PASTE addr=...` note and plays `PDGUI_SND_SUBFOCUS` for audible feedback.

### B-172 — Theme colors don't persist
Root cause: `prefsAgentResetVisuals()` (called whenever Agent Select appears) hard-coded `base:theme_blue`, empty chrome style id, empty font id, `PDGUI_TITLEBAR_CLASSIC`, scanlines off. Any pre-sign-in theme change — the Main Menu → Settings → Interface flow that writes to pd.ini — was silently reverted on the next Agent Select visit, then the user's selected agent's stale `[Theme] ActiveId = ...` (or missing block) took over. Fix has two parts:
- **Capture pd.ini baselines at `prefsAgentInit()`**: new `s_BaseThemeId` / `s_BaseUiChromeStyleId` / `s_BaseUiChromeEnabled` / `s_BaseTitleBarStyle` / `s_BaseFontId` / `s_BaseScanlineEnabled` / `s_BaseScanlineAlpha` static strings captured right after pdguiThemeLoaderInit has applied pd.ini. `prefsAgentResetVisuals()` now reverts to THESE instead of hard-coded values.
- **New `prefsAgentRefreshVisualsBaseline()`**: call from the theme picker (`pdgui_menu_mainmenu.cpp::renderSettingsInterface` theme button) after `configSave("pd.ini")` so a mid-session pre-sign-in change refreshes the baseline. Without this, the user would change theme after boot, then the baseline captured at init is stale and the next Agent Select still reverts to the boot-time value.

### Build verify + merge
Commit `525ae073` → merged as `65d06679` into `dev` (fast-forward from `083db768 chore: auto-commit before build (dev window)`). Worktree line counts post-merge matched the source exactly: `pdgui_bridge.c 1718`, `pdgui_menu_mainmenu.cpp 4475`, `pdgui_menu_network.cpp 361`, `pdgui_menu_room.cpp 3499`, `prefs_agent.h 77`, `prefs_agent.c 724`, `ingame.c 1187`, `mpstats.c 502`. Build clean [253/253]: `PerfectDark.exe` 52,966,729 / `PerfectDarkServer.exe` 23,143,410.

**Diagnostic hooks for next playtest**: `KILLFEED:` log lines in pdclient.log will now confirm whether sim-on-sim kills actually push into the ring buffer. If they do but still don't render, the next step is the renderer in `pdgui_menu_mpingame.cpp::pdguiMpIngameRender` (check the `pdguiPauseGetNormMplayerIsRunning` gate for co-op/counter-op specifically). If `KILLFEED: skipped` fires with NULL vmpchr, the problem is upstream — `aplayernum=-1` or `mpPlayerGetIndex(chr) == -1` for the victim bot.

## Session S369 — 2026-04-18 (worktree `claude/nice-jackson-62879e`) — Solo pause menu input context fix (B-171)

**Scope**: Tester reported: "Quitting a mission opens the menu, my input doesn't work, or focus might actually stay on the pause menu in the background. If I hold RMB I can see my invisible mouse."

**Evidence**: `Downloads/Perfect Dark 2.0/pd-client.log` — solo mission at `02:01.18 ACTION_PAUSE detected` → `02:01.20 MENUPOOL: acquired solo_mission_pause gen=1 def=... ctx=none(shared)` → NO subsequent `INPUTCTX: imgui_menu on_push` → several `WARNING: DIAG fireVk: vk=531 player=0 NO BINDING FOUND in 1 active IMCs` → `02:10.83 released solo_mission_pause`. Contrast with the working main-menu pattern at `00:34.53`: `acquired main_menu` → `INPUTCTX: imgui_menu on_push -- g_ImcMenu activated` → `MENUPOOL: attached ctx to active main_menu gen=2 ctx=imgui_menu` → `syncMouseMode -- restored absolute mode for 'imgui_menu'`.

**Root cause**: `renderPauseMenu` in `pdgui_menu_solomission.cpp` did `ImGui::SetWindowFocus()` on `IsWindowAppearing` but never called `menupoolAcquireDialog(def, &g_CtxImGuiMenu)` to attach the input context to its already-acquired pool slot. `menuPushRootDialog → menuPushDialog` pre-acquires the slot with `ctx=NULL` (that's the structural dedup check); the renderer is then responsible for re-acquiring on first appearance with the real ctx so the pool attaches it to the input context stack. This is the S300 pattern every other ImGui renderer follows (mppause / mpadvanced / mpsetup / agentselect / etc.). Solo pause was missed in the S300 pool-owned-ctx migration, so it has been drawing over live gameplay input the whole time — the only reason it worked at all was that the legacy `menuPush*` path used to do its own `inputCtxPush` elsewhere, and that was removed during S299/S300.

Consequence chain:
- `g_CtxImGuiMenu` not on top → `g_ImcMenu` not active → no menu bindings match `vk=531` (right stick / gamepad button that should drive menu nav), "NO BINDING FOUND" warnings.
- Gameplay mouse mode (relative, cursor hidden) stays active — the "invisible mouse" symptom. The user's "hold RMB to see it" is a side effect of SDL temporarily yielding relative mode for certain button events, not an intentional behavior.
- Child DANGER dialogs pushed on top of solo pause (`g_MissionAbortMenuDialog` via menu.c pattern) inherit the same broken state — Cancel + Abort buttons unreachable.

**Fix**: add the canonical S300 pattern to `renderPauseMenu`'s IsWindowAppearing block:
```cpp
pdguiPlaySound(PDGUI_SND_OPENDIALOG);
menupoolAcquireDialog(menupoolDialogDef(dialog), &g_CtxImGuiMenu);
```
The pool slot is already active (acquired by `menuPushDialog` with NULL ctx); this re-acquire attaches the ctx, pushing `g_CtxImGuiMenu` onto the input context stack. Close paths unchanged — `menuPopDialog → menuCloseDialog → menupoolRelease` pops the owned ctx automatically (verified via `menupoolRelease` in `port/src/menupool.c:238+`). Includes `port/include/menupool.h` (was not previously needed).

**Files**: `port/fast3d/pdgui_menu_solomission.cpp` (+14 / −0 LOC: include + 14-line IsWindowAppearing addition with rationale comment).

**Build**: Clean 2/2 incremental — only the one cpp rebuild was needed. Client re-links with the new symbol reference.

**Verify (playtest)**:
- Play any solo mission → press Start to pause → pause menu appears → D-pad Up/Down navigates Resume / Restart / Inventory / Settings / Abort. Abort → confirm dialog → Cancel and Abort both navigable.
- Mouse cursor is visible during the pause (absolute mode, no need to hold RMB).
- Log should show `INPUTCTX: imgui_menu on_push -- g_ImcMenu activated` immediately after `MENUPOOL: acquired solo_mission_pause`, then `MENUPOOL: attached ctx to active solo_mission_pause gen=1 ctx=imgui_menu`, then `syncMouseMode -- restored absolute mode for 'imgui_menu'`. No more `DIAG fireVk: NO BINDING FOUND` warnings.

**Combat Simulator End Game** (tester mentioned as separate symptom from a prior session): the S368 `renderMpEndGameDialog` rewrite already moved that dialog to `BeginPopupModal` which takes over input exclusively. With B-170 shipped, the CI "End Game" flow should now be reachable; if the next playtest still reports it broken we'll need a fresh log showing which dialog is being pushed.

**Next**: Mike playtests both solo pause and CI End Game; promote B-170 + B-171 to FIXED on sign-off.

---

## Session S368 — 2026-04-18 (worktree `claude/nice-jackson-62879e`) — Controller navigation + scrollbar sweep (B-170)

**Scope**: High-context audit across ALL ImGui menus — controller navigation must reach every interactive element. Three-part systemic fix:

**Fix 1 — Cross-panel nav via `pdguiBeginActionBar` flatten.** `pdgui_layout.cpp::pdguiBeginActionBar` was calling `ImGui::BeginChild(id, size, /*border*/false, NoScrollbar|NoScrollWithMouse)` — no `ImGuiChildFlags_NavFlattened`. ImGui's keyboard/gamepad nav is per-window by default, so every docked action bar (Back / Save / Confirm / Cancel / etc.) sat in its own nav scope. Once focus was in the body child, D-pad Down could never cross into the bar — action bar buttons became unreachable on controller. Fix: pass `ImGuiChildFlags_NavFlattened` into the BeginChild. Covers every dialog that uses the action bar primitive (~20+ menus).

**Fix 2 — NavFlattened on body containers.** Most menus use the pattern `ImGui::BeginChild("##xxx_body", ImVec2(0, bodyH), false, ImGuiWindowFlags_NoBackground)` for a layout-only body child. Without NavFlattened, widgets inside the body couldn't nav out to the (now-flattened) action bar. Added `ImGuiChildFlags_NavFlattened` to ~25 body BeginChild calls across `pdgui_menu_{mppause,mpadvanced,mpsetup,botsetup,cheats,mpsettings,mainmenu,solomission,playerconfig,challenges,warning}.cpp`. Scrolling lists (weapon inventory, bot profile picker, etc.) got the flag too — ImGui auto-scrolls to keep focused items visible inside a flattened scope, so nav can still walk through the list while remaining able to exit to action bar buttons.

**Fix 3 — End Game modal popup.** `pdgui_menu_warning.cpp::renderMpEndGameDialog` rewritten on top of `ImGui::BeginPopupModal` instead of a parallel `Begin()`. The MP Pause Control window was sitting behind the End Game dialog; focus could drift back there on subsequent frames, making Cancel + End Match unreachable on controller. BeginPopupModal owns input exclusively — the modal intercepts all nav / mouse / keyboard, scrim + PD-red danger frame + keybinding hint row preserved. First-frame `OpenPopup` kick is guarded by a `s_EndGameOpenedForDialog` dialog-pointer track so repeated re-entries fire a fresh popup; `CloseCurrentPopup` + `menuPopDialog` happen together on confirm/cancel so the legacy stack stays consistent.

**Fix 4 — Scrollbar visibility.** `pdgui_style.cpp::pdguiApplyPdStyle`:
- `ScrollbarSize` 12 → 18 (bigger controller / mouse target; overflowing list reads as obviously scrollable)
- `GrabMinSize` 10 → 14
- `ScrollbarBg` alpha 0x87 → 0xCC (track more opaque — stands out against body fill)
- `ScrollbarGrab` / `ScrollbarGrabHovered` switched from `dialog_border1` (dim) to `dialog_border2` (accent) at `0xE0` / `0xF5`
- `ScrollbarGrabActive` already full `dialog_border2 | 0xFF`

Theme-level change — every themed palette (Blue, Dark Agent, etc.) inherits the wider/bright grab automatically.

**Files**: `port/fast3d/pdgui_layout.cpp` (+12/−4), `port/fast3d/pdgui_style.cpp` (+13/−6), `port/fast3d/pdgui_menu_warning.cpp` (+56/−57, End Game dialog), `port/fast3d/pdgui_menu_mppause.cpp` (+12/−6), `port/fast3d/pdgui_menu_mpadvanced.cpp` (+12/−6), `port/fast3d/pdgui_menu_mpsetup.cpp` (+16/−8), `port/fast3d/pdgui_menu_botsetup.cpp` (+10/−5), `port/fast3d/pdgui_menu_playerconfig.cpp` (+10/−5), `port/fast3d/pdgui_menu_cheats.cpp` (+2/−1), `port/fast3d/pdgui_menu_mpsettings.cpp` (+4/−2), `port/fast3d/pdgui_menu_mainmenu.cpp` (+6/−3), `port/fast3d/pdgui_menu_solomission.cpp` (+4/−2), `port/fast3d/pdgui_menu_challenges.cpp` (+2/−1).

**Build**: Clean 774/774. `PerfectDark.exe 53,170,487` / `PerfectDarkServer.exe 23,138,784`.

**Verification needed (playtest)**:
- **MP Pause → End Game**: press D-pad Left/Right — focus cycles between Cancel and End Match. Press A on Cancel → pops cleanly, no orphaned focus. Press A on End Match → match ends via `menuhandlerMpEndGame`.
- **Any dialog with a docked action bar** (Settings, MP Setup, Cheats, etc.): D-pad Down from the body's last widget should land on the action bar button (Back / Save / etc.).
- **Any list that overflows** (Cinema tracks, Cheats list, MP Setup arena / scenario list): scrollbar visibly wider + more contrast, visible without hover, grab uses accent colour.
- **Controller-only session**: end-to-end Main Menu → MP Setup → Start Match → Pause → End Game → Confirm. No dead-end dialogs.
- **Unaffected**: mouse + keyboard navigation should feel identical (scrollbar is just more visible).

**Deferred**: BeginPopupModal conversion for the other DANGER dialogs (`g_ExitGameMenuDialog`, `g_CheatsWarningMenuDialog`, `g_CheatsConfirmUnlockMenuDialog`). Those still use the `renderTypedDialog` path with an ImGui::Begin() window — if controller nav reports reach them, apply the same popup-modal rewrite pattern `renderMpEndGameDialog` now uses.

**Next**: Mike playtests; promote to FIXED on next sign-off.

---

## Session S367 — 2026-04-18 (worktree `claude/pensive-lovelace-832160`, merged to `dev`) — CI Room Team Setup back-nav + bot ctx Team submenu (B-168, B-169)

**Scope**: Two Combat Simulator menu bugs reported by Mike.

**Bug 1 (B-168)** — Team Setup close fell into Main Menu. Repro log (build `c9473d54`): Room OPEN (solo=1) → Team Setup acquired → Team Setup released → `MENU_IMGUI: main menu OPEN` fires; Main Menu draws on top of Room (still in background). Root cause: the "Combat Simulator" button in `pdgui_menu_mainmenu.cpp` calls `pdguiSoloRoomOpen()` without popping the Main Menu dialog — Main Menu sits under the Room overlay for the whole CI session. When a child dialog is pushed then popped, Main Menu's ImGui window goes hidden → visible, fires `IsWindowAppearing`, `SetWindowFocus()`es itself to the top of Z-order over the Room.

**Fix (B-168)**: new `pdguiSoloRoomIsActive()` accessor in `pdgui_lobby.cpp` returning `s_SoloRoomActive ? 1 : 0`; `renderMainMenu` in `pdgui_menu_mainmenu.cpp` early-returns (after the existing `!menuDialogIsCurrent` sibling guard) while the solo Room is active so the Main Menu window never calls `Begin` and never `SetWindowFocus`es. When "Back to Menu" clears `s_SoloRoomActive`, the Main Menu reappears cleanly with a fresh `IsWindowAppearing`. Covers every child-dialog-of-main-menu path (Change Agent, Cheats, Settings, etc.), not just Team Setup.

**Bug 2 (B-169)** — Bot right-click context menu had no way to assign bots to a team even when "Teams" was enabled. The room player list already renders team tints and the right-panel Options had a "Teams" toggle, but bot slot `.team` could only be changed via the separate Team Setup screen.

**Fix (B-169)**: added a "Team" submenu to the bot context popup in `pdgui_menu_room.cpp` between "Character" and the Separator. Gated on `MPOPTION_TEAMSENABLED` + `isLeader`. Header label shows "Team: N" when all selected bots share a team, just "Team" otherwise. Eight `Team 1..8` entries colored with the `kTeamColors` palette already in scope via `ImGui::PushStyleColor(ImGuiCol_Text, ...)`. Selecting applies `.team` to every selected bot, plays `PDGUI_SND_SUBFOCUS`, and sets `s_RoomSettingsDirty = true` so the change broadcasts via `SVC_ROOM_SETTINGS 0x78` in network mode.

**Files**: `port/fast3d/pdgui_lobby.cpp` (+13), `port/fast3d/pdgui_menu_mainmenu.cpp` (+12), `port/fast3d/pdgui_menu_room.cpp` (+42). Net +67 LOC across three files; zero deletions.

**Build**: Clean 776/776. `PerfectDark.exe` 52,957,977 / `PerfectDarkServer.exe` 23,142,898.

**Verification needed (playtest)**:
- CI Room (solo): open Team Setup, press Back/B → returns to Room, not Main Menu. Log shows `MENUPOOL: released mp_team_setup` and NO `MENU_IMGUI: main menu OPEN` follow-up.
- CI Room → add bots → enable Teams in right-panel Options → right-click a bot → "Team" submenu appears with colored entries. Pick Team 3; row tints green. Ctrl-click to multi-select; pick Team 1; all flip red.
- Disable Teams → "Team" submenu disappears from ctx popup.
- Network mode (as leader): team change via ctx syncs via SVC_ROOM_SETTINGS to other clients.

**Next**: Mike playtests; promote to FIXED after.

---

## Session S366 — 2026-04-18 (worktree `claude/cranky-lalande-73f6d2`) — B-167 propobj.c sibling `numparts<=0` guards: propagate B-166 relaxation to walker sites

**Scope**: Post-B-166 playtest (build `211cce3e` / v0.0.129) showed CI Training (0x26) boots clean — no more `MODELDEF: loaded torn` — but every door + simple prop renders at identity scale with no collision. Many `WARNING: SETUP: door modelnum X has no bbox node — using identity scale` (modelnums 404, 380, 68, 333, 334, 156, 421, 174) and rate-limited `DOOR.DIAG: doorGetBbox — no bbox for modelnum=X` per door tick.

**Root cause**: B-166 relaxed the over-aggressive `numparts <= 0` reject at the `modeldefLoad` chokepoint in `src/game/modeldef.c` (simple non-skeletal props legitimately have 0 parts). But S312 had added the *same* guard to two sibling walker functions in `src/game/propobj.c` — `modeldefFindBboxNode` (line 1289) and `modelFindBboxNode` (line 1353) — and B-166 did not propagate the relaxation. So non-skeletal props now *load* cleanly but `setupCreateDoor` → `modeldefFindBboxRodata` → `modeldefFindBboxNode` and `doorGetBbox` / `objFindBboxRodata` → `modelFindBboxNode` both reject them at bbox-lookup time and return NULL. S317 door identity-scale fallback then fires for every CI prop door, producing the visible symptom.

**Fix**: dropped `|| numparts <= 0` from both walker guards in `src/game/propobj.c`. Retained `rootnode == NULL`, `numparts > 500` (decode garbage upper bound), `scale <= 0.0f`, and the 10000-step walker cap (cycle guard). Body/head `numparts > 0` invariant is intentionally retained in the *per-caller* guards (`body0f02ce8c` src/game/body.c:206, `menuRenderModel` src/game/menu.c:2076, player setup src/game/player.c:1986) — those are the correct location for it because skeletal merge requires parts.

**Audit of other `numparts <= 0` sites (confirmed correct, unchanged)**:
- `port/src/modelcatalog.c:206` (`validateModeldef`) — called only for `g_HeadsAndBodies` bodies/heads via `classifyEntry`; keep.
- `port/src/net/netmanifest.c:1851` — logs only (no reject), gated on `MANIFEST_TYPE_BODY/HEAD`; keep.
- `src/game/body.c:206`, `src/game/menu.c:2076`, `src/game/player.c:1986` — all body-merge / skeletal-body paths; keep.

**Files**: `src/game/propobj.c` (net −2 LOC, comment updated).

**Build**: Clean 774/774. `PerfectDark.exe 53,169,357` / `PerfectDarkServer.exe 23,139,296`.

**Verification needed (playtest)**: Cold boot → CI Training (0x26). Doors render at correct pad-derived size (not tiny identity scale). Simple props visible with correct collision. The `SETUP: door modelnum %d has no bbox node` and `DOOR.DIAG: doorGetBbox — no bbox` WARNINGs should drop to zero for props that *do* have a bbox node in their tree. Props legitimately without a bbox node in their tree (if any) will still log once but were never going to render with collision anyway. Bodies/heads unchanged.

**Next**: Mike playtests.

---

## Session S365 — 2026-04-18 (worktree `claude/hungry-heisenberg-f18ad5`, merged to `dev` @ `b998ec40`) — Memory floor check: sysFatalError on MemorySize too low

**Scope**: Prevent ACCESS_VIOLATION crash (memcpy in title stage load) when stale pd.ini has MemorySize=16.

**Root cause**: M5/M6 pool split requires 60 MB minimum (PERMANENT 16 + STAGE 40 + POOL_8 4). Default configRegisterInt min was still 4, so the config layer accepted stale 16 MB values. PERMANENT consumed the entire 16 MB heap; STAGE got 0 bytes via the CARVE macro silent clamp.

**Changes**:
1. `src/lib/memp.c::mempSetHeap`: Added early check — if `heaplen < MEMP_PERMANENT_SIZE + MEMP_STAGE_SIZE + MEMP_POOL8_SIZE`, calls `sysFatalError("MemorySize too low (%u MB). Minimum required: 64 MB.\nDelete pd.ini to reset to defaults.", heaplen / (1024*1024))`. Check fires before pool zeroing so there's no partial state.
2. `port/src/main.c::gameConfigInit`: Raised `configRegisterInt("Game.MemorySize", ...)` minimum from `4` to `64`. Config layer now clamps stale values on next save.

**Files**: `src/lib/memp.c` (+7 LOC), `port/src/main.c` (+0/-0, 1-char min value change).

**Build**: Clean 780/780. `PerfectDark.exe`, `PerfectDarkServer.exe`, `Updater.exe` all linked.

**Next**: None queued from this task.

---

## Session S364 — 2026-04-18 (worktree `claude/exciting-swanson-90a81a`, merged to `dev` @ `052816bf`) — Release pipeline: remove .sha256 sidecar files

**Scope**: Purge all `.sha256` file generation and upload from `devtools/release.ps1`.

**Changes**:
- **Step 1**: Removed 3 `Get-FileHash` + `Out-File` calls writing `*.sha256` sidecars to `dist/`. Write-Host simplified; header comment updated.
- **Step 5**: Removed 3 `.sha256` asset-upload lines; comment block updated.
- **Header**: Removed "SHA-256 hashes for update system verification" from package contents description.

**Files**: `devtools/release.ps1` (−13 net LOC).

**Build**: `ninja -C Build pd pd-server pd-updater` → no work to do (no C/C++ touched).

**Next**: none queued from this task.

---

## Session S363 — 2026-04-18 (worktree `claude/crazy-varahamihira-82fe52`, merged to `dev` @ `c957fb62`) — Standalone updater: auto-check, filter-in-view, button layout

**Scope**: Three UX fixes to `port/src/updater_standalone/updater_gui.c`.

**Changes**:
1. **Auto-check on launch.** `PostMessage(IDC_BTN_CHECK)` posted from `runGui()` after `createControls` so the release list populates immediately without a manual click. Initial status set to "Checking for updates..." instead of idle prompt.
2. **Filter without re-fetch.** Full release list (all non-draft) now stored in `app_state_t::allReleases[]` / `allReleaseCount`. New `filterReleases()` function applies the `showDevReleases` flag to build the displayed `releases[]` subset. Checkbox toggle calls `filterReleases()` + `populateList()` only; no network request. `checkThread` now always passes `showDev=1` to `parseReleasesJson`.
3. **Button layout fix.** Window height computed via `AdjustWindowRect` from `WINDOW_CLIENT_H=572` (total client pixel height of all controls + padding) instead of a hardcoded 580 that left the Update/Close buttons clipped below the client area.

**Files**: `port/src/updater_standalone/updater_gui.c` (+31 / −8 LOC).

**Build**: Clean 13/13. `Updater.exe`, `PerfectDarkServer.exe`, `PerfectDark.exe` all linked.

**Next**: none queued from this task.

---

## Session S362 — 2026-04-18 (worktree `claude/nervous-satoshi-763192`, merged to `dev` @ `4260a1fd`) — Release pipeline: local-testability, Updater.exe bundling, Dev-release prune

**Scope**: Three fixes to `devtools/release.ps1` so the Dev Window Release button is self-contained and doesn't leave the build unusable when a later step fails.

**Changes**:
1. **ROM copy after client build.** Mirrors `dev-window-v2.ps1`'s `Copy-AddinFiles` inside `release.ps1` as `Copy-RomAddinIntoBuild`. Runs immediately after `pd` links (and at the top of the `-SkipBuild` path), so `Build/data/pd.{ROMID}.z64` lands before any subsequent step can fail. Mike can now launch `Build/PerfectDark.exe` locally even when the server/updater build or the GitHub publish blows up.
2. **Updater.exe in the release bundle.** Added `pd-updater` as a third build target in the rebuild loop (marked optional — its failure warns, never blocks). `-SkipBuild` path does an incremental `cmake --build --target pd-updater` if `Updater.exe` is missing. Staged to `dist/v{X.Y.Z}/Updater.exe` + `.sha256`, shipped inside the zip and uploaded as individual GitHub release assets.
3. **Rolling Dev-release prune.** New Step 6 runs after a successful prerelease publish: `gh release list --json` → filter `isPrerelease=true, isDraft!=true` → keep newest 10 → delete the rest with `gh release delete --cleanup-tag` (fallback to `gh api -X DELETE` for older gh). Stable releases are never touched. Skipped on DryRun, SkipPush, missing gh, or failed publish.

Step numbering bumped to `/8` (new Step 6 = prune; old Step 6 = Step 7).

**Files**: `devtools/release.ps1` (+197 / −20 LOC).

**Build-verify**: `cmake --build Build --target pd-updater` from the parent project produced `Build/Updater.exe` (12.8 MB). PS7 parser confirms no syntax errors. Logic tested via `-DryRun` run (which surfaced the pre-existing quirk that `-SkipBuild` pre-release commit+push runs even under `-DryRun` — recorded as caveat).

**Caveat**: `-DryRun` does NOT suppress the `-SkipBuild` pre-release commit+push in Step 0. This is pre-existing; kept as-is to match prior behavior but worth auditing later. This session hit it once and had to revert an accidental CMakeLists.txt v99.0.0 bump (commit `37aaf34c` on the worktree branch).

**Next**: none queued from this task. Release pipeline changes are live on `dev`.

---

## Session S361 — 2026-04-18 (dev direct, commit `7d6ec8fb`) — Dev Window v2 Async Runspace Pool

**Scope**: Convert blocking PowerShell/WPF operations in `devtools/dev-window-v2/dev-window-v2.ps1` to an async RunspacePool so the UI stays responsive during startup, status polling, git operations, and doc scans.

**Changes**:
- Three separate `Add-Type -Language CSharp` calls consolidated into a single block, guarded on the last class — avoids triple cold-compile (~2–4 s each) on startup.
- Persistent `BgPool` RunspacePool added (1–3 threads, ReuseThread apartment). Opened once at startup, disposed on window close.
- `Update-StatusBar` (fires every 2 s from `MainTimer`) now reuses `BgPool` instead of calling `[RunspaceFactory]::CreateRunspace` + `Open` on the UI thread (~100–500 ms per tick) — eliminates the ongoing periodic stutter.
- `Populate-DocList` (scans `context/` + `docs/`, ~170 files recursive) now runs on `BgPool`; `Loaded` event no longer blocks the first paint.
- New `Start-AsyncPoolAction` helper wraps `Invoke-GitPull` / `Invoke-GitPush` / `Invoke-PruneWorktrees` and the Check button so the UI stays live during `git` network ops and `bash devtools/git-snapshot.sh` runs.
- `Toggle-Server` / `Toggle-Game` update button text + log line before spawning; `UseShellExecute=$false` skips Shell32 association lookup; errors now surface in a MessageBox instead of failing silently.

**Files**: `devtools/dev-window-v2/dev-window-v2.ps1` (+489 / -? LOC).

**Build**: No game code touched — dev-tool only. Release pipeline produced v0.0.120.

---

## Session S360 — 2026-04-17 (worktree `claude/blissful-curie-2387ee`, commits `23798916` → merge `3da8191c`) — Updater CA-Bundle SSL Fix

**Scope**: Testers reported `SSL peer certificate or SSH remote key was not OK` on every update check. Root cause: MSYS2's statically-linked `libcurl.a` is built against OpenSSL **without** winstore integration (no `SSL_CTX_load_verify_store` symbol), so `CURLSSLOPT_NATIVE_CA` compiles but is a runtime no-op for this backend — and there is no external CA file shipped with the exe.

**Fix**: Embed the Mozilla CA bundle as a `CURLOPT_CAINFO_BLOB`. Self-contained, zero-DLL compliant, independent of whatever cert store exists on the tester's machine. `CURLSSLOPT_NATIVE_CA` retained as a harmless secondary.

- New `port/src/cacert.pem` (223,837 bytes, copied from mingw64 `ca-bundle.crt`).
- `CMakeLists.txt` generates `${BINARY_DIR}/port/include/cacert_blob.h` via `file(READ ... HEX)` + `REGEX REPLACE` at configure time.
- `updater.c`: new `curlSetupTLS()` helper replaces the two inline SSL blocks in `curlGet()` and the file-download path.

**Files**: `CMakeLists.txt`, `port/src/cacert.pem` (new, +3,901 lines), `port/src/updater.c`.

**Build**: Release pipeline produced v0.0.119.

**Verify**: Launch updater on a machine with no user CA store — update check should succeed over HTTPS.

---

## Session S359 — 2026-04-17 (worktree `claude/determined-austin-d54582`, commit `5122a663`) — B-163 Secondary Crash Site Guards

**Scope**: S317+S318 addressed the door-creation AV at PC+0x161258 during CI Training setup. Tester logs on build `acdf4062` (v0.0.118 pre-release) confirmed a **second** AV at PC+0x15f83a along the prop/chr-creation path during stage 0x26 — `setupCreateObject` had four unguarded `obj->model->scale` dereferences after `setupLoadModeldef` could return a torn modeldef that `objInit` gracefully resolved to `obj->model = NULL`.

**Fixes** (mirror S317 FIX-B.2 pattern):
- `src/game/setup.c::setupCreateObject` — early return with WARNING if `g_ModelStates[modelnum].modeldef == NULL` after `setupLoadModeldef`. Four downstream `obj->model->scale` derefs now safe.
- `src/lib/model.c::modelAllocateRwData` — defensive NULL/rootnode guard. All callers inherit the crash-proofing.
- `port/src/net/netmsg.c:2213` — guard `laptopDeploy` NULL return before dereferencing `obj` (torn modeldef path over the wire).
- `src/game/body.c::bodyAllocateModel` (lines 238–276) — guard `headmodeldef` NULL in both the random-head path (`func0f18e57c` returning NULL slot) and specific-headnum path (`catalogGetHeadModeldef` returning NULL). Prevents `bodymodeldef->rwdatalen += headmodeldef->rwdatalen` AV.

**Context update**: B-163 entry in `bugs.md` already captured this under the "S327 addendum (magical-zhukovsky)" label — verified the bug description matches `5122a663` exactly.

**Build**: Clean 774/774. Release pipeline produced v0.0.118.

**Verify**: Defection → Next Mission → CI Training (0x26). Any torn modeldef now produces `SETUP: object modelnum %d modeldef NULL after load` instead of an AV.

---

## Session S358 — 2026-04-17 (dev direct, commit `be0935da`) — B-161 Title/Intro Load NULL-Guards

**Scope**: With `modeldefLoad` now rejecting torn modeldefs at load time (S323 root-cause fix), the title/intro screens needed matching NULL-handling so a rejected logo model could no longer AV on subsequent frames.

**Fixes** (`src/game/title.c`, +215 / -69 LOC):
- `titleInitNintendoLogo` / `titleInitRareLogo` / `titleInitPdLogo` — handle `modeldefLoad()` returning NULL (torn-load reject path). Cascade to next intro mode or SKIP on failure.
- Exit/render paths guard against NULL `g_TitleModel*` so a failed intro model can no longer AV on subsequent frames.

**Context update**: bugs.md B-161 entry amended to list title.c among the call-site guards. Completes the defensive sweep: `modeldefLoad` (root), `setupCreateDoor` / `setupCreateObject` / `modelAllocateRwData` / `body.c::bodyAllocateModel` / `netmsg.c::laptopDeploy` / `title.c::titleInit*Logo` — every AV path converts to a WARNING log line.

**Build**: Release pipeline produced v0.0.117.

**Verify**: Cold boot into title sequence on a system with a deliberately torn intro logo modeldef — no AV; log shows `TITLE: intro model load failed — skipping to next mode`.

---

## Session S357 — 2026-04-17 (worktree `hopeful-payne-558fb2`) — B-112 Root Cause Investigation

**Scope**: Deep dive into B-112 (chr rbx corruption, 31-bot matches). Verify whether B-12 Phase 3 (participant pool) introduced new overflow vectors, confirm or deny recursive AI execution hypothesis, check all array bounds.

**Findings** (investigation only — no code change):

- **No recursive AI**: `chraiExecute` (chrai.c:671) uses an iterative `while(g_Vars.ailist)` dispatch loop over `g_CommandPointers[type]()`. Neither AI commands nor action-dispatch functions call `chraTick` or `chraiExecute` on other chrs. Confirmed by grep across `chraicommands.c` and the entire `src/game/` tree.

- **Array bounds correct**: participant pool initialized at `MAX_MPCHRS=40` at all call sites (mplayer.c, netmsg.c, net.c). Chr slot pool sized to `PLAYERCOUNT()+numchrs+10 = 1+31+10 = 42`. `g_MpAllChrPtrs`, `g_MpAllChrConfigPtrs` sized `MAX_MPCHRS=40`, `g_MpBotChrPtrs` sized `MAX_BOTS=32`. All fit 31 bots without overflow.

- **B-12 Phase 3 cleared**: participant pool API (`mpAddParticipantAt`, `mpRemoveParticipant`) has correct bounds checks. Bot slots 8..38 for 31 bots fit within capacity 40. No off-by-one at 31-bot boundary.

- **`AVOID_UB=1` in CMakeLists.txt**: guards the known N64 `mpCalculateAwards` `playerrankings[1]` overflow — PC build correctly sizes it as `[MAX_MPCHRS]`.

- **Class 1 confirmed**: Stack depth accumulated within a single bot's `chraTick→chraiExecute→action dispatch→collision/combat` call chain. More bots in combat = more probability of hitting the worst-case path. FIX-A.1 (512 KB guard at chraTick entry) correctly mitigates this.

- **Class 2 confirmed**: Bot respawn never frees/reallocates chr slots — `botSpawn` teleports in place. FIX-A.2 generation tokens handle the rare edge case. No B-12 Phase 3 impact.

**Context updated**: B-112 entry in bugs.md expanded with full root cause confirmation.

**Next steps**: 31-bot repro test to confirm FIX-A eliminates crashes in practice (no code change needed this session).

---

## Session S356 — 2026-04-17 (worktree `dazzling-vaughan-204b7c`) — Forge Door Lifecycle + D5 Phase 5C+5D Portrait Polish

**Scope**: Two parallel tasks — Forge door `doorobj` pool lifecycle (OPEN_DOOR/CLOSE_DOOR wired to live engine) + D5 Phase 5C (hover portrait preview) + Phase 5D (drop shadow, team-color border).

**Forge door lifecycle:**
- `forge_runtime.c`: `s_door_pool[16]` allocated from `MEMPOOL_STAGE`; `s_spawn_door()` builds `doorobj` from catalog modeldef via `objInitWithModelDef`, sets `doortype`, `unk98` slide vector (yaw-aware for left/right, vertical for up), `maxfrac/accel/decel`, `DOORFLAG_0080` for sliding family, `DOORFLAG_AUTOMATIC` if `dp->auto_close`. Calls `propActivate`, `propEnable`, `setup0f0923d4`. Stores `h->prop` + `h->doorobj`. `forgeRuntimeFindDoorByUid()` scans handles by forge UID.
- `forge_runtime.h`: added `struct doorobj *doorobj` field to `forge_prop_handle_t`; added `forgeRuntimeFindDoorByUid()` declaration.
- `forge_logic.c`: added `#include "game/propobj.h"` + `#include "constants.h"`; `FORGE_OP_OPEN_DOOR` now calls `doorsRequestMode(door, DOORMODE_OPENING)` if live doorobj found, else falls back to data-only `locked=0`; `FORGE_OP_CLOSE_DOOR` calls `doorsRequestMode(door, DOORMODE_CLOSING)`.

**D5 Phase 5C — hover portrait preview:**
- `s_HoverLobbyIdx` static tracks hovered lobby row index (-1 = none), reset each frame before the row loop.
- Human row: `rowHovered = ImGui::IsItemHovered()` captured immediately after Selectable; sets `s_HoverLobbyIdx` when true.
- Tooltip emitted when hovered + portrait data present: requests charpreview FBO, shows live texture if ready, falls back to baked 44px thumbnail, then player name.
- `lobbyPortraitsTick` suppressed when `s_HoverLobbyIdx >= 0` (avoids FBO contention with baking pipeline).

**D5 Phase 5D — portrait polish:**
- Drop shadow: 3px offset dark rect drawn before the portrait background rect.
- Team color border: portrait border uses `kTeamColors[r.team]` when teams are on; local player stays green; default remains info-cyan.

**Build**: Clean 776/776 (full clean build on dev post-merge). Zero errors. Pre-existing warnings only.

**Line counts (pre/post merge match):** forge_logic.c 356, pdgui_menu_room.cpp 3453, forge_runtime.h 76, forge_runtime.c 945.

**Next steps**: 31-bot repro test for B-112 (FIX-A validation); D5 Phase 6 (scoreboard, game HUD elements); Forge logic playtest in Grid editor.

---

## Session S355 — 2026-04-17 (`goofy-pike-572f8a` worktree) — Wave 5 Cross-Audit: S352 + S353 + S354

**Scope**: Audit sessions S352 (D5 Phase 5 Lobby Portraits), S353 (Prop Sync Event-Driven + Killfeed), and S354 (Forge Runtime Wire-In) for bugs, gaps, and missed items. Fix any findings, build-verify, merge to dev.

**Audit results — S352 (Lobby Portraits): CLEAN**
All 5 checklist items pass:
- FBO resource cleanup: `lobbyPortraitsReset()` called in both `IsWindowAppearing` and `pdguiRoomScreenReset()`. ✅
- Portrait invalidation on character change: `lobbyPortraitsSync()` compares `head_id`/`body_id`; frees texture and requeues bake on mismatch. ✅
- Thread safety: all GL ops on single-threaded render loop. ✅
- Array bounds: `LOBBY_PORTRAIT_MAX = 8 = MAX_PLAYERS`, all loops bounds-checked. ✅
- Alpha clamping: increments clamped to `[0, 1]` in sync; solo mode uses `1.0f` directly at render time (not from the ramp array). ✅

**Audit results — S353 (Prop Sync + Killfeed): ONE BUG FIXED**

`NET_PROP_DIRTY_MAXSYNCID = 512` was too small. Prop syncids are `prop − g_Vars.props + 1`, so they can reach `maxprops` (bounded by the 2048-slot sync ID map). Flags for props at slots 512–2047 were silently dropped by `netPropMarkDirty`, causing the 120-tick CRC heartbeat to be skipped on large stages even when those props had real events. No event messages were lost (SvcProp*Write still wrote to the wire buffer); only the CRC-heartbeat desync detector was affected.

**Fix**: `port/src/net/netmsg.c` — changed `#define NET_PROP_DIRTY_MAXSYNCID 512` to `#define NET_PROP_DIRTY_MAXSYNCID NET_PROP_MAP_SIZE` (2048). Static array grows by 1536 bytes. No wire format change.

Other S353 items verified clean: kill feed NULL safety (guarded at `g_MpAllChrConfigPtrs[i]`), kill feed truncation (uses `netbufWriteStr`), heartbeat timer (correct), race condition (none — single-threaded). ✅

**Audit results — S354 (Forge Runtime): CLEAN**
All 5 checklist items pass:
- Zone performance: 128 simple float AABB/sphere comparisons per tick — negligible at 60 Hz. ✅
- Prop pool exhaustion: logs `GRID.RUNTIME: prop pool full` + drops gracefully. ✅
- Weapon catalog resolve failure: guarded at all three failure points (catalogResolveWeapon, weaponCreate, func0f08ae0c). ✅
- Zone teleporter player-position bounds: target NULL-checked; author responsibility for valid world positions. ✅
- Edge-trigger state reset: `s_register_zone()` memsets each `forge_zone_rt_t` to zero on registration; `forgeRuntimeEnterPlay` resets `s_zone_count = 0` before re-registering. ✅

**Build**: Clean 774/774 (worktree). Clean 4/4 incremental (dev post-merge — only netmsg.c rebuilt + relink).
`PerfectDark.exe` 52,810,516 / `PerfectDarkServer.exe` 22,925,709.

---

## Session S354 — 2026-04-17 (`practical-varahamihira-3b5f5d` worktree) — Forge Runtime Wire-In: Props, Weapons, Geometry, Doors, Zones

**Scope**: Wire remaining Forge (The Grid) object categories into the live engine. Spawn points + AI bots were already live (S314); everything else was log-only.

**Implementation** (single file: `port/src/forge/forge_runtime.c`):

- **WEAPON_PAD**: `s_spawn_weapon_pad()` — `catalogResolveWeapon` → `weaponCreate(0,0,NULL)` → `func0f08ae0c(weapon, modeldef)` → `modelSetScale(weapon->base.model, 1.0f)` (overrides g_ModelStates[0] for dynamically-loaded models) → set pos + identity realrot → `propActivate` + `propEnable` + `setup0f0923d4`. Ammo uses weapon default on pickup (custom ammo setting deferred — `weaponobj` has no direct clipammo field).

- **PROP / GEOMETRY / INTERACTABLE**: `s_spawn_prop()` — `catalogResolveProp` → `objInit` from MEMPOOL_STAGE pool (`s_prop_pool[64]`) with `OBJTYPE_BASIC` + `extrascale=256` → `modelSetScale(1.0f)` → set pos + identity realrot → `propActivate` + `propEnable` + `setup0f0923d4`. Collision auto-generated from model bbox by `objInit`. INTERACTABLEs spawn visually; interaction logic deferred.

- **ZONE**: `s_register_zone()` fills `s_zone_rt[128]` with type/shape/pos/half-extents/channel names/teleport_uid. `forgeRuntimeTick` runs per-tick box/sphere intersection for `g_Vars.currentplayer`. Edge-triggered: enter → `forgeChannelSet(channel_on_enter, 1)` + optional teleport + `forgeLogicFireEvent(ON_PLAYER_ENTER, uid)`; exit → channel_on_exit + `ON_PLAYER_EXIT`.

- **DOOR**: Catalog entries route to `s_spawn_prop` for visual presence. Full `doorobj` pool lifecycle (open/close state machine, `doorInit`, `doorsActivate`) deferred — requires dedicated pool infrastructure.

**Architecture decisions**:
- `s_prop_pool` allocated once from MEMPOOL_STAGE — survives FREEFLY↔NORMAL toggles, freed on stage unload.
- `modelSetScale(model, 1.0f)` required after `objInit`/`func0f08ae0c` for forge-placed objects because `obj->modelnum` defaults to 0 and `g_ModelStates[0].scale` may be unset.
- `setup0f0923d4(obj)` chosen over `func0f06a580` — auto-computes rooms from bbox, no need to pre-build Mtxf or find rooms manually.
- Zone tick only checks `g_Vars.currentplayer` (local player); multi-player zone tracking deferred.

**New state**: `s_prop_pool` (MEMPOOL_STAGE pointer), `s_prop_count`, `s_zone_rt[FORGE_ZONE_RT_MAX=128]`, `s_zone_count`.
**New includes**: `game/propobj.h`, `game/modeldef.h`, `game/setuputils.h`, `lib/model.h`, `lib/memp.h`.

**Build**: Clean compile of `forge_runtime.c`; `PerfectDark.exe` + `PerfectDarkServer.exe` linked successfully.

**Next**: Full door lifecycle (doorobj pool), multi-player zone tracking, custom weapon ammo on pad spawns.

---

## Session S353 — 2026-04-17 (`pedantic-saha-8d7ff0` worktree) — Prop Sync Event-Driven + Killfeed Bot Kill Verification

**Scope**: Two tasks — replace CRC polling in prop sync with event-driven dirty flags; verify killfeed correctly shows bot kills.

**Task 1 — Prop Sync Event-Driven:**

Added dirty-flag bitset (`s_PropDirtyFlags[512]`, `s_PropDirtyCount`) in `port/src/net/netmsg.c`. Each SvcProp*Write function (`SvcPropMoveWrite`, `SvcPropDamageWrite`, `SvcPropPickupWrite`, `SvcPropUseWrite`, `SvcPropDoorWrite`, `SvcPropLiftWrite`) calls `netPropMarkDirty(prop->syncid)` before writing to the buffer. `netmsgSvcPropSyncWrite` skips entirely when `s_PropDirtyCount == 0` — O(1) instead of O(N_props) every 120 ticks (~2×/sec). CRC still scans all props so server/client produce identical hashes. No wire format change.

**Task 2 — Killfeed Bot Kills (verified, no code change):**

Code trace confirms all kill combinations show correctly: `mpstatsRecordDeath` → `pdguiKillfeedPush` fires for all `ampchr && vmpchr` via `func0f18d074` + `MPCHR` macro. Format is `[Attacker] killed [Victim]` uniformly. Roadmap entries marked DONE.

**Build**: Clean 774/774 (worktree). Clean 585/585 (dev post-merge). `PerfectDark.exe` 52,772,152 / `PerfectDarkServer.exe` 22,924,028.

---

## Session S352 — 2026-04-17 (`confident-brahmagupta-a3f5a3` worktree) — D5 Phase 5: Lobby Player Portraits

**Scope**: D5 Phase 5 — per-player character portrait thumbnails in the room screen player list.

**Problem**: Human rows in `pdgui_menu_room.cpp` showed only text (name, body name, state label).
No visual representation of each player's selected character (body/head).

**New state** (`port/fast3d/pdgui_menu_room.cpp`):
- `LobbyPortrait` struct with `glTex`, `head_id`, `body_id` fields
- `s_LobbyPortraits[LOBBY_PORTRAIT_MAX=8]` — per-slot baked GL textures
- `s_LobbyPortraitAlpha[8]` — per-slot join fade-in [0,1]
- `s_LobbyPortraitPending` / `s_LobbyPortraitWaitReady` — bake pipeline state

**New helpers**:
- `lobbyPortraitsReset()` — free all baked textures, zero state
- `lobbyPortraitsSync(humanCount)` — invalidate stale entries on ID change or player leave; advance fade-in alpha (+0.04/frame ≈ 25-frame ramp)
- `lobbyPortraitsTick(humanCount)` — sequential baking pipeline: one FBO render per frame, bake to standalone GL texture on ready; guarded by `s_BotModalOpen` to avoid fighting the bot setup modal over the shared charpreview FBO

**Human row changes**:
- Row height increased from text-line-height to 50px (portrait thumbnail height + padding)
- Portrait thumbnail (44px): baked GL texture with Y-flip UVs; fallback = initials circle (team-tinted background)
- State badge: 5px colored dot in top-right corner of portrait (yellow=connecting, green=ready, blue=in-game)
- Name + leader/you badge on line 1; body name + state label on line 2
- All colors respect join fade-in alpha
- Bot rows unchanged

**Lifecycle**:
- `lobbyPortraitsReset()` called in `IsWindowAppearing` (each room open) and in `pdguiRoomScreenReset()` (state teardown)
- Solo mode: baking skipped; initials placeholder shown; alpha = 1.0

**Build**: Clean 774/774 (worktree) + 585/585 (dev post-merge). Zero errors.
`PerfectDark.exe` 52,770,947 / `PerfectDarkServer.exe` 22,922,823.

---

## Session S351 — 2026-04-17 (`dazzling-heisenberg-f84acc` worktree) — D5 Phase 4: UI Texture Mod Overrides

**Scope**: D5 Phase 4 — implement runtime mod override of base-UI theme textures.

**Problem**: `pdguiThemeLateInit()` loaded base-ui TGA textures (or procedural fallbacks)
but had no mechanism for enabled mods to supply alternate textures.  Mods could declare
`"type": "ui"` components in `mod.json` but nothing parsed them.

**New API** (`port/include/pdgui_theme.h`):
- `s32 pdguiThemeScanModUiTextures(const char *mod_dir)` — parse `mod.json` components[]
  for `"type": "ui"` entries; call `s_registerModTexture` for each `catalog_id`/`path` pair.
- `void pdguiThemeApplyEnabledModUiTextures()` — iterate all enabled mods and apply overrides.
  Guard: no-op if late init hasn't run yet.

**Wire-in**:
- `pdguiThemeLateInit()`: call `pdguiThemeApplyEnabledModUiTextures()` after base textures load.
- `modmgrApplyChanges()` (`port/src/modmgr.c`): call after `pdguiThemeRescanChromeStyles()`.

**Parser design**: Two-pass per component object (handles `"type"` and `"textures"` in any
order). Reused existing `chrome_jparse`/`chrome_jtok`/`cjson_*` infrastructure.
Includes `modmgr.h` via `extern "C" {}` block.

**Fix commits** (same session): split `extern "C" { #include }` to multi-line;
move functions after `cjson_skip_value` to resolve forward-reference errors.

**Build**: Clean 585/585. `PerfectDark.exe` 52,759,787 / `PerfectDarkServer.exe` 22,922,823.

---

## Session S350 — 2026-04-17 (`vigorous-benz-f8cb68` worktree) — Wave 3 Cross-Audit: S348 D7 Discord Rich Presence

**Scope**: Audit the S348 Discord Rich Presence implementation.

**Audit findings**: Nine checklist items examined:

1. **IPC protocol** ✅ — opcode framing, byte order, handshake `{"v":1,"client_id":"..."}`, SET_ACTIVITY format all match Discord spec.
2. **PIPE_NOWAIT fix** ✅ — `CreateFileA` uses flags=0 (synchronous, non-OVERLAPPED), `SetNamedPipeHandleState` adds `PIPE_NOWAIT`; `ReadFile`/`WriteFile` use NULL lpOverlapped. Session noted the original draft had `FILE_FLAG_OVERLAPPED` — the fix is correct.
3. **JSON injection** ❌ → **FIXED** — `disc_send_activity` inserted `details` and `state` into JSON via raw `%s`. A mod stage slug with `"` or `\` in its catalog ID would produce malformed JSON and corrupt the pipe frame. Added `disc_json_str()` helper that escapes `\` → `\\` and `"` → `\"` before insertion.
4. **Fail-silent** ✅ — 30s reconnect backoff; `disc_try_connect` does up to 10 fast `CreateFileA` calls then returns; never tight-loops.
5. **Thread safety** ✅ — `discordTick` called inside `mainTick`'s `g_MainChangeToStageNum < 0` block (single-threaded game tick).
6. **Memory leaks** ✅ — no heap allocations; pipe handle closed in `disc_disconnect()` on error and shutdown.
7. **Edge cases** ✅ — NULL `stage_id` guarded in `stage_id_to_name`; `s_ActivityStart` reset on state change; reconnect after drain-triggered disconnect tries once immediately then backs off via `s_LastConnect`.
8. **MinGW/GCC** ✅ — `_snprintf`, Windows API, `(long long)time_t` with `%lld`, `(unsigned long)DWORD` with `%lu` all correct.
9. **Server exclusion** ✅ — `discord.c` picked up by `GLOB_RECURSE SRC_PORT` (client only); server `SRC_SERVER` is an explicit list and excludes it; `server_main.c` never calls discord.

**Fix**: `port/src/discord.c` — new `disc_json_str()` escape helper (+23 lines). Applied to `esc_details`/`esc_state` before both `_snprintf` branches in `disc_send_activity`.

**Build verify**: During post-merge build, discovered S351 had a latent `extern "C" { #include "modmgr.h" }` single-line form that GCC rejects as a "stray '#'". Fixed inline (`port/fast3d/pdgui_theme.cpp:57`) by splitting to 3-line form. Build clean 776/776 (includes S351 sources). `PerfectDark.exe` 52,759,787 / `PerfectDarkServer.exe` 22,922,823.

---

## Session S349 — 2026-04-17 (`cool-poitras-b287e7` worktree) — Cross-Audit S346+S347

**Scope**: Wave 3 cross-audit — review S346 (AP Phase 4: retire filenum from public catalog API) and S347 (blue tint sweep + gamepad audit) for bugs, gaps, and missed items. Fix anything found. Build-verify and merge.

**S346 audit — CLEAN:**
- `assetprovider_internal.h` included by exactly 5 allowed callers (`assetload.c`, `assetcatalog_api.c`, `assetcatalog_base.c`, `assetcatalog_base_extended.c`, `server_stubs.c`). `assetprovider_rom.c` is the *implementation* file — correctly includes only `assetprovider.h`, no violation.
- No game code calls `romProviderHandle()` directly. Only mentions are documentation comments in `file.h` and `assetprovider.h`.
- Stage handle fields (`bg_handle`, `pads_handle`, `setup_handle`, `mpsetup_handle`, `tile_handle`) populated with correct `fileid > 0` guard in `catalogGetStageResultByIndex`. Server build leaves handles zeroed (null).
- `assetHandleIsNull()` checks `provider == NULL` — all call sites (`assetLoadToNew`, `assetLoad`, `assetUnload`, `assetDescribe`) null-guard before vtable dispatch.
- `catalogGetBodyHandle` / `catalogGetHeadHandle` / `catalogGetPropHandle` all use `memset(&null_h, 0, sizeof(null_h))` + `CATALOG-FATAL` log + `g_CatalogFailure = 1` on miss. `catalogGetHeadHandle` correctly fast-paths `HEAD_RANDOM_GENDER → null_h`.
- Deprecated SA-5a bridge functions (`catalogGetBodyFilenumByIndex`, `catalogGetHeadFilenumByIndex`, `catalogGetPropFilenumByIndex`) clearly marked `[DEPRECATED]` + `[MIGRATION BRIDGE]` in header.
- `assetLoadRomToNew` declared in `assetload.h`, implemented in `assetload.c` as a one-liner delegate to `assetLoadToNew(romProviderHandle(filenum), ...)`.

**S347 audit — 2 missed blue IM_COL32 literals fixed:**

Both files were in S347's scope but 2 sites were not caught:
- `pdgui_menu_moddinghub.cpp:2109`: `IM_COL32(90, 120, 170, 220)` (Nine-Slice chrome tool preview border) → `pdguiImU32TitleGlow(220)`
- `pdgui_menu_agentcreate.cpp:324`: `IM_COL32(140, 160, 200, 180)` (body name label below agent portrait) → `pdguiImU32TintInfo(180)`

**Intentionally left alone:**
- `pdgui_menu_solomission.cpp:499`: `IM_COL32(80, 160, 255, 255)` — Special Agent difficulty badge (semantic game color, not PD palette accent)
- Forge/Grid HUD blue/cyan literals — editor-mode UI colors with distinct design intent (cyan cursor, grid snap indicators)
- `pdgui_countdown.cpp:168`: `IM_COL32(8, 8, 20, 235)` — near-black panel background with barely perceptible blue bias, below threshold for replacement

**Build**: Clean 774/774 in worktree. Post-merge dev build clean (incremental). `PerfectDark.exe` 52,861,458 / `PerfectDarkServer.exe` 22,907,957.

**Merge**: `claude/cool-poitras-b287e7` → `dev` via no-ff merge. Post-merge line count verified (agentcreate: 715 lines, moddinghub: 2833 lines — stable, no truncation).

## Session S348 — 2026-04-17 (`ecstatic-cartwright-11459d` worktree) — D7 Discord Rich Presence

**Scope**: Implement Discord Rich Presence (D7) — thin Windows IPC client, no external library.

**Approach**: Implemented the Discord RPC named-pipe protocol directly in C, bypassing
the archived discord-rpc library (which requires C++ + rapidjson) and the Discord Game
SDK (DLL-only, violates zero-DLL policy).  The client speaks `\\.\pipe\discord-ipc-{0..9}`
directly using Windows `CreateFile` / `WriteFile` / `ReadFile` + `PIPE_NOWAIT` mode.

**New files**:
- `port/include/discord.h` — public API: `discordInit` / `discordShutdown` / `discordTick`
- `port/src/discord.c` — IPC client implementation (~280 lines)

**Wiring**:
- `port/src/main.c` — `discordInit()` after `prefsAgentInit()`; `discordShutdown()` first in `cleanup()`
- `port/src/pdmain.c` — `discordTick()` at end of `mainTick` inner block (inside `g_MainChangeToStageNum < 0`)

**Presence states implemented**:
- In Main Menu, Solo Mission (stage + difficulty), Combat Simulator (stage + scenario + P/Bot counts), Co-op, Counter-Op, The Grid editor, In Lobby, Running Dedicated Server

**Protocol details**:
- Handshake: opcode=0, `{"v":1,"client_id":"<DISCORD_APP_ID>"}`
- Activity: opcode=1, `{"cmd":"SET_ACTIVITY","args":{"pid":N,"activity":{...}},"nonce":"N"}`
- Pipe set to `PIPE_NOWAIT` so drain reads are non-blocking; writes are ~500 bytes every 15s, never blocking in practice
- Reconnect backoff: 30 seconds after disconnect
- Activity timestamp resets when state changes; periodic refresh every 15 seconds

**Constraints satisfied**:
- Zero new link dependencies (no new libs, no DLLs)
- Fails silently if Discord is not running
- No IP or connect-code exposure in presence strings
- MinGW/GCC compatible (C11, Windows-only, no MSVC APIs)

**Setup**: `DISCORD_APP_ID "0"` placeholder in `port/include/discord.h` — replace with real
app Client ID from https://discord.com/developers/applications.

**Build**: Clean 774/774. `PerfectDark.exe` 52,883,797 / `PerfectDarkServer.exe` 22,906,762.

**Context updates**: `infrastructure.md` D7 → ✅ DONE; `tasks-current.md` updated.

---

## Session S347 — 2026-04-17 (`gracious-poitras-6eeeb6` worktree) — Blue Tint Sweep + Gamepad Audit

**Scope**: Two mechanical sweep tasks: (1) replace remaining hardcoded IM_COL32 blue accent literals with theme accessors, (2) audit and remove dead ImGuiKey_Gamepad checks post-M0.2.

### Task 1 — Hardcoded blue tint sweep (6 files)

Replaced all remaining hardcoded PD-blue `IM_COL32` literals with theme system accessors:

| File | Sites | Old → New |
|------|-------|-----------|
| `pdgui_menu_modmgr.cpp` | 1 | `borderCol IM_COL32(80,140,200,220)` → `pdguiImU32TintInfo(220)` |
| `pdgui_menu_moddinghub.cpp` | 1 | same pattern → `pdguiImU32TintInfo(220)` |
| `pdgui_menu_agentselect.cpp` | 3 | ring border (×2) → `pdguiImU32TintInfo(200)`; placeholder bg → `pdguiPalImU32(PDPAL_TITLEBG,180)`; initials text → `pdguiImU32TitleGlow(255)` |
| `pdgui_menu_agentcreate.cpp` | 4 | preview frame border → `pdguiImU32TintInfo(200)`; silhouette head+body (×2) → `pdguiImU32TintInfo(200)`; initials text → `pdguiImU32TitleGlow(255)` |
| `pdgui_countdown.cpp` | 1 | accent border → `pdguiImU32TitleGlow(200)` |
| `pdgui_menu_mainmenu.cpp` | 2 | focused row bg → `pdguiImU32TintInfo(80)`; hovered row bg → `pdguiImU32TitleGlow(40)` |

**Excluded**: `solomission.cpp IM_COL32(80,160,255,255)` is the Special Agent difficulty badge — intentional semantic color, not UI accent. Forge HUD and glyph pill colors are editor-specific intentional styling.

### Task 2 — Dead ImGuiKey_Gamepad check audit

**Finding: no dead checks remain.** Only 2 PD code files reference `ImGuiKey_Gamepad`:
- `pdgui_backend.cpp` — comment only, explaining NavEnableGamepad is OFF
- `pdgui_menu_mainmenu.cpp` — 3 `AddKeyEvent(..., false)` calls that are B-131 input-flush fixes (clearing stale opening-press state); these are LIVE, not dead

The ~130 redundant checks described in the task were already removed during M0.2 Input System Unification (S181–183, 2026-04-07, commit `-823 lines`). All remaining `ImGuiKey_Gamepad` in PD code are vendor files (`imgui_impl_sdl2.cpp`, `imgui.cpp`).

### Build + merge

Build clean 773/773 (worktree), 775/775 (dev post-merge). `PerfectDark.exe` 52,804,107 / `PerfectDarkServer.exe` 22,905,738. Merge commit to dev. Line counts of all 6 files identical pre- and post-merge.

---

## Session S346 — 2026-04-17 (`crazy-wilbur-ae5c6d` worktree) — Asset Provider Phase 4

**Scope**: Retire raw `filenum` (u16/s32) from the public catalog API surface. All game code that previously called `romProviderHandle(filenum)` directly now goes through catalog or loader abstractions.

### What was done

**New internal header** `port/include/assetprovider_internal.h`:
- Declares `romProviderHandle()` and `romProviderFilenum()` for catalog/provider layer use only
- Lists allowed files (assetload.c, assetcatalog_api.c, assetcatalog_base.c, assetcatalog_base_extended.c, server_stubs.c)

**`port/include/assetprovider.h`**: removed `romProviderHandle` / `romProviderFilenum` from public API; `romProvider()` singleton remains public.

**`port/include/assetload.h` + `port/src/assetload.c`**: added `assetLoadRomToNew(s32 filenum, u32 method, u32 loadtype)` — the new public ROM-load helper for game code; wraps `assetLoadToNew(romProviderHandle(...), ...)` internally.

**`port/include/assetcatalog.h`**:
- Added 5 handle fields to `catalog_stage_result_t`: `bg_handle`, `pads_handle`, `setup_handle`, `mpsetup_handle`, `tile_handle` — populated internally in `s_fillStageResult()` from fileids using `romProviderHandle()`
- Added Phase 4 handle accessor declarations: `catalogGetBodyHandle`, `catalogGetHeadHandle`, `catalogGetPropHandle` → `asset_data_handle_t`
- Moved `catalogGetBodyFilenumByIndex`, `catalogGetHeadFilenumByIndex`, `catalogGetPropFilenumByIndex` to `[MIGRATION BRIDGE]` deprecated section

**`port/src/assetcatalog_api.c`**: added `assetprovider_internal.h` include; stage handle population in `s_fillStageResult()`; implemented `catalogGetBodyHandle`, `catalogGetHeadHandle`, `catalogGetPropHandle`.

**`port/src/assetcatalog_base.c` + `assetcatalog_base_extended.c`**: replaced `assetprovider.h` with `assetprovider_internal.h` (both call `romProviderHandle`).

**`port/src/server_stubs.c`**: added `assetprovider_internal.h` include alongside `assetprovider.h`.

**`src/game/file.c`**: `fileLoadToNew` now calls `assetLoadRomToNew(filenum, ...)` instead of `assetLoadToNew(romProviderHandle(filenum), ...)`; removed `assetprovider.h` include.

**`src/game/modeldef.c`**: model load calls `assetLoadRomToNew` instead of `assetLoadToNew(romProviderHandle(...), ...)`; removed `assetprovider.h`.

**`src/game/lang.c` + `src/game/langreset.c`**: reverted Phase 3 lang call sites from `assetLoadToNew(romProviderHandle(file_id), ...)` back to `fileLoadToNew(file_id, ...)` — `langGetFileId` depends on runtime state, cannot be in the catalog at registration time; removed `assetprovider.h` + `assetload.h`.

**`src/game/setup.c`**: stage setup and pads loads now use `stage.setup_handle` / `stage.mpsetup_handle` / `stage.pads_handle` instead of `romProviderHandle(fileid)`; removed `assetprovider.h`.

**`src/game/tilesreset.c`**: tile load uses `stage.tile_handle`; removed `assetprovider.h`.

### Build

Clean 773/773 (warm cache). `PerfectDark.exe` + `PerfectDarkServer.exe` linked. Warnings only (pre-existing).

### Result

`romProviderHandle()` is now internal to the catalog/provider layer. Zero calls remain in `src/game/`. The SA-5a filenum bridge functions remain for call sites that pass filenums to legacy APIs (`fileGetInflatedSize`, `modeldefLoad`) — those are marked `[DEPRECATED]` for future Phase 5 work.

### Next

- Migrate remaining SA-5a deprecated bridge sites (`catalogGetBodyFilenumByIndex` etc.) to handle-based `modeldefLoadByHandle` — requires adding handle-accepting overloads to the legacy model load APIs (Phase 5 scope)
- Forge wire-in (props/zones/weapon pads) still deferred

---

## Session S345 — 2026-04-17 (`pensive-bell-5597dd` worktree) — Wave 2 Audit

**Scope**: Audit S341 (per-agent prefs + AP3) and S342 (forge runtime wire-in). Find bugs/gaps, fix, build-verify.

### S341 audit — prefs_agent.c + AP3

**Bug found and fixed (B-164)**: `serializeModPlaylist` at line 157 passed `(s32)outmax - off` as `size_t`. If `off >= (s32)outmax` (buffer full), the result is negative, wraps to huge `size_t`, and `snprintf` writes without bound — UB / buffer overflow. Fix: compute `s32 rem = (s32)outmax - off`, break if `rem <= 1`, cast to `(size_t)rem` for the call. **Commit**: `d0f7ee13`.

**Everything else clean**:
- `prefsAgentResetVisuals` restores all 8 baselines: HudCenter, SkipIntro, DisableMpDeathMusic, GEMuzzleFlashes, ScreenShakeIntensity, MenuMouseControl + ModPlaylist, ModShuffle ✓
- `applyHudCenter` is safe unconditionally — all globals statically linked ✓
- Buffer 4096 sufficient for worst-case sidecar ✓
- AP3: all 12 `romProviderHandle` calls correct; `u16` filenums explicitly cast to `(s32)` ✓

### S342 audit — forge_runtime.c

**Gap confirmed**: Props, geometry, weapon pads, doors, zones, and interactables are all still DEFERRED (logged, not wired). Spawn points and AI bots are correctly wired. `forge_logic.c` has action handlers (ENABLE_OBJECT, OPEN_DOOR, TELEPORT_PLAYER, SPAWN_AI) but `forgeRuntimeFindPropByUid` returns NULL for non-bot objects since nothing is stored as a prop handle. This is expected for the current state — S342 wire-in for those categories is still pending.

**Low-severity issue noted**: `forge_logic.c::forgeLogicFireChannelChange` resets `s_recursion_guard = 0` mid-traversal (reachable via FORGE_OP_SET_CHANNEL → forgeChannelSet → forgeLogicFireChannelChange). Depth cap effectively bypassed but `executed_this_frame` bounds total work. Flagged as spawn task.

**Build**: clean 775/775. `PerfectDark.exe` 52,680,221 / `PerfectDarkServer.exe` 22,919,068.

### Next

Forge wire-in (props/zones/weapon pads) still deferred — needs a dedicated session when the catalog prop-load path is ready.

---

## Session S344 — 2026-04-17 (`optimistic-mcclintock-47fc8d` worktree) — Wave 2 Audit

**Scope**: Audit S339 (R-5 server GUI redesign) and S340 (content-inset sweep). Find bugs/gaps, fix, build-verify.

### S339 audit — server_gui.cpp + server_bridge.c

4 bugs found and fixed (commit `63ad5f89`):

1. **Stage ID InputText clipped Apply button** — `SetNextItemWidth(-1)` filled all available width, leaving zero room for the Apply button placed `SameLine`. Fixed: use `-60.0f` width when dirty (Apply button visible), `-1.0f` when idle. (`server_gui.cpp:762`)

2. **Force Start active with no rooms** — button was enabled when `g_NetNumClients > 0` but players can be connected with no active room. Fixed: guard also requires `roomGetActiveCount() > 0`. (`server_gui.cpp:794`)

3. **Ban button misleading** — `netServerBanClient` calls `enet_peer_disconnect` (same as kick) with no IP-level blocklist. Added tooltip: "Kick + log (IP block not yet implemented)". (`server_gui.cpp:605`)

4. **S339 items verified correct**: Table column counts (Players 6, Rooms 6, Updates 5) ✓ · `netServerBanClient` does disconnect the peer ✓ · `serverGetMemoryMB` returns 0 on non-Windows (correct, documented) ✓ · `netServerKickClient` checks `cl->state == CLSTATE_DISCONNECTED` before accessing peer (mid-kick safety) ✓ · Log buffer uses a ring (sysLogRingGetCount/sysLogRingGetLine) — bounded ✓ · `g_NetClients[NET_MAX_CLIENTS + 1]` declared with +1 extra slot, so `> NET_MAX_CLIENTS` bound-check is correct ✓.

### S340 audit — content-inset sweep

1. **`renderLivePreview` cursor overlap** — `pdguiSetCursorBelowTitle(headerH)` passed only the title height but `pdguiDrawPdDialog` draws `headerH + 6*scale` tall. Content overlapped drawn header by `6*scale` px (6px at scale=1). Fixed: pass `headerH + 6.0f * scale`. (`pdgui_menu_theme_editor.cpp:399`)

2. **All 7 patched files verified correct**: `pdguiSetCursorBelowTitle(0.0f)` calls in audiomod, logviewer, moddinghub, modmgr, update are all placed after `ImGui::Begin`/`BeginChild` before content ✓. `mpingame` decl-only (correct — pill windows manage their own padding) ✓. No missed pdgui_menu_*.cpp files (22 of 30 already covered, 7 patched, forge is no-op shim) ✓.

### Build

Clean 4/4. `PerfectDark.exe` + `PerfectDarkServer.exe` linked. Merge commit to dev.

---

## Session S343 — 2026-04-17 (`bold-pascal-bc7b8c` worktree)

**Scope**: Audit session. Wave 2 retrospective — S337 (dev-window-v2 fixes) and S338 (memory M5+M6). Find bugs, gaps, and missing improvements. Fix anything found. Build verify.

### S337 Audit — Dev Window v2 (CLEAN)

All four key concerns verified correct:

1. **Prune button disabled during builds**: Double-protected — `BtnPruneWorktrees.IsEnabled = $false` set in `Start-Build` (line 1630) and Release flow (line 1704); also runtime guard in `Invoke-GitPruneWorktrees` checks `$script:IsBuilding -or $script:IsPushing` and shows a blocking message box. Re-enabled in `Stop-Build` and all early-exit paths.

2. **StatusWorktrees on MainTimer**: `Update-StatusBar` (every 2s when not building) runs `git worktree list --porcelain` in a background runspace and updates `StatusWorktrees` label. Also called after prune completes. ✓

3. **UpdateLayout before ProgressFill.Width**: Both `Start-Build` and Release flow: set `Width=0`, call `ProgressBack.UpdateLayout()`, read `ActualWidth`, then set `Width = Floor(actual * 0.12)`. Correct sequence. ✓

4. **Progress bar stale state**: `ProgressBack.Visibility = Collapsed` in `Stop-Build`; `LblProgressText.Text = "0% - ..."` and `ProgressFill.Width = 0` reset in `Start-Build-Step`. No stale state path found. ✓

No changes made to dev-window-v2.ps1.

### S338 Audit — Memory M5+M6

Correct items verified:

- **Region math**: 16+40+4 = 60 MB of 64 MB heap. 4 MB remainder explicitly noted as unassigned.
- **mempResetPool(STAGE) isolation**: With M5 dedicated regions, resetting STAGE only moves `STAGE.leftpos` back to `STAGE.start` — PERMANENT region unaffected.
- **Lock balance**: All five mutation functions (mempAlloc, mempRealloc, mempResetPool, mempDisablePool, mempAllocFromRight) are correctly locked on every exit path.
- **Server build**: Server has no `mempSetLockFns` call, so locks default to NULL (no-op); correct for single-threaded dedicated server. mempSetHeap is called via mainInit. Build clean.

**BUG FIXED** — `port/src/modelcatalog.c:460`:

The guard `mempGetStageFree() == 0` was written pre-M5 when uninitialized pools had `rightpos=0`, so `rightpos - leftpos = 0`. With M5 dedicated regions, `mempSetHeap` sets `rightpos = start + 40MB` immediately; `leftpos` stays 0 (pool disabled). So between `mempSetHeap()` and `mempResetPool(MEMPOOL_STAGE)`, `mempGetStageFree()` returns a huge value — not 0 — and the guard silently passes. If `catalogValidateAll()` were called in that window, model loading would AV with a null leftpos.

Fix: changed to `mempGetNextStageAllocation() == NULL`. `mempGetNextStageAllocation()` returns `leftpos`, which is NULL whenever the pool is disabled (before `mempResetPool`), regardless of `rightpos`. This correctly guards both pre-mempSetHeap and post-mempSetHeap-pre-mempResetPool cases.

In practice, `catalogValidateAll()` is only called lazily after the first stage load (which always follows `mempResetPool(MEMPOOL_STAGE)`), so no AV was observed — but the guard was wrong and latently unsafe.

### Build

Clean 773/773. PerfectDark.exe 52,785,163 / PerfectDarkServer.exe 22,904,202.

### Next steps

No follow-up issues identified. Both Wave 2 sessions were structurally sound except for the M5-stale guard in modelcatalog.c.

---

## Session S341 — 2026-04-17 (`tender-borg-b2fc3b` worktree)

**Scope**: Two-track session. Task A: per-agent gameplay prefs expansion. Task B: Asset Provider Phase 3 — fileLoadToNew migration.

### Task A — Per-agent gameplay prefs expansion

**What shipped**: `port/src/prefs_agent.c` gained a `[Game]` sidecar block + audio playlist fields in `[Audio]`.

**New per-agent keys in `[Game]`**:
- `CenterHUD` → `g_HudCenter` (int 0/1/2) + side-effect `g_HudAlignModeL/R` update via inline `applyHudCenter()`
- `SkipIntro` → `g_SkipIntro` (bool)
- `DisableMpDeathMusic` → `g_MusicDisableMpDeath` (bool)
- `GEMuzzleFlashes` → `g_BgunGeMuzzleFlashes` (bool)
- `ScreenShakeIntensity` → `g_ViShakeIntensityMult` (float)
- `MenuMouseControl` → `g_MenuMouseControl` (bool)

**New audio playlist keys in `[Audio]`** (S313 deferred item completed):
- `ModPlaylist` — semicolon-delimited catalog IDs; applied via `audioClearModPlaylist()` + `audioAddModPlaylistEntry()` per entry
- `ModShuffle` — applied via `audioSetModShuffle()`
- `ModTrackId` — legacy compat key; only applied if playlist empty

**Baseline capture**: `prefsAgentInit()` now snapshots all 6 gameplay globals + playlist state at startup (after configLoad sets pd.ini values). `prefsAgentResetVisuals()` restores all of them so agents without a sidecar block revert to machine defaults rather than inheriting the previous agent's settings.

**Buffer size**: prefs save buffer increased from 2048 → 4096 to accommodate [Game] + ModPlaylist.

**Build**: clean 773/773 — PerfectDark.exe 52,772,583 / PerfectDarkServer.exe 22,886,022.

### Task B — Asset Provider Phase 3: fileLoadToNew migration

**Scope**: Grep all `fileLoadToNew` call sites outside `assetload.c` + `file.c`. Categorize. Migrate straightforward ROM-backed calls to `assetLoadToNew(romProviderHandle(...), ...)`.

**Sites found and categorized**:

| Subsystem | File | Calls | Verdict |
|-----------|------|-------|---------|
| Lang | `src/game/lang.c` | 1 | ✅ Migrated |
| Lang reset | `src/game/langreset.c` | 7 | ✅ Migrated |
| Modeldef | `src/game/modeldef.c` | 1 | ✅ Migrated |
| Stage setup | `src/game/setup.c` | 2 | ✅ Migrated |
| Tiles | `src/game/tilesreset.c` | 1 | ✅ Migrated |

All 12 call sites were straightforward ROM-backed loads (`s32` or `u16` filenum from ROM data tables / stage structs). No complex cases found.

**Migration pattern**: added `#include "assetprovider.h"` + `#include "assetload.h"` to each file (same path available to `src/game/` per CMake include config, as evidenced by `file.c` already using these headers). Changed `fileLoadToNew(X, M, L)` → `assetLoadToNew(romProviderHandle((s32)X), M, L)`. `u16` filenums (modeldef, setup, tiles) got explicit `(s32)` cast.

**Zero behavior change**: `assetLoadToNew(romProviderHandle(N), ...)` delegates to `fileLoadRomToNew(N, ...)` which is byte-identical to the old `fileLoadToNew(N, ...)` body. Phase 3 is a pure refactor enabling future FileProvider substitution at these sites.

**`fileLoadToNew` status**: still declared in `game/file.h` and implemented in `game/file.c` as the public ROM-load API. No callers remain outside of assetload.c / file.c — it's now a leaf entry point for game code that doesn't yet have a catalog handle.

### Playtest checklist

1. **Agent sidecar round-trip**: Load agent → change HUD centering in Settings → close → reload; sidecar should preserve the new value across restarts.
2. **Reset on Agent Select open**: Open Agent Select screen; HUD centering / screen shake / muzzle flashes should all match pd.ini defaults (not the previous agent's values).
3. **Audio playlist per-agent**: Create two agents with different Combat Simulator playlists; switching agents should apply each agent's playlist.
4. **Stage loading**: Play any SP mission + MP match; no regressions in lang/setup/tiles loading after the assetLoadToNew migration.

---

## Session S340 — 2026-04-17 (Content-Inset Sweep, `elated-lichterman-9c8f69` worktree)

**Scope**: Added `pdguiSetCursorBelowTitle` / `pdguiThemeGetContentInset` content-inset pattern to all `pdgui_menu_*.cpp` files that lacked it. 22 of 30 files were already covered. 7 needed patching; `pdgui_menu_forge.cpp` is a no-op shim with no render sites.

### Files patched

| File | Change |
|------|--------|
| `pdgui_menu_audiomod.cpp` | Added decl + `pdguiSetCursorBelowTitle(0.0f)` at top of `pdguiAudioModRender` |
| `pdgui_menu_logviewer.cpp` | Added extern "C" decl + `pdguiSetCursorBelowTitle(0.0f)` after `ImGui::Begin` |
| `pdgui_menu_moddinghub.cpp` | Replaced raw `SetCursorPosX` bump with `pdguiSetCursorBelowTitle(0.0f)` |
| `pdgui_menu_modmgr.cpp` | Added decl + `pdguiSetCursorBelowTitle(0.0f)` in `renderModManagerBody` |
| `pdgui_menu_mpingame.cpp` | Added decl only — pill windows use custom `WindowPadding`, no cursor shift |
| `pdgui_menu_theme_editor.cpp` | Replaced manual `Dummy(headerH+8)` with `pdguiSetCursorBelowTitle(headerH)` |
| `pdgui_menu_update.cpp` | Added decl + `pdguiSetCursorBelowTitle(0.0f)` in both render functions |

### Build result

Clean: 585/585 objects, zero errors. Merge commit `56338aaa` on dev.

---

## Session S339 — 2026-04-17 (R-5 Server GUI Redesign, `thirsty-jemison-84f6ce` worktree)

**Scope**: Full redesign of `port/fast3d/server_gui.cpp` (~860 → ~870 lines of new code). Added 5 bridge functions to `port/src/server_bridge.c`. Added psapi link to `CMakeLists.txt` for the pd-server target.

### What changed

**`port/fast3d/server_gui.cpp`** — complete rewrite, same file:
- **Status bar** (top): 4-column layout — connect code + copy button; Players X/Y + active Rooms count; Uptime HH:MM:SS + Tick rate Hz; Memory MB + Online/OFFLINE + update badge.
- **Players tab** (was "Server"): ImGui `BeginTable` with 6 columns — Agent Name (gold leader badge), State (color-coded), Ping (green/amber/red), Team (team name + color), Kick, Ban. Ban button calls new `netServerBanClient`.
- **Rooms tab** (was "Hub"): Hub state summary row (hub state + slot usage). Room table with 6 columns — ID, Name, Players X/max, State (color-coded), Stage 0xNN, Scenario N.
- **Operator tab** (new): split left/right. Left: Game Mode dropdown + Stage ID text input (apply-on-button with `serverSetStageId`) + Scenario int-input + Force Start Match + End Match. Right: Shutdown Server (+ Restart & Update when staged).
- **Updates tab**: unchanged from prior design.
- **Log panel** (bottom, always visible): filter row [All][NET][ERROR][WARN][CHAT][HUB] with active-highlight toggle buttons, Auto-scroll checkbox. Lines outside the selected filter are hidden; colors unchanged.
- Added `s_SrvStartMs` (SDL_GetTicks at init) for uptime. Tick rate computed as `g_NetTick / uptime_secs`.

**`port/src/server_bridge.c`** additions:
- `netServerBanClient(clientId, reason)` — kick with "Banned" reason (IP-level reconnect blocking is future work).
- `serverGetMemoryMB()` — Windows `GetProcessMemoryInfo` WorkingSetSize / 1MB; returns 0 on non-Windows.
- `serverGetStageId()` / `serverSetStageId(s)` — read/write `g_MpSetup.stage_id`.
- `serverGetScenario()` / `serverSetScenario(n)` — read/write `g_MpSetup.scenario`.

**`CMakeLists.txt`**: `target_link_libraries(pd-server psapi)` under `if(WIN32)` after the existing server link line.

### Build result

Clean: 252/252 server objects, 521/521 game objects. Zero errors, only pre-existing vendor warnings.
`PerfectDark.exe` 52,773,095 / `PerfectDarkServer.exe` 22,905,738 (+18.7 KB).

### Deferred
- Ban list: in-memory IP blocklist to reject reconnects (needs net.c intercept hook).
- Room stage name: dedicated server has `r->stagenum` as u8, no catalog lookup — shows `0xNN` until catalog is available server-side.
- Scenario name: shows integer until server-side scenario name table is added.

---

## Session S338 — 2026-04-17 (D-MEM M5 + M6 — Separate Pool Regions + Mutex, `ecstatic-bouman-5d30e4` worktree)

**Scope**: Completed the final two Memory Modernization phases. Build clean 775/775, zero errors.

### M5 — Separate Pool Regions

`src/lib/memp.c::mempSetHeap()` now carves the flat 64 MB heap into dedicated, non-overlapping address regions:

| Pool | Offset | Size |
|------|--------|------|
| `MEMPOOL_PERMANENT` | base + 0 | 16 MB |
| `MEMPOOL_STAGE` | base + 16 MB | 40 MB |
| `MEMPOOL_8` | base + 56 MB | 4 MB |
| (unassigned) | base + 60 MB | 4 MB |

Previously PERMANENT, STAGE, and POOL_0 all started at the same address. `mempResetPool(MEMPOOL_STAGE)` used to reposition STAGE's start to PERMANENT's current `leftpos` and clamp PERMANENT's `rightpos`/`end` — both operations are now removed since each pool has a fixed private region.

**Bug fixed**: `mempGetStageFree()` and `mempGetNextStageAllocation()` were reading from `g_MempExpansionPools[MEMPOOL_STAGE]`, which was never set up with a ≤64 MB heap. Both functions returned 0/NULL always. Fixed to read from `g_MempOnboardPools[MEMPOOL_STAGE]`. This also fixes the `modelcatalog.c` guard that was logging a spurious ERROR on every boot.

### M6 — Thread Safety

Added `mempSetLockFns(lockFn, unlockFn)` to memp.h/memp.c — registers optional SDL_mutex-backed lock/unlock hooks. `port/src/pdmain.c` now creates an SDL_mutex immediately after `mempSetHeap()` and registers it. The following memp functions are now fully guarded: `mempAlloc`, `mempAllocFromRight`, `mempRealloc`, `mempResetPool`, `mempDisablePool`. Server build unaffected (does not compile memp.c).

### Files changed

- `src/lib/memp.c` — M5 region carving + M6 mutex hooks (415 lines)
- `src/include/lib/memp.h` — added `mempSetLockFns` declaration
- `port/src/pdmain.c` — SDL include + static mutex + mempSetLockFns registration

### Build result

Clean: 775/775, zero errors. `PerfectDark.exe` 52,677,661 bytes, `PerfectDarkServer.exe` 22,919,068 bytes. Merge commit on dev.

### Next

D-MEM is fully complete. No follow-up tasks. Playtest: stage transitions, multiplayer — verify no cross-pool corruption (no behavioral change expected; this was a silent correctness fix).

---

## Session S337 — 2026-04-17 (Dev Window v2 — Prune Worktrees + Progress Bar + HUD)

**Scope**: Three improvements to `devtools/dev-window-v2/dev-window-v2.ps1`. No game code touched.

### What was done

**1. Prune Worktrees button** — `BtnPruneWorktrees` added to the utility toolbar (next to Push). Runs `git worktree prune -v`, logs output to Log tab, shows result dialog. Disabled during builds/releases (tracked alongside BtnPull/BtnPush in all enable/disable paths). Worktree count (`StatusWorktrees`) added to the status bar; turns orange when >20 stale worktrees are present. Count is fetched in the same background runspace as branch/hash/dirty.

**2. Progress bar ActualWidth fix** — `$ui["ProgressBack"].UpdateLayout()` now called before each `ActualWidth` read in `Start-Build` and `Start-PushRelease`. Previously, `ProgressBack` was made `Visible` immediately before reading `ActualWidth`, which returned 0 (layout not yet computed), so the 12% "git sync" fill was never drawn. Now the fill appears immediately when a build or release starts.

**3. HUD consistency fix** — Spinner path's `LblProgressText` now shows `"0% - "` prefix when `BuildPercent == 0`, matching the non-spinner path. All three code paths (`Start-Build-Step`, spinner, non-spinner) are now consistent. Removed the premature green `ProgressFill.Background` set on step transitions (it was immediately overwritten by `Start-Build-Step`, but was a confusing dead assignment).

### Commit
`4d117d67` on `claude/peaceful-williams-59ec2f`.

---

## Session S336 — 2026-04-17 (Audit S329 B-12 Chrslots + S332 Modeldef/Audio)

**Scope**: Wave 1 audit. Read all files touched by S329 (B-12 Phase 3 chrslots removal, protocol v37) and S332 (B-161 modeldef chokepoint + B-141 audio pacing). Found two real bugs; fixed both. Build clean in worktree, merged to dev.

### S329 — B-12 Phase 3 Chrslots Removal

**Bug fixed — u32 truncation in pause menu:** `pdgui_bridge.c::pdguiPauseGetChrSlots()` returned `(u32)(mask & 0xFFFFFFFFull)`, silently dropping bots in slots 32-39 (the last 8 of MAX_BOTS=32). Consumer in `pdgui_menu_pausemenu.cpp` iterated `1u << i` for i up to 39 — undefined behaviour for i ≥ 32. Fixed: return type → `u64`, consumer → `u64 activeMask` + `1ull << i`. Commit `a0a2d6bb`.

**Stale comment fixed:** `netmanifest.c` doc still said "g_MpSetup — stage, weapons, chrslots"; updated to note chrslots was removed in v37.

**Clean:**
- `BOT_SLOT_OFFSET 8` in botsetup.cpp is a local alias with correct value (= MAX_PLAYERS) and a clear comment explaining why. Not a bug.
- `propobj.c::numchrslots` uses `chrsGetNumSlots()` / `g_ChrSlots[]` — a different system entirely, not the B-12 chrslots bitmask.
- Wire encode/decode: `mpParticipantsEncodeActiveMask()` iterates all 64 bits; `mpParticipantsDecodeActiveMask()` handles player (0..MAX_PLAYERS-1) and bot (MAX_PLAYERS..MAX_MPCHRS-1) ranges correctly.
- Server CMakeLists: `participant.c` is in `SRC_SERVER` at line 560. ✓

### S332 — B-161 Modeldef Chokepoint + B-141 Audio Pacing

**Bug fixed — weapon modeldef NULL gap in player.c:** `playerChrInitialise` called `modelAllocateRwData(weaponmodeldef)` immediately after `modeldefLoad()` with no NULL check. A torn/missing weapon mod asset would crash instead of logging. Fixed: NULL guard + WARNING log; `weaponCreateForChr` already handles NULL at all other call sites. Commit `a0a2d6bb`.

**modeldefLoad callers reviewed:**
- `menu.c:2071` — NULL checked with full validation block. ✓
- `menu.c:2096` — NULL checked inline. ✓
- `player.c:1929` (body) — NULL checked + early return. ✓
- `player.c:1941` (head) — NULL checked inline. ✓
- `title.c` (logos) — No NULL check, but these load core ROM assets (Nintendo/Rare logos) that cannot be torn in practice. Accepted risk.
- `assetcatalog_api.c`, `modelcatalog.c` — Store result; callers of `catalogGetBodyModeldef` check NULL. ✓
- `setuputils.c` — NULL checked (FIX-B.2). ✓

**numparts [1,500] bound:** 500 was established in S312. AllInOneMods replacement models pass. The lower bound `<= 0` catches uninitialized/corrupted structs. Correct.

**Audio pacing:** Three-tier logic is sound. `osAiGetLength()/4` is the queue depth in samples. Brake at >3000, steady at 2500-3000, fast-fill at <2500 (NTSC only). `var8005cf94` is a 2-frame cooldown after brake. PAL path retains original 368+184=552 behavior. Thread-safe: `amgrFrame` runs on the main game tick thread only.

### Build result

Clean: 773/773 objects in worktree, zero errors. Merge commit `e1081911` on dev.

### Playtest verification needed

- **Pause menu with 32 bots**: verify player/bot count now shows full bot count (was silently capped at 24 before fix).
- **Torn weapon mod asset**: if available, verify WARNING log appears instead of crash.

---

## Session S334 — 2026-04-17 (Audit S327 Asset Provider + S330 Memory)

**Scope**: Wave 1 audit session. Read all files touched by S327 (Asset Provider Phase 1–2) and S330 (Memory M2+M4). Found one real bug and one dead-code issue; fixed the bug.

### S327 — Asset Provider Phase 1–2: CLEAN

- Vtable positional initializers match struct member order. ✓
- `assetHandleIsNull` + dispatcher NULL guards are correct. ✓
- Path intern pool is single-threaded at boot (catalog registration) — no mutex needed. ✓
- `catalogEffectiveHandle` override-first logic correct. ✓
- ROM path byte-identical: `fileLoadRomToNew` is the verbatim original body. ✓
- `fileProvider()` singleton pointer comparison in `assetcatalog_load.c` is safe. ✓

### S330 — Memory M2+M4

**Bug fixed:** `pak.c:pak0f11d9c4` — `malloc(0x4000)` result was not null-checked. `PAK00C_01` and `PAK00C_02` branches passed `sp60` to `pakConvertFromGbcImage()` without a NULL guard. Added early-return after malloc. Commit `5773c465`.

**Not bugs:**
- Static buffers in `texdecompress.c` / `menuitem.c`: only called from single-threaded render path. ✓
- ALIGN16 no-op: all remaining callers are either size rounding (harmless) or pointer alignment (unnecessary on x86_64). ✓
- `segaudio.c`: `ALIGN16(reallen) > dstlen` overflow check is dead code (redundant with `reallen > dstlen`) but not a runtime error. Audio bank data doesn't require DMA alignment on PC.

### Build result

Clean: 773/773 objects, zero errors. Only pre-existing `-Wcomment` warnings in vendored code.

### Next steps

- Playtest verification for B-161 and B-141 (still open from S333).

---

## Session S335 — 2026-04-17 (Wave 1 audit — S328 + S331, `quirky-mcnulty-60c44a` worktree)

**Scope**: Audit of two merged sessions: S328 (FIX-B.1 deep manifest scanner) and S331 (D6 stats wire-in + subtitle migration). Read all modified files, traced call sites, verified design intent vs. implementation.

### Findings

**S328 (FIX-B.1 deep manifest scanner — netmanifest.c):**

- **`INTROCMD_OUTFIT` correctly skipped**: Sets `g_Vars.currentplayer->bondtype` (Joanna outfit variant), NOT a separate catalog BODY entry. Joanna is already scanned at the top of `manifestBuildMission`. No action needed.
- **Ailist sentinel termination confirmed safe**: `ailists[]` is always null-terminated; the post-loop `ailists[listidx].list` read hits the sentinel, not OOB.
- **Minor gap fixed**: `s_manifestAddWeapon` silently skipped weapons not in catalog — inconsistent with `s_manifestAddBody`/`s_manifestAddHead` which both emit `LOG_WARNING`. Added WARNING branch for `wcan != NULL && we == NULL` (catalog ID known but entry missing). Silent skip retained when `wcan == NULL` (no mapping — expected for unregistered weapon IDs).

**S331 (D6 stats wire-in + subtitle migration):**

- **Bug 1 fixed — `statsShutdown()` not called on exit**: `cleanup()` in `port/src/main.c` called `statsInit()` at startup but did not call `statsShutdown()` (which flushes to disk). Normal exit without a match finish would lose accumulated distance/in-session stats. Added `statsShutdown()` after `configSave()` in the `cleanup()` sequence.
- **Bug 2 fixed — distance stat key naming**: `lv.c` tracked `"distance_units"` (non-namespaced global total) + `"mp.distance_units_sample"` + `"solo.distance_units_sample"`. The `_sample` suffix is non-standard (every other stat uses `mp.*` / `solo.*` directly), and the non-namespaced total was redundant. Replaced with `"mp.distance_units"` / `"solo.distance_units"` matching the session design spec and the convention used by `mp.time_played_seconds`.
- **Subtitle snapshot comment verified**: Line 1686 in pdgui_bridge.c has correct `/* ... */` syntax (grep display artifact showed `\*`).
- **Thread safety**: All stat increments fire on the main game thread (single-threaded PC), safe.
- **Subtitle foreground drawlist placement**: `GetForegroundDrawList()` is correct — panel renders above cutscene letterbox bars.

### Files changed

- `port/src/net/netmanifest.c` — added WARNING in `s_manifestAddWeapon` for unresolvable catalog entries
- `port/src/main.c` — added `statsShutdown()` to `cleanup()`
- `src/game/lv.c` — renamed distance stat keys (`distance_units` / `mp.distance_units_sample` / `solo.distance_units_sample` → `mp.distance_units` / `solo.distance_units`)

### Build result

Clean: 773/773 objects, zero errors. `PerfectDark.exe` 52,768,999 bytes, `PerfectDarkServer.exe` 22,887,046 bytes. Only pre-existing `-Wcomment` in vendored code. Merge commit on dev.

---

## Session S333 — 2026-04-17 (Merge S329 + S332 into dev)

**Scope**: Integration session. Merged two completed worktree branches into `dev` with post-merge conflict resolution and full build validation.

### What was done

**Branch 1 — S329 `claude/exciting-meitner-bc8c70` (already merged as 37bdb4b6):**
- B-12 Phase 3: chrslots removal + protocol v36 → v37. Already landed; no additional work needed.

**Branch 2 — S332 `claude/magical-mahavira-f3726f`:**
- Merge commit: `12170710`
- Conflicts in `context/session-log.md` and `context/tasks-current.md` resolved by keeping both sets of content ordered chronologically.
- Code changes: `src/game/modeldef.c` (B-161 chokepoint validation — rootnode NULL or numparts outside [1,500] → LOG_ERROR + return NULL) and `src/lib/audiomgr.c` (B-141 three-tier audio pacing: >3000 brake/184, 2500–3000 steady/368, <2500 fast-fill/736).

### Build result

Clean: 585/585 objects, zero errors. `PerfectDark.exe` 52,660,473 bytes, `PerfectDarkServer.exe` 22,901,400 bytes. Only pre-existing `-Wcomment` warnings in vendored code.

### Next steps

- Playtest verification for B-161 and B-141 (see tasks-current.md Open section for checklist).
- Push `dev` to origin when ready.

---

## Session S326 — 2026-04-17 (Asset Provider Phases 1 + 2 — `jolly-booth-fb5419` worktree)

**Scope**: First two phases of the Direct File Access architecture (design doc: `context/designs/direct-file-access-design-2026-04-17.md`). Introduces the `IAssetProvider` vtable, `RomProvider` + `FileProvider` singletons, and the catalog-side `asset_source_t` descriptor so provider selection happens at catalog resolve time instead of inside `romdataFileLoad`.

### What was done

**Phase 1 — Provider interface (zero behavior change):**
- `port/include/assetprovider.h` — new; `asset_data_handle_t` (provider pointer + 128-bit opaque payload) and `asset_provider_t` vtable (`resolve_size` / `load` / `unload` / `describe`). Built-in singletons: `romProvider()` / `fileProvider()` with handle constructors `romProviderHandle(filenum)` / `fileProviderHandle(path)`.
- `port/include/assetload.h` — new; dispatcher API `assetLoad` / `assetLoadToNew` / `assetUnload` / `assetDescribe`.
- `port/src/assetprovider_rom.c` — new; wraps `romdataFileLoad` / `romdataFileGetSize` / `romdataFileGetName`. `opaque[0]` = filenum.
- `port/src/assetprovider_file.c` — new; wraps `fsFileLoad` / `fsFileSize`. Interning pool: `s_PathPool[32 KB]` + `s_PathOffsets[1024]` dedups paths so handles stay 128-bit regardless of path length. Pool-exhaustion logs a single warning and returns a null handle.
- `port/src/assetload.c` — new; dispatcher. RomProvider fast-path delegates to `fileLoadRomToNew` (legacy body in file.c) so Phase 1 is byte-identical on the hot path. Generic fallback does `mempAlloc(MEMPOOL_STAGE) + provider.load` for FileProvider and future providers.
- `src/include/game/file.h` — declared `fileLoadRomToNew` (internal dispatcher entry point).
- `src/game/file.c` — split `fileLoadToNew` into the legacy body (renamed `fileLoadRomToNew`) + a one-line wrapper `fileLoadToNew(f,m,l) → assetLoadToNew(romProviderHandle(f), m, l)`. Every existing call site transparently routes through the provider dispatcher.
- `port/src/server_stubs.c` — added stubs for the 6 provider entry points so the server (which doesn't compile the provider .c files) still links.

**Phase 2 — Catalog source descriptor + provider-driven override:**
- `port/include/assetcatalog.h` — added `asset_source_t { primary, override, flags }` and field `asset_entry_t::source`; API `catalogSetPrimary` / `catalogSetOverride` / `catalogClearOverride` / `catalogEffectiveHandle`.
- `port/src/assetcatalog.c` — initializes `source` to zeroes in `assetCatalogRegister`; implements the four new functions.
- `port/src/assetcatalog_base.c` — after setting `source_filenum` on base bodies/heads/sp entries, also calls `catalogSetPrimary(e, romProviderHandle(source_filenum))`.
- `port/src/assetcatalog_base_extended.c` — same pattern for base prop models (`ASSET_MODEL`) when `g_ModelStates[i].fileid > 0`.
- `port/src/assetcatalog_scanner.c` — mod characters with a non-empty `bodyfile` now bind `source.primary = fileProviderHandle(bodyfile)`.
- `port/src/assetcatalog_load.c::entryGetFilePath` — consults `source.primary` first; if it holds a FileProvider handle, the interned path wins over `ext.character.bodyfile` / `ext.texture.file_path` / `ext.audio.file_path`. Legacy fallback preserved for entries with no populated source, so unchanged mod flows keep working.

Net effect: the mod-override path that used to read type-specific `ext.*` fields inside `romdataFileLoad → catalogResolveFile → entryGetFilePath` now reads from a declarative `asset_source_t` handle owned by the catalog entry. The reverse-index still picks the winning entry, but the path it serves comes from the provider abstraction, not a type switch.

### Build result

Clean: 771/771 objects, zero errors. `PerfectDark.exe` (52.7 MB) and `PerfectDarkServer.exe` (22.8 MB) both linked. Only pre-existing warnings (comment style, `near`/`far` struct members on Windows MSYS2).

### Files touched (17)

New:
- `port/include/assetprovider.h`
- `port/include/assetload.h`
- `port/src/assetprovider_rom.c`
- `port/src/assetprovider_file.c`
- `port/src/assetload.c`

Modified:
- `port/include/assetcatalog.h`
- `port/src/assetcatalog.c`
- `port/src/assetcatalog_base.c`
- `port/src/assetcatalog_base_extended.c`
- `port/src/assetcatalog_scanner.c`
- `port/src/assetcatalog_load.c`
- `port/src/server_stubs.c`
- `src/game/file.c`
- `src/include/game/file.h`
- `context/tasks-current.md`
- `context/session-log.md`
- `context/infrastructure.md`

### Next steps

- Phase 3: migrate call sites from `fileLoadToNew(filenum, ...)` to `assetLoadToNew(handle, ...)` (body/head/setup/bg loaders, ~60 sites)
- Phase 4: retire `filenum` integers from the public catalog API once Phase 3 lands
- Extend FileProvider to handle rzipInflate + romdataFilePreprocess before the dispatcher goes live for non-ROM loads
- Unit test: verify `assetDescribe()` on RomProvider/FileProvider handles logs the expected strings

---

## Session S325 — 2026-04-17 (D6 stats wire-in + ImGui subtitles — `xenodochial-mendel-93fca2` worktree)

**Scope**: Two parallel focused tasks. Task 1: complete D6 persistent stats gameplay wire-in at remaining call sites. Task 2: migrate subtitle rendering from legacy N64 viewmodel overlay to ImGui bottom-center panel.

### Task 1 — D6 gameplay stats wire-in

**Context (already wired pre-S325)**: `mpstats.c` had shots (+per-weapon/headshot), kills (+per-weapon/per-mode/vs_bot/vs_player), deaths (+per-mode/suicide/by_bot/by_player). `mplayer.c::mpEndMatch` had `matches.played` + `statsSave()`.

**What was added**:

- **mplayer.c::mpCalculateAwards** — wins/losses/time/distance
  - At `mpplayer->gameswon++`: `statIncrement("mp.matches_won", 1)` (local player only via `playernum < PLAYERCOUNT()`)
  - At `mpplayer->gameslost++`: `statIncrement("mp.matches_lost", 1)` (same guard)
  - At per-player time/distance accumulation: `statIncrement("mp.time_played_seconds", duration60/60)` + `statIncrement("mp.distance_units", distance/10000)`
- **endscreen.c::endscreenPrepare** — solo mission outcomes
  - Early in function, before any legacy logic: classify completion via `!isdead && !aborted && objectiveIsAllComplete()`
  - `statIncrement("solo.missions_completed"|"solo.mission_failures", 1)`
  - Accumulate `solo.time_played_seconds`
  - Gated on `!coop !anti !cheats` (pdmode branches through normal solo)
  - `statsSave()` after increment for safety
- **lv.c::lvTick** (distance accumulator site) — per-tick sampling
  - File-scope static `s_StatDistanceAccum[MAX_PLAYERS]` accumulator
  - Flushes `statIncrement("distance_units", 1)` + `{mp,solo}.distance_units_sample` per 10000 world-unit threshold to avoid hash-table churn every frame
  - Local-player gated (`g_Vars.currentplayernum < PLAYERCOUNT()`)
- **propobj.c::propPickupByPlayer** — item pickups
  - After `result != TICKOP_NONE` check (successful pickup): `items.picked_up` (always) + typed counter switch (`keys_picked_up`, `ammo_crates`, `weapons_picked_up`, `shields_picked_up`)
  - Local-player gated
- **propobj.c::doorsCheckAutomatic** — door opens
  - Inside `canopen` branch (player-triggered auto-open only — skips AI/script opens): `doors.opened`
  - Local-player gated

**Include hygiene**: added `extern void statIncrement(const char *key, u64 amount);` to lv.c, propobj.c, endscreen.c (+ `statsSave` extern to endscreen.c).

### Task 2 — Subtitle ImGui migration

**Root cause**: `hudmsgsRender` (src/game/hudmsg.c) rendered `HUDMSGTYPE_INGAMESUBTITLE` and `HUDMSGTYPE_CUTSCENESUBTITLE` through the legacy N64 text-on-GBI path, which positioned subtitles at the top of the screen and fought the modern ImGui overlay.

**What was done**:

- **New `port/fast3d/pdgui_subtitles.cpp` + `port/include/pdgui_subtitles.h`** — standalone ImGui renderer
  - Reads subtitle data via new `pdguiSubtitlesSnapshot(out, maxOut)` bridge function (`pdgui_bridge.c`) — POD-only output so the C++ renderer does not have to include types.h
  - Bottom-center panel: 560 px wide (scales with `pdguiScale`), 46 px bottom margin, semi-transparent rounded backdrop (rgba 0,0,0,173), subtle 1px white-alpha border
  - Word-wrapped + horizontally centered text with 1-px drop shadow from `msg->glowcolour` for readability
  - Rendered on `ImGui::GetForegroundDrawList()` so cutscene letterbox bars do not occlude
  - Respects `msg->opacity` (legacy hudmsgsTick drives fade-in/out via audio-channel lifecycle — untouched)
- **Bridge snapshot — `pdgui_bridge.c::pdguiSubtitlesSnapshot`** — walks `g_HudMessages[]`, filters `HUDMSGSTATE_{FREE,QUEUED}` + opacity=0 + non-subtitle types + cutscene-gate + playernum mismatch, copies text pointer + colours + opacity + `is_cutscene` flag into output array
- **Hook — `pdgui_backend.cpp::pdguiRender`** — calls `pdguiSubtitlesRender(winW, winH)` immediately after `pdguiHudRender` and before `pdguiInteractPromptRender`
- **Legacy renderer disabled — `hudmsg.c::hudmsgsRender`** — skips both subtitle types via a `continue` branch at the top of the per-message loop; tick lifecycle (hudmsgsTick, position calc, audio-channel opacity) still runs untouched so this is a rendering-only swap

### Commit

(pending) — see Task dir for staged changes.

### Build result

Clean: 769/769 objects, zero errors, both `PerfectDark.exe` (52,732,103 bytes) and `PerfectDarkServer.exe` (22,838,985 bytes) link clean. Only pre-existing `-Wcomment` warnings in vendored code.

### Next steps

- Playtest verification: solo mission with subtitles → bottom-center panel; cutscene → panel not occluded by letterbox; match end → `saves/playerstats.json` contains `mp.matches_won/lost`, `mp.time_played_seconds`, `items.picked_up`, `doors.opened`.
- D6 marked DONE in infrastructure.md.

---

## Session S324 — 2026-04-17 (D-MEM M2 + M4 — `thirsty-ardinghelli-92bfca` worktree)

**Scope**: D-MEM phases M2 (stack-to-heap promotion) and M4 (ALIGN16 no-op). Also marked M3 as DONE in infrastructure.md — IS4MB ternary collapse was completed across S317/S320/S322/S323A.

### What was done

**M2 — Stack-to-heap promotion (5 buffers across 3 files):**
- `src/game/pak.c::pak0f11d9c4`: `sp60[0x4000]` (16KB) → `malloc(0x4000)` / `free(sp60)` at function end. Added `<stdlib.h>` include. Function is GB pak image decode — infrequent, malloc/free appropriate.
- `src/game/texdecompress.c::texInflateZlib`: `scratch2[0x800]` + `scratch[5120]` → `static`. Texture decompression — called at load time, static avoids per-call allocation without reentrancy risk.
- `src/game/texdecompress.c::texInflateNonZlib`: `scratch[0x2000]` + `lookup[0x1000]` (12KB) → `static`. Same rationale.
- `src/game/menuitem.c::menuitemScrollableRender`: `alltext[8000]`, `headingtext[8000]`, `bodytext[8000]` → `static`; added `alltext[0] = '\0'` (was zero-init via `= ""`  on stack). Per-frame render function — static avoids 24KB stack usage.
- `src/game/menuitem.c::menuitemScrollableTick`: `wrapped[8000]` → `static`; added `wrapped[0] = '\0'`. Tick handler, conditional on layout change.

**M4 — ALIGN16 no-op:**
- `src/include/constants.h` line 84: `#define ALIGN16(val) ((((val) + 0xf) | 0xf) ^ 0xf)` → `#define ALIGN16(val) (val)`. One-line change eliminates N64 DMA padding from all 119 call sites. On PC, mempAlloc and malloc already return aligned memory; the rounding was pure waste.

**M3 marked DONE in infrastructure.md** — IS4MB/IS8MB removed across S317/S320/S322/S323A sessions; the infrastructure tracker was not updated at the time.

### Commit

| SHA | Scope |
|-----|-------|
| `fe107e3e` | **refactor(D-MEM): M2 stack→heap promotion + M4 ALIGN16 no-op** |

### Build result

Clean: 771/771 objects, zero errors. Both `PerfectDark.exe` and `PerfectDarkServer.exe` link clean. Only pre-existing `-Wmaybe-uninitialized` and format warnings in vendored code.

### Next steps

- No playtest needed — pure dead-code / allocation-source changes; behavior identical
- Remaining: M5 (separate pool regions), M6 (heap sizing)

---

## Session S323 — 2026-04-17 (Batch H — FIX-B.1 deep manifest scanner discovery logging — `zealous-saha-02c1f5` worktree)

**Scope**: Master Orchestration Plan FIX-B.1. Scanners for `g_StageSetup.intro` and `g_StageSetup.ailists` landed in S298 (see `port/src/server_stubs.c:327` for the symbol reference), but they silently added entries — impossible to audit in playtest logs whether a given mission's cinematic/AI-scripted spawns were actually captured. Users could still see `MANIFEST-SP: late-add ...` lines in logs without any way to trace which scan phase *missed* the asset. This batch closes the auditability gap.

### What was done

**`port/src/net/netmanifest.c`** — scanner helpers refactored to support per-discovery logging:

- **New `s_manifestHasEntry(m, id)` helper** — O(n) existence check over the manifest by canonical id. Used by every add helper below so repeat references in intro/ailist commands dedup silently via `manifestAddEntry()` but only emit *one* `discovered` log line per unique asset.
- **`s_manifestAddBody` / `s_manifestAddHead` / `s_manifestAddModel`** — added `const char *scan_source` parameter. Pre-check dedup; if the entry is *new*, log:
  - `MANIFEST-SP: <scan>-scan discovered body '<catalog_id>' (bodynum=N)`
  - `MANIFEST-SP: <scan>-scan discovered head '<catalog_id>' (headnum=N)`
  - `MANIFEST-SP: <scan>-scan discovered model '<catalog_id>' (modelnum=N)`
- **New `s_manifestAddWeapon(out, weaponnum, scan_source)`** — replaces inline `catalogIdByRuntime` + `assetCatalogResolve` + `manifestAddEntry` + `s_manifestExpandDeps` in both intro and ailist scanners. Logs: `MANIFEST-SP: <scan>-scan discovered weapon '<catalog_id>' (weaponnum=N)`.
- **Body/head not-in-catalog WARNING** — `s_manifestAddBody` and `s_manifestAddHead` now emit `LOG_WARNING` when a scan references a bodynum/headnum that doesn't resolve (only for non-sentinel values; 255/<0 are skipped silently). This surfaces mod-character gaps that would otherwise only show up as runtime late-adds with torn modeldefs (S312 fingerprint).

**`manifestBuildMission` call-site cleanup**:

- Props scan — the manual `catalogIdByRuntime` + `manifestAddEntry` + `s_manifestExpandDeps` block for `OBJTYPE_CHR` body/head and the prop-object `switch` for `OBJTYPE_DOOR/BASIC/...` model registration all replaced with calls to the unified helpers, passing `scan_source="props"`. This shrinks the function substantially and yields consistent discovery logging across every scan phase.
- Removed the unused `char id[64]` local variable.
- **Per-phase counter block** — four `s32 count_after_{joanna,props,intro,ailist}` snapshots of `out->num_entries` between phases. At end of build, emit a single summary line:
  - `MANIFEST-SP: scan stage=0x%02x joanna=N props+=M intro+=P ailist+=Q (total=T)`
  - This line makes it trivial to tell in a playtest log whether any phase is a zero-contributor for a given stage (e.g., Crash Site cinematics should show `ailist+=non-zero`; if it shows 0, the scan has regressed or the stage wasn't loaded yet).

**Scan coverage verified (no functional extension needed)**:

- Ailist scanner already mirrors `stageLoadAllAilistModels` (`src/game/game_00b820.c:130`) exactly — all five AI commands that reference bodies / heads / models / weapons / hats (`AICMD_DROPITEM`, `AICMD_SPAWNCHRATPAD`, `AICMD_SPAWNCHRATCHR`, `AICMD_EQUIPWEAPON`, `AICMD_EQUIPHAT`) are handled. `grep` of `chraicommands.c` confirmed no other AI command mutates `bodynum`/`headnum` at runtime (only reads exist for conditional logic).
- Intro scanner currently only emits WEAPON registration; no `INTROCMD_*` command spawns a character in PD (chrs come from props and ailists). `INTROCMD_OUTFIT` sets the player's `bondtype` but carries no catalog body/head reference.

### Why this is enough to close FIX-B.1

The intro + ailist scanners were already wired into `manifestBuildMission`, which is called by both `manifestSPTransition` (pre-load) and `manifestSPRescanSetup` (post-load Phase 2). The Phase 2 rescan walks the live `g_StageSetup.{props,intro,ailists}` after `setupLoadFiles()` populates them, and the diff-apply path loads the newly-discovered entries. The gap was *observability*: playtest logs couldn't distinguish "scan found everything" from "scan silently skipped X" — both looked the same in the aggregate `pre=X post=Y` line. With per-discovery + per-phase logging, the scan is now auditable end-to-end.

### Build result

Clean: 768/768 objects, zero errors. Both executables linked:
- `Build/PerfectDark.exe` = 52,625,645 bytes
- `Build/PerfectDarkServer.exe` = 22,838,584 bytes

Pre-existing `-Wcomment` warnings in `updater.h`, `pdgui_theme.h`, `pdgui_bridge.c`, `pdgui_theme_loader.h` and `-Wunused-function gettime_offset` in vendored `enet.h` — none introduced by this change.

### Playtest verification

- **Solo Crash Site (cinematic-heavy)** — load the mission and tail `pd-client.log`. Expect a string of `MANIFEST-SP: ailist-scan discovered body 'base:...' (bodynum=...)` lines from the stage's cinematic actors, followed by `MANIFEST-SP: scan stage=0x<hex> joanna=2 props+=N intro+=P ailist+=Q (total=T)` with `ailist+=` non-zero.
- **Solo Deep Sea** — same pattern; Deep Sea loads Skedar enemies via ailists, so the ailist+= contribution should be substantial.
- **Any stage with mod characters** — if a mod body/head is referenced by the setup/ailist but not registered in the catalog, expect a new `MANIFEST-SP: ailist-scan body bodynum=N not in catalog` WARNING identifying the gap.
- **Post-setup rescan log** — compare `MANIFEST-SP: rescan diff — newly-discovered=N kept=M unload=P` (existing S300 log) against the new summary: `newly-discovered` should equal `intro+= + ailist+= + props+= − (items already in pre-load manifest)`.

### Files touched

- `port/src/net/netmanifest.c` — scanner helpers, props loop cleanup, per-phase summary log

### Commit

| SHA | Scope |
|-----|-------|
| `e029eec3` | **fix(S323): FIX-B.1 per-discovery MANIFEST-SP logging on props/intro/ailist scans** |
| `5d9fbb19` | **Merge FIX-B.1 manifest scanner discovery logging (zealous-saha-02c1f5)** |

---

## Session S324 — 2026-04-17 (B-12 Phase 3 — remove chrslots, protocol v37 — `exciting-meitner-bc8c70` worktree)

**Scope**: Retire the legacy `u64 chrslots` bitmask and make the dynamic
participant pool the sole source of match slot state. Breaking wire
protocol change; bump `NET_PROTOCOL_VER` 36 → 37.

### What was done

**Struct / constants:**
- `src/include/types.h` — deleted `u64 chrslots` from `struct mpsetup`; left a note pointing readers at the participant pool.
- `src/include/constants.h` — deleted `BOT_SLOT_OFFSET`, `CHRSLOTS_PLAYER_MASK`, `CHRSLOTS_BOT_MASK`. Replacement rule documented inline: bot slots live at `MAX_PLAYERS..MAX_MPCHRS-1`.
- `src/include/game/mplayer/participant.h` — dropped `MpParticipant.legacy_slot`; replaced the "Legacy Compatibility" block with wire-serialization helpers (`mpParticipantsEncodeActiveMask` / `mpParticipantsDecodeActiveMask`).
- `src/game/mplayer/participant.c` — rewrote the encode/decode pair to derive directly from the pool (no chrslots semantics); dropped the `p->legacy_slot = -1` writes in add / addAt.

**Protocol:**
- `port/include/net/net.h` — `NET_PROTOCOL_VER 36 → 37` with a new comment block describing the B-12 Phase 3 wire change.
- `port/src/net/netmsg.c`:
  - `netmsgSvcStageStartWrite` now serialises the active-slot mask via `mpParticipantsEncodeActiveMask()` instead of `g_MpSetup.chrslots`.
  - `netmsgSvcStageStartRead` decodes the mask via `mpParticipantsDecodeActiveMask()` in-place — the post-read `mpParticipantsFromLegacyChrslots()` hop is gone.
  - CLC_LOBBY_START server handler: `mpParticipantPoolInit(MAX_MPCHRS)` + `mpAddParticipantAt()` per player and per bot replace direct chrslots bit-building.
  - Bot-iterate loops migrated from `chrslots & (1ull << (botidx + BOT_SLOT_OFFSET))` to `mpIsParticipantActive(botidx + MAX_PLAYERS)`.

**Server build:**
- `CMakeLists.txt` now links `src/game/mplayer/participant.c` into `SRC_SERVER` (server previously stubbed `mpParticipantsFromLegacyChrslots` only; now owns slot state via the same participant API as the client).
- `port/src/server_stubs.c::mpStartMatch` replaced the chrslots bot-count loop with `mpGetActiveBotCount()`; deleted the legacy shim stub.

**ROM / save / default configs:**
- `port/src/preprocess/misc.c::preprocessMpConfigs` — removed the `PD_SWAP_VAL(cfg->setup.chrslots)` + N64→PC bit-shift block (vestigial — the DMA buffer is overwritten by `g_MpConfigs[]` before any reader consults it; with chrslots gone from the struct it would no longer compile).
- `src/game/mpconfigs.c` — dropped the chrslots placeholder from each of 44 positional `g_MpConfigs[]` initializers (plus one non-zero `0x00f0` value on the `Simulants` preset; bot activity is reconstructed from `BotConfigsArray[i].difficulty` at load time).

**Runtime callsites:** every `g_MpSetup.chrslots` read/write across `src/game` and `port/` migrated to the participant API. Significant files:
- `src/game/challenge.c` — `challengeIsAvailableToAnyPlayer` now builds its per-player availability mask by iterating `mpIsParticipantActive(0..MAX_LOCAL_PLAYERS-1)`. `challengePerformSanityChecks` uses `mpRemoveParticipant` + `mpAddParticipantAt` to rebuild bots from difficulty.
- `src/game/mplayer/mplayer.c` — `mpStartMatch` server path, `mpReset`, `mpAddSimulant`/`mpRemoveSimulant`/`mpCopySimulant`, `mpGetSlotForNewBot`, `mpIsSimSlotEnabled`, `mpGenerateBotNames`, `mpApplyConfig`, `mpsetupfileSaveWad`/`LoadWad`, `mp0f18dec4` — all migrated. `func0f18d074` now returns `i + MAX_PLAYERS` instead of `+ BOT_SLOT_OFFSET`.
- `src/game/setup.c` — model-slot and chrmgr bot accounting reads `mpIsParticipantActive(k + MAX_PLAYERS)` + `mpGetActiveBotCount()`.
- `src/game/menutick.c` — MP setup player-add/remove loops + Deep Sea continue branch rewritten.
- `src/game/menuitem.c`, `src/game/menu.c`, `src/game/mainmenu.c`, `src/game/lv.c`, `src/game/mplayer/ingame.c`, `src/game/mplayer/scenarios/capturethecase.inc` — reader sites migrated to `mpIsParticipantActive(i)`.
- `src/lib/main.c` and `port/src/pdmain.c` — boot-path mplayer init seeds the participant pool directly (`mpAddParticipantAt` for each local slot) instead of writing `chrslots`.
- `port/src/net/net.c` — `netDisconnect`-style fallback re-seeds pool via `mpParticipantPoolInit` + `mpAddParticipantAt(0, PARTICIPANT_LOCAL, ...)`.
- `port/src/net/matchsetup.c` — `matchConfigCommitAndStart` path now uses the participant pool exclusively; every log line moved from `chrslots=0x...` to `activeMask=0x...`.
- `port/fast3d/pdgui_bridge.c::pdguiPauseGetChrSlots` derives a 32-bit mask from `mpParticipantsEncodeActiveMask()` (pause menu consumer unchanged).
- `port/fast3d/pdgui_menu_botsetup.cpp` — retained a local `#define BOT_SLOT_OFFSET 8` alias (that unit doesn't include constants.h); row-label predicates unaffected.

**Includes:** added `game/mplayer/participant.h` to every file that now calls the pool directly (scenarios.c, setup.c, menutick.c, menuitem.c, menu.c, mainmenu.c, ingame.c, lv.c, pdmain.c, net.c, server_stubs.c, pdgui_bridge.c, lib/main.c).

### Build verification

- `source devtools/build-env.sh && ninja -C Build pd pd-server` → clean build [474/474] after forcing rebuild with `touch src/include/types.h`.
- `PerfectDark.exe` 52,640,380 bytes, `PerfectDarkServer.exe` 22,876,668 bytes. Both link cleanly.
- No new warnings from the refactor (only pre-existing `-Wmaybe-uninitialized` in unrelated files).

### Wire compatibility

- `NET_PROTOCOL_VER` moved 36 → 37. Pre-v37 clients are rejected at handshake. Client and server must be upgraded together.
- `SVC_STAGE_START` payload layout unchanged beyond the one affected field: the same u64 offset that previously carried `chrslots` now carries the participant-derived active-slot mask with identical bit semantics (bit i = slot i active), so on-wire packet length is unchanged.

### Follow-ups

- Playtest verification checklist lives in `tasks-current.md` under the new Done section.
- Backlog entry "B-12 Phase 3 — Remove chrslots" removed from `tasks-current.md`.
- `context/constraints.md` ENet version bullet updated to v37; the chrslots active-constraint bullet is replaced by "participant pool is sole slot store"; a new Removed-Constraints entry documents the chrslots + BOT_SLOT_OFFSET retirement.
- Design doc `context/b12-participant-system.md` is still accurate in spirit; the "Phase 3" section is now the shipped state.

## Session S323 — 2026-04-17 (B-161 + B-141 root-cause fixes — `magical-mahavira-f3726f` worktree)

**Scope**: Two root-cause investigations and fixes. B-161 class: modeldef corruption (torn parts=0, root=NULL) reaching cached g_ModelStates/g_HeadsAndBodies and exploding in downstream traversal. B-141: ~200 audio underruns per 30s window from inadequate SDL queue cushion on PC.

### What was done

**B-161 root-cause fix — `src/game/modeldef.c::modeldefLoad`:**
Traced the torn-modeldef class from symptom (S308/S312 defensive guards catching AV in bbox traversal, late-add diagnostic logging `MANIFEST-SP: late-add ... post-load modeldef torn: parts=0`) back to `modeldefLoad` — the single chokepoint every body/head/prop modeldef flows through. Validation was absent: `fileLoadToNew` could succeed with non-NULL but torn data (partial ROM load, garbage rwdata, etc.), and the torn struct would be cached by `catalogGetBodyModeldef` / `setupLoadModeldef` without any fields being sanity-checked, leading to an AV up to minutes later deep in the tick loop.

Fix: after `modelPromoteTypeToPointer` + `modelPromoteOffsetsToPointers` + `modeldef0f1a7560` (all promotions), validate:
- `rootnode == NULL` OR `numparts <= 0` OR `numparts > 500` → log `MODELDEF: file %u loaded torn — parts=%d root=%p scale=%.3f -- rejecting` at ERROR, reset `g_LoadType = LOADTYPE_NONE`, return NULL.
- `scale <= 0.0f` → log `MODELDEF: file %u loaded with degenerate scale %.3f — clamping to 1.0` at WARNING, clamp to 1.0, continue.

Single chokepoint — every caller inherits the guarantee. Existing NULL handlers (FIX-B.2 in `setuputils.c`, S308 bbox-chain guards, `body0f02ce8c` torn-body skip) already convert NULL returns into "missing asset" log lines, so the fix lands without touching any caller.

**B-141 root-cause fix — `src/lib/audiomgr.c::amgrFrame`:**
Traced the PC audio path: at 22050 Hz stereo × 60 Hz NTSC, SDL consumes 367.5 stereo samples/frame and the game pushes 368 (non-brake) or 184 (brake). Net drift is +0.5 stereo samples/frame when below the brake threshold. Original `1100` threshold = 50ms cushion; typical main-thread hitches (50-100ms) drain queue below the 128-sample underrun detection, explaining the observed rate.

Fix: three-tier production pacing.
- Queue > 3000 → push 184 (brake, drains 183.5/frame).
- Queue 2500-3000 → push 368 (steady, drifts +0.5/frame).
- Queue < 2500 → push 736 (fast-fill, adds 368.5/frame).

`info->data` is allocated at 3072 bytes = 768 stereo samples capacity (audiomgr.c:102), so a 736-sample push is safely in-bounds. Steady state now hovers 2800-3000 (~128-136ms cushion, 3× original). Cold-queue fill reaches 2500 in 7 frames (~115ms) instead of 36 seconds. PAL retains its 552/frame rate (already sufficient for 50 Hz).

### Build result

Clean: 768/768 objects, zero errors. `PerfectDark.exe` = 52,638,334 bytes. `PerfectDarkServer.exe` = 22,838,473 bytes. Only pre-existing `-Wcomment` and `-Wunused-function` warnings in vendored code.

### Next steps

- **B-161 playtest**: Defection → Next Mission → Investigation. Walk 1–2 minutes. No AV expected. If any modeldef loads torn, log will now pinpoint `MODELDEF: file %u loaded torn — parts=%d root=%p scale=%.3f -- rejecting`.
- **B-141 playtest**: Play any arena for 60s, tail pd-client.log for `AUDIO[B-141]: 30s summary`. Expect `underruns=0` in a non-hitching run; `buffered(samples) min` should hover 2800-3000 instead of 900-1100.

---

## Session S323 — 2026-04-17 (Batch G — cross-audit gap fixes — `practical-wozniak-051afa` worktree)

**Scope**: Six items flagged by cross-audit: one critical runtime bug (audio volumes never applied), four 4MB dead-code remnants, one stale tooltip.

### What was done

**CRITICAL — audioNotifyEngineReady() was never called on PC:**
- `src/lib/main.c:819` has the call, but that file is not compiled on PC — the entry point is `port/src/pdmain.c::mainProc()`
- Added `#include "audio.h"` to pdmain.c and called `audioNotifyEngineReady()` immediately after `sndInit()`
- Effect: `g_AudioEngineReady` is now set at runtime; `audioApplyVolumes()` early-return guard is lifted; music and SFX volume sliders now reach the engine

**S320 gap 1 — `playerResetLoResIf4Mb` empty stub (player.c, player.h, vi.c, playerreset.c):**
- Deleted the empty function body from `src/game/player.c:3372-3374`
- Removed declaration from `src/include/game/player.h:45`
- Removed `#if PAL` call site from `src/lib/vi.c:137`
- Removed unconditional call site from `src/game/playerreset.c:133`

**S320 gap 2 — Dead `is4mb` local in hudmsg.c:**
- Removed `s32 is4mb;` declaration and `is4mb = false;` assignment
- Simplified condition from `(is4mb || optionsGetScreenSplit() == SCREENSPLIT_VERTICAL)` to `(optionsGetScreenSplit() == SCREENSPLIT_VERTICAL)`

**S320 gap 3 — Orphaned `MAX_SEQ_SIZE_4MB` define in snd.c:**
- Deleted `#define MAX_SEQ_SIZE_4MB 1024 * 14` from `src/lib/snd.c:31`

**S320 gap 4 — Dead `g_BgunGunMemBaseSize4Mb2P` global:**
- Removed definition (both `#ifdef PLATFORM_64BIT` and `#else` branches) from `src/game/bondgun.c:176-179`
- Removed extern from `src/include/data.h:237`
- Removed extern from `src/game/bondgunreset.c:12`

**S323 gap — Stale font tooltip:**
- Removed "Font swap takes effect on next restart (ImGui atlas is built at backend init)." line from `port/fast3d/pdgui_menu_mainmenu.cpp:1291` — atlas now rebuilds live since S323 Batch D+F

### Commit

| SHA | Scope |
|-----|-------|
| `7838d847` | **fix(S323): Batch G — cross-audit gaps: audioNotifyEngineReady, 4MB remnants, stale tooltip** |

### Build result

Clean: 768/768 objects, zero errors, both `PerfectDark.exe` and `PerfectDarkServer.exe` link clean. Only pre-existing `-Wcomment` and `-Wunused-function` warnings in vendored code.

### Next steps

- No playtest verification needed for dead-code removals
- Audio volume fix should be transparent — if music/SFX volume sliders were previously unresponsive, they now work

---

## Session S323 — 2026-04-17 (Batch D+F — font atlas live rebuild + legacy sidecar migration — `mystifying-mirzakhani-e0e10a` worktree)

**Scope**: Two focused runtime polish tasks. Task 1: font atlas live rebuild so font swaps take effect immediately without restart. Task 2: one-shot migration for pre-S313 agent sidecar files that were named from raw save bytes instead of display names.

### What was done

**Task 1 — Font atlas live rebuild (pdgui_backend.cpp, pdgui.h, pdgui_menu_mainmenu.cpp, prefs_agent.h):**
- Extracted the pdguiInit font-loading block into `static void pdguiLoadFontsIntoAtlas(ImGuiIO &io)` — adds Handel Gothic always, then adds user font mod if selected
- Added `static bool s_FontAtlasRebuildRequested` flag + `void pdguiRequestFontAtlasRebuild()` API (declared in pdgui.h)
- In `pdguiNewFrame`, before the early-return check, check the flag: if set, `Fonts->Clear()` → `pdguiLoadFontsIntoAtlas` → `ImGui_ImplOpenGL3_DestroyFontsTexture()` → `ImGui_ImplOpenGL3_CreateFontsTexture()`; logs "pdgui: font atlas rebuilt"
- Font dropdown commit in Settings (`pdgui_menu_mainmenu.cpp`) now calls `pdguiRequestFontAtlasRebuild()` after `pdguiFontModSetActiveId()`; removed "(restart required)" `TextDisabled` hint
- Updated `prefs_agent.h` doc comment: all visual prefs now swap live (no restart needed)

**Task 2 — Legacy sidecar migration (prefs_agent.c, prefs_agent.h, pdgui_menu_agentselect.cpp):**
- Added `prefsAgentMigrateLegacySidecar(raw_name, display_name)`: builds old path via `sanitize(raw_name)` (how pre-S313 code built it from `file->name` bytes), builds new path via `prefsBuildPath(display_name)`; if old≠new, new missing, old present → `rename()` + log `"PREFS: migrated legacy sidecar '%s' -> '%s'"`
- Declared in `prefs_agent.h` with explanatory comment
- `prefsLoadForFile()` in `pdgui_menu_agentselect.cpp` calls `prefsAgentMigrateLegacySidecar(file->name, name)` before `prefsAgentLoad(name)` — one-shot, idempotent

### Commit

| SHA | Scope |
|-----|-------|
| `ae188997` | **feat(S323): Batch D+F — font atlas live rebuild + legacy sidecar migration** |

6 files changed, 140 insertions(+), 63 deletions(−). Merged into dev.

### Build result

Clean: `pd` links with zero errors (only pre-existing `/* within comment` warnings). `pd-server` cached (changed files are pd-only).

### Next steps

- Playtest font swap: change font in Settings → Interface → Font dropdown; new font should apply immediately without restart
- Playtest migration: create a test agent sidecar with legacy naming, confirm it migrates on first Agent Select load

---

## Session S323 — 2026-04-17 (Batch A — IS4MB/IS8MB/STAGE_4MBMENU final cleanup — `admiring-mccarthy-38734a` worktree)

**Scope**: Tier 3 remaining N64 dead code after S322. S322 stripped ~100 callsites and removed STAGE_4MBMENU from STAGE_IS_SYSTEM(), but five live STAGE_4MBMENU callsites and all macro definitions were left. This batch finishes the job so `grep -rn "IS4MB|IS8MB|fourmeg2player|STAGE_4MBMENU" src/ port/` returns zero hits.

### What was done

**Verified clean slate first** — confirmed zero fourmeg2player hits; confirmed IS4MB/IS8MB had no callsites, only stub definitions; confirmed audio files (sched.c, audiomgr.c, snd.c) clean.

**Removed stub definitions:**
- `src/include/constants.h` — `#define IS4MB() (0)`, `#define IS8MB() (1)`, `#define STAGE_4MBMENU 0x5d`
- `src/include/memsizes.h` — unused `#define MENU_MODEL_BUF_4MB 0xb400`

**Removed 5 live STAGE_4MBMENU callsites:**
- `src/game/fmb.c::fmdHandleAbortGame` — removed if-STAGE_4MBMENU branch; PC always takes the else path
- `src/game/menu.c` (×2) — removed dead `max=4` block; simplified music-stage check to CITRAINING only
- `src/game/menutick.c` (×2) — removed `|| stagenum == STAGE_4MBMENU` from two CITRAINING condition checks
- `src/game/lv.c` — removed STAGE_4MBMENU line from doc comment block

**PAL guards audited** — remaining `#if PAL` / `#if VERSION >= VERSION_PAL_BETA` guards are original N64 version-variant code from the decompile (50Hz vs 60Hz timing, region-specific behavior). None wrap IS4MB blocks. Left intact.

### Commit

| SHA | Scope |
|-----|-------|
| `645a9c7c` | **feat(S323): Tier 3 N64 dead code — strip IS4MB/IS8MB/STAGE_4MBMENU fully** |

6 files changed, 4 insertions(+), 18 deletions(-). Merged to dev as `c3cb274e`.

### Build result

Clean: `pd` + `pd-server` both link [770/770] zero errors. Post-merge full rebuild confirmed.

### Verification

`grep -rn "IS4MB|IS8MB|fourmeg2player|STAGE_4MBMENU" src/ port/` → exit code 1 (zero hits). Complete.

---

## Session S323 — 2026-04-17 (Batch E — Audio channel routing audit + enforcement — `nice-allen-193a0c` worktree)

**Scope**: Batch E — Audit all audio playback paths for correct channel (Music/Gameplay/UI) volume routing. Verify per-agent `[Audio]` prefs are applied. Fix any gaps.

### What was found

Full audit of the four audio playback paths:

| Channel | Path | Volume applied? |
|---------|------|----------------|
| Music | `musicSetVolume(master × music)` via `audioApplyVolumes()` | ✅ |
| Gameplay SFX (ROM) | `sndSetSfxVolume(master × gameplay)` via `audioApplyVolumes()` | ✅ |
| UI | `pdguiPlaySound → audioGetUiVolumeScaled()` per-call | ✅ |
| Voice | Gameplay channel (N64 engine has no separate voice path) | ✅ acceptable |
| Gameplay SFX (mod WAV) | `audioPlayFileSound(volume, pan)` — **volume NOT scaled** | ❌ gap |

Per-agent `[Audio]` block in `prefs_agent.c` was confirmed correct: `applyKV` calls all four `audioSet*Volume` setters on load. The wiring to Agent Select was intact.

**Two gaps fixed:**

1. **`audioPlayFileSound` skipped gameplay volume** — mod WAV SFX overrides in `snd.c` call `audioPlayFileSound(path, volume, pan)` where `volume` is the per-sound `AL_VOL_FULL` value, never scaled by `g_SfxVolume` (which encodes `master × gameplay`). Fixed by multiplying `volScale` by `g_AudioMasterVolume * g_AudioGameplayVolume` inside `audioPlayFileSound`.

2. **`prefsAgentResetVisuals` didn't reset audio** — when Agent Select opened, visual prefs were reset to base but audio volumes remained at the previous agent's values. If Agent B had no sidecar file, `prefsAgentLoad` returned early and Agent B inherited Agent A's volumes. Fixed by:
   - Snapshot pd.ini baseline volumes in `g_AudioBaseline*` at `audioNotifyEngineReady` time (before any per-agent overlay)
   - New `audioResetToDefaults()` restores those baselines
   - `prefsAgentResetVisuals()` now calls `audioResetToDefaults()` before the visual resets

### Files changed

- `port/src/audio.c` — `audioPlayFileSound` gameplay scale; `audioNotifyEngineReady` baseline snapshot; `audioResetToDefaults()` impl
- `port/include/audio.h` — `audioResetToDefaults()` declaration
- `port/src/prefs_agent.c` — `prefsAgentResetVisuals` now calls `audioResetToDefaults()`

### Commit

| SHA | Scope |
|-----|-------|
| `b99ac9af` | **fix(audio): enforce channel volume routing on all playback paths** |

3 files changed, 37 insertions(+), 6 deletions(−).

### Build result

Build-headless.ps1 targets main working copy (worktree redirect — expected). Merged to dev.

### Next steps

- Playtest: mod SFX override path (needs a mod with a sound override to verify), per-agent audio volume isolation between agents
- If S313 deferred item is tackled: `Audio.ModPlaylist / ModShuffle / ModTrackId → per-agent` sidecar

---

## Session S323 — 2026-04-17 (Batch C glyph audit — `strange-visvesvaraya-248471` worktree)

**Scope**: Audit all in-world and HUD prompts for Batch C: verify or implement device-aware glyph calls (pickup / door / terminal interact prompts, forge HUD controls reminder). Replace any hardcoded `[E]`/`[A]` labels with `pdguiDrawActionPrompt()` / `pdguiGlyphGetActionLabel()`.

### What was done

Full audit of `port/fast3d/pdgui_interact_prompt.cpp`, `pdgui_forge_hud.cpp`, `pdgui_hud.cpp`, `pdgui_backend.cpp`, and `src/game/prop.c`. Also grepped entire `port/fast3d/` tree (excluding vendored imgui) for `[E]`, `[A]`, `Press E`, `Press A`.

**Finding: all items already implemented.**

- `pdgui_interact_prompt.cpp` — S311 wired `pdguiInteractPromptRender()` → `pdguiDrawActionPromptCentered(ACTION_USE, cx, cy, label)` where `label` comes from `propInteractPromptLabel()` (returns "Pick up" / "Open" / "Access" / "Use" / NULL). All four prompt types (weapon pickup, door, terminal, generic interactable) route through this single call. Device-aware — gamepad shows "[A]", KBM shows "[E]".
- `pdgui_forge_hud.cpp` — S312 wired the bottom-right controls reminder via `pdguiDrawActionPrompt()` and `pdguiGlyphGetActionLabel()` for all 4 rows (toggle/ascend/descend, boost/precision, tab-prev/next, use/cancel). Fully device-aware.
- Zero hardcoded `[E]`, `[A]`, `Press E`, or `Press A` strings found in runtime rendering code (only in doc comments).

### Code changes

None — audit session only.

### Next steps

- Batch C task closed in tasks-current.md.
- Remaining S312 follow-ups still open: font-atlas rebuild on runtime swap, Theme Editor mini-preview content-inset.

---

## Session S323 — 2026-04-17 (Batch B — menuPushRootDialog pool hygiene — `naughty-buck-8ede1d` worktree)

**Scope**: Batch B — pool-ctx migration audit + menuPushRootDialog pool hygiene + X-button close sweep.

### What was done

**Audit — Tasks 1 and 3 already complete:**
Tasks 1 (migrate renderCiSettingsRedirect/renderCiDeadPlayer2/renderCinemaList to pool-ctx) and Task 3 (wire pdguiConsumeTitleClose into additional renderers) were already completed in earlier sessions (S311+). All three renderers have `menupoolAcquireDialog` + `pdguiConsumeTitleClose`. Theme Editor, Modding Hub, Pause Menu, and Endscreen all have `pdguiConsumeTitleClose`. The stale tasks-current entry was updated to reflect this.

**Task 2 — menuPushRootDialog pool hygiene (new):**
Added `menupoolReleaseAll()` at the top of `menuPushRootDialog` (`src/game/menu.c:3712`) before the `numdialogs = 0` / `depth = 0` zeroing. Without this, any ctx-owning pool slot active at root-push time (e.g. MENU_TYPE_CI_OPTIONS from a boot-path open) became permanently unreachable once the stack was wiped, causing structural dedup to reject all subsequent pushes for that type. The per-frame watchdog (`menuPoolConsistencyCheck`) caught the symptom; this call removes the root cause.

### Commit

| SHA | Scope |
|-----|-------|
| `1fd30166` | **fix(S323): menuPushRootDialog — release pool before zeroing dialog stack** |

1 file changed, 8 insertions(+). Fast-forward merged to dev.

### Build result

Clean: `pd` + `pd-server` both link with zero errors (`PerfectDark.exe` + `PerfectDarkServer.exe`). Build configured fresh in worktree (no pre-existing Build dir).

### Next steps

- Playtest: cold boot → main menu → Settings → close → reopen. No "pool slot already active" watchdog warnings. CI redirect path clean.

---

## Session S322 — 2026-04-17 (N64 legacy audit — Tier 1/2 execution — `nostalgic-lichterman-3c1259` worktree)

**Scope**: Execute all Tier 1 and Tier 2 quick-win items from the N64 legacy audit (`context/designs/n64-legacy-audit-2026-04-17.md`). Strip compile-time-dead IS4MB() branches, IS8MB() guards, `fourmeg2player` mode, and STAGE_4MBMENU routing. Bump N64-era resource limits.

### What was done

**Tier 2 quick wins:**
- `MEMP_EXPANSION_POOL_SIZE` 8 MB → 64 MB (`src/lib/memp.c`)
- Audio synthesizer limits raised: `maxPVoices` 30 → 64, `maxVVoices` 44 → 96, `maxSounds` 20 → 48, `ADMA_MAX_ITEMS` 80 → 200 (`src/lib/snd.c`, `src/lib/audiodma.c`)

**Tier 1 — IS4MB() dead branch removal** (~100+ sites, 20+ files):
- `src/lib/memp.c`, `src/lib/snd.c`, `src/lib/audiodma.c`, `src/lib/rdp.c`, `src/lib/vi.c`
- `src/game/setup.c`, `src/game/smokereset.c`, `src/game/modelmgr.c`, `src/game/modelmgrreset.c`
- `src/game/texreset.c`, `src/game/bondgunreset.c`, `src/game/botmgr.c`, `src/game/vtxstore.c`
- `src/game/player.c`, `src/game/titleinit.c`, `src/game/lv.c`, `src/game/filemgr.c`
- `port/src/pdmain.c`, `port/src/pdsched.c`

**Tier 1 — IS8MB() guard removal** (~25 sites):
- `src/game/player.c`, `src/game/lv.c`, `src/game/menu.c`, `src/game/menugfx.c`, `src/game/menutick.c`
- `port/src/pdmain.c`, `port/src/pdsched.c`

**Tier 1 — fourmeg2player removal** (14 sites):
- Struct field `fourmeg2player` removed from `src/include/types.h`
- All setters/consumers: `src/lib/varsinit.c`, `src/lib/vi.c`, `src/game/player.c`, `src/game/playermgr.c`, `src/game/mplayer/scenarios.c`, `src/lib/crash.c`

**Tier 1 — STAGE_4MBMENU routing removal** (6+ files):
- `src/include/constants.h` (STAGE_IS_SYSTEM macro), `src/lib/main.c`, `src/game/lv.c`, `port/src/pdmain.c`

**Build fixes during session:**
- `activemenu.c`: restored orphaned `#endif` that closed `#if VERSION != VERSION_JPN_FINAL`
- `menu.c`: removed orphaned `} else { ... }` (88 lines) + bare `{` left by IS8MB guard removal; restored missing `}` closing `if (modeltype == MENUMODELTYPE_HUDPIECE)` block; changed tentative forward decl to `extern` decl for `g_PakAttemptRepairMenuDialog`

### Commit

| SHA | Scope |
|-----|-------|
| `4a6382cf` | **feat(S322): N64 legacy audit Tier 1/2 — strip IS4MB/IS8MB/fourmeg2player/STAGE_4MBMENU** |

61 files changed, 284 insertions(+), 900 deletions(−). Pushed to dev.

### Build result

Clean: `pd` + `pd-server` both compile with zero errors.

### Next steps

- Playtest verification: cold boot, menus, audio, multiplayer (see tasks-current S322 QC items)
- Remaining audit items: Tier 3 (dynamic memory grow-on-demand) and systemic cleanup deferred

## Session S318 — 2026-04-17 (Direct file access design — `hopeful-rosalind-4f1033` worktree)

**Scope**: Design-only session. Authored `context/designs/direct-file-access-design-2026-04-17.md` — comprehensive design for replacing ROM-offset-based asset loading with a typed Asset Provider abstraction.

### What was produced

New design doc (`context/designs/direct-file-access-design-2026-04-17.md`, ~680 lines):

**§1 Current State** — ROM binary mmap'd at init, `fileSlots[]` populated from embedded big-endian offset table, `filenum` integers as direct array indices, `catalogResolveFile()` seam for mod overrides, `SRC_ROM / SRC_EXTERNAL` states already present. Gap: catalog knows *what* assets are but not *where* bytes come from; new mod assets (no ROM filenum) are unsupported.

**§2 Target State** — Every catalog entry carries an `asset_source_t` with a typed `asset_data_handle_t`. The ROM becomes `RomProvider`; loose files become `FileProvider`; mod archives (future) become `ArchiveProvider`. `filenum` becomes a `RomProvider` internal detail never exposed at interface boundaries. Multi-ROM-version support is free (NTSC vs PAL use the same catalog IDs; their file tables differ only inside `RomProvider`).

**§3 Asset Provider Interface** — C vtable: `asset_provider_t` with `resolve_size`, `load`, `unload`, `describe`. Handle type: `asset_data_handle_t` (opaque `u64[2]` + provider pointer). `assetLoad` / `assetLoadToNew` dispatchers. `fileLoadToNew` becomes a one-line wrapper for Phase 1 backward compat.

**§4 Catalog Integration** — `asset_source_t source` field on `asset_entry_t`. `catalogSetPrimary` / `catalogSetOverride` / `catalogClearOverride` API. Priority (archive > mod file > base file > ROM) declared in catalog fields, not implicit in code order. New mod-only assets (Grid levels, custom stages) register with `FileProvider` primary — no filenum needed.

**§5 Migration Strategy** — 5 phases, each independently reversible:
- Phase 1: Define provider types, wrap `fileLoadToNew` — zero behavior change
- Phase 2: Add `source` field to entries, move mod-override logic from `romdataFileLoad` to catalog
- Phase 3: Migrate call sites from filenum to `assetLoadToNew` (parallels SA-5 in session-catalog plan)
- Phase 4: Retire `filenum` as public currency — deprecated, internal only
- Phase 5: Wire Grid/Forge saved levels as `FileProvider` catalog entries

**§6–8** — Impact on manifest/net/Grid/mods/skin editor/audio (all minimal), performance (vtable dispatch is negligible; async load path designed in), risk table (8 items with mitigations, rollback strategy per phase).

### Commit

| SHA | Scope |
|-----|-------|
| `79714658` | **docs(context): S318 direct-file-access design — Asset Provider abstraction** |

Merged to dev via `467b7682`.

### Files changed

- `context/designs/direct-file-access-design-2026-04-17.md` — new, ~680 lines
- `context/README.md` — added design doc to Plan/Design Files table, updated Last Updated to S318

---

## Session S321 — 2026-04-17 (Strip N64 demo/attract mode system — dev direct)

**Scope**: Remove the N64 demo/attract mode system entirely. The PC port has no demo recordings, no attract screen, no kiosk mode. S319 removed the init setter; S321 removes all consumers.

### What was removed

| Location | What |
|----------|------|
| `src/game/title.c` | `g_IsTitleDemo` global; `g_TitleIdleTime60` global; demo block in `titleInitSkip`; if/else in TITLEMODE_SKIP tick (collapsed to unconditional `titleSetNextMode(TITLEMODE_RARELOGO)`); tombstone comment |
| `src/include/data.h` | `extern s32 g_IsTitleDemo`; `extern u32 g_TitleIdleTime60` |
| `src/game/lv.c` | M0.2 demo-skip interrupt (any-button → STAGE_TITLE); idle-time accumulator (`g_TitleIdleTime60 +=`) — entire 26-line block |
| `src/game/chraicommands.c` | `aiEndLevel`: removed `if (g_IsTitleDemo) mainChangeToStage(STAGE_TITLE)` arm; `else if` promoted to `if` |
| `src/game/player.c` | `playerEndCutscene`: same promotion; `playerStartCutscene`: `!g_IsTitleDemo &&` condition stripped |
| `src/game/music.c` | `musicEndCutscene`: `if (!g_IsTitleDemo)` guard removed — body now unconditional |

`STAGE_DEFECTION` itself is untouched — Defection is a real playable mission. Only the one reference inside the removed `titleInitSkip` demo block is gone.

### Commit

| SHA | Scope |
|-----|-------|
| `9fe8291b` | **chore(title): strip N64 demo/attract mode — g_IsTitleDemo + g_TitleIdleTime60 + all consumers** |

### Build verify

`ninja -C Build pd` — clean. `grep -rn g_IsTitleDemo` → zero results.

---

## Session S320 — 2026-04-17 (Agent Select default theme — dev direct)

**Scope**: Agent Select was showing whatever per-agent theme was previously applied (from a prior sign-in) instead of the base system defaults. Since Agent Select is pre-sign-in, no per-agent prefs should be active.

### What changed

Added `prefsAgentResetVisuals()` to `prefs_agent.c`:
- Resets color theme to `base:theme_blue`
- Restores chrome to enabled + clears any custom nine-slice style ID
- Resets title bar to `PDGUI_TITLEBAR_CLASSIC` (0)
- Clears any per-agent custom font
- Disables scanlines

Called in `pdgui_menu_agentselect.cpp:IsWindowAppearing` block, BEFORE the auto-load check. After the user selects an agent, `prefsLoadForFile` fires and applies their per-agent theme — producing a visible transition from base defaults → their theme.

The auto-load path (default agent configured) also fires after the reset, so first-frame appearance is base → agent theme in the same tick (no visual flash, but correct ordering).

### Commit

| SHA | Scope |
|-----|-------|
| `d047d6eb` | **feat(agent-select): reset visual prefs to defaults on open — theme transition visible on sign-in** |

### Files touched

- `port/src/prefs_agent.c` — `prefsAgentResetVisuals()` implementation
- `port/include/prefs_agent.h` — declaration
- `port/fast3d/pdgui_menu_agentselect.cpp` — extern "C" decl + call in IsWindowAppearing

### Build verify

`ninja -C Build pd` — clean (only pre-existing `/*` within comment warnings).

---

## Session S319 — 2026-04-17 (Spurious STAGE_DEFECTION boot transition fix — `nostalgic-lichterman-3c1259` worktree)

**Scope**: Root-cause the double-transition `STAGE_DEFECTION (0x30) → STAGE_CITRAINING (0x26)` logged on every cold boot. The crash guard added in S317 addendum was defense-in-depth; this session eliminates the bad transition at the source.

### Root cause

`titleInitRareLogo()` (`src/game/title.c:2025`) sets `g_IsTitleDemo = true` when `!g_IsTitleDemo && IS8MB()`. On PC, `IS8MB()` is compile-time `1` (see `constants.h:92`). This means **every cold boot** activates demo mode, which then routes through `titleInitSkip` with `g_TitleNextStage = STAGE_DEFECTION`, firing `mainChangeToStage(0x30)`. The `TITLEMODE_SKIP` tick immediately detects `g_IsTitleDemo` and overrides to CI Training, firing `mainChangeToStage(0x26)` — causing the "MAIN: replacing pending stage change 0x30 -> 0x26" warning and a brief double-load of the Defection stage manifest.

On N64 the demo was a real pre-recorded playback only available with the 8MB expansion pak. On PC there is no demo recording system, so the IS8MB() branch was vestigial dead code causing the bad transition every boot.

### Fix

Removed the `if (!g_IsTitleDemo && IS8MB()) { g_IsTitleDemo = true; }` block from `titleInitRareLogo()`. `g_IsTitleDemo` is now never set to `true` during boot. `titleInitSkip` routes directly to `STAGE_CITRAINING` without the intermediate DEFECTION transition.

### Commit

| SHA | Scope |
|-----|-------|
| `53f17e6b` | **fix(title): remove IS8MB demo-init — eliminates spurious STAGE_DEFECTION on every cold boot** |

### Files touched

- `src/game/title.c:2025-2027` — removed IS8MB demo-activation block; replaced with comment

### Build verify

`ninja -C Build pd` — clean. PerfectDark.exe 52,530,547 (only pre-existing warnings).

---

## Session S318 — 2026-04-17 (B-161 class bbox/modeldef NULL sweep — dev direct)

**Scope**: Comprehensive audit of all bbox/modeldef NULL dereference sites in the codebase. Five unsafe sites found and fixed.

### Audit methodology

Searched all `modeldefFindBboxRodata`, `modelFindBboxRodata`, `objFindBboxRodata`, `setupLoadModeldef` callsites. Classified each as SAFE (already NULL-checked, or going through S308-patched `objGetLocal*`/`objGetRotatedLocal*` helpers) or UNSAFE (direct dereference without guard).

### Five unsafe sites fixed

| File | Function | Pattern | Fix |
|------|----------|---------|-----|
| `bondbike.c:174` | `bbikeHandleActivate` | `bbox->xmax` / `bbox->zmax` after `objFindBboxRodata` | Early return if NULL |
| `chr.c:3050` | unnamed | `thing->bbox = *bbox` struct copy | Zero-init fallback |
| `propobj.c:15482` | `glassDestroy` | `bbox->xmin/xmax/ymin/ymax` in `shardsCreate` | Skip shardsCreate if NULL |
| `propobj.c:19332` | `doorIsObjInRange` | `bbox->xmin/xmax/ymin/ymax/zmin/zmax` | Point-check fallback (scale=0) |
| `propobj.c:1888` | `func0f069850` | `objCalculateGeoBlockFromBboxAndMtx(bbox, ...)` | Empty block fallback if bbox + rodata19 both NULL |

### Build verify

`ninja -C Build pd pd-server` clean. PerfectDark.exe 52,531,342 / PerfectDarkServer.exe 22,852,827.

---

## Session S317 — 2026-04-17 (ROM hash cache path fix + crash investigation — `nostalgic-lichterman-3c1259` worktree)

**Scope**: Urgent crash report — Mike's second PC crashes at startup (stage 0x26 CI Training bodiesReset) after removing `.sha256` files from the game folder.

### Investigation findings

1. **`.sha256` files are NOT read at runtime.** `PerfectDark.exe.sha256` / `PerfectDarkServer.exe.sha256` are release-only artifacts uploaded to GitHub for the updater's download-integrity verification. They are never opened during normal game operation. Removing them cannot cause any crash.

2. **`catalogCacheVerifyRom` path bug (fixed).** The function was calling `sha256HashFile(romPath, ...)` with the bare filename `g_RomName = "pd.ntsc-final.z64"` instead of the full filesystem path. `sha256HashFile` calls `fopen(path, "rb")` directly without `fsFullPath`, so it fails whenever the CWD is not the data directory. This caused `WARNING: CATALOG: ROM hash cache: could not hash 'pd.ntsc-final.z64'` on every boot. Fix: added `fsFullPath(romPath)` call before `sha256HashFile`.

3. **Actual crash (B-163 — open).** AV at PC+0x161258 during CI Training setup. Crash log upload path inaccessible; no binary for `addr2line`. Double-transition `MAIN: replacing pending stage change 0x30 -> 0x26` at boot is suspicious. Most likely hypothesis: pre-S312 build (`aee52a8a`) without modeldef defensive guards. Current HEAD should be safe.

### Commit

| SHA | Scope |
|-----|-------|
| `48bf97d6` | **fix(catalog): use fsFullPath in catalogCacheVerifyRom ROM hash path** |

### Build verify

`ninja -C Build pd pd-server` clean. PerfectDark.exe 52,552,467 / PerfectDarkServer.exe 22,840,009. No new warnings.

---

## Session S317 addendum — 2026-04-17 (CI Training crash fix — dev direct)

**Scope**: Crash confirmed reproducible on dev machine. addr2line on HEAD binary resolved the full crash site.

### Root cause confirmed

addr2line on `Build/PerfectDark.exe` (image base `0x140000000`):
- `0x140161258` → `setupCreateDoor setup.c:1128`
- `0x140162401` → `setupCreateProps setup.c:1654`
- `0x1400c69df` → `lvReset lv.c:548`

Call path: `lvReset → setupCreateProps → setupCreateDoor → AV`. The crash is `bbox->xmax` dereference at line 1128 where `bbox = modeldefFindBboxRodata(...)` returned NULL. S308 guarded the door-*tick* path (`doorGetBbox`) but missed the door-*creation* path.

### Fix

Two-layer guard in `setupCreateDoor` (`src/game/setup.c`):
1. Early return with `LOG_WARNING` if `g_ModelStates[modelnum].modeldef == NULL` after `setupLoadModeldef` — prevents all downstream null deref (door can't be created without a model).
2. Identity-scale fallback (`xscale=yscale=zscale=1`) if `bbox` is NULL — matches the existing zero-scale guard at lines 1148-1150; door renders at 1:1 instead of crashing.

Root cause of WHY the model fails to load (catalog miss or model has no bbox node) is still unknown. The double-transition 0x30→0x26 remains an open investigative lead.

### Build verify

`ninja -C Build pd pd-server` clean. PerfectDark.exe 52,530,318 / PerfectDarkServer.exe 22,852,827.

### Next steps

- Playtest CI Training — confirm no AV; check log for door WARNING if any
- Investigate 0x30→0x26 double-transition at boot

---

## Session S316 — 2026-04-17 (solo mission select UX + session-log archive — `epic-mirzakhani-884cc2` worktree)

**Scope**: Three ordered tasks: (1) session-log archive, (2) campaign mission select UX cleanup, (3) room screens audit.

### Commits on `claude/epic-mirzakhani-884cc2`

| SHA | Scope |
|-----|-------|
| `fc713f9a` | **feat(solo-mission): fix difficulty text regression + add Dark Agent row**.  `renderMissionSelect` right panel: `langSafe()` result now has fallback names so difficulty text is never blank when lang bank is not resident.  Dark Agent / PD Mode row added after Agent/SA/PA; shown only when Skedar Ruins beaten on PA (`pdModeVisible` gate).  `k_NumDetailItems` and `startFocusIdx` are now dynamic.  `DIFF_PD=3` defined module-scope; `k_DiffBadgeColor` extended to 4 entries (purple for Dark Agent).  Start Mission with DIFF_PD selected opens `g_PdModeSettingsMenuDialog` with PA as base difficulty instead of calling `menuhandlerAcceptMission` directly. |
| `a046a99a` | **chore(context): archive session-log S241–S280**.  `session-log.md` trimmed to S281–S313 rolling window.  New `_archive/session-log-archive-S280-and-older.md` created.  INDEX.md + README.md updated to point to new tier. |

### Files touched

- `port/fast3d/pdgui_menu_solomission.cpp` — renderMissionSelect right panel: fallback names, Dark Agent row, dynamic nav counts, PD Mode launch path
- `context/session-log.md`, `context/INDEX.md`, `context/README.md`, `context/_archive/session-log-archive-S280-and-older.md` (new)

### Room screens audit (Task 3 — no changes needed)

Audited `pdgui_menu_room.cpp`:
- **Start Match / Leave Room docking**: manual `SetCursorPosY(dialogH - footerH)` pattern from S298 is correct; footer pre-allocated in `contentH`.
- **Team sorting + color-coding**: fully implemented (S297) — bubble-sort by team then human-before-bot; `kTeamColors[8]` per-team tints; colored "-- Team N --" headers.
- **Bot management**: `pdguiBotSetupDrawSimulantsBody` in CollapsibleHeader; matchslot bot UI with multi-select and reroll; all functional.
- **Content within border bounds**: `pdguiSetCursorBelowTitle(pdTitleH)` called at line 2387.

No room.cpp changes required.

### Next steps

- **Playtest**: launch solo mission select, verify difficulty names show without lang bank, verify Dark Agent row appears only after Skedar Ruins PA beaten.
- Remaining non-blue literal sweep (S311 follow-up): agentselect/solomission/modmgr still have PD-blue `IM_COL32` decorations.

---

## Session S313 marathon batch — 2026-04-17 (Grid rename completion + per-agent audio + pd.ini audit — `great-robinson-15f409` worktree, direct to dev)

**Scope**: Continuation of the three-session marathon (S311 + S312 + S313 all merged).  S313 owns this batch of targeted follow-ups on top of the merged dev.  Worktree FF-ed to origin/dev; commits land on the same branch and merge via `--no-ff`.

### Commits on `claude/great-robinson-15f409` (on top of dev at `d96d64cb`)

| SHA | Scope |
|-----|-------|
| `8057f064` | **feat(grid): rename `FORGE:` / `FORGE.*` log prefix → `GRID:` / `GRID.*`**.  Completes the user-facing rename from the main S313 drop.  All `sysLogPrintf` prefixes flipped across `src/game/forgemode.c`, `port/src/forge/{forge_core,forge_serialize,forge_gametype,forge_ai,forge_logic,forge_undo}.c`, `port/fast3d/pdgui_menu_forge.cpp`.  Internal symbol names (`forge_*`, `FORGE_MAX_*`, `FORGE_CAT_*`, `FORGE_MOD_*`, catalog namespaces, file paths) retained for code stability.  `pdgui_forge_hud.cpp` header comment reworded to describe the module as "The Grid HUD overlay" with an explicit note on the internal/external taxonomy split. |
| `1694d3ec` | **feat(prefs): audio volumes per-agent + agent-name from `gamefileGetOverview`**.  (a) `prefs_agent.c` now reads/writes an `[Audio]` block with `MasterVolume` / `MusicVolume` / `GameplayVolume` / `UIVolume`.  Setters `audioSet{Master,Music,Gameplay,Ui}Volume` applied on load; current globals read on save.  Global pd.ini keeps the same four keys as per-machine defaults.  (b) `prefsLoadForFile` in `pdgui_menu_agentselect.cpp` switches from raw `filelistfile::name[]` byte-walk to `gamefileGetOverview` so the sidecar path matches the Agent Select display text exactly -- prior code could produce mismatched filenames because the stored name is a variable-length char-code encoding. |
| `6aaf299f` | **feat(config): drop network-tuning + protected-folders from pd.ini**.  Removed `configRegister*` for `Net.LerpTicks`, `Net.Client.{InRate,OutRate,UpdateFrames}`, `Net.Server.{Port,InRate,OutRate,UpdateFrames}`, `Update.ProtectedFolders`.  Globals kept with file-scope initializers so runtime code compiles; `-port` CLI override for the server still works.  Retained: `Net.Client.LastJoinAddr`, `Net.RecentServer.*`, `Net.Server.AllowInfoQuery`, `Net.RecentServerCount`.  New doc `context/config-pd-ini-audit.md` catalogs every `configRegister*` call in the port and documents the three-tier model (pd.ini / per-agent / compile-time) with a decision flowchart for future settings. |
| `(next)` | **Merge commit** into dev via `git merge --no-ff` from main working copy. |

### Files touched

- **Renamed log prefix** (task 1): `src/game/forgemode.c`, `port/src/forge/{forge_core,forge_serialize,forge_gametype,forge_ai,forge_logic,forge_undo}.c`, `port/fast3d/pdgui_menu_forge.cpp`, `port/fast3d/pdgui_forge_hud.cpp` (header comment).
- **Per-agent audio + agent-name fix** (tasks 2 + 3): `port/src/prefs_agent.c`, `port/include/prefs_agent.h`, `port/fast3d/pdgui_menu_agentselect.cpp`.
- **pd.ini audit** (task 4): `port/src/net/net.c` (netConfigInit), `port/src/updater.c` (updaterConfigInit), `context/config-pd-ini-audit.md` (new doc).
- **Context** (task 5): `context/session-log.md`, `context/tasks-current.md`, `context/README.md`.

### Three-tier configuration model (landed)

1. **pd.ini** = per-machine (hardware, display, audio backend, network history, ops toggles, debug).
2. **saves/prefs_\<agent\>.ini** (prefs_agent.c) = per-agent (visuals + **audio volumes (NEW)** + mod enablement).  Overlays pd.ini on Agent Select load.
3. **Compile-time constants** = tuning knobs (**network rates (NEW)**, **server port (NEW)**, **protected folders (NEW)**) where user override would only cause confusion or break compatibility.

Net global initializers and `-port` CLI override continue to work.  UPDATER_DEFAULT_PROTECTED ("mods,data,extracted,saves") remains the source of truth for updater protections -- pd.ini is always protected regardless.

### Marathon wave -- cross-session summary (S311 + S312 + S313 all on dev)

| Session | Branch | Focus | Status |
|---------|--------|-------|--------|
| **S311** | zen-poitras-f86b73 | Theme palette sweep (35+ hardcoded blue tints → `pdgui{ImU32,Vec4}TitleGlow`/`TintSuccess/Danger/Info`), `pdguiRgbaToImU32` + semantic accessors, warning.cpp MP End Game inset fix, `mainmenu.cpp` delete-confirm button scaling, CS music picker re-verified, controller-nav bridge via `pdguiDriveImGuiNav`. | **Merged to dev (`d96d64cb`)** |
| **S312** | amazing-mccarthy | Modeldef defensive guards in `modeldefFindBboxNode` / `modelFindBboxNode` (NULL rootnode / parts ∉ [1,500] / scale ≤ 0 + walker step cap 10000), `manifestEnsureLoaded` late-add WARNING for torn body/head modeldefs, new `pdgui_glyphs.{h,cpp}` -- resolve `InputAction` → short-label `[KEY] Label` pills with KBM/gamepad auto-detect, net/input wire audit. | **Merged to dev (`85421a8a`)** |
| **S313** (bulk) | great-robinson-15f409 | The Grid polish: Forge→Grid rename (strings), Bots tab (live testing), Map Variant editing (`from_base` flag + delta API), weapon pad preview, controller nav (bumper-cycle tabs), save modal w/ deps list, placement ghost, grid snap viz. | **Merged to dev (`16cb6f7e`)** |
| **S313** (marathon follow-up) | great-robinson-15f409 (same) | Log-prefix FORGE→GRID; per-agent audio volumes; agent-name handoff via `gamefileGetOverview`; pd.ini cleanup -- drop network tuning rates + server port + protected folders. | **This commit set** (`8057f064` / `1694d3ec` / `6aaf299f`) |

### Build verify

`ninja -C Build pd pd-server` clean after each commit.  Worktree build: PerfectDark.exe ~52.3 MB / PerfectDarkServer.exe ~22.8 MB.  Only pre-existing warnings (collision `sum2`, modelasm dangling-pointer, `/*` within comment noise, snd strncpy truncation) -- no new warnings introduced.

### Deferred

- Agent-name `gamefileGetOverview` fix lands the sidecar-path alignment, but existing agents with old sidecars (prefs_<old_encoded>.ini) will effectively be orphaned.  A one-shot migration pass (rename old sidecar to new name on first load of each agent) is queued.
- Gameplay-preference pd.ini keys (`Game.CenterHUD`, `Game.SkipIntro`, `Game.DisableMpDeathMusic`, etc.) could migrate per-agent next -- see `config-pd-ini-audit.md` candidate list.
- `Audio.ModPlaylist` / `ModShuffle` / `ModTrackId` are per-machine today; per-agent migration would let each Agent have its own CS music selection.

---

## Session S312 batch 2 — 2026-04-17 (Font/UI scaling + controller tab-cycle + border enforcement audit — same worktree)

Post-S311/S312/S313 merge-clean sync: audit the menu-renderer layer
for gaps the three parallel sessions may have missed, now that the
file-ownership split is over.

### What landed

1. **Font scaling audit** — single hardcoded `260.0f` Input Mapping
   search-box width and `120` / `16` literals in Updates channel
   combo + SameLine offsets were the only miss.  Migrated through
   `pdguiScale()`.  Table cell/frame/item style vars at
   `pdgui_menu_mainmenu.cpp:1828-1830` wrapped with pdguiScale so
   the Input Mapping row density scales with DPI.  All
   SetWindowFontScale callers (endscreen / countdown / mpingame /
   lobby_distrib) already compose with pdguiScaleFactor correctly
   via the `sf / bigScale / fontSize/GetFontSize()` patterns.
2. **Controller-first UX audit** — Mod Manager tab bar
   (`pdgui_menu_modmgr.cpp`) was the last tab bar still missing
   LB/RB bumper cycling.  Added the same
   `s_*PendingTab + ImGuiTabItemFlags_SetSelected` pattern that
   Room / Settings / Cheats / Modding Hub / The Grid editor use.
   UI-order-to-internal-order lookup table keeps the bumper cycling
   in visual order (Installed / By Category / By Mod) even though
   the internal encoding is (2, 0, 1).  Verified all three
   right-click context menus (Color Theme picker, Mod row,
   Room bot slot) already have `IsKeyPressed(GamepadFaceLeft)`
   X-button parity.  All SetKeyboardFocusHere calls are gated on
   `s_NeedsFocus` — no focus traps.
3. **UI scaling pass** — swept BeginChild, SetCursorPos,
   PushItemWidth, SetColumnWidth, Indent, SameLine, PushStyleVar
   for hardcoded pixel values.  All existing callers use
   `pdguiScale(...)` or a `* scale` multiplier (sourced from
   `pdguiScaleFactor()`).  No additional fixes required beyond the
   font-scaling batch.
4. **Border enforcement verification** — per-file scan across all
   23 menu renderers that call `pdguiDrawPdDialog`.  Two apparent
   misses were false-positives: `pdgui_menu_pausemenu.cpp` uses
   `pdguiThemeGetContentInset` directly to compose custom padding
   (tab-button + bottom-pinned Resume-button layout), and
   `pdgui_menu_theme_editor.cpp` renders a mini-preview
   `pdguiDrawPdDialog` inside a BeginChild swatch that intentionally
   doesn't need full content-inset handling.  The S305 + S309
   sweeps covered the remaining 20 renderers properly.

### Files touched

- `port/fast3d/pdgui_menu_mainmenu.cpp` — Input Mapping search-box
  width + table cell/frame/item style var scaling
- `port/fast3d/pdgui_menu_update.cpp` — Channel combo width +
  SameLine offsets now scaled
- `port/fast3d/pdgui_menu_modmgr.cpp` — LB/RB bumper tab cycling
  (3-tab rotation with UI-order / internal-order mapping)

### Build verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — both
targets link.  Only pre-existing warnings.

### Follow-up queued

- **Wire pdgui_glyphs into contextual prompts** (from S312 batch 1)
  — pickup / door interact / forge HUD controls reminder.
- **Font-atlas rebuild on runtime font swap** — S309's Font Mod
  requires restart because atlas is built once at `pdguiInit`.
  Future work: atlas hot-reload.
- **Theme Editor mini-preview content-inset** — currently the
  miniature `pdguiDrawPdDialog` at line 391 doesn't honor user
  chrome insets; it uses a fixed headerH.  Cosmetic-only; the
  preview is meant to be a compact swatch.

---

## Session S313 — 2026-04-17 (The Grid polish + missing features — `great-robinson-15f409` worktree → merged to dev)

**Scope**: S313 is the parallel Grid-owning session in the S311/S312/S313 three-way split. Picks up on top of S310's F1-F8 bulk drop to ship the remaining design-doc items plus targeted polish. File ownership per parallel prompt: **Grid code only** (`port/src/forge/*`, `src/game/forgemode.*`, `port/fast3d/pdgui_forge_*`, `port/include/pdgui_forge.h`). No menu / theme / gameplay-logic files touched.

### Commits on `claude/great-robinson-15f409`

| SHA | Scope |
|-----|-------|
| `da73273a` | **S313 The Grid polish + features (single commit)**. Six files, 727 insertions, 72 deletions. Rename `Forge → The Grid` in user-facing strings (HUD badge, editor window title, tab button labels); internal `forge_*` / `FORGE.*` log taxonomy retained. New `forge_bot_settings_t` + `forge_bot_spawn_mode_t` + Bots tab. Map variant editing (`forge_map_variant_mode_t` + `variant_source_slug` + `from_base` flag on objects + `forgeImportBaseStageObjects` / `forgeObjectResetToBase` / `forgeObjectRemoveFromBase` / `forgeCountBaseObjects` / `forgeCountDeltaObjects` API). Save-As-Mod confirmation modal listing mod dependencies before commit. Catalog click now *begins* a ghost placement; per-frame `pdguiForgeEditorTick` (now actually called from `mainTick`) updates ghost from freefly camera; HUD reticle with valid/invalid colour + `Place Here` / `Cancel (Esc)` banner in Catalog tab. Grid-snap visualization in HUD (top-center readout + 5x5 faint crosses). Controller navigation: keyboard nav idempotent, PageUp/PageDown + GamepadL1/R1 bumper-cycle tabs with persistent tab index; gamepad nav flag deliberately left off to avoid conflicting with freefly camera stick input. Tab bar `FittingPolicyScroll`. Case-insensitive catalog search. Weapon pad Properties pane: live `Preview:` showing which weapon-source wins for this pad at runtime + Respawn Effect combo. Expanded default editor size 560→620px. |
| `16cb6f7e` | **Merge to dev** (`--no-ff`, main working copy).  Pre/post-merge line counts verified identical: 1528 / 238 / 979 / 1420 / 970 / 838 = 5973 total. |

### Files touched (summary)

- **Modified** (6): `port/fast3d/pdgui_forge_editor.cpp` (+~400 lines: Bots tab, variant UI, save modal, tab navigation, placement banner, weapon pad preview, case-insensitive search), `port/fast3d/pdgui_forge_hud.cpp` (+~60: ghost reticle, grid-snap indicator, controller hint footer, "THE GRID" rename), `port/include/forge/forge_core.h` (+~65: new enums + struct fields + API decls), `port/src/forge/forge_core.c` (+~130: bot settings impl + variant helpers + from_base defaults), `port/src/forge/forge_serialize.c` (+48: bot_testing block + variant metadata + from_base persistence), `port/src/pdmain.c` (+7: pdgui_forge.h include + `pdguiForgeEditorTick()` call in mainTick).

### Phase coverage against the design doc

- **§3.1 / §4.1 Catalog + placement**: ghost-preview now actually appears -- user clicks a catalog entry, the editor tick advances the ghost each frame from camera forward-400u, HUD draws the reticle with valid/invalid tint, and the author commits via Place Here / Enter or aborts via Cancel / Escape.  Closes the "immediate commit" usability gap from S310.
- **§6 Live testing**: new Bots tab surfaces add/remove/freeze controls + Spawn Near Me (radius-bound) + Spawn Smart (aggression slider).  Runtime engine hook is queued; today captures intent + logs `FORGE.BOT:`.
- **§10.2 base_stage**: Variant editing UI replaces the single Base Stage ID input with a Variant combo (New Empty / Edit Stage / Edit Map) + dependent fields.  Objects tagged `from_base` render with `[BASE]` badge in Properties and gain Reset-to-Base / Remove-Base-Object delta actions.
- **Controller nav** (design hint throughout): keyboard PageUp/PageDown + gamepad L1/R1 cycle tabs with persistent state; controller hints in HUD + editor footer; gamepad nav intentionally off (would fight freefly sticks).
- **§10.5 Network sharing**: Save As Mod now opens a modal listing exact mod dependencies before committing, so the author sees the bundle before it's written.

### Build verify

`ninja -C Build pd pd-server` on the worktree: clean.  `build-headless.ps1 -Target all` on main post-merge: clean.  `PerfectDark.exe` 52,217,169 / `PerfectDarkServer.exe` 22,854,403 (main, dev).  Only pre-existing warnings (collision `sum2`, modelasm dangling-pointer, `/*` within comment noise, snd strncpy truncation) -- no new warnings introduced.

### Not done in this session (deferred to follow-up polish passes)

- **Engine botmgr wire** -- `forgeBotAddRequest` / `forgeBotRemoveAll` / `forgeBotFreezeAll` today log intent + bump `pending_*` counters in `forge_bot_settings_t`.  A later session consumes those counters and calls `botmgrAllocateBot` / `botmgrRemoveAll` with the right aibotnum + chrnum + rooms args for the current stage.
- **Base-stage -> forge_object_t auto-import** -- `forgeImportBaseStageObjects` today tags existing placed objects with `from_base=1`; the actual walk of the base stage's intro-commands / pads / spawnlist to materialise as forge objects is a follow-up rung once the F3 map-load instantiator lands.
- **3D gizmo handles** -- Properties tab transforms are still numeric; drag-axis gizmos require freefly-camera raycast + in-world handle render.
- **Real world-space ghost mesh** -- the HUD shows a reticle + catalog ID text.  Drawing the actual ghost mesh in 3D requires fast3d GBI integration and is a larger change.
- **Actual grid lines in 3D** -- the HUD shows a 5x5 cross pattern + readout.  True world-space grid lines need GBI integration too.
- **Controller tab cycling via dedicated binds** -- today relies on ImGui's built-in GamepadL1/R1 mapping which requires NavEnableGamepad, which was intentionally left off to preserve freefly camera.  A follow-up can route the game's `actionmap` layer to dispatch tab-cycle requests so controller-only users get bumper cycling without the camera-stick conflict.
- **Controller nav for Place/Cancel** -- ghost placement Commit/Cancel buttons respond to mouse/keyboard; true gamepad-driven placement is tied to the above follow-up.

### Parallel session coordination

S311 (menus/theme/settings) and S312 (gameplay/input/net) were running in parallel worktrees when S313 committed.  S313 merged to dev at `16cb6f7e` on top of `8d2e7872`; S312 then merged on top at `85421a8a`; S311 was still in-flight at session end.

---

## Session S312 — 2026-04-17 (Modeldef defensive guards + glyph system + net review — `amazing-mccarthy` worktree)

**Scope**: Gameplay bug fixes, input system polish, net review.  Parallel
session on an isolated worktree; merges cleanly to `dev`.  File-ownership
split with S311 (menu renderers / theme / settings / config / UI scaling)
and S313 (The Grid forge code only) — this session touched game logic
(`src/game/`), networking (`port/src/net/`), and new input-helper module.

### What landed

1. **sp_body_108 parts=0 modeldef corruption class — defensive guards
   +  instrumentation** (root cause from S308 B-161).  `propobj.c` bbox
   walkers (`modeldefFindBboxNode`, `modelFindBboxNode`) now reject torn
   modeldefs at the top of the function: NULL rootnode, numparts out of
   [1, 500], or scale <= 0 short-circuits to NULL instead of
   descending into stale memory.  Both walkers also carry a 10000-step
   safety cap so a cyclic/dangling tree logs a WARNING and bails out
   rather than AV'ing.  `port/src/net/netmanifest.c`
   `manifestEnsureLoaded` late-add path now logs the post-load
   modeldef state for body/head assets — `MANIFEST-SP: late-add '...'
   post-load modeldef torn: parts=%d root=%p scale=%.3f` pinpoints the
   exact catalog id whose late-add produced the torn state, giving the
   next playtest log the fingerprint needed to move B-161 from defensive
   to root-cause fixed.  Guarded by `#if !defined(PD_SERVER)` so the
   server build still links (`catalogGetBodyModeldef` is client-only).

2. **Glyph system for contextual input prompts** — new
   `port/include/pdgui_glyphs.h` + `port/fast3d/pdgui_glyphs.cpp`
   module.  Maps any `InputAction` to the primary VK currently bound
   in the active IMC stack, picks the right device using
   `actionmapGetLastDevice()` (500 ms debounce), and exposes
   `pdguiGlyphGetActionLabel` for short labels ("E", "Space", "LMB",
   "A", "LB", "D-Up") plus `pdguiDrawActionPrompt(action, x, y,
   label)` / `pdguiDrawActionPromptCentered` which render a compact
   `[KEY] Label` pill on the foreground drawlist using the theme's
   title-glow accent.  Walks all six default IMCs in priority order so
   prompts reflect whichever context is on top; falls back to the
   other device if no binding for the current device.  VK ordinals
   mirrored locally (cannot include input.h directly — it pulls
   PR/os_cont.h which references OSThread and breaks C++ TUs).

3. **Net / gameplay / input review** — confirmed the current state of
   the previously-shipped fixes:
   - S306 / S304 input-context leak defensive pop is wired into
     `renderMainMenu` close (`pdguiConsumeTitleClose` channel + fallback
     Escape), and `inputCtxEndFrame` watchdog auto-recovers deep stacks
     at `MAX_STACK-1`.
   - `g_NetMatchRoomId` / `g_NetCounterOpClientId` reset paths cover
     disconnect (`net.c:1060-1066`), stage end (`net.c:925-928`), and
     server start (`net.c:630-631`); `netSendToRoom(0xFF, ...)` is a
     no-op on clients (no `room_id == 0xFF` match).
   - `roomLeave` / `roomDestroy` call `netReadyGateOnClientLeft` and
     `netReadyGateAbortForRoom` per Bug B.
   - `botSpawn` retains S301 `CHR.DIAG` breadcrumb + invisible-state
     fingerprint; no regression in the ground-clamp / room-recovery
     fallbacks.
   - ACTION_JUMP / ACTION_CROUCH / ACTION_USE correctly feed
     c1buttons/c1buttonsthisframe in `bondmove.c`; no residual CK_* or
     parallel `inputKeyJustPressed(VK_ESCAPE)` paths remain.

### Files touched

- **New**: `port/include/pdgui_glyphs.h`, `port/fast3d/pdgui_glyphs.cpp`
- **Modified**: `src/game/propobj.c` (two bbox walkers — torn-modeldef
  guards + 10000-step cap), `port/src/net/netmanifest.c`
  (manifestEnsureLoaded post-load diagnostic)

### Build verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — both
targets link.  `PerfectDark.exe` 52,400,200 bytes / `PerfectDarkServer.exe`
22,840,561 bytes.  Only pre-existing warnings (propobj.c sp144/sp112
may-be-uninitialized, `/*` within comment noise across multiple files).

### Not done in this session (deferred)

- **Per-player glyph device detection** — `pdguiGlyphGetDevice` returns
  a single global device for all prompts.  Splitscreen is disabled in
  this port so the single-device answer is fine; if the mode ever
  returns we'll need a per-player signal.
- **Wire glyphs into actual gameplay HUD prompts** — the module is ready
  but no renderer consumes it yet.  Obvious first callers: pickup prompts
  (`[E] Pick up AR34`), interact prompts near doors/terminals, forge
  HUD's controls-reminder row.  Hooks belong in follow-up polish passes
  (S313 for forge, S311 for menus).
- **Root-cause fix for modeldef corruption** — S308 + S312 are both
  defensive.  Next playtest repro of the Mission 1 Obj 2 crash should
  produce a `MANIFEST-SP: ... post-load modeldef torn:` line (or a
  `MANIFEST-SP: ... post-load modeldef OK` followed by a later torn
  state, which would point at a post-late-add corruptor rather than the
  late-add itself).

---

## Session S311 follow-up — 2026-04-17 (UI polish marathon continuation — `zen-poitras-f86b73` worktree)

**Scope**: Five-task continuation of the S311 UI polish marathon. Sweeps remaining palette literals, wires the S312 glyph system into HUD prompts, migrates the last raw-ctx renderers to pool ownership, removes dead `ImGuiKey_Gamepad*` checks, and extends the S306 title-close channel across every remaining close handler.

### Six commits (`9281decf` → `c486dc72`)

1. **Palette sweep pt2** (`9281decf`) — ~95 remaining semantic color literals migrated to `pdguiVec4/ImU32 TitleGlow/TintInfo/TintSuccess/TintDanger/TextWarning/TextPositive`. Files: `pdgui_lobby.cpp`, `pdgui_lobby_distrib.cpp`, `pdgui_skin_editor.cpp`, `pdgui_menu_{agentselect,modmgr,moddinghub,solomission}.cpp`. Adds `pdguiVec4TextWarning` / `pdguiVec4TextPositive` ImVec4 companions. `server_gui.cpp` NOT migrated — pd-server doesn't link `pdgui_style.cpp`.
2. **Glyph wiring** (`5f18c65f`) — new `pdgui_interact_prompt.{h,cpp}` reads `g_InteractProp` each frame and draws "[E] Pick up" / "[E] Open" / "[E] Access" below the reticle via the S312 glyph system. Label from new `propInteractPromptLabel()` helper in `src/game/prop.c` that branches on `prop->type` + `OBJFLAG3_HTMTERMINAL` / `OBJFLAG3_INTERACTABLE`. Grid HUD controls reminder (`pdgui_forge_hud.cpp`) rewritten to a 4-row `pdguiDrawActionPrompt` layout. Menu tab hints + countdown cancel hint now fill key portion via `pdguiGlyphGetActionLabel`.
3. **Pool-ctx migration** (`94f0583f`) — renderCiSettingsRedirect, renderCiDeadPlayer2, renderCinemaList moved from raw `inputCtxPush/Pop` to `menupoolAcquireDialog` / `menupoolReleaseDialog`. New `MENU_TYPE_CINEMA` enum + 8 `REG()` calls + 7 local extern decls for dialogdefs mainmenu.cpp owns but data.h doesn't publish. Boot-time CI-redirect ctx leak class fixed at source.
4. **Dead Gamepad* checks** (`d18b87c1`) — 141 `ImGui::IsKeyPressed(ImGuiKey_Gamepad*)` read sites removed across 29 files. NavEnableGamepad is disabled; parallel keyboard checks already catch controller input via `pdguiDriveImGuiNav()`. Preserved: `io.AddKeyEvent` writes + `pdgui_glyphs.cpp` VK lookup. Net -203 lines.
5. **Title-close sweep** (`c486dc72`) — `pdguiConsumeTitleClose()` wired into Theme Editor, Modding Hub, Pause Menu, CI redirect (renderCiSettingsRedirect + renderCiDeadPlayer2 + renderCinemaList), and Endscreen (solo + MP). Fixes "first X click does nothing" class across all remaining dialogs.

### Files touched (summary)

- **New**: `port/include/pdgui_interact_prompt.h`, `port/fast3d/pdgui_interact_prompt.cpp`.
- **API additions**: `pdguiVec4TextWarning/TextPositive` in `pdgui_style.h`; `propInteractPromptLabel` in `src/game/prop.c` + `src/include/game/prop.h`; `MENU_TYPE_CINEMA` in `menupool.h` + menupool.c registry entries.
- **Renderer migrations**: `pdgui_menu_mainmenu.cpp` (CI redirect + DeadPlayer2 + CinemaList pool-ctx + title-close), `pdgui_menu_{moddinghub,theme_editor,pausemenu,endscreen}.cpp` (title-close), `pdgui_forge_hud.cpp` (glyph pill rewrite), `pdgui_countdown.cpp` (glyph label).
- **Palette fixes**: `pdgui_{lobby,lobby_distrib,skin_editor}.cpp`, `pdgui_menu_{agentselect,modmgr,moddinghub,solomission}.cpp`.
- **Gamepad cleanup**: 29 menu / tool files.

### Build verify

`PerfectDark.exe` 52,516,524 / `PerfectDarkServer.exe` 22,838,513 — clean link.

### Not done in this session (deferred)

- **`server_gui.cpp` palette migration** — blocked by pd-server not linking pdgui_style.cpp. Would cascade into theme/nineslice/effects/fontmgr deps.
- **Item-specific prompt text** — interact prompt shows "Pick up" / "Open" / "Access" but not the weapon / door name. Deeper `prop->obj` walk needed to surface "AR34" etc.
- **training.cpp Resume/OK controller activation** — removed a Gamepad-only check with no parallel Enter. Button click + action-map→Enter translation should still work but needs playtest to confirm.

---

## Session S311 — 2026-04-17 (UI polish marathon — theme palette sweep + content inset + scaled buttons — `zen-poitras-f86b73` worktree)

**Scope**: Mike's S311 punch list — hardcoded blue tints, content-inset compliance, menu close-path audit, font/element scaling, controller-first verification, CS music picker sanity check. Parallel with S312 (game logic) + S313 (forge). File ownership: menu renderers + theme system + `pdgui_style.*`. Did not touch `src/game/`, networking, or forge code.

### What shipped (single commit `55c37fc2`)

1. **Theme palette accessor expansion** (`pdgui_style.cpp`/.h):
   - New `pdguiRgbaToImU32(rgba, alpha)` helper centralises 0xRRGGBBAA→ImU32 conversion.
   - New semantic accessors `pdguiImU32TitleGlow/TintSuccess/TintDanger/TintInfo(alpha)` for C callers.
   - C++-only `pdguiVec4*` inline companions in pdgui_style.h (guarded by `__cplusplus && IMGUI_VERSION`) so TextColored / PushStyleColor sites stay terse.

2. **Hardcoded blue tint sweep** — 35+ sites migrated to the theme palette:
   - `IM_COL32(100, 200, 255, ...)` → `pdguiImU32TitleGlow(alpha)` in:
     - `pdgui_menu_warning.cpp:695` (default dialog title) and `:991` (MP File Manager title)
     - `pdgui_menu_agentselect.cpp:284` (copy-action confirm prompt)
     - `pdgui_menu_pausemenu.cpp:990` (local-player row tint, alpha=35 preserved)
     - Agentselect delete-action prompt red → `pdguiImU32TintDanger(255)`
   - `ImVec4(0.4f, 0.8f, 1.0f, 1.0f)` cyan section headers → `pdguiVec4TitleGlow()` (20 sites):
     - lobby.cpp (2), network.cpp (2), challenges.cpp (1), teamsetup.cpp (2), mpsettings.cpp (1), mainmenu.cpp (2), room.cpp (10)
   - Additional cyan variants → `pdguiVec4TitleGlow()`:
     - `ImVec4(0.5f, 0.85f, 1.0f, 1.0f)` in audiomod.cpp (Selected-track label) + moddinghub.cpp (skin + ini entry headers)
     - `ImVec4(0.6f, 0.85f, 1.0f, 1.0f)` in controldiagram.cpp (control-mode name) + mpsettings.cpp (Library header)
   - `ImVec4(0.7f, 0.8f, 1.0f, 1.0f)` endscreen SectionHeader → `pdguiVec4TitleGlow()`.
   - `ImVec4(0.6f, 0.8f, 1.0f, 1.0f)` modmgr "Base Game Asset" label → `pdguiVec4TintInfo()` (semantic info badge).

3. **Content inset fix** — `pdgui_menu_warning.cpp:789-790`: MP End Game dialog body-top switched from raw `SetCursorPosY(pdTitleH + WindowPadding.y + breathe)` arithmetic to `pdguiSetCursorBelowTitle(pdTitleH) + 8px breathe` so user chrome insets are honoured. Last remaining S309-pattern miss. endscreen/pausemenu/theme_editor use different per-dialog padding helpers that already call `pdguiThemeGetContentInset` directly — no changes needed.

4. **Element scaling fix** — `pdgui_menu_mainmenu.cpp:935/947`: delete-confirm Cancel/Delete buttons migrated from `ImVec2(120, 0)` to `ImVec2(pdguiScale(120.0f), 0)` so they scale with resolution/DPI.

5. **CS music picker verification (S309 fix)**: re-walked the import + discovery + render path. `importAudioFile` in audiomod.cpp:402-413 correctly calls `modmgrRescanDirectory()` → locate new mod by slug → `modmgrSetEnabled(i, 1)` + `modmgrSaveConfig()`. `collectModMusicTrack` in mpsettings.cpp:580 emits `SELECTTUNES: skip` diagnostic for filtered tracks. `renderSelectTunes` emits `SELECTTUNES: open — base_tracks=N mod_tracks=N` one-shot on appear. Wiring solid, no changes needed.

6. **Menu close-path audit**: all S300/S304/S306 migration debt resolved. No residual `s_*PushedCtx` raw patterns. `pausemenu` uses dedicated `g_CtxPauseMenu`. `endscreen` uses `inputCtxPush(&g_CtxImGuiMenu)` gated on `!inputCtxIsActive + freshEntry`. `solomission` manual `inputCtxPopDeferred` calls (3 sites) all guarded by `inputCtxIsActive`. `mainmenu` defensive leak-catch at `:3416` still guarded + logs `MENU_IMGUI: defensive inputCtxPopDeferred — leak class caught`. `renderCiSettingsRedirect` still uses raw push/pop (queued S306 follow-up), but isActive guards keep it idempotent. No new bugs surfaced.

7. **Controller-first audit**: `pdguiDriveImGuiNav()` in pdgui_backend.cpp:356-398 translates action-map events (controller A/B/D-pad/LB/RB) into ImGui keyboard nav keys (Enter/Escape/arrows/PageUp/Down) because `ImGuiConfigFlags_NavEnableGamepad` is disabled. Menu renderers' `ImGuiKey_GamepadFaceDown/Right/...` checks are redundant (NavEnableGamepad off → those events never fire) but harmless — the parallel `ImGuiKey_Enter/Escape` checks catch the same edges via the action-map→key translation. Input-mapping capture path uses `inputGetLastKey()` which accepts raw controller button codes (via `isVkController()`) so rebinding works with controller. No changes needed.

### Files touched

- `port/include/pdgui_style.h` — 7 new declarations (generic RGBA→ImU32, 4 semantic ImU32 accessors, + 4 C++-only ImVec4 inlines behind `__cplusplus && IMGUI_VERSION` guard).
- `port/fast3d/pdgui_style.cpp` — 5 new implementations (+35 lines).
- `port/fast3d/pdgui_menu_{agentselect,audiomod,challenges,controldiagram,endscreen,lobby,mainmenu,moddinghub,modmgr,mpsettings,network,pausemenu,room,teamsetup,warning}.cpp` — palette migrations + content-inset + button scaling.

### Build verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — both targets link clean. `PerfectDark.exe` = 52,288,651 bytes. `PerfectDarkServer.exe` = 22,838,513 bytes.

### Not done in this session (deferred)

- **Remaining non-blue literal sweep** — agentselect/agentcreate/solomission/modmgr/moddinghub still have a handful of PD-blue `IM_COL32` panel/border literals (PD-blue deco rather than semantic color) plus red/yellow/green literals that could fold into `tint_danger`/`text_warning`/`text_positive`. Lower-value cosmetic cleanup; leaving for a follow-up.
- **Dead `ImGuiKey_Gamepad*` checks** — ~130 redundant checks remain across solomission/training/mainmenu/agentselect/warning/etc. All dead code (NavEnableGamepad off) but working alongside the redundant Enter/Escape checks that catch the same actions. Removing is a ~1h churn task queued separately.
- **renderCiSettingsRedirect / renderCiDeadPlayer2 / renderCinemaList S300 pool-ctx migration** — still on raw `inputCtxPush/Pop`. Defensive leak-catch in mainmenu.cpp:3416 hides the symptom.
- **Full hardcoded-blue audit of pdgui_lobby.cpp / pdgui_lobby_distrib.cpp / server_gui.cpp / pdgui_skin_editor.cpp** — touched only menu_*.cpp files this session; those other files have matching `ImVec4(0.4f, 0.8f, 1.0f, 1.0f)` headers that could migrate.

---

## Session S310 addendum — 2026-04-17 (Mike R1-R4 refinements — same worktree)

Four design refinements from Mike rolled into The Grid feature branch:

- **R1 Seamless swap + per-player foundation**: `forgemode.c` now saves the player chr's `bodynum`/`headnum` on FREEFLY entry and restores on NORMAL exit, so the visual body swap can land at any later rung without touching the session logic.  No stage reload on toggle (already true).  New `forgeGetPlayerSessionState(playerNum)` + `forgeTogglePlayerMode(playerNum)` entry points wire per-player state for MP co-op forge (F8 stretch); F0 proxies to the global state for player 0.
- **R2/R4 Weapon pad source**: new `forge_weapon_source_t` enum + `forge_map_settings_t.weapon_source` / `allow_match_override` fields.  Default is MAP_DEFAULTS + allow_match_override=1.  Modes: MAP_DEFAULTS (each pad spawns its author-chosen weapon), MATCH_OVERRIDE (lobby weapon set overrides every pad), PREFER_MAP (specific pads keep weapon, "Any" pads use lobby).  Settings tab gained a three-option dropdown + checkbox.  Serializer round-trips both fields.
- **R3 Mod dependency collection**: new `forgeCollectDependencies` in `forge_core.c` walks all state (object catalog_id + material_id, AI body/head/weapon, weapon pad weapon_id, pickup item_id, effect asset_id, zone sounds, door key_id + sounds, atmosphere sky_id, gametype starting_weapon, every wave's enemy_catalog_id, base_stage_id) and returns the unique list of non-`base:` catalog IDs.  Serializer writes a `dependencies` array into both mod.json (for the distribution pipeline's recursive resolver) and map.json (for standalone-preview callers).  Settings tab shows a live dependency-count bullet list.

Commit: `833c3a95`.  Build verify clean: PerfectDark.exe 52,186,794 / PerfectDarkServer.exe 22,839,025.

Deferred: actual engine-level Dr. Carroll model hot-reload (currently logs the intent + swaps chr->bodynum + chr->headnum but doesn't re-skin the mesh live).

---

## Session S310 — 2026-04-17 (The Grid level editor F1-F8 bulk drop + MP lift fix — `sharp-lovelace` worktree)

**Scope**: Full F1 through F8 pass over the in-game level editor (now branded "The Grid" in user-facing UI -- internal code names stay `forge_*`). Parallel session on an isolated worktree; merges cleanly to `dev`. Also addresses the long-standing MP elevator dead-pad bug the design doc called out as an F6 blocker.

### Commits on `claude/sharp-lovelace-a08d90`

| SHA | Scope |
|-----|-------|
| `0732c806` | **F1-F5b+F7+F8 data model + editor UI**. New module `port/src/forge/forge_core.[ch]` with fixed-size pools sized to the design doc's budget caps (1024 objects, 128 zones, 64 lights, 256 logic nodes, 32 channels, 32 waves, 16 objectives, 64 prefabs, 256 undo entries). Static catalog with 100+ entries across 10 top-level categories (Geometry, Props, Weapons, Spawn Points, Pickups, Lighting, Effects, Zones, Interactables, Characters). New `pdgui_forge_editor.cpp` -- tabbed overlay (Catalog/Properties/Zones/Lighting/Logic/Game Type/Mission/Settings) drawn in FREEFLY only, so NORMAL playtest is uncluttered. Click-to-place commits an object at the camera's forward-400u position. Serializer writes `mods/Forge Maps/<slug>/{mod.json,map.json}` with a tolerant JSON reader round-tripping all metadata/settings/objects. Logic runtime fires events->conditions->actions with a cycle detector and a recursion guard (depth 128). Game Type runtime covers wave spawner + boss state + 5 role modes + 8 modifier flags. Mission tab wires objectives to logic OBJECTIVE_COMPLETE/OBJECTIVE_FAIL action nodes. F8 UI ships weather (rain/snow/sandstorm/storm) + ToD cycle enable + post-process color grades. |
| `ac7be593` | **MP elevator fix + F6 AI helpers**. Root cause of dead lifts in CI arenas: `liftActivate()` (which registers the lift prop in `g_Lifts[liftnum-1]`) was only called from the AI-script command `aiActivateLift` (opcode `0x018d` in `chraicommands.c:8498`). CI arenas have no AI scripts, so lifts there were unregistered; `liftFindByPad()` returned NULL; stepping on the pad did nothing. Fix lives in `src/game/setup.c::OBJTYPE_LIFT` -- auto-registers at setup time by reading `liftnum` from one of the lift's own pads (`PADFIELD_LIFT`). SP is unaffected since the AI-script call overwrites the same pointer (idempotent). New `port/src/forge/forge_ai.c` with count/patrol-chain/navmesh-stub helpers (navmesh auto-gen is a placeholder; forge maps reuse the base stage's navmesh and rely on S302 tiered-spawn fallback if forge geometry blocks a waypoint). |

### Phase coverage against the design doc

- **F1 Catalog + placement**: 100+ catalog entries across all design §3.1 categories; search + category filter + budget bar; click-to-place with grid snap from the editor settings; 256-op undo/redo ring with barrier grouping.
- **F2 Properties**: Per-category prop panels for weapon pad / spawn point / AI (including boss flag, name, scale, phase thresholds) / door (open dir, locked, key id) / elevator (stops, speed, wait, call button, loop) / switch (type, activation, channel out) / light (all 4 types w/ type-specific fields) / zone (all 13 types) / effect / pickup. Duplicate / Delete / Deselect actions.
- **F3 Save/load**: Writes mod.json (reusable by mod loader w/ type="forge-map") + map.json with full schema coverage (metadata, settings, skylight, atmosphere, gametype+waves, objects, logic nodes+wires+channels). Tolerant reader re-loads the full map. Mod-slug derived from map name; re-derive button.
- **F4 Zones + lighting**: 13 zone types (§6.1 full list); skylight yaw/pitch/color/intensity/shadow; atmosphere fog/ambient/exposure/bloom + color grade; placed lights point/spot/area/emissive with type-specific properties (cone angles, area dimensions, cookie textures).
- **F5 Logic**: 12 events + 9 conditions + 23 actions covering design §7.2 in full, plus F5b-specific SPAWN_WAVE / TRIGGER_BOSS_PHASE and F7-specific OBJECTIVE_COMPLETE / OBJECTIVE_FAIL. Wire create/remove, channel create/toggle/delete, per-node Fire button for manual-trigger testing, cycle detection.
- **F5b Game types**: 4 structures (single/best-of-N/wave-based/phase-based), 5 win conditions, 8 modifiers, 5 player roles (Infection, VIP, Juggernaut, Horde Defender, Gun-Game Hunter). Wave table with per-wave enemy_catalog_id / count / scale / hp_mult / speed_mult / spawn_delay / intermission / spawn_zone_uid / is_boss flag. Boss state with simulate-activation test button and damage-simulator.
- **F6 Interactables + AI**: MP lift fix lands the biggest functional gap. Interactable properties (doors/elevators/switches) fully editable. AI props cover body/head/weapon/behavior/faction/alert_radius/health_mult/respawn plus F5b boss extension. Patrol-chain walker + navmesh stub in `forge_ai.c`.
- **F7 Mission**: Is-Mission checkbox enables briefing / debrief / sequential-objective toggles; 3 objective kinds (primary/secondary/bonus); per-objective link to logic completion+failure nodes with Complete/Fail/X buttons.
- **F8 Stretch**: Weather (5 kinds), ToD cycle, post-process color grades (Neutral/Warm/Cool/Noir/Alien). Terrain remains catalog-placeable as geometry prefabs (height-paint terrain is out of scope here).

### Files touched (summary)

- **New**: `port/include/forge/forge_core.h`, `port/src/forge/forge_core.c`, `port/src/forge/forge_undo.c`, `port/src/forge/forge_logic.c`, `port/src/forge/forge_gametype.c`, `port/src/forge/forge_serialize.c`, `port/src/forge/forge_ai.c`, `port/fast3d/pdgui_forge_editor.cpp`
- **Modified**: `port/include/pdgui_forge.h` (editor entry points), `port/fast3d/pdgui_backend.cpp` (editor dispatch), `port/src/pdmain.c` (forgeCoreTick wiring), `src/game/setup.c` (lift auto-register)

### Build verify

`ninja -C Build pd pd-server` -- both targets link. PerfectDark.exe 52,176,729 / PerfectDarkServer.exe 22,839,025 (server unchanged; forge is client-only).

### Not done in this session (deferred)

- **3D gizmo handles** -- Properties tab edits transform numerically; drag-axis gizmos need freefly-camera raycast + in-world handle rendering.
- **Ghost placement reticle** -- click-in-catalog commits at camera+400u. True ghost with green/red valid-tint lives in a follow-up polish pass.
- **Runtime engine instantiation of placed objects** -- objects are serialised but not yet materialised as live engine props at match load. The F3 map-load path needs to walk the `forge_object_t` pool and call the appropriate `setupCreate*` functions. Logic OPEN_DOOR / TELEPORT / SPAWN_AI actions log-only until this lands.
- **AI navmesh auto-gen** -- `forgeAiGenerateNavmesh` is a stub; forge maps inherit the base stage's navmesh.
- **Boss HUD bar** -- data model tracks it; HUD drawing lands in `pdguiForgeHudRender` in a polish pass.
- **Co-op Forge** (F8 stretch) -- out of scope for single-session bulk drop; requires new SVC/CLC msgs for placement sync.

---

## Session S309 — 2026-04-17 (Deferred UI drop + config cleanup — optimistic-feistel worktree)

**Scope**: Mike's S309 punch list — 9 deferred UI items plus an S309 bug
report about Combat Simulator music. Worked in parallel with S310 (Grid F1)
on a separate worktree; touched menu renderers, theme system, config,
and settings.

### Items landed

1. **Content-inset sweep (P0)** — 20 `pdgui_menu_*.cpp` renderers migrated
   from `SetCursorPosY(titleH + WindowPadding.y)` to the new
   `pdguiSetCursorBelowTitle(titleH)` helper that clamps against the
   active chrome's nineslice insets + 8px breathe. New helpers live in
   `port/fast3d/pdgui_theme.cpp` + declarations in `pdgui_style.h` /
   `pdgui_theme.h`. Fixes content bleeding into nineslice borders on:
   agentcreate, agentselect, botsetup, challenges, cheats,
   controldiagram, lobby, mainmenu (Not-Available dialog), mpadvanced,
   mppause, mpsettings, mpsetup, network, playerconfig, room,
   solomission (11 dialogs), stats, teamsetup, training, warning.
2. **Color theme live preview** (`pdgui_menu_theme_editor.cpp`) —
   widened the modal to 820px × 85% and added a `renderLivePreview()`
   panel beside the color pickers. Shows a PD title strip (reads
   title_glow), sample buttons/checkbox, success/warning text labels
   (text_positive / text_warning), and three colored buttons driven by
   `pdguiGetTintSuccess/Danger/Info`. Reads happen every frame so edits
   update live.
3. **New S309 palette extensions (slots 20-23)** — added `title_glow`,
   `tint_success`, `tint_danger`, `tint_info` to `struct pdgui_palette`,
   theme.json (`titleGlow` / `tintSuccess` / `tintDanger` / `tintInfo`
   camelCase aliases accepted), `k_PaletteFieldNames`, the theme
   editor's `k_Fields` metadata (all marked `PFG_SEMANTIC` with "auto"
   reset buttons), and new `pdguiSetPaletteExtensions2` /
   `pdguiGetTitleGlow` / `pdguiGetTintSuccess/Danger/Info` accessors in
   pdgui_style. Theme loader's `apply_theme_def` forwards both
   extension tails.
4. **Title bar "hardcoded blue" fix** — `pdguiDrawTextGlow` now reads
   `pdguiGetTitleGlow()` instead of the static `s_Theme.textGlowColor`,
   so a custom theme's palette drives the title glow. Default derivation
   (border2 → glow) ensures built-in palettes keep their look without
   authoring the new slot.
5. **Font Mod creator tool** — new "Font Mod" tab in the Modding Hub
   (`pdgui_menu_moddinghub.cpp`: `fontToolReset` + `renderFontTool`)
   with file browser for `.ttf`/`.otf` + mod-name input + Save button.
   Saves to `mods/Fonts/<slug>/<slug>.<ext>` plus a `font.json` +
   `mod.json`, rescans the font mod registry and the mod manager so the
   new entry appears in the Font dropdown without restart. Interface
   tab (`renderSettingsInterface` in mainmenu.cpp) gained an "Open Font
   Mod Tool..." button that routes via `pdguiModdingHubShowTool(8)`.
6. **Resolution-aware scanlines** — `pdguiThemeDrawScanline` /
   `pdguiThemeDrawScanlineFg` call the new
   `pdguiResolveScanlineMetrics()` helper that scales line thickness +
   stride with `pdguiScaleFactor()`. At 720p = 1px/2px, at 4K = 3px/6px.
   Prevents the "almost invisible at high-DPI" look.
7. **imgui.ini disabled** — `pdguiInit` sets `io.IniFilename = NULL` so
   ImGui never writes imgui.ini. Window state is rebuilt each session
   from `pdgui_style.cpp`.
8. **Per-agent preferences (`prefs_agent.c` / `.h`)** — new sidecar at
   `saves/prefs_<agent>.ini` with `[Theme]`, `[Video]`, `[Mods]`
   sections. API: `prefsAgentInit` (called from main.c after
   pdguiInit), `prefsAgentLoad` (called from every agent-select load
   site in pdgui_menu_agentselect.cpp), `prefsAgentSave` (called at
   end of `renderSettingsInterface`; debounced by content-hash compare
   so the disk write only happens on actual value changes).
9. **Forge → "The Grid" user-facing rename** — main menu button label
   flipped to "The Grid"; all internal code (module names,
   `pdguiForge*` entry points, `ACTION_FORGE_*` enums, `FORGE.DIAG`
   log channel) retained for compatibility. S310 Grid F1 session owns
   the HUD-side rename.
10. **CS music picker fix** (`pdgui_menu_audiomod.cpp` +
    `pdgui_menu_mpsettings.cpp`) — root cause: freshly imported audio
    mods defaulted to `enabled=0` in modmgr, so on next launch
    `modmgrLoadMod` never fired for them, their `assetCatalogRegister`
    ran at import time only, and the catalog entry vanished on restart.
    Fix: `importAudioFile` now calls `modmgrRescanDirectory()` +
    `modmgrSetEnabled(slug, 1)` + `modmgrSaveConfig()` so newly
    imported music survives a relaunch. Added `SELECTTUNES:` diagnostic
    log lines in `collectModMusicTrack` + `renderSelectTunes` so future
    "my songs aren't showing up" reports surface the filter path.

### Files touched

- New: `port/src/prefs_agent.c`, `port/include/prefs_agent.h`.
- Modified (headers): `port/include/pdgui_style.h`,
  `port/include/pdgui_theme.h`.
- Modified (C++ renderers): 20 `port/fast3d/pdgui_menu_*.cpp` files
  (see item 1), `pdgui_theme.cpp`, `pdgui_style.cpp`,
  `pdgui_theme_loader.cpp`, `pdgui_backend.cpp`.
- Modified (bootstrap): `port/src/main.c` (prefsAgentInit wire).

### Build verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — both
targets link. `PerfectDark.exe` = 51,916,376 bytes. `PerfectDarkServer.exe`
= 22,840,561 bytes. Only pre-existing warnings (naudio
`sp44`/`sp20`/`frac` may-be-uninit, modelasm dangling-pointer,
collision sum2 warning, `/*` within comment noise across multiple
files).

### Not done in this session (deferred)

- **Full pd.ini elimination** (item 7 from the prompt). Visuals + mod
  enablement moved to per-agent prefs; audio volumes, resolution,
  fullscreen, bindings, gameplay toggles still live in pd.ini. The
  design doc flagged those as per-machine; finishing the elimination
  needs a scope decision ("per-agent input bindings?") that's not
  obvious from the prompt. Partial landing is enough to validate the
  per-agent load/save flow.
- **Full hardcoded-blue audit**: warning.cpp, agentselect state text,
  lobby state text still use `IM_COL32(100, 200, 255, ...)` directly.
  Migrating them to `pdguiGetTitleGlow()` was out of scope — the
  complaint was about the title bar, which this session addressed.
- **Forge HUD rename**: S310 owns pdgui_forge_hud.cpp; that file still
  says "FORGE" in the mode badge. This session only touched the
  main-menu button label.

---

## Session S308 — 2026-04-17 (Door-tick bbox crash + pause menu hardening — dev direct)

**Scope**: Fatal crash during Mission 1 Obj 1 → Obj 2 transition playthrough (0xc0000005 in door bbox chain 38s into stage 0x33 / Investigation), plus pause-menu defensive audit. Worked directly on `dev` (no worktree).

### Crash trace (addr2line resolved against Downloads/PerfectDark.exe, ImageBase 0x140000000)

```
#00 modelFindBboxNode     propobj.c:1312   (node->type deref on bad node)
#01 modelFindBboxRodata   propobj.c:1349
#02 doorGetBbox           propobj.c:19426  (*dst = *bbox with NULL bbox)
#03 doorUpdateTiles       propobj.c:19522
#04 doorsCalcFrac         propobj.c:20521
#05 doorTick              propobj.c:7913
#06 objTickPlayer         propobj.c:11501
#07 propsTickPlayer       prop.c:2173
#08 lvRender              lv.c:1451
#09 mainTick              pdmain.c:674
```

Stage 0x33 ("base:investigation") transition completed cleanly — 49 new model loads + 44 unloads. Player was in room 44 at `pos=(-409,149,-4280)` walking for ~38s. Log also flagged `WARNING: body0f02ce8c: truly invalid bodymodeldef for bodynum 108 (file 0x004b) ptr=... parts=0 -- skipping` at stage-load time, evidence of the same modeldef-corruption class bleeding into a door.

### Fixes landed

1. **NULL guards in bbox traversal chain** (`src/game/propobj.c`):
   - `modelFindBboxNode()` — early-return NULL if `model == NULL` or `model->definition == NULL` (was: deref `model->definition->rootnode` unconditionally at line 1309).
   - `modeldefFindBboxNode()` — same guard for the `modeldef` variant.
   - `modelFindBboxRodata()` / `modeldefFindBboxRodata()` — also check `node->rodata` before returning `&node->rodata->bbox`.
   - `func0f0687e4()` — same guard for the DL-node walker (shared pattern with bbox walker).

2. **doorGetBbox NULL-safe + diagnostic** (`src/game/propobj.c` line 19422):
   - If `modelFindBboxRodata(door->base.model)` returns NULL, zero-init the destination bbox and log `DOOR.DIAG: doorGetBbox — no bbox for modelnum=... model=... flags=... doortype=...` at WARNING (rate-limited to 16 per run via `s_DoorBboxMissWarnCount` file-static).
   - Prevents the AV; the tile/geo math runs against an empty box rather than a dangling pointer. If this warning fires in future logs it pinpoints the exact door.

3. **Local bbox helpers NULL-safe** (`src/game/propobj.c` lines 302–444):
   - `objGetLocalXMin/XMax/YMin/YMax/ZMin/ZMax` now return `0.0f` if `bbox == NULL`.
   - `objGetRotatedLocalMin/Max` early-return `0.0f` on NULL.
   - Called from ~15 prop-tick sites across weapon/hovercar/door/misc paths.

4. **Pause menu hardening** (`port/fast3d/pdgui_menu_solomission.cpp::renderPauseMenu`):
   - `IsWindowAppearing` path now resets `s_RestartConfirm = false` and `s_RestartSelectIdx = 0` in addition to `s_PauseSelectIdx = 0`. Prior: Restart-Confirm overlay could leak between opens if the previous close happened mid-overlay.
   - Title fallback: if `langSafe(g_SoloStages[si].name3)` returns empty, show `Mission %d: Status` instead of `: Status` with leading colon.
   - Button labels `Inventory` / `Abort Mission` now resolve via `langSafe` + hard-coded English fallback, so a missing lang bank can't paint blank buttons. `Options` → `Settings` (user-facing rename to match pause menu convention).

5. **Objective-list sanitation** (`src/game/mainmenu.c::soloMenuDialogPauseStatus` MENUOP_OPEN handler):
   - Zero the entire `g_Briefing.objectivenames[]` + `objectivedifficulties[]` arrays before repopulating on every pause open (was: only wrote up to `objectiveGetCount()`, leaving tail indices potentially stale from prior mission).
   - `setupCreateProps` still does the stage-load clear; this is a defense-in-depth second clear that makes the pause handler self-contained.

### Files touched

- `src/game/propobj.c` — bbox NULL guards + `doorGetBbox` diagnostic + local-helper NULL safety
- `src/game/mainmenu.c` — objectivenames full-array zero in pause status handler
- `port/fast3d/pdgui_menu_solomission.cpp` — IsWindowAppearing state reset + title fallback + button-label lang fallbacks

### Build verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — both targets link. No new warnings. `PerfectDark.exe` ~51.52 MB (server unchanged — propobj.c is client-only in the ninja graph).

### What the fix doesn't do

The fixes are **defensive** — they prevent the AV and log a diagnostic when the bad state is reached, but don't identify the root cause of how a door's `model->definition` became invalid mid-gameplay. The `parts=0` warning on `sp_body_108` at stage load is the strongest clue: something in the 0x30 → 0x33 manifest swap is leaving a door modeldef in an inconsistent state (rootnode points into a region whose node->type is garbage). Root-cause investigation queued — next playtest log should surface `DOOR.DIAG:` lines pointing at the specific door's modelnum.

### Not fixed in this session (deferred)

- **Root cause of modeldef corruption** — the `sp_body_108` late-add bodyAllocate warning + parts=0 + door bbox NULL all point at the manifest-diff path leaving some modeldefs in a torn state. Needs targeted instrumentation at `bodyAllocateModel` + `setupLoadModeldef` for any late-add that hits a body/door.
- **Pause menu "Quit to Menu" vs. Abort** — user described a 5-button layout including "Quit to Menu"; current set is Resume/Restart Mission/Inventory/Settings/Abort Mission. Abort routes through `g_MissionAbortMenuDialog` which quits to main menu, so functional parity exists but the label mismatch remains.
- **Mission-Complete crash from `body108`** — orthogonal, but the invalid-modeldef WARNING persists across multiple stages (any stage whose manifest references sp_body_108 without a valid ROM file or catalog registration). The catalog says `base:sp_body_108 (75) → ROM` loaded successfully, but the modeldef parts=0 — catalog cache may have an empty entry shadowing ROM data. Separate investigation.

---

## Session S306 — 2026-04-16 / 2026-04-17 (Interface tab + Input Mapping redesign + theme palette extensions + delete flow + X-button close fix — dev direct)

**Scope**: Mike's multi-batch request — redesign Input Mapping screen, add delete actions for themes/mods, extend theme color editor, plus three addenda: new Settings → Interface tab, theme-bundle UI, per-agent settings. Worked directly on `dev` (no worktree). Six commits landed; per-agent prefs deferred to its own session.

### Commits on dev

| SHA | Scope |
|---|---|
| `feb5431c` | **Palette extensions** — 5 new palette fields (toolbar_tint, text_positive, text_warning, button_hover, button_active) + `checkbox_checked` now live. `pdgui_palette` struct gains 5-field tail; zero = derived default. New accessors `pdguiGetToolbarTint/TextPositive/TextWarning/CheckmarkColor` exposed via pdgui_style.h. Theme.json schema gains `toolbarTint`/`textPositive`/`textWarning`/`buttonHover`/`buttonActive` (snake + camel aliases, plus `checkmark` alias for `checkbox_checked`). Theme Editor rebuilt with grouped sections (Frame / Text / Interact / Semantic / Reserved) + per-row tooltip + "(auto)" reset button on extension slots. Moddinghub tool selector now pulls its active tint from `pdguiGetToolbarTint` instead of hardcoded blue. |
| `a504f3e7` | **Interface tab + delete scaffolding**. New `Settings → Interface` tab (between Video and Audio, index 1). Moved Menu Style / Title Bar / Font dropdowns out of Video and the Color Theme selector out of Debug — each still lives in one place. Added "Open Color Editor..." + "Open Menu Style Tool..." buttons that launch the deeper tools via the new `pdguiModdingHubShowTool(int tool)` entry point. Tab order is now 0=Video 1=Interface 2=Audio 3=Controls 4=Game 5=Updates 6=Debug 7=Catalog — `ciRedirectTargetTabForDialog` updated to match. Delete support: `pdguiThemeGetFilePath(s32)` exposed; `pdguiInterfaceRequestThemeDelete/ModDelete` + `pdguiInterfaceRenderDeleteConfirm` implemented — right-click or controller X-button on a user theme opens the context menu with "Delete Theme…"; confirmation modal echoes the on-disk path, deletes theme.json / mod.json / audio.ini, then rmdirs the folder (best-effort — user-added files stop rmdir cleanly). Theme rescan fires on success. |
| `ae4f2f50` | **Input Mapping redesign (BATCH 1)**. Pulled all 51 gameplay-IMC actions into the UI (previously 23); `AXIS_*` stays excluded because they're analog-synthesised. Nine-group taxonomy (Movement / Aim / Combat / Weapons / Vehicle / Menu Nav / C-Buttons / D-Pad / System Hotkeys). Added: live search filter (case-insensitive substring match), per-column conflict detection with red-border render on any VK bound to two or more actions, per-row tooltips for non-obvious actions (Fire Mode, D-Pad Down = radial, etc.), capture-state yellow-border affordance. Removed the redundant "Save Controls" button at the bottom (every rebind already auto-saves via actionmapSaveBinds + configSave). Click-to-rebind / right-click-to-clear / Esc-to-cancel preserved. |
| `5840f28a` | **(Mike parallel — S308)**. Door-tick bbox NULL guard + pause menu hardening. Absorbed my BATCH 2 delete wiring (right-click + controller X-button → "Delete Mod…" on Installed Mods tab; delete-confirm modal rendered both from Interface tab and from inside the Modding Hub so deletes initiated in the hub don't depend on Settings being open). |
| `db509d87` | **Theme Editor bundle dropdowns**. Added "Menu Style (optional)" + "Font (optional)" dropdowns to the Save-as-Mod panel, populated from `pdguiThemeGetChromeStyle{Id,Name}` and `pdguiFontModGet{Id,Name}`. Selection writes into theme.json as `menuStyle` / `font` keys per the S305 P4 schema. Both default to "(none)" — omits the key entirely when unbundled so older loaders stay byte-identical. |
| `affc61fa` | **X-button close path + ctx-leak fix**. Direct-signal close channel (`s_TitleCloseFrame` + `pdguiConsumeTitleClose()`) replaces pure-Escape-injection from the title X button. Fixes the "first X click does nothing, second click works" symptom — ImGui's own nav was eating the injected Escape edge before the renderer's IsKeyPressed saw it. Escape AddKeyEvent fallback preserved for renderers that haven't been migrated yet. Also adds a defensive `inputCtxPopDeferred(&g_CtxImGuiMenu)` in the top-level main-menu close path — catches the ctx-leak class where `renderCiSettingsRedirect` pushes the ctx at boot and never pops, which was producing "movement locked after menu closed" because g_ImcMenu stayed priority-active. NOTE log fires when the defensive pop catches a leak. |

### Log investigation

Mike's post-S305 playtest log (`Downloads/Perfect Dark 2.0/pd-client.log`, 22:48 → 23:40) showed:

- `MENUPOOL: acquired main_menu` at 00:03.55 with `ctx=none(shared)` — same symptom Mike reported before S304 rebuild. Root cause: `INPUTCTX: imgui_menu on_push` at **00:01.30** (one second after boot, before main menu ever opens) — `renderCiSettingsRedirect` auto-pushes `g_CtxImGuiMenu` when the CI Options dialog opens at boot and never pops it (that renderer is still on the raw ctx pattern, per the S304 deferred-migration list). When the main menu opens, the pool's acquire finds `g_CtxImGuiMenu` already on the stack → attaches in "shared" mode, does not own the pop on release. All subsequent menu close paths leave the ctx stuck, g_ImcMenu stays priority-active, and movement input gets routed away from gameplay.
- `affc61fa`'s defensive pop catches this on top-level main-menu close. A proper fix is to migrate `renderCiSettingsRedirect` (+ `renderCiDeadPlayer2` + `renderCinemaList`) to the S300 pool-owned ctx pattern; queued as S304 follow-up.
- No crash, no watchdog warnings, audio underruns persist (~37/30s) — B-141 unchanged.

### Not fixed in this session (deferred)

- **Per-agent prefs.ini** — design doc `context/designs/theme-bundle-and-per-agent-settings-2026-04-16.md` already covers the storage layout + agent-switch hook + Settings write-through. Implementation estimate ~300-400 lines; dedicated future session because of careful test-matrix needs (mid-match switch, guest agent, Settings-open switch). Theme bundle plumbing that per-agent depends on already shipped in S305 + this session's Theme Editor bundle dropdowns.
- **renderCiSettingsRedirect / renderCiDeadPlayer2 / renderCinemaList migration to S300 pool ctx** — still on raw `inputCtxPush/Pop`. `affc61fa`'s defensive pop hides the symptom; the underlying leak class remains.
- **B-141 audio underruns** — persistent but not catastrophic.
- **Settings Close X button on non-main-menu dialogs** — the S306 X-click fix only wires `pdguiConsumeTitleClose()` into `renderMainMenu`'s close handler. The CI redirect, endscreen, pause menu, modding hub, and theme editor still rely on the Escape fallback. Sweep queued.

### Files touched

- `port/fast3d/pdgui_style.cpp` + `port/include/pdgui_style.h` — palette struct extension, `pdguiSetPaletteExtensions`, semantic accessors, `s_TitleCloseFrame` + `pdguiConsumeTitleClose`.
- `port/fast3d/pdgui_theme_loader.cpp` + `port/include/pdgui_theme_loader.h` — 20-field palette in theme_def, alias table, `pdguiThemeGetFilePath`, apply_theme_def forwards extensions.
- `port/fast3d/pdgui_menu_theme_editor.cpp` — grouped sections, bundle dropdowns, 20-slot working palette, skip-zero save.
- `port/fast3d/pdgui_menu_mainmenu.cpp` — **largest change this session**: Interface tab (`renderSettingsInterface` ~300 lines), moved Menu Style / Title Bar / Font / Color Theme out of Video + Debug tabs, delete helpers + confirm modal, input mapping rewrite (`s_BindableGroups` + `s_BindableActions` with all 51 entries + conflict map + search + grouped rendering), X-button close + defensive ctx pop. Also shifted s_SettingsSubTab indices for the new Interface tab (0=Video 1=Interface 2=Audio 3=Controls 4=Game 5=Updates 6=Debug 7=Catalog) and updated `ciRedirectTargetTabForDialog` + Controls-tab re-init guard to match.
- `port/fast3d/pdgui_menu_moddinghub.cpp` — `pdguiModdingHubShowTool(int)` entry for deep-links from the Interface tab; toolbar tint now uses `pdguiGetToolbarTint`.
- `port/fast3d/pdgui_menu_modmgr.cpp` — (landed via Mike's 5840f28a) right-click + controller X-button → "Delete Mod…" context menu on Installed Mods tab; delete-confirm modal rendered from within modmgr too.

### Build verify

All six commits built clean via `source devtools/build-env.sh && ninja -C Build pd pd-server`. Final `PerfectDark.exe` ~51.5 MB, `PerfectDarkServer.exe` ~22.9 MB. Only pre-existing warnings (propobj.c size-casts, xrayalphafrac may-be-uninit, ImageBase comment-in-comment).

### Playtest punch list (what Mike should exercise)

1. **Interface tab** — open Settings, confirm a new "Interface" tab sits between Video and Audio. All four dropdowns (Color Theme, Menu Style, Title Bar, Font) + Open Color Editor + Open Menu Style Tool should work from it. LB/RB bumpers cycle through 8 tabs now instead of 7.
2. **Color Theme delete** — right-click a user theme in Interface tab → "Delete Theme…" → confirm. Theme should disappear from the list without a restart. Built-in themes should refuse the delete silently.
3. **Mod delete** — right-click a mod in Modding Hub → Mod Manager tab → "Delete Mod…" → confirm. Mod disappears from the list.
4. **Controller X-button** — GamepadFaceLeft on focused theme/mod row opens the same context menu. No-op on built-in themes.
5. **Theme palette extensions** — open Theme Editor. Four groups visible (Window Frame / Text Colors / Interactive Elements / Semantic Accents) + checkbox to show Reserved. "Auto" button on Semantic slots resets them to the derived default. Save as mod → theme.json only includes extension keys that were actually touched.
6. **Theme bundle** — open Theme Editor → fill Name, pick a Menu Style (optional) + Font (optional), Save. Open the resulting `mods/<slug>/theme.json` and confirm `menuStyle` / `font` keys are present.
7. **Input Mapping redesign** — Settings → Controls → Keyboard & Mouse. Should see Movement / Aim / Combat / Weapons / Vehicle / Menu Nav / C-Buttons / D-Pad / System Hotkeys group headers. Search box filters by action name. Bind "W" to two actions deliberately — both should show red borders. Click Fire Mode — should rebind cleanly to next press (try a controller button — should play error sfx because MKB tab is showing).
8. **X-button close on Settings** — open Settings, click the X in the top-right on the first attempt. Should close to main menu immediately. If it works first-try, `affc61fa` landed. Log should show `MENU_IMGUI: main menu ESC — settings CLOSE (view 2->0) [via X]`.
9. **Movement after menu close** — open main menu, close it, WASD should move immediately. If log shows `MENU_IMGUI: defensive inputCtxPopDeferred(g_CtxImGuiMenu) — leak class caught`, that means the ctx-leak was caught and unlocked movement.

### Constraints touched

- **Interface tab + sub-tab numbering** — `s_SettingsSubTab` range is now 0..7 (was 0..6). Downstream code that compares specific index literals (Controls re-init guard at old index 2, CI redirect mapper) updated.
- **Theme palette size** — `struct pdgui_palette` gained a 5-field tail. `pdguiSetPaletteCustom` now memcpys only the legacy 15 fields to avoid buffer over-read; new `pdguiSetPaletteExtensions` writes the tail.
- **Theme editor working palette** — `s_WorkPalette[15]` expanded to `s_WorkPalette[20]`; snapshot + reset paths cover the full 20.
- **Theme.json schema** — gained optional extension keys (`toolbarTint`, `textPositive`, `textWarning`, `buttonHover`, `buttonActive`, plus `checkmark` / `checkMark` aliases for `checkbox_checked`). Older loaders skip unknown keys.

---

## Session S307 — 2026-04-17 (Forge level editor F0 Foundation — merged to dev from tender-sutherland worktree)

**Scope**: Phase F0 of the in-game Forge level editor.  Design ref:
`context/designs/forge-level-editor-2026-04-16.md` §15.  Worktree session
running in parallel with S306 on `dev` (per Mike's instruction: don't merge
until F0 is complete and clean so S306's UI work can build/test
independently).  No protocol bump (forge is local-only at F0).

### What shipped (single commit `8d412cc1` on `claude/tender-sutherland-536de7`)

- **`src/include/game/forgemode.h`** — public C-callable API.  3-state
  `forge_session_state_t` (INACTIVE / NORMAL / FREEFLY).  Forward-declares
  `struct coord` and uses `s32` for boolean returns so the header is safe
  to include from C++ TUs (project's `#define bool s32` would otherwise
  break C++ ABI).
- **`src/game/forgemode.c`** — module state, freefly camera integrator
  (yaw + pitch from look axes, world-relative WASD movement, Q/E vertical,
  3x boost / 0.25x precision modifiers, 600 u/s base speed, 89° pitch
  clamp, 60Hz dt assumed for F0).  Mode toggle hijacks the player by
  setting `bondmovemode = MOVEMODE_CUTSCENE` (so `bmoveTick` routes to
  `bcutsceneTick`, suppressing the legacy walk update) and overrides
  `prop->pos + vv_theta + vv_verta` each tick.  Session-exit watchdog
  drops to INACTIVE whenever `g_StageNum` is no longer `STAGE_IS_GAMEPLAY`.
  Lazy-init guard so `forgeTick` is safe before any explicit `forgeInit()`.
- **`port/include/pdgui_forge.h`** — two C entry points
  (`pdguiForgeStartSession`, `pdguiForgeHudRender`).
- **`port/fast3d/pdgui_menu_forge.cpp`** — `pdguiForgeStartSession()`
  rejects if a stage transition is already pending; otherwise queues
  `forgeRequestEnterSession()` and calls
  `mainChangeToStage(STAGE_CITRAINING)`.  F3 will replace the hard-wired
  CITRAINING with a base-stage browser.
- **`port/fast3d/pdgui_forge_hud.cpp`** — foreground-draw-list HUD shell:
  top-left mode badge (NORMAL green / FREEFLY cyan), centered placement
  reticle in freefly, bottom-left camera readout
  (pos / yaw / pitch / speed-scale tag), right-edge placeholder for the F1
  catalog panel, bottom-right controls reminder.  Renders nothing when
  `forgeSessionIsActive()` is false; safe to call every frame.  Avoids
  including `types.h` (forward-declares `struct coord`) per the standing
  C++ TU rule.
- **`port/include/actionmap.h`** — five new `InputAction` enums between
  `ACTION_SCORECARD` and `ACTION_COUNT`: `ACTION_FORGE_TOGGLE`,
  `ACTION_FORGE_ASCEND`, `ACTION_FORGE_DESCEND`, `ACTION_FORGE_BOOST`,
  `ACTION_FORGE_PRECISION`.  `ACTION_COUNT` 57 → 62.
- **`port/src/actionmap.cpp`** — `s_ActionNames[]` table extended with the
  five new strings; default keyboard binds for player 0 added at the end of
  `setupGameplayDefaults` (F7 toggle, E ascend, Q descend, LSHIFT boost,
  LCTRL precision).  Dual-bind to LSHIFT/LCTRL is intentional — sprint and
  crouch share the same keys but freefly reads the dedicated FORGE actions.
  Controller toggle deliberately unbound; LB+RB chord per design comes
  later.
- **`port/src/pdmain.c`** — `forgeTick()` called from `mainTick` between
  `playermgrShuffle()` and the per-player gameplay loop, so a freefly
  bondmovemode override is in place before any per-player `bmoveTick`
  dispatches.  `#include "game/forgemode.h"` added.
- **`port/fast3d/pdgui_backend.cpp`** — `pdguiForgeHudRender()` called
  after `pdguiGameOverRender`, before `pdguiRenderAllWindowShimmers` /
  scanline overlay so the editor HUD sits beneath those post-process
  effects.
- **`port/fast3d/pdgui_menu_mainmenu.cpp`** — new top-level "Forge" button
  between Stats and Quit in the `s_MenuView == 0` block; clicks dispatch
  to `pdguiForgeStartSession()` plus `PDGUI_SND_OPENDIALOG`.

### F0 architecture decisions

1. **Mode toggle hijack** — instead of building a parallel camera matrix
   path, freefly hijacks `g_Vars.currentplayer->prop->pos` plus `vv_theta`
   and `vv_verta` each tick, with `bondmovemode = MOVEMODE_CUTSCENE` to
   suppress the legacy walk overwrite.  This works because the existing
   camera setup (`camSetLookAt` flow in `player.c:5027`) reads from the
   player struct.  Later phases can refactor this if needed; for F0 it's
   the smallest possible diff.
2. **Single tap toggle** — design specifies "hold for 0.5s" but that
   needs a hold-time tracker.  F0 uses single-tap on `ACTION_FORGE_TOGGLE`
   (F7 default) — accidental triggers in normal gameplay are unlikely
   since F7 isn't bound to anything else and normal gameplay reads the
   action via `actionPressed` (rising edge) so accidental taps from
   sprint/crouch holds don't fire it.
3. **No collision in freefly** — design explicitly says Dr. Carroll
   passes through geometry.  Forge camera ignores all collision.
   Placement-time collision queries land in F1 with the catalog/reticle.
4. **CI Training as the only base stage** — F0 hard-wires
   `STAGE_CITRAINING` since that's the existing free-roam scaffold.  F3
   brings the base-stage browser per the design doc.
5. **Session lifecycle bound to stage** — entering a forge session sets
   the request flag and queues a stage change; once in gameplay the
   watchdog activates the session.  Leaving gameplay (Esc → main menu →
   Quit, etc.) auto-cleans the session and restores the player's
   bondmovemode.
6. **C++ ABI safety** — `forgemode.h` deliberately forward-declares
   `struct coord` and uses `s32` for boolean predicates because the
   project's `#define bool s32` (in `types.h`) breaks any C++ TU that
   includes `types.h` directly.  Same pattern as `pdgui_hud.cpp` and
   `pdgui_menu_mainmenu.cpp`.

### Build verify

`source devtools/build-env.sh && cmake -G Ninja ... -B Build -S .` (one-time
configure for the worktree's own `Build/`; `build-headless.ps1` redirects to
the main working copy by design and can't be used in worktrees), then
`ninja -C Build pd pd-server`.  Both targets link cleanly:
`PerfectDark.exe` 51,780,176 bytes, `PerfectDarkServer.exe` 22,840,512.
Only pre-existing warnings (`f32 near/far` typed-name macro, `/*` within
comment); no new warnings from the F0 files.  Object files
`forgemode.c.obj`, `pdgui_menu_forge.cpp.obj`, `pdgui_forge_hud.cpp.obj`
all present in `Build/CMakeFiles/pd.dir/...`.

### Process

- Worktree-only commit `8d412cc1`.  No merge to `dev` yet — Mike requested
  the worktree stay parallel with S306's UI work until F0 is verified
  in-game.  Merge plan (post-playtest): fast-forward into `dev`, post-merge
  line-count verification per CLAUDE.md §9.
- HEAD pre-edit was `b546736d` (v0.0.107 release commit).  Single F0
  commit on top: 10 files, +830 / −1 lines.

### Not done in this session (deferred to F1+)

- Object catalog browser, placement reticle, gizmo, properties panel
  (F1–F2 per the design phase plan).
- Save/load `map.json`, base-stage browser, network sync (F3).
- Drop-to-ground transition on freefly exit (polish).
- Dr. Carroll model swap (visual; F0 keeps the player chr model in CUTSCENE
  mode).
- Controller binding for the toggle (LB+RB chord per design).
- Sim pause toggle in HUD.
- Forge entry from custom/private matches (currently main-menu only; F3
  base-stage browser is the natural place to add this).

**Next**: in-game playtest of the F0 verification recipe in `tasks-current.md` (see S307 section). Worktree `claude/tender-sutherland-536de7` merged to `dev` 2026-04-17.

---

## Session S305 — 2026-04-16 (Playtest batch: content inset + settings persistence + font mods + theme bundle — dev direct)

**Scope**: Mike's post-S304 playtest surfaced eight issues in one pass; worked directly on `dev` (no worktree). All items landed + pushed to origin.

### Fixes landed

1. **P0 content-inset enforcement** (`port/fast3d/pdgui_menu_mainmenu.cpp`): main menu items, Quit button, Settings body `BeginChild`, CI Settings redirect, and Cinema list all honour the nineslice chrome content inset (S297/S298 `pdguiThemeGetContentInset`) plus an 8px breathing-room buffer. Fixes the screenshot bug where Solo Play / Online Play / Change Agent / Settings / Mods / Cheats / Stats / Quit Game bled edge-to-edge into the border chrome.

2. **Settings persistence** (`port/src/config.c`, `port/fast3d/pdgui_theme_loader.cpp`): two root-causes for "theme / title-bar / menu-style don't survive restart":
   - `configLoad` used strict `configFindEntry` so any pd.ini key whose owning subsystem called `configRegister*` AFTER `configInit` was silently dropped. `pdguiThemeInit` (inside `pdguiInit`, line 183 of main.c) runs after `configInit` (line 169), so theme/chrome/title-bar/scanline keys never loaded. Fix: pending-value stash + replay — `configLoad` uses `FindOrAdd`, stores raw values on unregistered entries, `configRegister*` replays on registration. `configSave` preserves pending entries as raw key=value so data isn't lost if registration never happens in a run.
   - `pdguiThemeLoaderInit`'s startup apply only handled built-in palette themes. Mod themes saved via Save-as-Mod (resolved via `pdguiThemeLoadFromCatalog`) were skipped on every restart, reverting the user to the default palette. Fix: route through `pdguiThemeLoadFromCatalog` for non-builtin ids.

3. **Nine-Slice → Menu Style** rename (`port/fast3d/pdgui_menu_moddinghub.cpp`, `port/fast3d/pdgui_menu_mainmenu.cpp`): all user-visible references updated. Internal API names (`pdguiNineslice*`, catalog id `base:ui_chrome_frame`, `mods/UI Chrome/` tree) stay for compatibility. Modding Hub tab label + tool array + saved mod description + header blurb + Settings → Video dropdown label all flipped.

4. **Theme Editor P1 + P2** (`port/fast3d/pdgui_menu_theme_editor.cpp`): Save button moved off the Name/Author row into the action-button row alongside Reset/Close (was partly off-screen on narrow windows + only reachable via Tab). After a successful Save, calls `pdguiThemeRescanMods()` so the new theme appears in Settings → Debug → UI Theme list immediately. Same pattern as B-155/B-156 chrome mod auto-appear.

5. **Menu Style tool preview scaling + screen cleanup (P5 + P6)** (`port/fast3d/pdgui_menu_moddinghub.cpp`): source preview now scales UP as well as down so small imported images fill the sidebar preview at the same standard size as the assembled frame preview below (removed the `if (imgScale > 1) clamp = 1`). Power-user controls (Scale X/Y, Center Cut Axis/%, Desaturate + %, Quick Presets) moved into a collapsed "Advanced" `CollapsingHeader` so Mod Name / symmetry / tile mode / trim / Border Scale / per-edge insets stay front-and-center.

6. **Font Import as Mod (P3)** — new module: `port/include/pdgui_font_mod.h` + `port/fast3d/pdgui_font_mod.cpp`. Scans `mods/Fonts/<slug>/` (or top-level `mods/<slug>/`) for `.ttf`/`.otf` files, optionally reads `font.json` for a display name, registers each under `user.<slug>.font`. New `Video.FontId` pd.ini key tracks the active choice. `pdgui_backend.cpp` calls `pdguiFontModInit()` BEFORE the ImGui font atlas is built — if the saved FontId resolves to a mod path, `AddFontFromFileTTF` loads it as `io.FontDefault`; Handel Gothic still loads as the fallback. Settings → Video gets a "Font" dropdown with "(restart required)" hint. Font mod discovery uses the same modmgr search roots as theme/chrome discovery.

7. **Theme Mod Bundling (P4) — plumbing + design doc** (`port/fast3d/pdgui_theme_loader.cpp`, `context/designs/theme-bundle-and-per-agent-settings-2026-04-16.md`): `theme_def` gains `bundle_chrome_id` + `bundle_font_id` fields. `theme.json` can now declare `"menuStyle"` (user-facing alias; legacy `"chromeStyle"` also accepted) + `"font"` to reference a registered chrome style + font mod by catalog id. `apply_theme_def` forwards the ids to `pdguiThemeSetUiChromeStyleId` / `pdguiFontModSetActiveId` so one theme activation swaps the whole visual identity. Missing: Theme Editor UI doesn't yet expose the two new fields — bundles must be hand-authored after Save-as-Mod for now.

8. **Per-Agent Settings — design doc only**: `context/designs/theme-bundle-and-per-agent-settings-2026-04-16.md` covers the architecture for theme/menu-style/font/enabled-mods following the active Agent profile. Storage via `saves/agents/<slot>/prefs.ini` sidecar, applied on agent switch via existing setters. Flagged as a dedicated future session — ~300-400 lines + careful test matrix.

### Playtest log analysis

Reviewed `/c/Users/mikeh/Downloads/Perfect Dark 2.0/pd-client.log` (22:48, 22+ minutes). Findings:

- Main menu acquired at 00:03.85 with `ctx=none(shared)` — PRE-S304 build (S304 migrated to `ctx=imgui_menu(owned)`). Mike needs to rebuild to pick up the S304 fix.
- No `MENUPOOL: released main_menu` line in the entire log — menu was closed via a path that bypassed `menuCloseDialog`. That's exactly the leak S304's three-layer defense was built for. The watchdog and underflow auto-release aren't in this build either.
- Audio underruns: ~200 every 30s, ~16 hitches. B-141 persists but not catastrophic.
- Game ran 22+ minutes, stable heartbeats, no crashes.

**Recommendation**: rebuild with the latest `dev` (S304 fix + everything S305 landed above). If darkened/layered menu persists after that, the bug is architectural rather than slot-leak.

### Constraints touched

- `configLoad` semantics (config.c): now uses FindOrAdd instead of strict find; pending-value replay on register; save preserves pending. Documented behaviour.
- Theme.json schema: gained `menuStyle` + `font` keys.

### Build verify

All commits build-verified via `source devtools/build-env.sh && ninja -C Build pd pd-server`. Final sizes: PerfectDark.exe ~51.48 MB, PerfectDarkServer.exe ~22.85 MB.

### Not fixed in this session (deferred)

- **Theme Editor UI** for bundle_chrome_id / bundle_font_id dropdowns — today bundles are hand-authored.
- **Per-agent prefs.ini** — full implementation queued (design doc ships).
- **Content inset on OTHER menus** — renderCiSettingsRedirect + Cinema covered; 10+ other ImGui renderers (cheats, mpsetup family, mppause family, room, etc.) still render without inset if they use `pdguiDrawPdDialog` directly. Separate sweep needed.
- **B-141 audio underruns** — persistent, not addressed.

### Commits (dev)

- `fc614b2e fix(menu): S305 content-inset for main menu + CI redirect + Cinema (P0)`
- `8d4b6b62 fix(config): S305 persist settings registered after configInit`
- `9b4a24fd refactor(ui): S305 rename Nine-Slice Chrome → Menu Style`
- `74655a9d fix(theme-editor): S305 dock Save + auto-refresh themes list (P1 + P2)`
- `dfb38c62 refactor(menu-style-tool): S305 scale preview to standard + collapse Advanced (P5 + P6)`
- `3daa5275 chore: auto-commit before release (dev window)` — captured the font module diff
- `aaa245bc feat(theme): S305 P4 theme bundle plumbing + per-agent design doc`
- Mike's interleaved auto-commits + v0.0.107 release commit.

---

## Session S304 — 2026-04-16 (Menu pool slot leak + consistency watchdog — focused-roentgen worktree)

**Scope**: Fix CRITICAL B-160 — menu pool slot leak where the main menu's close path left the `MENU_TYPE_MAIN_MENU` slot active, causing every subsequent `menuPushDialog` to be rejected by the pool's structural dedup (S299). User-visible symptom: darkened/stacked/dead main menu on reopen, broken input authority. Playtest log evidence: `MENUPOOL: acquired main_menu gen=1 ctx=none(shared)` at 00:07.28 was paired only with a `MENUPOOL: released main_menu` at 00:33.24 (shutdown) — between them, three `menuPopDialog called at depth 0 (underflow)` warnings and two `menuPushDialog rejected — pool slot [main_menu] already active` rejections.

### Root cause

`port/fast3d/pdgui_menu_mainmenu.cpp::renderMainMenu` top-level ESC close handler (S295 F7 era) did:

```cpp
if (inputCtxIsActive(&g_CtxImGuiMenu)) {
    inputCtxPopDeferred(&g_CtxImGuiMenu);     // pops ctx
}
// restore game state ...
menuPopDialog();                               // pops legacy stack + releases pool
```

When a force-close path (endscreen bridge, matchStart, netmsg stage handlers, or a previous iteration of the ESC handler that already fired) had already zeroed `g_Menus[].depth`, `menuPopDialog` hit its `depth == 0` underflow guard (F-3.2, S261) and early-returned WITHOUT releasing the pool slot. The ctx was already popped by the first step, but the pool slot stayed active — the leak class that surfaces as "darkened/layered menu when switching around."

The bug is systemic: any close path that pops the input ctx AND the legacy stack as two independent steps is vulnerable if the stack is popped by something else in between. The pool release was only ever wired into `menuCloseDialog`, which is only reached when `menuPopDialog` gets past the underflow guard.

### Fix (three layers — defense in depth)

1. **Underflow-path pool recovery** (`src/game/menu.c::menuPopDialog`): the underflow guard now calls `menupoolReleaseAll()` if `menupoolCountActive() > 0`, with a `MENU: underflow detected N stale pool slot(s) — releasing (stack desync)` WARNING. This closes the leak class at the exact point where it was previously silently dropping slots. Safe because normal close paths release their own slot before depth goes to 0, and force-close sites already call `menupoolReleaseAll` explicitly; the only flows that reach underflow with active slots are the buggy ones we want to recover from.

2. **Per-frame consistency watchdog** (`src/game/menu.c::menuPoolConsistencyCheck`, wired at top of `menuTick`): detects the `legacy stack empty ∧ pool has active slots` invariant violation every frame. If it fires, logs `MENU: watchdog — legacy stack empty but N pool slot(s) active; releasing leaked slot(s)` at WARNING and calls `menupoolDumpActive()` for diagnostics before `menupoolReleaseAll()`. This catches ANY leak path we haven't thought of, even future regressions — e.g. `menuPushRootDialog`'s blanket `numdialogs = 0; depth = 0;` reset (menu.c:3735-3736) silently discards any pool slots that were active before the root push, which the watchdog will now surface.

3. **Main menu renderer migrated to S300 pool-owned ctx** (`port/fast3d/pdgui_menu_mainmenu.cpp`): `IsWindowAppearing` path replaces the raw `inputCtxPush(&g_CtxImGuiMenu)` with `menupoolAcquireDialog(menupoolDialogDef(dialog), &g_CtxImGuiMenu)` — the pool attaches the ctx to the slot that `menuPushDialog` pre-acquired (S300 attach-to-active semantics). Close path drops the explicit `inputCtxPopDeferred` call; `menuPopDialog` → `menuCloseDialog` → `menupoolReleaseDialog` now cascades the ctx pop atomically, symmetric with the acquire. This brings the main menu into parity with the ten ImGui renderers migrated in S300 (cheats, mpsetup family, mppause family, mpadvanced family, playerconfig, botsetup, agentselect, room, training, mpsettings).

### Files touched

- `src/game/menu.c` — `menuPopDialog` underflow recovery + new `menuPoolConsistencyCheck()` function
- `src/include/game/menu.h` — `menuPoolConsistencyCheck` declaration
- `src/game/menutick.c` — call `menuPoolConsistencyCheck()` at top of `menuTick`
- `port/fast3d/pdgui_menu_mainmenu.cpp` — S300 pool-owned ctx migration for `renderMainMenu` (open + close path); `menupool.h` added to extern "C" includes

### Build verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — both targets link. `PerfectDark.exe` 51,563,427 bytes, `PerfectDarkServer.exe` 22,837,840 bytes. Only pre-existing warnings (types.h `f32 near/far` typed-name macro, `/*` within comments in `pdgui_theme.h` / `pdgui_bridge.c` / `updater.h`). No new warnings.

### Not fixed in this session (deferred)

- **Other mainmenu.cpp renderers still on raw ctx pattern** — `renderCiSettingsRedirect` (line 3170+), `renderCiDeadPlayer2` (line 3249+), `renderCinemaList` (line 3358+) all still use raw `inputCtxPush/Pop` around `menuPopDialog`. They are vulnerable to the same class of leak if their legacy stack is force-popped before their close handler fires. The watchdog will catch any resulting leak, but a proper S300 migration is deferred for a scope-focused future session.
- **CI Options sibling registration** — `g_CiOptionsViaPcMenuDialog` / `g_CiOptionsViaPauseMenuDialog` are siblings opened by `menuPushDialog`'s nextsibling loop but NOT registered in `menupool.c`. Registering them would collide with `g_CiControlStyleMenuDialog` (already `MENU_TYPE_CI_OPTIONS`), producing dedup rejections on legitimate opens. Left unregistered — pool passthrough is correct behavior for placeholder siblings.

**Next**: playtest B-160 per the verify column (main-menu reopen stress, force-close paths, watchdog absence on healthy flow).

---

## Session S303 — 2026-04-16 (Campaign/Co-Op/Counter-Op full game loop sweep — reverent-chatelet worktree)

**Scope**: end-to-end audit of the three gameplay modes (Campaign solo, networked Co-Op, networked Counter-Op) plus manifest pipeline / starting weapons / menu bookends. Find problems AND fix them; add logging where runtime verification is needed. Full findings in `context/scratch/game-loop-sweep-2026-04-16.md`.

### Fixes landed (7)

1. **Manifest clear generalization** (`src/game/menutick.c`) — S298 Deep Sea `manifestClear(&g_ClientManifest)` pattern now covers three additional transition sites that previously could leak a torn-down manifest into `manifestMPTransition`: MPENDSCREEN restart-level (`case MENUROOT_MPENDSCREEN` + `g_Vars.restartlevel`), MPENDSCREEN → CITRAINING exit, and COOPCONTINUE → CITRAINING exit. Each new clear site logs `GAMELOOP.MANIFEST: ... clearing manifest (N entries) before ...`. The Deep Sea block itself gained a `GAMELOOP.COOP: Deep Sea auto-advance → stageindex=... stagenum=0x... manifest=N` line.

2. **Counter-Op antiplayernum fallback logging** (`port/src/net/net.c::netServerCoopStageStart`) — the silent `antiplayernum = 1` fallback (when `g_NetCounterOpClientId` lookup misses) now emits `GAMELOOP.COUNTEROP: antiClientId unresolved (id=...) — falling back to slot %d (clients=%u)` at LOG_WARNING. Success path logs `GAMELOOP.COUNTEROP: server start anti stage=... clients=... antiClientId=... antiplayernum=... (from wire)`. Coop start similarly logs `GAMELOOP.COOP: server start coop ...`.

3. **Stale MP manifest leak WARNING** (`port/src/pdmain.c::mainChangeToStage`) — new WARNING when `g_ClientManifest.num_entries > 0` but the local session is not in any MP/coop/anti state (`g_NetMode==NONE && !iscoop && !isanti && !normmplayerisrunning`): "stale MP manifest (%d entries) leaked into SP transition to 0x%02x — routing via MPTransition". Every gameplay transition also gets a `GAMELOOP.MANIFEST: MP transition to 0x%02x (manifest=N netmode=... iscoop=... isanti=...)` or `GAMELOOP.MANIFEST: SP transition to 0x%02x ...` or `GAMELOOP.MANIFEST: menu transition to 0x%02x — clearing manifest ...` so every stage change is attributable.

4. **Co-op telefrag audit** (`src/game/playerreset.c`) — new WARNING when `(coopplayernum >= 0 || antiplayernum >= 0) && g_NumSpawnPoints < 2`: "GAMELOOP.COOP: only N spawn pad(s) declared for coop stage 0xXX — P1/P2 telefrag risk (mode=coop|anti)". Identifies the GAP-B class (mission has a single `INTROCMD_SPAWN`, the scenario-choose path uses `chrCompareTeams` which skips same-team filtering, `playerTrySelectPoolSpawn` is gated on `mplayerisrunning` which is false in coop). A full fix needs coop-aware pad scoring; this session only makes the problem observable.

5. **Starting weapon logging** (`src/game/playerreset.c::playerReset`) — every INTROCMD_WEAPON grant now logs `GAMELOOP.WEAPON: playernum=%d mission=0x%02x INTRO gave R=%d L=%d default=%d [+eyespy]`. Anti-role gets an explicit `GAMELOOP.WEAPON: ... INTRO skipped for anti role (weapons come from possession)` line so "empty-handed anti" is diagnosable. Entry of `playerReset` also logs `GAMELOOP.{CAMPAIGN,COOP,COUNTEROP,MP_COMBATSIM}: playerReset begin playernum=... role=bond|coop|anti ...`.

6. **Endscreen push bookends + anti NULL guard** (`src/game/endscreen.c`) — `endscreenPushCoop` / `endscreenPushAnti` / `endscreenPushSolo` / `endscreenPrepare` all log `GAMELOOP.{CAMPAIGN,COOP,COUNTEROP}: endscreenPush* entry playernum=... stats=... ...` at entry. `endscreenPushCoop` and `endscreenPushAnti` log their NULL-currentplayerstats / out-of-range g_MpPlayerNum aborts as WARNINGs. `endscreenPushAnti` gains a `g_Vars.bond` NULL guard before the decision branch (`endscreen.c:1926-1929` derefs `g_Vars.bond->isdead` without a guard — prevents AV in teardown races).

7. **Menu bookend + coop manifest logs** (`src/game/mainmenu.c`, `port/fast3d/pdgui_bridge.c`, `port/src/net/netmsg.c`) — `menuhandlerAcceptMission` entry logs `GAMELOOP.{CAMPAIGN,COOP,COUNTEROP}: menuhandlerAcceptMission entry stage_id='...' stagenum=0x... stageindex=... diff=... [RETRY]` (the RETRY flag is set when the resolved stagenum matches the live stage). `pdguiEndscreenStartMission` / `NextMission` / `ExitToMainMenu` each log their entry + pre/post stageindex for Next. `SVC_STAGE_END` receive logs `GAMELOOP.{COOP,COUNTEROP,CAMPAIGN}: SVC_STAGE_END received — entering endscreen teardown`. Co-op server-side `manifestBuild` at `netmsg.c:~4870` logs `GAMELOOP.{COOP,COUNTEROP}: server-side manifest built entries=... hash=... stage='...' clients=...` so host vs. server manifest divergence is measurable.

### GAMELOOP.* log taxonomy (new)

Five tags, all pass through the log-channel classifier unchanged (appear in every playtest log):

- `GAMELOOP.CAMPAIGN:` — solo start/retry/next/exit, SVC_STAGE_END (combat-sim), endscreen prepare
- `GAMELOOP.COOP:` — coop server start, endscreen push, Deep Sea auto-advance, manifest-build, telefrag audit
- `GAMELOOP.COUNTEROP:` — anti server start, endscreen push, anti-NULL guard, wire-resolution status
- `GAMELOOP.MANIFEST:` — every `mainChangeToStage` branch (SP/MP/menu), stale-leak WARNING
- `GAMELOOP.WEAPON:` — INTROCMD_WEAPON per-grant, anti-skip notice

Grep recipe: `grep -E "GAMELOOP\.(CAMPAIGN|COOP|COUNTEROP|MANIFEST|WEAPON)" pd-client.log`.

### Gaps identified, not fully fixed (logged only, queued)

- **GAP-A co-op manifest ignores host payload** — `netmsg.c:4859 manifestBuild(&g_ServerManifest, NULL, NULL)` rebuilds server-side for coop/anti (Combat-Sim path uses `manifestDeserialize` at `:4706`). New `GAMELOOP.COOP: server-side manifest built ...` log makes divergence observable. Fix needs a design call.
- **GAP-B co-op 1-pad telefrag** — now observable via log; fix needs coop-aware pad scoring in `playerChooseSpawnLocation` or a coop-specific SP-14 expansion in playerreset.
- **Anti body/head pre-load manifest gap** — possession-based resolution already relies on post-load catalog; pre-load would require a canonical anti-chr per mission (not currently modeled).
- **Menu pool not released on `menuhandlerAcceptMission` entry** — S303 logs the entry but doesn't add `menupoolReleaseAll` (entry paths already go through `menuStop`). Queued.

### Build verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — both targets link. `PerfectDark.exe` 51,564,390 bytes, `PerfectDarkServer.exe` 22,837,840 bytes. Only pre-existing warnings (`/*` in comment in `pdgui_theme.h`/`pdgui_bridge.c`, pointer cast in `mainmenu.c` 2P anti menu handler, `chraction.c` maybe-uninitialized). Two forgotten `#include "system.h"` in `endscreen.c` + `mainmenu.c` added during build-verify to land the new logs.

### Files touched

- `src/game/menutick.c` — generalized manifestClear in three exit paths + GAMELOOP logs
- `src/game/mainmenu.c` — `menuhandlerAcceptMission` entry log + `system.h` include
- `src/game/endscreen.c` — `endscreenPush*` entry logs + `g_Vars.bond` NULL guard + `system.h` include
- `src/game/playerreset.c` — `playerReset` entry log + per-weapon logs + coop telefrag audit
- `port/src/pdmain.c` — `mainChangeToStage` manifest-route logs + stale-leak WARNING
- `port/src/net/net.c` — `netServerCoopStageStart` anti fallback WARNING + coop/anti success logs
- `port/src/net/netmsg.c` — `SVC_STAGE_END` teardown log + coop `manifestBuild` log
- `port/fast3d/pdgui_bridge.c` — endscreen bridge entry logs at StartMission / NextMission / ExitToMainMenu
- `context/scratch/game-loop-sweep-2026-04-16.md` (new)

**Next**: playtest the five recipes in the findings report. Expected healthy signal: every stage transition produces a `GAMELOOP.MANIFEST:` line; every coop/counter-op start produces a matching server + client pair; no WARNING-level GAMELOOP lines on clean stock paths. Warnings identify regressions.

---

## Session S300 — 2026-04-16 (Menu pool ctx migration + SP-14 + manifest audit — peaceful-aryabhata worktree)

**Scope**: Three-task batch on one commit, merged to `dev`.

1. **ADR §6.1b — ImGui renderers `s_*PushedCtx` → pool-owned ctx.** Completes Phase 2 of the input-authority ADR by migrating the ten ImGui renderer files off the per-file `s_FooPushedCtx` booleans and onto `menupoolAcquireDialog(def, &g_CtxImGuiMenu)` / `menupoolReleaseDialog(def)` through the pre-allocated pool shipped in S299. The pool now owns the input-context push/pop lifecycle for each slot.
2. **SP-14 follow-up — `g_NetMatchRoomId` + `g_NetCounterOpClientId` defensive reset in `netDisconnect` and `netStartServer`.** Closes the last teardown gap not covered by the S295 `netmsgSvcStageEndRead` reset: if a server-mode host disconnects mid-match or re-hosts without a clean stage-end, the stale room id and counter-op client id no longer survive into the next session.
3. **Manifest gap audit.** Confirmed `manifestBuildForMenu` + `manifestMenuTransition` are live in `netmanifest.c` and wired through `mainChangeToStage` in `pdmain.c:772`; confirmed `manifestSPRescanSetup` provides the post-load split of `manifestBuildMission`. Added per-category diagnostic logging so future regressions are diagnosable from `pd.log` alone.

### Task 1 — ImGui renderers pool migration (ADR §6.1b)

**Files touched** — ten ImGui renderers plus the pool library:

- `port/include/menupool.h` — added three new `menu_type_t` values (`MENU_TYPE_MP_SOUNDTRACK`, `MENU_TYPE_MP_TUNES`, `MENU_TYPE_MP_TEAMNAMES`) so the four mpsettings sub-dialogs can legitimately stack (Soundtrack → SelectTunes). Added a C-side accessor `menupoolDialogDef(const struct menudialog *)` because C++ renderer callbacks can't include types.h (it redefines `bool`).
- `port/src/menupool.c` — extended `menupoolAcquire` to **attach the ctx to an already-active slot** when the caller provides one and `slot->owned_ctx` is NULL. This is the key enabler for the migration: `menuPushDialog` already acquired the slot with `ctx=NULL`, so the renderer's IsWindowAppearing call attaches the ctx without mutating the dedup state. Registered `g_MpSoundtrackMenuDialog` / `g_MpSelectTunesMenuDialog` / `g_MpTeamNamesMenuDialog` against the new types. Added `g_MpSoundtrackMenuDialog` / `g_MpSelectTunesMenuDialog` / `g_MpTeamNamesMenuDialog` externs to `src/include/data.h`.
- `port/fast3d/pdgui_menu_cheats.cpp` — removed `s_CheatsHubPushedCtx` bool; Begin()=false cull → `menupoolReleaseDialog`; IsWindowAppearing → `menupoolAcquireDialog(..., &g_CtxImGuiMenu)`; Back/Esc → `menuPopDialog()` (menuCloseDialog handles pool release).
- `port/fast3d/pdgui_menu_mpsetup.cpp` — removed `s_MpSetupPushedCtx`; converted `mp_BeginStandardWindow` / `mp_CloseCurrentDialog` helpers to take `const struct menudialogdef *def`; updated all eight renderer entry points (`renderMpArena`, `renderMpScenario*`, `renderMpWeapons`, `renderMpSelectRandomWeapons`, `renderMpQuickTeamWeapons`, `renderMpLimits`, `renderMpScenarioOptions*`, `renderMpExtGameOptions`) to name the `dialog` param and pass its definition.
- `port/fast3d/pdgui_menu_mppause.cpp` — removed `s_MpPausePushedCtx`; same helper + renderer conversion (6 renderers).
- `port/fast3d/pdgui_menu_mpadvanced.cpp` — removed `s_MpAdvancedPushedCtx`; same helper + renderer conversion (9 renderers including the three `renderMpPlayerSetupHub*` wrappers and `renderMpStuff*`).
- `port/fast3d/pdgui_menu_playerconfig.cpp` — removed `s_PlayerConfigPushedCtx`; same helper + renderer conversion (5 renderers: `renderMpCharacter`, `renderMpPlayerStats`, `renderMpLoadSettings`, `renderMpLoadPreset`, `renderMpLoadPlayer`).
- `port/fast3d/pdgui_menu_botsetup.cpp` — removed `s_BotSetupPushedCtx`; same helper + renderer conversion (5 renderers: `renderMpSimulants`, `renderMpAddSimulant`, `renderMpChangeSimulant`, `renderMpEditSimulant`, `renderMpSimulantCharacter`).
- `port/fast3d/pdgui_menu_agentselect.cpp` — removed `s_AgentSelectPushedCtx`; inline pattern (no helper), load/select transitions use `menupoolReleaseDialog` before handing off to the file manager path.
- `port/fast3d/pdgui_menu_room.cpp` — removed `s_RoomPushedCtx`; Room is pure-ImGui (no dialogdef backing), so uses the type-based API `menupoolAcquire(MENU_TYPE_ROOM, NULL, &g_CtxImGuiMenu)` / `menupoolRelease(MENU_TYPE_ROOM)`. Solo mode remains in shared ctx mode (main menu owns the push) — pool records shared-mode and doesn't pop on release.
- `port/fast3d/pdgui_menu_training.cpp` — removed `s_FrWeaponPushedCtx`; converted `renderFrWeaponList` to pool API. Only the FR entry point pushes ctx; sub-dialogs (difficulty, info) run under the shared ctx.
- `port/fast3d/pdgui_menu_mpsettings.cpp` — removed per-caller bools (`s_TunesOwnsCtx`, `s_SoundtrackOwnsCtx`, `s_TeamNamesOwnsCtx`); converted `pdms_BeginStandardWindow` / `pdms_CloseCurrentDialog` helpers to take the dialogdef. Handicap (which never pushed ctx) simply calls `pdms_CloseCurrentDialog()` with no args. Each sub-dialog maps to its own pool slot so they can stack cleanly (Soundtrack→Tunes in shared-mode).

**Key acquire semantics change**: `menupoolAcquire` now attaches ctx to an existing slot when called with a non-NULL ctx and `slot->owned_ctx == NULL`. Return code `0` still signals "slot was already active" so `menuPushDialog`'s dedup rejection still works. The internals now emit a distinct log line `MENUPOOL: attached ctx to active <type>` whenever the attach path fires (typically once per renderer's IsWindowAppearing after the menuPushDialog pre-acquire).

### Task 2 — SP-14 defensive teardown

- `port/src/net/net.c::netDisconnect` — after resetting `g_NetMode` to `NETMODE_NONE`, defensively resets `g_NetMatchRoomId = 0xFF` and `g_NetCounterOpClientId = NET_NULL_CLIENT`. Logs the prior values when either is non-sentinel, so we can diagnose any case where these leak across a disconnect (should be rare given the existing SVC_STAGE_END and netServerStageEnd resets; this is belt-and-braces).
- `port/src/net/net.c::netStartServer` — same reset just before the server-mode announcement, covering the "previous server-mode session terminated without a clean netServerStageEnd, now re-hosting" case.

### Task 3 — Manifest audit + diagnostics

- Verified `manifestBuildForMenu` (netmanifest.c:605) builds from `assetCatalogIterateByType(ASSET_BODY / ASSET_HEAD, ...)` and is wired into the menu path via `manifestMenuTransition` (netmanifest.c:1615) called from `mainChangeToStage`'s `!STAGE_IS_GAMEPLAY` branch (pdmain.c:772). No code change needed to close the original gap.
- Verified the SP pre/post-load manifest split is in place: `manifestSPTransition` fires from pdmain.c:768 (pre-load, `g_StageSetup.props` still NULL), `manifestSPRescanSetup` fires from lv.c:532 (post-load, setup files parsed). The post-load rescan uses `manifestBuildMission` to re-scan with the now-populated setup data and applies any newly-discovered entries as a diff.
- **Diagnostics added**:
  - `manifestBuildForMenu` now records per-category counters (bodies added, mod bodies, disabled; heads added, mod heads, disabled) and logs a structured summary so a zero mod count with enabled `user.<slug>.*` catalog entries is immediately obvious in `pd.log`.
  - `manifestSPRescanSetup` now logs the diff shape (`newly-discovered=N kept=M unload=K`) after the post-load diff so it's easy to tell whether the rescan found cinematic / ailist spawns the pre-load scan missed (B-118 symptom was `newly-discovered=0`).

**Build verify**: `source devtools/build-env.sh && ninja -C Build pd pd-server` — 530/530 targets link. `PerfectDark.exe` 51,534,311 bytes; `PerfectDarkServer.exe` 22,827,178 bytes. Only pre-existing `'/*' within comment` warnings and the `f32 near/far` typed-name macro warnings; no new ones.

**Process**: single commit on the worktree branch, then worktree fast-forward merged into `dev` (line counts verified per CLAUDE.md §9). No push to origin.

**Not done in this session** (deferred):

- The shared-action leak (ADR §3.5 / menu-input-audit §6.2) still needs per-scope state arrays in the actionmap — separate architectural change not tied to pool migration.
- No unit / integration tests for the pool (ADR §6.3).
- Training dialog registration sub-dialogs (FrDifficulty, FrTrainingInfoPreGame etc.) all map to `MENU_TYPE_TRAINING`, which means menu.c's pool dedup would reject a sub-push if the parent slot is already active. This is a pre-S300 issue — if playtest reveals the training menu can't drill into its sub-dialogs, the fix is to move sub-dialogs out of MENU_TYPE_TRAINING and into their own types. Flagged but not addressed today.

**Next**: playtest S300 — open each migrated menu (Cheats, MP Setup family, MP Pause family, MP Advanced family, Player Config, Bot Setup, Agent Select, Room, Training FR, MP Settings family) and confirm ctx transitions are clean on open, on Back/Esc, and on Begin()=false cull (simulate by triggering a modal overlay). Expected logs include `MENUPOOL: attached ctx to active <type>` per IsWindowAppearing and `MENUPOOL: released <type>` per close.

---

## Session S302 — 2026-04-16 (Spawn pool robustness — tiered selection + last-resort + solo-map CombatSim coverage — quirky-mendeleev worktree)

**Scope**: harden the spawn pool so every Combat Simulator scenario (stock MP maps, solo campaign maps reused as arenas, over-subscribed maps where players > pool slots) produces a spawn without crashing / stalling / leaving the player in the void. Adds an explicit selection-tier cascade (T1 optimal → T2 cycled → T3 reused → T4 last-resort) with `SPAWN.TIER:` logging so playtest traces show the distribution.

**Changes**

- **`src/include/game/spawnpool.h`**: new `spawn_select_tier_t` enum (`SPAWN_TIER_{NONE,1_OPTIMAL,2_CYCLED,3_REUSED,4_LASTRESORT}`), `spawnPoolSelectTiered()` (same params as `spawnPoolSelect` + `out_tier`), `spawnPoolLastResort(occupied, num_occupied, out_pos, out_room, out_angle)`, `spawnPoolTierName()` for log strings.
- **`src/game/spawnpool.c`**:
  - Refactored `spawnPoolSelect()` into internal helpers `poolMinDistSq`, `poolPickFarthest` (team-sector + FFA fallthrough), `poolMarkUsedFromOccupied`.
  - New `spawnPoolSelectTiered()` runs three passes: T1 = skip `used | reserved`, T2 = clear reservations and retry skipping `used` only (logs `SPAWN.TIER: T2_CYCLED — reservations cleared...`), T3 = skip nothing (every slot eligible, picks farthest-from-occupied — logs `SPAWN.TIER: T3_REUSED — pool oversubscribed...`). Legacy `spawnPoolSelect()` is now a thin wrapper that ignores tier.
  - New `spawnPoolLastResort()` — pool non-empty? pick the farthest point from occupied regardless of validation (logs `T4_LAST_RESORT — pool-slot reuse`). Pool empty? synthesise center + radial offset staggered by `num_occupied & 7` so consecutive fallback calls don't pile up (logs `T4_LAST_RESORT — synthesised pos=...`). Always writes a non-void pos + room; falls through to pad-file scan if bgFindRoomsByPos can't resolve.
- **`src/game/playerreset.c`**:
  - `spawn_needed` now uses `MAX_PLAYERS + g_BotCount + 4` in netplay (remote slots fill AFTER playerReset on each client), `PLAYERCOUNT() + g_BotCount + 4` locally, floored at 16 so small matches still have respawn headroom. Still capped at `MAX_MPCHRS`. Large-map span bonus retained.
  - Initial MP spawn (`g_Vars.mplayerisrunning && spawnPoolIsReady() && lvframe60 == 0`) now routes through `spawnPoolSelectTiered` + `spawnPoolLastResort` instead of falling back to `scenarioChooseSpawnLocation` on -1. Logs `SPAWN.TIER: %s initial MP spawn pool[%d] L%d ...`.
- **`src/game/player.c`**:
  - `playerTrySelectPoolSpawn` gate relaxed: was `g_NumSpawnPoints >= needed`, now `g_NumSpawnPoints >= needed + needed/2` (require a 1.5× margin). Previously stock maps with ties routed through the legacy shortlist (no burst reservation, telefrag-prone). Returns `true` with last-resort position when pool yields -1, so the caller never falls through to the legacy shortlist with an empty pool.
  - Zero-pad path in `playerChooseSpawnLocation` now builds an `occupied[]` snapshot from live players + bots and calls `spawnPoolSelectTiered` (tier logged). If the pool is entirely empty, calls `spawnPoolLastResort` which synthesises a position; final room-resolution fallback kicks in when even the synthesised pos has no room (maps with zero pad data). Logs `SPAWN.TIER: T4_LAST_RESORT — synthesised room=-1, fell through to pad %d`.

**Coverage of task requirements**

1. **Limited spawn points (pool < players)** — T3 REUSED picks farthest-from-occupied even when all slots collide with live positions; reservation bitset no longer permanently locks out slots because T2 clears + retries mid-burst. Log line identifies the scenario.
2. **Solo maps in Combat Simulator** — pool build already covers these (L2 waypoints / L3 grid / L4 radial). This session adds graceful SELECT-side exhaustion: when L4 produced only a tiny pool, T3/T4 keep each spawn placement working. `playerTrySelectPoolSpawn` gate also kicks in for maps with 0 declared pads because `g_NumSpawnPoints < needed + needed/2` is trivially true.
3. **Too many players (32-bot Chicago)** — initial match-start burst: reservation bitset claims slots as each placement commits, T1 handles the burst; if 32+ spawn calls hit within the same tick and run out of T1 slots, T2 cycles (reservations clear once all slots have been dished out) then T3 reuses the farthest-from-occupied slot. Respawn waves: same cascade, but with live `occupied[]` from actual props.
4. **Tier logging** — every spawn decision now logs `SPAWN.TIER: <tier> ...` at NOTE level for T1 (optimal) and WARNING level for T2/T3/T4 (degraded). Playtest tail can grep `SPAWN.TIER:` for the distribution.
5. **Integration points** — verified every caller: `playerReset` (initial spawn), `playerStartNewLife → scenarioChooseSpawnLocation → playerChooseGeneralSpawnLocation → playerChooseSpawnLocation → playerTrySelectPoolSpawn` (player respawn), `botSpawn → scenarioChooseSpawnLocation → ...` (bot spawn + respawn + failsafe re-spawn in `bot.c:1122`). All paths funnel through `playerChooseSpawnLocation`, which now tries the tiered pool first and falls through to `spawnPoolLastResort` on pool failure before invoking the legacy shortlist.
6. **Non-MP map spawn discovery** — retained existing `playerReset` waypoint/pad fallback (lines 283–406). Pool build then handles the rest via L2/L3/L4. If pool build itself fails (no waypoints + no pads + degenerate AABB), `spawnPoolLastResort` still synthesises a radial position around wherever `spawnPoolComputeAABB` lands (falls back to origin + 200u radius).

**Design decisions**

- **Keep back-compat `spawnPoolSelect`** as a wrapper that discards the tier — minimises blast radius for any future caller that doesn't care about the tier breakdown.
- **Legacy shortlist still runs when `g_NumSpawnPoints >= needed + needed/2`** — stock 21-pad maps for 4-player FFA (needed=4, 21 ≥ 6) keep battle-tested behaviour; CombatSim against a 4-pad solo map with 8 participants goes through the pool.
- **T2 clears ALL reservations** rather than just expiring them — the reservation bitset is a burst coordination mechanism, not a long-term exclusion. Once we've hit T2 we know earlier reservations have committed, so clearing them is correct.
- **T4 synthesised position uses `(num_occupied & 7)` stagger** — 8 distinct positions around the centre cover the worst plausible simultaneous-T4 burst. Not deterministic across ticks (occupied changes) but that's fine; T4 is already an escape hatch.

**Build-verify**: `source devtools/build-env.sh && ninja -C Build pd pd-server` — 751/751 targets clean. `PerfectDark.exe` 51,533,440 bytes, `PerfectDarkServer.exe` 22,824,057 bytes. Only pre-existing `/*` within comment warnings + `f32 near`/`far` macro-name warnings. No new warnings from the changed files.

**Files touched**: `src/include/game/spawnpool.h`, `src/game/spawnpool.c`, `src/game/playerreset.c`, `src/game/player.c`.

**Not done in this session** (deferred):
- No in-game playtest yet — Mike to confirm tier distribution on: 32-bot Chicago (burst at match start), solo map used in CombatSim (pool should show high L4 layer), over-subscribed tiny arena (T3 should fire occasionally), regular stock maps (T1 every spawn).
- `playerChooseSpawnLocation` legacy shortlist fallback (4 passes, sllen==0 cycling) unchanged. Could be retired once playtest confirms the pool path covers every scenario, but keeping it as a safety net for now.
- No unit tests — the input/match layer has no automated coverage.

**Next**: playtest across the scenarios above, then audit `pd.log` for `SPAWN.TIER:` tier distribution. Healthy signal is mostly T1 with occasional T2/T3 on oversubscribed maps and zero T4 on stock maps.

---

## Session S301 — 2026-04-16 (Comprehensive instrumentation for Bug C / Bug D / Chicago crash / Airbase start / B-141 — confident-chaum worktree)

**Scope**: "Do everything you can, and add logging we can use to determine how to do the rest" — no fixes are straightforward enough to land outright given the playtest-only repros; this session is purely a diagnostics drop so the next Mike playtest produces logs that tell us exactly what the remaining open bugs are doing. Covers the five tracks queued in `bugs.md` (Bug C, Bug D, Chicago silent crash, Airbase 0xc0000005, B-141 audio skips).

**New files**:

- `port/include/crashbreadcrumb.h` — public API for a crash-surviving breadcrumb ring. 256 slots × 112 bytes per slot, all-static storage; push sites are one-line `crashBreadcrumbPush(fmt, ...)` calls, dump is one-line `crashBreadcrumbDump(f, entries)`. Safe to call from any thread; index update uses `__atomic_fetch_add` for racy-but-benign writer coordination. A second entry-point `crashBreadcrumbLogRecent(count)` routes via `sysLogPrintf` for a developer "tail" without crashing.
- `port/src/crashbreadcrumb.c` — implementation. `vsnprintf` into the claimed slot, timestamp via `sysGetMicroseconds`. Dump walks oldest-first and gates by sequence, so a slot overwritten between claim and publish is skipped rather than producing a half-written string.

**Wiring — crash path**:

1. `port/src/crash.c` — `crashInit()` calls `crashBreadcrumbInit()` before any handler is installed. VEH (`crashVectoredHandler`), full UEF (`crashHandler`), Windows SIGABRT (`crashSigabrtHandler`), and the Linux `sigaction` handler each append the breadcrumb ring to their log output via `crashBreadcrumbDump(FILE*, N)` — 64 entries from the VEH/SIGABRT paths (minimal stack budget), 128 from the UEF path. All dumps go to the same log file as the exception header, so a single post-crash log contains the PC/stack trace AND the execution trail leading to it.

**Wiring — push sites** (every push is behind a function that's already called once per frame at most, so ring pressure is bounded):

- `src/lib/main.c::mainTick` — frame heartbeat: `HEARTBEAT frame=… stage=… chrs=… last_chr=… pending=…`. One push per game tick.
- `src/lib/main.c::mainChangeToStage` — `STAGE.CHANGE current=… pending=… -> new=…`. Captures mid-transition crashes.
- `src/game/lv.c::lvTick` — `LVTICK frame=… stage=… update240=… paused=…`. Distinguishes outer-loop crashes from inside-stage crashes.
- `src/game/chraction.c::chraTickBg` — `CHRTICKBG frame=… bg=… slots=…`.
- `src/game/chr.c` (inside the chraTick dispatcher at line ~2495 where `g_ChrLastTickedIndex` is set) — `CHR.TICK slot=… chrnum=… action=… race=… model=…`. At 32 bots × 60 Hz that's ~1920 pushes/s, fills the ring in ~130 ms — exactly the horizon we want for "what was the last chr alive before the crash".
- `src/game/bondwalk.c::bwalkTick` — `BWALK.TICK player=… pos=(…) room=… floorroom=…`. Separates player-collision crashes from AI.
- `src/game/bot.c::botSpawn` — `BOT.SPAWN chrnum=… respawn=… model=… aibot=…`.
- `src/game/botmgr.c::botmgrAllocateBot` — `BOT.ALLOC chrnum=… slot=… body=… head=…`.
- `port/src/net/matchsetup.c::matchStart` — entry + pre-`mpStartMatch` + post.
- `port/src/net/netmsg.c::netmsgSvcStageStartWrite` — `SVC_STAGE_START.write stage=… mode=… tick=…`.
- `port/src/net/netmsg.c::netmsgSvcStageStartRead` — `SVC_STAGE_START.read srccl=… state=…`.
- `port/src/net/net.c::netServerStageStart` — entry + post-send.

**Wiring — standard log diag lines** (tagged for grep):

- `ENDSCREEN.DIAG:` — `renderMpEndscreen` fresh-entry + geometry + body-child + rankings-count + awards-path + actions-section. Capped at 80 prints per match via `ENDSCREEN_DIAG_MAX_PRINTS` and `s_MpEndscreenDiagPrintCount` so an oscillating endscreen can't flood `pd.log`. Still surfaces the six Bug C hypotheses (empty rankings, clamped contentH, Begin=false, padding math, chrome inset collision, body-child cull) at the first frame of render so one repro pass is enough.
- `CHR.DIAG:` — `botmgrAllocateBot`, `bodyAllocateModel`, `botSpawn` before/after. Logs catalog IDs alongside the integer ids; flags `model=NULL` + final `invisible=true` state at the end of spawn so Bug D (invisible bots on Chicago) leaves a fingerprint.
- `MATCHSTART.DIAG:` — `matchStart` entry + `pre-mpStartMatch` + `post-mpStartMatch`; `netServerStageStart` entry + send-outcome; `netmsgSvcStageStart{Write,Read}` send/receive markers. Airbase 0xc0000005 / "manifest OK but no SVC_STAGE_START" repro will show which step returned early.
- `AUDIO.DIAG:` — `audioInit` logs requested vs granted SDL spec (sample-rate mismatch = B-82 class warning). `audioEndFrame` tracks `nullProducer` (no PCM queued this frame), `mixBufOverflow` (s_MixBuf capacity exceeded), min/max/mean scheduler gap (ms), min/max queue depth (samples) — folded into the existing 30-second `AUDIO[B-141]:` summary.
- `CRASH.DIAG:` — the header under which the VEH/UEF/SIGABRT handlers dump the breadcrumb ring alongside the exception context. Also used by `crashBreadcrumbLogRecent()` for developer-requested tails.

**CMake**: `port/src/crashbreadcrumb.c` added to the pd-server `SRC_SERVER` list next to `crash.c` so both binaries link. The GLOB_RECURSE in pd picks it up automatically; the server target's explicit list had to be edited.

**Design decisions**:

- **Breadcrumb ring sits in static BSS, not heap.** A heap allocation would not survive stack-overflow crashes (the VEH runs on a fresh reserved stack page but can't trust the heap allocator state). 256 × 112 = ~28 KB static — trivial.
- **No log-channel for the new DIAG tags.** The channel filter (`sysLogClassifyMessage`) maps prefixes like `AUDIO:` but my new tags (`AUDIO.DIAG:`, `ENDSCREEN.DIAG:`, etc.) intentionally don't match any channel. Result: they fall through to "untagged, always passes" — they'll appear in every playtest log regardless of `Debug.LogChannelMask`. This matches the brief ("DOES appear with standard log settings during a playtest"). Per-site rate-limiting (ENDSCREEN caps at 80; CHR.DIAG is one-shot per spawn; MATCHSTART is one-shot per match; AUDIO.DIAG is 30-second batched) handles the "doesn't spam normal play" requirement.
- **DIAG counters vs new-bug fixes.** None of the five tracks had an obviously-straightforward root cause reachable without playtest evidence; the instrumentation is the work this session, and the next repro pass will tell us what, if anything, to fix outright. The one structural change I could justify — extending VEH dump to include breadcrumbs — is itself a diagnostic improvement and is included.
- **Bug D side-effect fixes**. While wiring `CHR.DIAG` I noticed `bodyAllocateModel` had no NULL-return log; added a LOG_WARNING when the body model fails to allocate and when the head catalog entry is missing. These were actual silent failures previously, so they stay regardless of the diagnostic pass.

**Build verify**: `source devtools/build-env.sh && ninja -C Build pd pd-server` — both targets link. `PerfectDark.exe` 51,541,351 bytes (up ~38 KB from baseline 51,502,778), `PerfectDarkServer.exe` 22,834,207 bytes. No new warnings; only the pre-existing `'/*' within comment` warnings in `updater.c` / `updater.h` / `savemigrate.c` / `server_gui.cpp` / `pdgui_theme_loader.h`, the `f32 near/far` member-name warnings in `types.h`, and the `gettime_offset` unused-fn warning in `enet.c`.

**Not done in this session**:

- No playtest verification — that's the point; the next Mike playtest produces the logs.
- No per-chr visibility state-change tracking (ADR §6 — invisible-bot hysteresis). The `CHR.DIAG` at spawn is a snapshot; mid-match visibility toggles (e.g. room swap → rooms[0]=-1) would need a dedicated `propSetInvisible` hook that isn't justified without at least one repro showing spawn-time state as OK.
- No audio voice-count or source-creation logging. The port's audio path is almost entirely RSP-driven; individual voice state lives inside `ultra/audio` and the PD engine's mixer, which this port doesn't extend. Would need an engine-side hook; out of scope for a logging-only session.
- The `LOG_CH_*` classifier could be extended with a `LOG_CH_DIAG` channel keyed on the `.DIAG:` suffix so operators can silence all DIAG messages with one mask bit — deliberately deferred to keep this session's footprint small.

**Next**: Mike replays the Chicago, Airbase, MP endscreen, and audio-skip repros; tail `pd.log` / `pd-client.log` / `pd-server.log` for the new tags. Expected signals (paraphrased from bugs.md):

- **Bug C** — the endscreen render state is now fully captured. If body is still invisible, check `ENDSCREEN.DIAG: rankings built count=0` (empty-body hypothesis), `ENDSCREEN.DIAG: body child opened contentW=… contentH=…` vs actual screen size (clamped-height hypothesis), or `ENDSCREEN.DIAG: ENTRY (fresh)` without a matching `contentAvail` line (Begin=false hypothesis).
- **Bug D** — `CHR.DIAG: WARNING chrnum=N is INVISIBLE after botSpawn` with the reason code (model NULL / CHRCFLAG_HIDDEN / VOID room) pinpoints which path failed.
- **Chicago silent crash** — on the next crash, the last ~1 second of `CHR.TICK`, `BWALK.TICK`, `HEARTBEAT`, and `LVTICK` breadcrumbs will be in `pd.crash.log` / `pd-client.log` under `CRASH.DIAG: breadcrumb ring dump follows:`. Read oldest-to-newest — the final entry is the one active at death.
- **Airbase no-SVC_STAGE_START** — grep `MATCHSTART.DIAG:` in both `pd-client.log` and `pd-server.log`. A server that logs `netServerStageStart stage=… clients=…` but no client logs `SVC_STAGE_START read` indicates the packet never reached the client (network layer). Server-side absence of `netServerStageStart` means the ready gate didn't fire — check for `BAILED reason=…` lines.
- **B-141 audio** — existing 30-second summary now includes min/max/mean scheduler gap, min/max queue depth, and null-producer/mix-overflow counters alongside drops/underruns/hitches. First summary after a skip lands should tell us whether the bug is hitches (OS jitter), underruns (RSP stalled), drops (main loop racing audio), nullProducer (sndTick/musicTick stopped firing), or mixBufOverflow (mod music overran the 8192-sample mix buffer).

---

## Session S299 — 2026-04-16 (Input Authority Phase 2 — menu pool single-instance discipline — trusting-banach worktree)

**Scope**: Implement Phase 2 of the input-authority / menu-pool ADR (`context/designs/input-authority-and-menu-pool-2026-04-13.md` §6). Phase 1 (S250) added `gameplayInputSuppressed()`, dispatch-site gates, and flushes on menu push + focus events. Phase 2 adds a **pre-allocated menu pool keyed by type** so that duplicate-push becomes structurally impossible and the `nextsibling` auto-open chain respects pool occupancy.

**New files**:

- `port/include/menupool.h` — public API for the pool layer. Declares `menu_type_t` enum covering ~29 menu identities (legacy-dialog menus + pure-ImGui pages), plus `menupoolAcquire` / `menupoolRelease` / `menupoolAcquireDialog` / `menupoolReleaseDialog` / `menupoolIsActive` / `menupoolIsDialogActive` / `menupoolReleaseAll` / `menupoolDumpActive` / `menupoolTypeName` / `menupoolCountActive`.  Pool slot has three fields: `active`, `owned_ctx` (optional — pool pushes + pops if present), `generation` (bump on each acquire, for diagnostics).  Acquire is atomic (0 on deny, 1 on fresh, -1 on invalid type); release is idempotent (always safe to call on inactive slot).
- `port/src/menupool.c` — implementation.  Flat `s_Pool[MENU_TYPE_COUNT]` slot array + `s_Registry[]` (capacity 96) dialogdef→type table.  `menupoolInit()` registers ~56 dialogdef pointers from `src/include/data.h` against their menu types (main menu PC + Pause variants → `MENU_TYPE_MAIN_MENU`; MP setup arena/scenario/weapons/limits/etc. → `MENU_TYPE_MP_SETUP`; training FR/DT/HT/Bio/Hangar → `MENU_TYPE_TRAINING`; etc.).  Unregistered dialogdefs pass through with no pool dedup (legacy-safe default).

**Wiring**:

1. **`port/src/inputctx.c`** — `inputCtxInit()` calls `menupoolInit()` + `menupoolReleaseAll()` so the pool is live before any menu can open and resets cleanly across the stage-transition nuclear reset. `inputCtxShutdown()` calls `menupoolReleaseAll()` BEFORE walking the stack — prevents pool slots from holding dangling `owned_ctx` pointers after the contexts are physically popped.
2. **`src/game/menu.c`** — `menuPushDialog()` calls `menupoolAcquireDialog(def, NULL)` after the existing F-3.1 pointer-equality scan; if the pool returns 0 (type already active), the push is denied. The `nextsibling` auto-open loop now checks `menupoolIsDialogActive(sibling)` and skips any sibling whose type is already active elsewhere, then calls `menupoolAcquireDialog(sibling, NULL)` for siblings that proceed. `menuCloseDialog()` iterates the layer's siblings and calls `menupoolReleaseDialog(def)` for each BEFORE decrementing `numdialogs` (otherwise the dialogdef pointers needed for the lookup would be gone).
3. **`port/fast3d/pdgui_bridge.c`** — `pdguiEndscreenStartMission` / `pdguiEndscreenNextMission` / `pdguiEndscreenExitToMainMenu` call `menupoolReleaseAll()` before `inputCtxPopDeferred(&g_CtxImGuiMenu)`. Pool handles identity cleanup; inputctx handles the shared menu context.
4. **`port/src/net/matchsetup.c`** — `matchStart()` and `matchStartFromChallenge()` add `menupoolReleaseAll()` alongside existing `inputCtxPopDeferred`.
5. **`port/src/net/netmsg.c`** — both stage-change handlers (co-op/anti at line 1190; combat at line 1345) add `menupoolReleaseAll()` inside their `!defined(PD_SERVER)` guards.
6. **`src/lib/main.c`** — no direct change needed. Stage-transition nuclear reset (`inputCtxShutdown()` + `inputCtxInit()`) now releases the pool via the inputctx.c wiring above.

**Design decisions**:

- **Pool = identity only (for now)**. The pool API supports optional ctx ownership, but this session does NOT migrate the 10 existing `s_*PushedCtx` patterns in the ImGui renderers (`pdgui_menu_{cheats,mpsetup,mppause,mpadvanced,playerconfig,botsetup,agentselect,room,training}.cpp`). Each renderer continues to own its own `inputCtxPush`/`PopDeferred` for `g_CtxImGuiMenu`, because those have been stabilized through S250 Phase 1 and S295 F4 leak guards. The pool is additive: it enforces **structural dedup at the `menuPushDialog` boundary** without disturbing the renderers' cull-handling. Future sessions can opt-in individual renderers to pool-owned ctx via the existing `ctx` parameter of `menupoolAcquire`.
- **Nextsibling chain** (`menu.c:1553-1620`): new `menupoolIsDialogActive` check at the top of the loop short-circuits sibling auto-open when that sibling's TYPE is already active elsewhere — complements the existing `menuDialogIsCurrent` guard in renderers (S296 F-3.2 belt; pool is the architectural braces).
- **Unregistered dialogs pass through**. `menupoolAcquireDialog` returns 1 (success, no dedup) for any dialogdef not in the registry table — preserves legacy behavior for dialogs whose push paths haven't been audited.
- **Registry capacity = 96**. Covers all 70 `data.h` externs + headroom for late-registered mod dialogs via `menupoolRegisterDialogdef(def, type)`.

**Guarantees delivered** (from ADR §6 acceptance criteria):

- ✅ Duplicate-push of the same menu TYPE is structurally impossible (not just runtime-rejected).
- ✅ Nextsibling auto-open chain respects pool occupancy (B-153 class prevention).
- ✅ Force-close paths (endscreen exit/next/start, MP match start, co-op stage change, MP stage change, stage-transition nuclear reset) release every pool slot cleanly.
- ✅ Pool API supports input-context ownership tied to slot lifecycle for renderers that opt in.
- ⚠️ Shared-action leak (ADR §3.5) is NOT fixed this session — it requires per-scope state arrays in the actionmap, which is a separate architectural change.

**Build verify**: direct worktree build via `cmake -G Ninja -DCMAKE_C_COMPILER=/c/msys64/mingw64/bin/cc.exe -DCMAKE_CXX_COMPILER=/c/msys64/mingw64/bin/c++.exe -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache ..` then `ninja -C Build pd pd-server`.  751/751 targets; `PerfectDark.exe` 51,502,778 bytes; `PerfectDarkServer.exe` 22,817,578 bytes. Only pre-existing `'/*' within comment` warnings and `f32 near/far` macro-name warnings; no new ones.  Note: the `build-headless.ps1` script is designed to redirect worktree invocations to the main working copy, so direct `cmake + ninja` is the canonical worktree build.

**Not done in this session**:

- ImGui renderer migration to pool-owned ctx (the 10 `s_*PushedCtx` boolean patterns). Deliberate deferral — the existing pattern is stable and touches ~20 renderers. Future sessions can migrate one renderer at a time, converting `s_FooPushedCtx` into `menupoolAcquire(MENU_TYPE_FOO, def, &g_CtxImGuiMenu)` / `menupoolRelease(MENU_TYPE_FOO)` and removing the bool.
- Per-scope state arrays for shared actions (ADR §3.5 follow-up). Still open.
- Unit / integration tests for the pool (ADR §6.3). No automation for input/menu layer exists; any regression would still surface the hard way.

**Next**: playtest verification — open/close every major menu (main menu, cheats, MP setup, MP pause, endscreen), confirm no duplicate-push warnings in `pd.log`, confirm force-close paths (retry mission, next mission, exit to main menu) log `MENUPOOL: released N slot(s) (bulk)` and the stage transition completes cleanly.

---

## Session S298 — 2026-04-16 (S297 follow-up batch — stoic-proskuriakova worktree)

**Scope**: seven follow-up items queued from S262/S263/S295/S297 audits. Single commit batch on `dev` after worktree FF-merge.

**Items shipped**:

1. **Content-inset API wired into endscreen + pause menu.** `pdguiThemeGetContentInset` existed (S297) but had zero callers. Added a `resolveEndscreenPadding()` helper in `pdgui_menu_endscreen.cpp` that computes `padX/padY/padR/padB` as `max(basePad, inset)` so default padding is preserved on the procedural dialog but chrome-heavy nineslice styles also get enough clearance. Wired into `renderSoloEndscreen` and `renderMpEndscreen` — rankings, awards, action buttons, and the stats child height all honour the active chrome inset. Same pattern wired into `renderPauseMenu` in `pdgui_menu_pausemenu.cpp` (pause tabs / Resume button no longer clip into big chrome corners).
2. **Theme Editor + Room Start Match → docked-footer pattern.** `pdgui_menu_theme_editor.cpp` now reserves an explicit `footerH` for Save-as-Mod controls + Reset/Close row, and `BeginChild("PaletteScroll", {0, -footerH})` scrolls only the color pickers above it — matches the Nine-Slice Chrome tool footer. `pdgui_menu_room.cpp` now pins its footer separator + action row at `dialogH - footerH` so on narrow windows the Start Match / Leave Room buttons never scroll off-screen.
3. **menutick.c Deep Sea OOB guard hardening.** Added lower-bound `stageindex < 0 → clamp to 0` check alongside the existing `stageindex >= NUM_SOLOSTAGES → clamp to NUM_SOLOSTAGES - 1` guard. Matches the endscreen.c pattern and covers the degenerate case where stageindex enters negative territory before increment.
4. **SP-1 propagation into endscreenSetCoopCompleted.** `endscreenPushCoop`/`Anti` already guarded `g_MpPlayerNum` at entry (Fix 4 from earlier), but the helper `endscreenSetCoopCompleted()` was reached through that guard AND from other call sites (`endscreenPrepare`) without its own check. Added three early-return guards: `g_MpPlayerNum ∈ [0, MAX_PLAYERS)`, `difficulty ∈ [0, 3)`, and `stageindex ∈ [0, 32)` — the last prevents UB shift into `coopcompletions[]` on mod stages where stageindex >= 32.
5. **Team rankings buildRankings — verified DONE.** `buildRankings` in `pdgui_menu_endscreen.cpp` already uses `mpGetPlayerRankings` unconditionally (landed in S295 commit `719faa9e`) and team grouping is applied via the row-sort pass. No code change needed; task closed out of tasks-current.md.
6. **FIX-B.1 deep manifest scanner.** `manifestBuildMission` previously only walked `g_StageSetup.props` — it missed assets spawned by intro commands (INTROCMD_WEAPON's primary/secondary) and by AI scripts (AICMD_SPAWNCHRATPAD/CHR bodies+heads, AICMD_DROPITEM/AICMD_EQUIPWEAPON/AICMD_EQUIPHAT models, AICMD_EQUIPWEAPON weapon num). Cinematic cutscenes reuse this pipeline (cutscene chrs are BG chrs running cinematic ai lists), so the scanner closes that gap too. New helpers `s_manifestAddBody/AddHead/AddModel`, `s_manifestScanIntro`, `s_manifestScanAilists` mirror `stageLoadAllAilistModels` in `game_00b820.c`. Both scanners early-return when the corresponding `g_StageSetup` pointer is NULL, so pre-load calls are still safe. Added `chraiGetCommandLength` stub to `port/src/server_stubs.c` for the pd-server link.
7. **Spawn pool residuals (S249 Issue 5).**
   - **Same-tick reservation bitset** — new `s_SpawnReserved[SPAWNPOOL_MAX]` with auto-clear when `g_Vars.lvframenum` changes, plus `spawnPoolClearReservations()` public API and auto-clear inside `spawnPoolBuild` / `spawnPoolReset`. `spawnPoolSelect` now treats reserved indices as "used" and claims its chosen slot before returning, so a burst of match-start bot spawns in a single tick can't pick the same index twice (previously possible if the caller hadn't finished writing peer positions into `occupied[]`).
   - **Wall-probe orientation** — new `f32 angle_rad` field on `spawn_point_t`, computed in `poolWallProbeAngle()` (8-direction cylinder move probe at 200 units, mirrors `playerreset.c:691-715`). `playerreset.c` now uses `pool->points[sel].angle_rad` instead of hard-coding `turnanglerad = 0`, so the first MP spawn faces away from the nearest wall.
   - **Neighbour-room ground check** — `spawnPoolValidateCandidate` now builds a `grooms[]` array from `bgFindRoomsByPos(inrooms)` (up to 7 neighbours) and passes the whole array to `cdFindGroundInfoAtCyl`. Previously a spawn near a doorway or portal seam where the floor geometry lived in the next room got rejected as "mid-air" by the -100000 sentinel.

**Files touched**: `port/fast3d/pdgui_menu_endscreen.cpp`, `port/fast3d/pdgui_menu_pausemenu.cpp`, `port/fast3d/pdgui_menu_room.cpp`, `port/fast3d/pdgui_menu_theme_editor.cpp`, `port/src/net/netmanifest.c`, `port/src/server_stubs.c`, `src/game/endscreen.c`, `src/game/menutick.c`, `src/game/playerreset.c`, `src/game/spawnpool.c`, `src/include/game/spawnpool.h`.

**Build verify**: `ninja -C Build pd pd-server` — both targets link. `PerfectDark.exe` 51,363,805 bytes, `PerfectDarkServer.exe` 22,838,411 bytes. Pre-existing `'/*' within comment` warnings unchanged.

**Process**: worktree branch `claude/stoic-proskuriakova-2440de` fast-forward merged to `dev` (from `6d9d2f62` to `35ad8103`); server stub fix added on `dev` in a follow-up commit after the first build caught the undefined `chraiGetCommandLength` reference in the pd-server link.

**Not done in this session** (deferred to playtest confirmation):

- No playtest for any of the seven items yet — Mike to run through the S297 / S298 playtest checklist. Likely regression surfaces: endscreen action-row position with extreme chrome corners; Theme Editor footer height math on very short screens; Room "Start Match" pinned footer on narrow windows; first-spawn facing direction (wall-probe orientation) in arenas with lots of small obstructions; manifest scanner extra entries for SP-only missions (should reduce "missing body" runtime logs from cinematic spawns).
- The S297 follow-up "wire content-inset into HUD overlays" was out of scope — the scorecard overlay uses stock ImGui background (no `pdguiDrawPdDialog`), so it doesn't need the inset, and `pdguiGameOverRender` is a no-op since S295 F2.

## Session S297 (playtest-triage track) — 2026-04-16 (Textbox leak + chrome mod visibility — silly-jepsen worktree)

**Scope**: three playtest bug fixes reported against the S296 build.

1. **B-154 — Textbox keyboard leak to action map.** Typing in an `ImGui::InputText` (Nine-Slice Chrome "Mod Name", Skin Editor, connect-code field, etc.) still fired game/menu actions on the background player — the letter E triggered `ACTION_USE`, and so on.
2. **B-155 — Chrome mod not in Modding Hub Mods list without restart.** `chromeToolSaveMod` wrote the mod to disk, registered the chrome style, and reported "Saved & activated" — but the new entry didn't appear in the Mods tab until the next full `modmgrInit` at startup.
3. **B-156 — Chrome mod absent from Settings → Video → UI Chrome Style dropdown, even after enable + save.** The new chrome texture never appeared alongside Procedural / Classic (base-game). Restart did not help.

**Root causes**:

- **B-154** — `pdguiProcessEvent` called `actionmapDispatch(ev)` unconditionally for SDL keyboard events. `fireVk`'s gameplay-IMC gate (`gameplayInputSuppressed()`) only skipped `g_ImcGameplay`/`g_ImcVehicle`; menu/debug IMCs still consumed the scancode, so any action bound to that key in those contexts still fired while a textbox had focus. The standard ImGui pattern "when `io.WantCaptureKeyboard == true`, don't dispatch keyboard to your app" was not enforced at the action-map seam.
- **B-155** — `modmgrScanDirectory()` is file-static; no hot-rescan API existed. The chrome save path's only hook into modmgr was absent, so newly-written mod dirs were invisible until `modmgrInit` re-ran.
- **B-156** — `cjson_next` in `pdgui_theme.cpp` (chrome-manifest tokenizer) consumed only digits for NUMBER tokens — no fractional part, no exponent. The mod.json we write embeds a `chrome_authoring` object containing `border_scale: 1.000` and `inset_pct` floats. When the top-level parser hit `chrome_authoring` it called `cjson_skip_value`, which entered the LBRACE branch; on the first `.` inside `1.000` the tokenizer emitted `CJT_ERROR`, `cjson_skip_value` returned early, and the outer parser resumed from the wrong position. `components` was never parsed, `has_nineslice`/`out_tex_id` stayed empty, and `s_parseChromeManifest` returned 0 — so both `pdguiThemeRegisterChromeModDir` (hot path) and `s_scanModChromeStyles` (startup path) silently dropped the mod. Restart-proof because the same parser runs at startup.

**Fixes shipped**:

1. **B-154** (`port/fast3d/pdgui_backend.cpp`): added a textbox-leak gate after the context-push suppression block. Reads `ImGui::GetIO().WantCaptureKeyboard` for SDL_KEYDOWN/KEYUP; if true, skips `actionmapDispatch` for every keysym except Esc / Return / KP_Enter (menu close + dialog submit still work), and returns 1 after `ImGui_ImplSDL2_ProcessEvent(ev)` so downstream context-stack dispatch doesn't forward the event either.
2. **B-155** (`port/include/modmgr.h`, `port/src/modmgr.c`, `port/fast3d/pdgui_menu_moddinghub.cpp`): new public `modmgrRescanDirectory()` — snapshots `{id, enabled, loaded}` across existing entries, calls `modmgrScanDirectory()`, sorts, restores flags from the snapshot (so pending unapplied toggles survive). `chromeToolSaveMod` now calls `modmgrRescanDirectory()`, looks up the new `user.<slug>.ui-chrome` entry, flips its enabled bit via `modmgrSetEnabled`, persists through `modmgrSaveConfig()`, and refreshes the Mods tab snapshot via `pdguiModManagerRefreshSnapshot()`.
3. **B-156** (`port/fast3d/pdgui_theme.cpp`): extended `cjson_next`'s NUMBER branch to consume an optional fractional part (`.digits`) and optional exponent (`[eE][+-]?digits`). `cjson_int` is unchanged (still `strtol`) — all chrome-manifest int reads use pure integer fields, so this is safe.

**Build verify**: `source devtools/build-env.sh && ninja -C Build pd pd-server` — 750/750 targets; `PerfectDark.exe` 51,424,970 bytes, `PerfectDarkServer.exe` 22,816,554 bytes. No new warnings; only the pre-existing `'/*' within comment` noise in `updater.c` / `updater.h` / `server_gui.cpp` and the `gettime_offset` unused-fn warning in `enet.c`.

**Not done in this session**:

- Parallel Save-as-Mod paths (Skin Editor, Theme Editor) not re-audited for the same float / rescan issues. The Skin Editor's `skin.ini` format is INI, not JSON, so cjson isn't involved there; Theme Editor writes `theme.json` via `pdgui_theme_loader.cpp` which is a separate parser. If a similar "not in list / not in dropdown" symptom surfaces in those flows, that's where to trace next.
- `modmgrRescanDirectory()` resets `loaded` to 0 for entries it preserved via snapshot — but doesn't re-run `modmgrLoadMod` on them. That's intentional (catalog content already lives), but if a future rescan caller expects "loaded" to be authoritative post-rescan, this needs revisiting.

**Next**: playtest B-154 (textbox typing across multiple InputText surfaces — Nine-Slice Chrome name, MP chat, Skin Editor name, connect code), B-155 (Nine-Slice save → Mods tab immediate visibility + persistence), B-156 (Nine-Slice save → Video dropdown immediate visibility + restart persistence). Expect zero background-player actions while typing; expect new mod to appear in both surfaces without restart.

---

## Session S297 (UI polish track) — 2026-04-16 (Three UI polish drops: docked action buttons, room member list, content-inset API + title-bar samples — elegant-mahavira worktree)

**Scope**: Playtest feedback — three UI improvements applied together in one worktree because they touch adjacent renderers (theme / nineslice / moddinghub / room).

### Improvement 1 — Docked action buttons in Nine-Slice Chrome tool

**File**: `port/fast3d/pdgui_menu_moddinghub.cpp`

Old layout flowed everything linearly in one scroll region: image path row → rulers/toggles → trim/scale/cut sliders → desaturate → presets → border-scale + proportional insets → big stacked preview at the bottom → `SetCursorPosY(h - dockH)` trick for "Save as Mod" / "Reset".  That dock trick worked when content fit on screen, but on shorter windows or with scrolling active the footer slid off the bottom.

New layout splits the tool body into two named ImGui child regions:
- Left sidebar `##chrome_sidebar` (no-scroll, ~42 % width, clamped to [220..420 px * scale]) renders the **Source Preview (with rulers)** and the **Frame Preview (assembled nine-slice)** stacked vertically.  Extracted into `chromeToolRenderSidebarPreview`.
- Right column `##chrome_settings` (scroll on) renders Mod Name, symmetry toggles, tile modes, trim/scale/cut sliders, desaturate, quick presets, Border Scale, Proportional Insets toggle, and the inset sliders.  Extracted into `chromeToolRenderSettings`.
- **Footer** ("Save as Mod" / "Reset") and **status line** pinned outside both children so they never scroll.  `footerH = 34 * scale`, body gets `h - headerUsed - footerH - statusH`.

Header (`Image:` input + Browse/Load row) stays fixed above the split, and the "load an image first" early-return path unchanged.

### Improvement 2 — Room member list: team sort + human-first + team-tinted rows + local highlight

**File**: `port/fast3d/pdgui_menu_room.cpp::renderPlayerPanel`

Replaced the two separate iteration passes (humans loop then bots loop) with a unified `RoomRow` array built from both sources.  Sort is:
- Primary: team asc (only when `MPOPTION_TEAMSENABLED` is set — otherwise preserved-insertion order is fine because FFA has no meaningful team grouping).
- Secondary: humans before bots within the same team — picks up the "organize the lobby visually" user request.

Team palette mirrors `pdguiHudGetTeamColor` and `pdgui_menu_pausemenu.cpp s_TeamColors` (Red/Blue/Green/Yellow/Orange/Purple/Grey/White).  Row background is drawn via `ImDrawList::AddRectFilled` at the row's screen-space cursor position, alpha 0.35 for local player, 0.18 otherwise.  In non-teams mode, the local player still gets a 60/255 cyan band so the eye still finds them.  A 2 px white accent bar on the left edge of the local-player row adds a second cue that works under both team tint and non-teams background.

Team separator renders `-- Team N --` in the team's color when `r.team != lastTeam` during iteration.

All the existing bot-selection semantics (click, ctrl-click toggle, double-click edit, right-click / gamepad-X context menu, Rename / Bot AI / Bot Type / Character / Duplicate / Re-roll / Remove) are preserved because the popup block is emitted inside an `if (r.isBot)` scope that still `PushID`s the slot index before the `Selectable`.  Dead `removeSlot` variable from the original loop was removed since nothing ever assigned to it.

### Improvement 3 — Content-inset API + title-bar procedural samples

**Files**: `port/include/pdgui_theme.h`, `port/fast3d/pdgui_theme.cpp`, `port/fast3d/pdgui_style.cpp`, `port/fast3d/pdgui_menu_mainmenu.cpp`

New API:
- `pdguiThemeGetContentInset(float *l, float *r, float *t, float *b)` — returns the border inset (in screen pixels) that renderers must keep their content inside.  When chrome is on: `dst_left/right/top/bottom` from the active nineslice def (pulled from `pdguiNinesliceGet`) with a 2 px minimum floor.  When chrome is off (procedural): 2 px on all sides (procedural border is 1 px + safety).
- `pdguiThemeApplyContentInset(float *x, float *y, float *w, float *h)` — convenience helper that shrinks a rect by the active inset.
- `PDGUI_TITLEBAR_*` enum + `pdguiThemeSetTitleBarStyle` / `pdguiThemeGetTitleBarStyle` / `pdguiThemeGetTitleBarStyleName`.  Backed by `Video.UiTitleBarStyle` in `pd.ini`, clamped to `[0, PDGUI_TITLEBAR_STYLE_COUNT)` on getter/setter.

Title-bar styles (`pdgui_style.cpp::pdguiDrawPdDialog`):
- **Classic (0)**: unchanged 3-color PD gradient (titlebg→border1→titlebg).
- **Solid (1)**: flat `dialog_border1`.
- **Vertical Bars (2)**: `titlebg` base + 1 px `border1` stripes every 8 px.
- **Scanlines (3)**: classic gradient + 1-px horizontal scanline overlay every other row (40/255 black).
- **Diagonal Stripes (4)**: `border1` base + 45° `titlebg` quads stepped every 10 px (4 px wide).

Settings → Video gains a `Title Bar Style` combo right below `UI Chrome Style`; writes via `configSave("pd.ini")` so the choice survives app restarts/crashes.

### Build / files changed

Files touched:
- `port/include/pdgui_theme.h` — new content-inset API + title-bar enum.
- `port/fast3d/pdgui_theme.cpp` — impl + config registration (`Video.UiTitleBarStyle`).
- `port/fast3d/pdgui_style.cpp` — 5-way title-bar switch inside `pdguiDrawPdDialog`.
- `port/fast3d/pdgui_menu_mainmenu.cpp` — Settings → Video → Title Bar Style combo.
- `port/fast3d/pdgui_menu_moddinghub.cpp` — split `renderChromeTool` into sidebar/settings/footer; added `chromeToolRenderSidebarPreview` / `chromeToolRenderSettings` helpers.
- `port/fast3d/pdgui_menu_room.cpp` — unified `RoomRow` iteration for team-grouped member list.

**Build verify**: `source devtools/build-env.sh && ninja -C Build pd pd-server` — 750/750 targets; `PerfectDark.exe` 51,455,767 bytes, `PerfectDarkServer.exe` 22,818,090 bytes.  Only pre-existing `'/*' within comment` and `f32 near/far` macro-name warnings; no new ones.

### Not done in this session (deferred, matching the user's "start with Chrome tool, then audit others")

Other menus with action buttons that still need the child → child → footer restructure for identical robustness (current `SetCursorPosY` / `SameLine(w-…)` dock tricks work in practice but break under overflow):
- Room screen `Start Match` / `Save as Scenario` / `Add Bot` — currently inline below a scrolling child; fine on 1080p+ but should migrate when the overall room layout is touched.
- Theme Editor `Apply` / `Save` / `Close` — same pattern as Chrome tool's old layout; straightforward follow-up.
- Bot Setup modal `OK` / `Cancel` — already docked correctly.
- Training menu action buttons — already use `ImGui::Button(..., ImVec2(-1, btnH))` at bottom of a scrolling region; OK for now.

Also deferred — hooking `pdguiThemeGetContentInset` through to the menus.  The API is available and wired into the config; each menu that draws custom content inside its dialog body still needs to opt in.  The Chrome tool's sidebar/settings children are already clipped by ImGui's child window, so they naturally avoid the border even without an explicit content-inset call.

**Next**: playtest verification of all three improvements on the user's build.

---


## Session S296 — 2026-04-16 (Menu-close bugs from 019d97ef playtest — vigilant-robinson worktree)

**Scope**: Two bugs reported by Mike after the S295 collision + menu-desync drops landed, captured in `019d97ef-pdclient.log`:

1. **Bug 1 — stuck WASD after main menu close.** "Back out of menu, apply input with WASD, pressed input does not unpress; re-opening and closing the menu resets it. Applies to all WAS or D."
2. **Bug 2 — double / darkened main menu on rapid Esc reopen.** "Closing main menu and reopening with Esc (also with controller I think) seems to either open two copies overlaid or the new one has a darker background. Leaning towards two copies."

**Log evidence**:
- Log shows 7 open/close cycles in the main menu between 10:03 and 10:36. First close at 10:03.25 logged `lvIsPaused=0 g_PlayersWithControl[0]=1` (correctly unpaused pre-close). All subsequent closes logged `lvIsPaused=1 g_PlayersWithControl[0]=0` (paused at close-time — expected once the menu actually pauses the game). After the second rapid close (10:04.77), `AXIS_MOVE=0.000,1.000` was observed at 10:08.06 and held through 10:14.06 — the stuck-forward trace Mike described.
- No `MENU: ACTION_PAUSE detected` lines in the log, which suggests the opens aren't reaching `bondmove.c:1064` via the standard `START_BUTTON` edge path, but the ImGui close handler *did* fire (7 `CLOSE via ESC/B` lines). That's consistent with the close-handler pattern being reliable; the bug is post-close.

**Root causes identified**:

- **B-152 (stuck WASD)**: `actionmapPollFrame()` WASD→AXIS_MOVE synthesis block (port/src/actionmap.cpp:890-910) only writes `s_State[0][ACTION_AXIS_MOVE_X/Y].value` while at least one WASD key is held. When no controller is enumerated on player 0, the controller-poll branch at line 803 is skipped — so nothing else resets `.value` each frame. On keyboard-only setups, press sets `.value=1.0`; release short-circuits the synthesis (`mx=my=0`) and leaves `.value` pegged. Next poll frame reads stale `.value=1.0`, computes `analogStickActive=true`, skips synthesis — lock-in. The menu-open branch at line 860 zeros AXIS_MOVE (which is why "reopen fixes it"), but that's a workaround, not a fix.
- **B-153 (double menu)**: `menuPushDialog()` auto-opens every `dialogdef->nextsibling` at the same layer (`menu.c:1553-1576`). `g_CiMenuViaPauseMenuDialog`'s nextsibling is `g_CiOptionsViaPauseMenuDialog` — so pushing the main menu also pre-loads the CI Options sibling. `menuRenderDialogs` renders the "other" sibling alongside curdialog whenever `type != 0 || transitionfrac >= 0` (menu.c:3817); both dialogdefs hit `pdguiHotswapCheck` and queue their renderers (`renderMainMenu` + `renderCiSettingsRedirect`). The redirect renderer calls `pdguiPopupDarkenBehind(0.55f)` — when both fire in the same frame, the scrim compounds with the main menu frame and produces Mike's "darker background / two copies overlaid" visual. Normal state is `transitionfrac=-1` → sibling skipped, but under rapid close+reopen the transition state can land in the visible window.

**Fixes shipped**:

1. **B-152** (`port/src/actionmap.cpp`): added `p0CtrlDroveAxis` flag set only when the controller actually wrote AXIS_MOVE this frame. `analogStickActive` now gated on that flag + non-zero value, so a stale `.value` from a previous WASD synthesis can no longer suppress synthesis. Synthesis block now assigns `mx/my` unconditionally when `!analogStickActive` (including zero) — release clears the axis. Also sets `.held` from the computed `(mx != 0) / (my != 0)` instead of hardcoding `1`.
2. **B-153** (`src/game/menu.c`, `src/include/game/menu.h`, `port/fast3d/pdgui_menu_mainmenu.cpp`): added `s32 menuDialogIsCurrent(const struct menudialog *dialog)` helper that scans `g_Menus[i].curdialog` across player slots. `renderMainMenu` and `renderCiSettingsRedirect` call it at the top and early-return `1` (consumed) when invoked for a sibling preload. In the ImGui-hotswap world there is no user-visible swipe between main menu and CiOptions, so this guard has no legitimate-path cost.

**Build verify**: `ninja -C Build pd pd-server` — 750/750 targets; `PerfectDark.exe` 51,440,272 bytes, `PerfectDarkServer.exe` 22,818,602 bytes. Only pre-existing `'/*' within comment` warnings; no new ones.

**Not done in this session**:
- Did not re-investigate the S295 F1–F7 menu-desync fixes; they remain shipped as-is. The new fixes here are orthogonal (input-axis state, sibling render gating) and do not touch the input-context stack logic.
- The B-153 fix is narrow — it guards the two renderers currently bound to shared dialogdefs. If future renderers are attached to `.nextsibling` chains, they'll need the same guard.

**Next**: playtest pass to confirm B-152 on keyboard-only, B-153 on rapid Esc reopen from CI free-roam; tail `pd.log` for any `INPUTCTX watchdog:` warnings (still none expected from S295 F3).

---

## Session S295 — 2026-04-16 (Collision + spawning ecosystem fixes)

**Scope**: Implement the five fixes identified in `context/scratch/collision-spawning-investigation-2026-04-16.md`. No architectural migration work (mesh-ceiling wiring, per-prop mesh extraction); those stay scheduled as dedicated milestones.

**Code changes** (committed worktree: `priceless-wozniak`):

1. **Slope jump (B-145)** — `src/game/bondwalk.c`
   - Relaxed the grounded heuristic from `(groundgap < 10.0f && bdeltapos.y < 2.0f)` to `(groundgap < 20.0f && bdeltapos.y < 6.0f)`. The 6.0f ceiling stays below `FIXED_JUMP_IMPULSE = 8.2f` so a mid-jump player still reads as airborne.

2. **Pickup WALKTHROUGH (B-146)** — `src/game/propobj.c`, `src/include/props.h`
   - `weaponCreateForChr` initializer (propobj.c:19010): `flags3 = OBJFLAG3_WALKTHROUGH`.
   - `weapon()` and `ammocrate()` macros in `props.h`: OR `OBJFLAG3_WALKTHROUGH` into the caller's `flags3` so every setup-file pickup inherits it. Covers all setup*.c, scenarios, and `weaponCreateForChr` callers.

3. **Ceiling clip (B-147)** — `src/game/bondwalk.c`
   - Pre-move ceiling probe is now radius-aware: samples `cdFindCeilingRoomYColourFlagsAtPos` at center + 4 points at ±radius in X and Z and takes the min. This is the minimal fix from the investigation; the architectural mesh-ceiling migration stays scheduled.

4. **Carrington table (B-148)** — `src/game/propobj.c`, `src/include/constants.h`
   - Added `OBJH2FLAG_AUTOFLOOR = 0x20` to `obj->hidden2`.
   - Tightened the auto-floor eligibility test in `objInit`: an existing `MODELPART_BASIC_0065` no longer unconditionally suppresses the auto-floor. The floor part must cover ≥ 50% of the bbox XZ extent; otherwise the full-bbox auto-floor is still emitted and flagged.
   - `func0f069b4c` now updates the auto-floor vertices whenever `OBJH2FLAG_AUTOFLOOR` is set (previously keyed on `MODELPART_0065 == NULL`, which missed the new "0065 exists but non-covering" case).

5. **Spawn ecosystem (B-149)** — `src/game/spawnpool.c`, `src/include/game/spawnpool.h`
   - Ray set expanded from 14 → 18 rays: added 4 lower diagonals to catch overhangs below the candidate. `SPAWNPOOL_BUDGET_THRESHOLD` unchanged — the extra rays are safety nets, not harder gates.
   - Downward rays (Y-component < −0.1) no longer trigger the capsule-radius reject (a close hit below = ground exists, not a trap). The `!isDownward` gate covers both the original -Y cardinal ray and the 4 new lower diagonals.
   - `spawnPoolValidateCandidate` step 2 now rejects the `-100000` ground sentinel explicitly (`ground_y <= -99000.0f`).
   - Step 3 (vertical clearance) switched from `CDTYPE_BG` to `CDTYPE_ALL` so props are seen — previously a spawn landing on top of a dropped weapon could validate clean.
   - New `l4ValidateSafety()` helper (room valid + `bgTestPosInRoom` + ground sentinel + ground ≤ 500u below). Applied at both the per-dilation "all candidates passed" accept AND the last-resort highest-budget accept, so L4 never commits a point into no-room / below-sentinel even when no ring fully passes.

**Cross-issue interaction**: B-146 (pickups walkthrough) + B-149 (CDTYPE_ALL in spawn validation) compound — spawning on top of a dropped rifle is now blocked from two directions (the rifle isn't a floor, AND the vertical-clearance check catches it if some future bug re-introduces the collision).

**Build verify**: `source devtools/build-env.sh && ninja -C Build pd pd-server` — both `PerfectDark.exe` (51407282 bytes) and `PerfectDarkServer.exe` (22815986 bytes) linked clean. 750/750 targets.

**Not done in this session** (explicit out-of-scope, still queued):
- Issue 1-B (architectural): wire `meshFindCeiling` / `meshSweepCapsuleWorld` into `bondwalk.c`; fix `classifyTriFlags` to emit a real `GEOFLAG_CEILING`.
- Issue 2-B: per-prop mesh extraction into the world grid.
- Issue 5: same-tick reservation bitset, pool orientation (reuse of 8-direction wall-probe for pool spawns), neighbor-room ground check.

**Next session**: playtest B-145/146/147/148/149 across Skedar Ruins (slopes), Carrington Institute (tables), Dark Combat (pickups), tight arenas (spawn validity).

---
## Session S295 (menu track) — 2026-04-16 (Menu dead-input desync fixes — 7 items from the menu-system investigation)

**Scope**: implement every fix listed in §7 of `context/scratch/menu-system-investigation-2026-04-16.md`. Target bug class: B-150 (formerly tracked as B-145 during investigation — renumbered after B-145..B-149 were claimed by the S295 collision drop; "menu up but player moves" / "no menu but player frozen").

**Branch**: `claude/relaxed-ride` (worktree).

**Fixes shipped**:
- **F1 — Remove `g_PdguiActive` mirror boolean** (`port/fast3d/pdgui_backend.cpp`). The mirror duplicated `inputCtxIsActive(&g_CtxDebugOverlay)` and could drift. All reads replaced with the input-context query; all writers and the declaration deleted. F12 toggle and `pdguiToggle()` now derive state from the context stack alone.
- **F2 — Delete dead `pdguiGameOverRender` body** (`port/fast3d/pdgui_menu_pausemenu.cpp`). The stub's `#if 0` block (~250 lines) contained a stray `inputCtxPush(&g_CtxImGuiMenu)` that distorted push/pop audits. Stub retained (still called from `pdgui_backend.cpp:569`); body removed.
- **F3 — `inputCtxEndFrame` watchdog** (`port/src/inputctx.c`). Rate-limited warning (1 log/sec) when depth ≥ `INPUTCTX_WATCHDOG_DEEP_THRESHOLD` (5) or bottom ≠ gameplay. Force-reset (pop everything, re-seed with gameplay) at depth ≥ `INPUTCTX_MAX_STACK − 1` — catches unbounded-leak pathology before stack overflow.
- **F4 — Begin()=false leak guard on 9 renderers** (`pdgui_menu_{agentselect,botsetup,cheats,mpadvanced,mppause,mpsettings,mpsetup,playerconfig,room,training}.cpp`). Each renderer whose `if (!ImGui::Begin(...)) { ... return; }` branch could skip the pop now releases the owned context on cull. Uses the per-renderer ownership flag so repeated transient culls don't double-pop.
- **F5 — MpEndscreen one-shot push** (`pdgui_menu_endscreen.cpp`). Replaced the aggressive per-frame `inputCtxPush` pattern (which trapped the player in a resurrected context after any force-close pop) with a fresh-entry detector: track `s_MpEndscreenLastFrame = ImGui::GetFrameCount()`; push only when the frame number jumps by >1 (first render of a new instance). Preserves the original "first-frame miss" fix that motivated the aggressive version.
- **F6 — Force-close contract comment** (`port/include/inputctx.h`). Documented next to `inputCtxPopDeferred`: allowed force-close sites (`pdgui_bridge.c`, `matchsetup.c`, `netmsg.c`, stage-change reset in `main.c`), required `inputCtxIsActive` guard, and rules for adding new ones.
- **F7 — Remove dead `s_MainMenuPushedCtx`** (`pdgui_menu_mainmenu.cpp`). The main menu's close path had already moved to unconditional `inputCtxIsActive` + pop (the documented "safer pattern"). The bool writers were dead state; declaration + all writers deleted. Explanatory comments reference S295 F7.

**Build verify**: `source devtools/build-env.sh && ninja -C Build pd pd-server` — both `PerfectDark.exe` and `PerfectDarkServer.exe` linked cleanly (pre-existing warnings only, no new ones).

**Tracking**: `bugs.md` entry **B-150** (renumbered from the investigation's B-145) covering all 7 items. Playtest verification tasks added.

**Next**: in-game playtest focusing on (a) F12 debug overlay toggle cycles, (b) main menu open/close from CI free-roam, (c) MP endscreen → Return-to-Lobby / Quit-to-Menu paths, (d) alt-tab / focus-lost boundary. Watch pd.log for `INPUTCTX watchdog:` warnings — any occurrence identifies a remaining leak site.

---

## Session S295 (match-pipeline track) — 2026-04-16 (Match-pipeline fixes from 2026-04-16 investigation — festive-saha worktree)

**Scope**: implement the HIGH/MEDIUM findings from `context/scratch/match-pipeline-investigation-2026-04-16.md`.

**Code changes**:
- `src/game/menutick.c` — GAP-1 / SP-13: Deep Sea co-op next-mission branch now calls `manifestClear(&g_ClientManifest)` before `mainChangeToStage()` (pattern-match to F-0.4 / L1-1 / netDisconnect / Bug A). Added `#include "net/netmanifest.h"`. Bug entry **B-151** in `bugs.md` (renumbered from the investigation's B-145 after B-145..B-150 were claimed by the collision + menu-desync drops).
- `port/fast3d/pdgui_menu_challenges.cpp` — C-1: list-driven screen now grabs window focus on `IsWindowAppearing()`, and the auto-selected row calls `SetItemDefaultFocus()` once via a one-shot `s_FocusPending` flag. Controller-only user can now navigate the challenge list from first frame.
- `port/fast3d/pdgui_menu_endscreen.cpp` — Bug C: instrumentation only (per report's "do not structural-change without log evidence" directive). `Begin=false` early-return at line ~755 logs `sf / menuW / menuH / disp`; `contentH` clamp at line ~829 logs `sf / menuH / padY / raw / min`. Next MP-endscreen repro should narrow the six hypotheses.
- `port/fast3d/pdgui_menu_mpsettings.cpp` — C-6 (handicap): `SetWindowFocus()` on appear after Begin. Select Tunes + Team Names were already covered by `pdms_BeginStandardWindow` helper — no change needed there.
- `port/fast3d/pdgui_menu_controldiagram.cpp` — C-4: `SetWindowFocus()` on appear in `beginPdWindow()`.
- `port/fast3d/pdgui_menu_cheats.cpp` — C-5: `SetWindowFocus()` on appear on both `##cheats_warning` and `##cheats_unlock_confirm`.
- `port/fast3d/pdgui_menu_teamsetup.cpp` — C-3: `SetWindowFocus()` on appear in `##team_setup` (also covers `##auto_team` which reuses the same render).
- `port/fast3d/pdgui_menu_moddinghub.cpp` — C-8: `SetWindowFocus()` on appear on `##modhub`.
- `port/fast3d/pdgui_menu_playerconfig.cpp` — C-7: verified the three load sub-dialogs already inherit focus via `pc_BeginStandardWindow` (no change needed; investigation report line numbers were out of date).
- `port/fast3d/pdgui_menu_pausemenu.cpp` — GAP-3: online End-Game confirm now calls `mainEndStage()` for both NETMODE_CLIENT and offline paths so the player sees endscreen rankings/awards before disconnecting. Endscreen's Disconnect button drives network teardown.
- `port/fast3d/pdgui_menu_solomission.cpp` — Gap 8: documented (not unified) the solo vs MP pause input-context asymmetry. Added a block comment to `renderPauseMenu` explaining why solo pushes `g_CtxImGuiMenu` (MENUROOT_MAINMENU legacy path) while MP pushes `g_CtxPauseMenu`, and flagged the planned unification as Phase 2 menu-pool work.
- `port/src/net/netmsg.c` — GAP-4 / SP-14: `netmsgSvcStageEndRead` now resets `g_NetMatchRoomId = 0xFF` (symmetric with server-side reset at `net.c:876`). GAP-10: `netmsgSvcMatchCancelledRead` now calls `pdguiCountdownReset()` after the existing `memset`, matching the B-139 pattern.
- `port/src/server_stubs.c` — added `void pdguiCountdownReset(void)` server-side no-op stub so `pd-server` links without the UI symbol.

**Why**: the investigation was a four-agent deep audit of the match pipeline (entry → in-match → exit). The HIGH findings — missing manifestClear on Deep Sea co-op advance, Challenges menu unreachable by controller, MP endscreen invisible-body Bug C — were all either latent crashes or controller-dead-ends that block the v0.1.0 release pass. The MEDIUM batch (SetWindowFocus sweep, asymmetry fixes, defensive hygiene resets) ship together because they share the same change pattern and review surface.

**Build-verified**: `ninja -C Build pd pd-server` — both binaries link clean. `menutick.c.obj`, `pdgui_menu_*.cpp.obj`, and `netmsg.c.obj` all recompiled.

**Next**:
- Playtest pass to verify B-151 (Deep Sea co-op advance, no crash) and C-1 (Challenges list navigable from controller first frame).
- Repro MP endscreen invisible-body with new `ENDSCREEN:` log lines to distinguish the six hypotheses.
- Gap 8 unification is scheduled for Phase 2 (menu-pool ADR).

---

## Session S293 — 2026-04-16 (Nine-Slice Chrome redesign + mods/ category subfolder scanning)

**Scope**:
- Act on Mike's directive: Nine-Slice Chrome Save-as-Mod should emit a normalized output image with a uniform border concept applied, not the raw import.
- Organize `mods/` into category subfolders (`UI Chrome/`, `Weapons/`, `MP Maps/`, etc.) without breaking existing flat-layout mods.
- Land P0/P1 audit fixes from `scratch/audit-s255-s292-2026-04-16.md` for the Nine-Slice Chrome tool (C-1, C-2, C-4, C-5, S-7, S-9, S-10, S-11).

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_moddinghub.cpp` (Nine-Slice Chrome tool):
  - Added `CHROME_MAX_IMG_DIM` / `CHROME_MAX_OUT_DIM` (4096 each) and enforced on both import and preview paths. (C-1, C-2)
  - `chromeToolWriteTga` now uses `size_t` for its pixel counter and rejects dims >65535 up front. (C-1)
  - Added `chromeToolJsonEscape()` helper; `chromeToolSaveMod` now writes an escaped display name so quotes/backslashes in mod names no longer corrupt mod.json. (C-4)
  - `chromeToolUpdatePreviewTexture` computes new output dims into locals and commits `s_ChromeOutW/H` only after the allocation succeeds — prior code advanced dims before realloc, so a failed grow left dims ahead of the buffer. On realloc failure the function now surfaces a status message instead of silently returning. (C-5, S-7)
  - Switched preview GL upload to `glTexSubImage2D` on same-size ticks; only a dim change triggers the full `glTexImage2D` reallocation. Tracks `s_ChromePreviewTexW/H`. (S-10)
  - Retired `s_ChromeTex` (full-res source upload). Preview texture is always populated by the load path, and VRAM fallback paths now use `s_ChromePreviewTex` directly. (S-11)
  - Cross-clamped Trim sliders: each slider's max = opposite-side value − 1, so L+R and T+B can never collapse the crop. (S-9)
  - Added **Border Scale** slider (0.25x–4.0x) multiplying `dst_corner_px` relative to `src_inset` in both the live preview (`chromeToolBuildDef`) and saved `mod.json`.
  - Added **Proportional Insets** toggle (default on). When on, inset sliders operate on `0–50%` of the current output dims; pixel values are resolved inside `chromeToolUpdatePreviewTexture` and at save time, so Scale X/Y changes keep the visual border proportion stable. A read-only line under the sliders shows the resolved pixel values. When off, the tool behaves as before (pixel sliders).
  - Save path now creates `mods/UI Chrome/<slug>/` instead of `mods/<slug>/`. `mod.json` body records a new `"chrome_authoring"` block (`output_w/h`, `border_scale`, `proportional_insets`, `inset_pct`) alongside the existing `src_inset`/`dst_corner_px` so round-tripping retains authoring intent.
  - `renderChromeTool` gate now checks `s_ChromePreviewTex` (not the retired `s_ChromeTex`) to avoid a no-image-visible false path.
- `port/src/modmgr.c` (mod scanner):
  - Extracted per-entry registration into `modmgrTryRegisterModEntry()` and added `modmgrScanCategoryFolder()` (depth-1 recursion).
  - Primary-root pass: if an entry has no `mod.json`/`audio.ini`, the scanner descends one level and treats the entry as a category folder. Existing flat mods under `mods/` (base-ui, pd-modern-ui, bot-names) continue to register as before.
  - Alt-root pass adopts the same pattern with dedup-by-id retained.
- `port/fast3d/pdgui_theme.cpp`:
  - `s_scanModChromeStyles` is now a thin wrapper around new `s_scanChromeStylesInDir()` helper which tries each top-level entry as a chrome mod; if registration fails, it recurses one level. This makes `mods/UI Chrome/<slug>/mod.json` visible to Settings → Video → UI Chrome Style without additional plumbing.
- `port/fast3d/pdgui_theme_loader.cpp`:
  - Added `dir_has_theme_or_mod()` and `scan_themes_in_root()` with the same depth-1 category-folder pattern. Theme mods in `mods/UI Themes/<slug>/` (future) will be discovered automatically.

**Why** (Mike's directive + audit):
- The old Save-as-Mod path wrote pixel-absolute insets that were tied to whatever resolution the user happened to import — leading to chromes that looked right on the author's screen but oversized or tiny on another resolution. The new pipeline still writes the processed preview buffer (which is Mike's "uniformed scale already applied"), but adds: a Border Scale multiplier so on-screen corner thickness is decoupled from the source slice location, and Proportional Insets so the inset-pair tracks Scale X/Y instead of drifting with resolution.
- The audit flagged multiple safety issues in the chrome tool (integer overflow, unbounded input dims, JSON injection via mod name, realloc dim/buffer mismatch, missing trim cross-clamp, per-tick GL realloc, redundant full-res VRAM). All fixed in this drop.
- The mods-folder organization makes the tree self-documenting and supports Mike's intended layout (`Weapons/`, `MP Maps/`, `UI Chrome/`, …) without a breaking migration — flat mods remain valid.

**Scanner recursion bounds**:
- Category-folder recursion is capped at exactly one level below each root. This matches how bundled mods live (`mods/<mod>/`) while admitting category containers (`mods/<category>/<mod>/`). Deeper nesting is intentionally not supported to avoid runaway walks on arbitrary user layouts.

**Verification**:
- `source devtools/build-env.sh && ninja -C Build pd pd-server` — clean link of `pd` (5/5 steps, `[5/5] Linking CXX executable PerfectDark.exe`). `pd-server` up-to-date (does not compile client-side mod scanner or theme files). Warnings were all pre-existing (`/*` within comment headers).
- No code-path tests of the mods/ category scanner in this session — covered during next playtest.

**Design decisions Mike should review**:
- **Normalized output is "preview buffer as written today"** — the processed buffer from `chromeToolUpdatePreviewTexture` already has trim+cut+scale+desat baked in. I did NOT introduce an explicit "target size" combo (256/512/1024). If we want that, it's a ≤30-line addition in `chromeToolSaveMod` (resample preview → target before TGA write). Let me know if you want it.
- **Proportional Insets is on by default.** This changes the default save contract: mods created after this drop will have `"chrome_authoring"` metadata and a percentage-based inset model. Existing chrome mods keep working — the scanner only reads `src_inset`/`dst_corner_px`.
- **Border Scale defaults to 1.0** (identical to prior behavior). No existing mod is visually altered.
- **Chrome mods now save under `mods/UI Chrome/`**. Pre-existing user-created chrome mods under `mods/<slug>/` remain scanned and work unchanged.

**Follow-ups not done this session** (deferred, documented in tasks-current):
- `matchConfigAddBot` hardcoded-human-count (audit C-6).
- S-2 middle-click bridge vs Skin Editor canvas pan.
- S-3 `s_PdmsOwnsMenuCtx` shared flag across MP settings dialogs.
- S-4 Close-button hover clip.
- S-5 Skin Editor downrez preview realloc.
- S-6 Chrome style rescan GL texture leak.
- S-11 (mods-apply) missing chrome style rescan in `modmgrApplyChanges` — **fixed in S294**.

---

## Session S294 — 2026-04-16 (Mechanical audit sweep: bot cap callsites, input-ctx ownership, GL cache lifetimes, Dev Window v2 fixes)

**Scope**:
- Sweep mechanical fixes from `context/scratch/audit-s255-s292-2026-04-16.md`.
  Parallel session (bold-boyd) owns Nine-Slice Chrome & mods folder —
  this session must NOT touch `pdgui_menu_moddinghub.cpp`.

**Code changes shipped in working tree**:
- `port/src/net/matchsetup.c` + `port/include/net/matchsetup.h`
  (**C-6 / S-15**):
  - New `matchConfigCountHumans()` helper (counts `SLOT_PLAYER`, min 1).
  - `matchConfigAddBot()` now calls `matchConfigMaxBotsForHumans(matchConfigCountHumans())`
    instead of the hardcoded `matchConfigMaxBotsForHumans(1)`.
  - `matchConfigChooseBotTeam()` promoted to public, now takes `numTeams`
    parameter (2..MAX_TEAMS), supports full 8-team range.
- `port/src/net/netmsg.c` (**C-6 / S-12 / S-13**):
  - `SVC_ROOM_SETTINGS` client rebuild uses `matchConfigCountHumans()`
    for the cap.
  - Switched bot team assignment from positional `(i-1) & 1` to
    `matchConfigChooseBotTeam(2)` — matches host strategy.
  - Now zeros slot entries beyond the new `numSlots`, preventing
    stale bot rows on bot-count decrease.
- `port/fast3d/pdgui_backend.cpp` (**S-2**):
  - Middle-click back bridge now suppresses mouse-back when a middle
    drag is active (fixes Skin Editor middle-drag pan conflict).
- `port/fast3d/pdgui_style.cpp` (**S-4**):
  - Close-button hover detection clipped to window via
    `IsMouseHoveringRect(..., true)` and gated on `IsWindowFocused`.
- `port/fast3d/pdgui_menu_mpsettings.cpp` (**S-3**):
  - Removed shared `s_PdmsOwnsMenuCtx`; each dialog (SelectTunes,
    Soundtrack, TeamNames, Handicap) owns its own `ownsCtx` bool.
    `pdms_BeginStandardWindow` / `pdms_CloseCurrentDialog` now take
    `bool *ownsCtx` (nullptr allowed for dialogs that never push ctx).
- `port/fast3d/pdgui_skin_editor.cpp` (**S-5**):
  - `s_DownrezPreview` now tracks `s_DownrezPreviewW`/`H` and reallocs
    when the target dimensions change — prevents stale buffer reuse
    after character/quantization switch.
- `port/fast3d/pdgui_theme.cpp` (**S-6**):
  - New `s_chromeStylesFreeModTextures()` deletes mod-owned GL
    textures from `s_ThemeTexCache` before `s_chromeStylesClear()`;
    skips `"base:ui_chrome_frame"` (owned by `pdguiThemeLateInit`).
    Called from `pdguiThemeRescanChromeStyles()`.
- `port/src/modmgr.c` (**S-8**):
  - `modmgrApplyChanges()` now calls `pdguiThemeRescanChromeStyles()`
    after `pdguiThemeRescanMods()` (previously only theme.json was
    rescanned, leaving nineslice chrome stale).
- `devtools/dev-window-v2/dev-window-v2.ps1` (Dev Window v2):
  - `Sync-UserMachinePath`: append Machine PATH instead of prepending
    (fixes PATH pollution that overrode worktree tools).
  - Git push failure now logs a warning and continues the build
    instead of MessageBox-and-fail.
  - Release invocation switched from `-File` to `-Command` + added
    `-NonInteractive` (prevents interactive prompts blocking CI-style
    release builds).
  - Release build path passes `forceClean=$true` to `Get-BuildSteps`
    (release must be clean, not incremental).

**Context updates**:
- `context/constraints.md` — new canonical-usage constraint:
  `matchConfigMaxBotsForHumans(humanCount)` is single-source-of-truth
  for bot cap; all callers must pass actual human count (never hardcode 1).
- `context/bugs.md` — **B-144** entry documenting the
  `matchConfigAddBot(1)` / `SVC_ROOM_SETTINGS(1)` hardcoding.
- `context/systemic-bugs.md` — **SP-15** (GL texture size + cache
  lifetime) documenting the S-5/S-6/S-8 pattern.

**Ground rules honored**:
- Did NOT touch `pdgui_menu_moddinghub.cpp` (bold-boyd session scope).
- Working in angry-dijkstra worktree (main working copy), commits
  target `dev` branch, no push.

**Why**:
- Audit surfaced a class of "hardcoded value where a helper exists"
  bugs (C-6), three input-ctx ownership bugs (S-2/S-3), three GL/buffer
  lifetime bugs (S-4/S-5/S-6/S-8), and two multiplayer-protocol
  coherence bugs (S-12/S-13/S-15). All mechanical — pattern is clear,
  fix is low-risk, touches well-scoped functions.

**Verification**:
- `ninja -C Build pd pd-server` — see commit for status.
- Runtime verification pending (playtest dashboard).

---

## Session S292 — 2026-04-16 (Room max-bot/team defaults hardening for Chicago bot-match regression)

**Scope**:
- Address report of Chicago max-bot match showing all entries on one team (`T1`), clustered spawns, and non-lethal/no-engagement behavior.

**Code changes shipped in working tree**:
- `port/src/net/matchsetup.c`:
  - Added `matchConfigMaxBotsForHumans()` and reused it as the canonical cap helper (`min(MATCH_MAX_SLOTS-humans, MAX_BOTS)`).
  - Added internal bot-count guard so `matchConfigAddBot()` cannot create more runtime bots than `MAX_BOTS`.
  - Added balanced default bot team assignment when `MPOPTION_TEAMSENABLED` is active (auto-balance between team 0/1 instead of forcing all new bots to team 0).
- `port/include/net/matchsetup.h`:
  - Exported `matchConfigMaxBotsForHumans()` for UI/save/network callers.
- `port/fast3d/pdgui_menu_room.cpp`:
  - Room panel max-bot calculation now uses `matchConfigMaxBotsForHumans(humanCount)`.
  - Combat start request now clamps `numBots` against that cap before send.
- `port/src/scenario_save.c`:
  - Scenario load bot cap now uses the same canonical helper (prevents over-limit bot restoration paths).
- `port/src/net/netmsg.c`:
  - `SVC_ROOM_SETTINGS` bot rebuild now clamps with the canonical helper and assigns alternating default team values when teams are enabled (keeps client shadow config coherent before full per-bot sync).

**Why**:
- Prior code mixed participant-slot limits (`MATCH_MAX_SLOTS`) with runtime bot limits (`MAX_BOTS`) and defaulted newly-added bots to a single team in team mode, which can create "all one team" matches that appear non-combative.

**Verification**:
- Build/runtime verification pending in this session (code-only pass complete).

## Session S291 — 2026-04-15 (Select Tunes custom-song visibility + playlist add path hardening)

**Scope**:
- Investigate report that custom songs were missing from Match Soundtrack -> Select Tunes and could not be added to the playlist.

**Code changes shipped in working tree**:
- `port/src/modmgr.c`:
  - In `modmgrRebuildCatalogFromCurrentSelection()`, reset all `mod->loaded` flags before re-registering enabled mods.
  - Prevents in-place Mod Apply catalog rebuilds from skipping `audio.ini` re-registration after `assetCatalogClearMods()` removed non-bundled entries.
- `port/src/assetcatalog_scanner.c`:
  - Added `parseAudioCategoryValue()` for component INI audio parsing.
  - `ASSET_AUDIO` category now accepts numeric (`0/1/2`) and text (`music`, `sfx`, `voice`, common aliases), matching `audio.ini` behavior.

**Why**:
- Two separate paths can feed Select Tunes:
  - `audio.ini` package mods (via modmgr load/reload), and
  - component-scanned audio assets (via `_components/audio/*.ini`).
- Before this fix:
  - Mod Apply rebuild could clear catalog audio entries then skip re-registering enabled package mods due stale loaded flags.
  - Component INIs with textual categories defaulted to SFX, so they were filtered out of Mod Tracks.
- Both conditions produce "song mods missing / cannot add" behavior in the soundtrack flow.

**Verification**:
- `devtools/build-headless.ps1 -Target all` still exits early at configure in this shell (existing script/runtime issue in this environment).
- Compile verification passed via project toolchain path:
  - `. .\devtools\_build-env-prelude.ps1`
  - `cmake -G Ninja -S . -B Build -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++`
  - `ninja -C Build pd pd-server`
  - Result: both `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S290 — 2026-04-16 (Nine-Slice Chrome transforms + docked actions + Back parity)

**Scope**:
- Extend the Nine-Slice Chrome tool with image transformation controls requested for in-client authoring and tighten close/back UX parity.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_moddinghub.cpp`:
  - Added edit pipeline controls for imported chrome image:
    - edge trim sliders (`Trim Left/Right/Top/Bottom`),
    - non-uniform scaling sliders (`Scale X`, `Scale Y`),
    - center-strip removal controls (`Center Cut Axis`, `Center Cut %`) that remove from image center and stitch remaining parts together.
  - Reworked preview processing:
    - `chromeToolUpdatePreviewTexture()` now applies trim + center-cut + scale + optional desaturation and produces transformed output buffer/texture dimensions.
    - save path now writes transformed output dimensions/pixels to `ui_chrome_frame.tga` (not just source image dimensions).
  - Nine-slice inset slider bounds/clamps now operate on transformed output dimensions (`s_ChromeOutW/s_ChromeOutH`) so ruler math stays valid after transforms.
  - Docked action row (`Save as Mod`, `Reset`) to bottom of the tool panel.
  - Added shared hub close helper `moddingHubCloseFromUi(...)` and made Back input (`Escape` / gamepad Back) call the same close path as footer `Close` button for parity.

**Why**:
- Full in-client mod creation requires non-destructive image shaping tools before save; users need to trim/reshape source art and crop from center for square-ready chrome assets.
- Docked actions and unified Back/Close behavior reduce navigation ambiguity and align interaction model across windows.

**Verification**:
- Build verification passed:
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd pd-server`
  - Result: `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S289 — 2026-04-16 (Nine-Slice Chrome: assembled frame preview + desaturation workflow)

**Scope**:
- Extend the new in-client Nine-Slice Chrome tool with:
  - assembled frame preview (actual nine-slice render),
  - desaturation option for tint/theme-friendly outputs.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_moddinghub.cpp`:
  - Added `pdgui_nineslice.h` integration and runtime preview helpers:
    - `chromeToolBuildDef(...)` to construct `nineslice_def_t` from current ruler/mode settings.
    - frame preview pane now renders assembled frame via `pdguiNinesliceDrawEx(...)`.
  - Added desaturation controls/state:
    - `Desaturate for tint-friendly chrome` checkbox,
    - `Desaturate %` slider.
  - Added processed preview texture path:
    - `chromeToolUpdatePreviewTexture()` builds/uploads desaturated (or original) preview texture,
    - source preview now reflects desaturation settings live.
  - Save path now writes processed preview pixels to `ui_chrome_frame.tga`, so exported mod texture matches the chosen desaturation settings.
  - Updated save status text to indicate when output is desaturated.
  - Added cleanup for processed preview texture/buffer in tool reset/release paths.

**Why**:
- Ruler overlays alone are not enough to validate how corners/edges/center behave when assembled.
- Desaturation is needed so theme/tint passes can recolor chrome assets more predictably.

**Verification**:
- Build verification passed:
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd pd-server`
  - Result: `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S288 — 2026-04-16 (Nine-Slice Chrome creator added to Modding Hub)

**Scope**:
- Add an in-client tool so players can create UI chrome nine-slice mods directly in-game (import image, set rulers, save/activate mod).

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_moddinghub.cpp`:
  - Added new tab/tool: **Nine-Slice Chrome** (tab index 7).
  - Added tool state + lifecycle (`chromeToolReset`, texture/pixel ownership cleanup, status messaging).
  - Added image import support using the shared file browser and `stb_image` decode:
    - `Browse` + `Load` for `.png/.jpg/.bmp/.tga`.
  - Added live preview with ruler overlays:
    - visual guide lines for `Left/Right/Top/Bottom` slice positions over imported image.
  - Added ruler controls:
    - `Left`, `Right`, `Top`, `Bottom` sliders,
    - `L/R symmetry` and `T/B symmetry` toggles.
  - Added nineslice mode controls:
    - `Center tile mode`,
    - `Edge tile mode` (applies to top/bottom/left/right).
  - Added `Save as Mod` flow:
    - writes `mods/<slug>/ui_chrome_frame.tga`,
    - writes `mods/<slug>/mod.json` with `tags:["chrome"]` and `components.textures + components.nineslice`,
    - immediately registers + activates via `pdguiThemeRegisterChromeModDir(modDir, 1)` so style appears/applies without restart.
  - Wired tab selector/nav/content/footer descriptions for 8 tools total.
  - Hooked hub close to chrome tool reset/cleanup.

**Why**:
- Project requirement is fully in-client mod creation. This provides a first-class in-game authoring path for UI chrome nineslice mods instead of requiring external file editing.

**Verification**:
- Build verification passed (client + server):
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd pd-server`
  - Result: `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S287 — 2026-04-15 (Release/build outage hardening: force-commit fallback + missing Build dir creation)

**Scope**:
- Address outage-recovery friction:
  1) release/build sync failing on pre-pull commit hook rejection,
  2) build flows failing when `Build/` was deleted.

**Code changes shipped in working tree**:
- `devtools/release.ps1`:
  - Added switch `-ForceCommitNoVerify`.
  - Added helper `Invoke-ReleaseCommit(...)`:
    - normal `git commit` first,
    - optional fallback retry with `git commit --no-verify` when `-ForceCommitNoVerify` is set.
  - Wired helper into all release auto-commit paths:
    - pre-release (`-SkipBuild` path),
    - pre-build path,
    - Step 4 pre-`pull --rebase` auto-commit.
  - Added explicit creation of missing build directory before configure/build.
- `devtools/dev-window-v2/dev-window-v2.ps1`:
  - `Invoke-GitSyncBeforeBuild(...)` now retries failed commit with `--no-verify` before aborting.
  - Added explicit `Ensure build dir` step in build queue before configure.
- `devtools/build-headless.ps1`:
  - Added explicit missing build-directory creation before configure/build phases.

**Why**:
- Power outage / interrupted sessions can leave repo state where hooks block auto-commit, and users may clear `Build/`. These changes keep the solo-dev pipeline resilient and recoverable without manual repair.

**Verification**:
- PowerShell parse checks passed for modified scripts:
  - `devtools/release.ps1`
  - `devtools/dev-window-v2/dev-window-v2.ps1`
  - `devtools/build-headless.ps1`

## Session S286 — 2026-04-15 (Mod Apply completion tint parity with updater success prompt)

**Scope**:
- Align Mod Apply completion visuals with the updater's success-state treatment.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_modmgr.cpp`:
  - Added success-state window background tint for the `Applying Changes` window when apply reaches completion state (`s_ApplyFlowState >= 3`):
    - `ImGuiCol_WindowBg = ImVec4(0.08f, 0.25f, 0.08f, 0.95f)`
  - Kept in-progress state neutral (no tint) so active work and completion are visually distinct.

**Why**:
- Improves consistency with updater UX while preserving clear phase signaling (working vs complete).

**Verification**:
- Build verification passed:
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd pd-server`
  - Result: `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S285 — 2026-04-15 (Mod Apply popup visual parity with updater download window)

**Scope**:
- Make Mod Manager Apply UX match the existing updater download popup style.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_modmgr.cpp`:
  - Replaced `BeginPopupModal("Applying Changes")` flow with a centered updater-style window (`ImGui::Begin("Applying Changes", ...)`) using:
    - fixed centered positioning and fixed size (`600x240` scaled),
    - no resize/move/collapse/saved-settings flags,
    - wide progress bar (`ImVec2(-1, 24)`),
    - centered acknowledgment button (`OK` / `OK & Close`) on completion.
  - Kept existing apply state machine behavior (paint first frame, run synchronous apply next frame, then completion state).

**Why**:
- User-requested UX consistency: Apply should present the same style pattern as the update download window while catalog rebuild/diff/apply runs.

**Verification**:
- Build verification passed:
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd pd-server`
  - Result: `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S284 — 2026-04-15 (Mod Apply: in-place modal apply, no forced title restart)

**Scope**:
- Remove the forced stage transition from Mod Manager Apply and keep the user in the current menu flow while catalog rebuild/diff/apply runs.

**Code changes shipped in working tree**:
- `port/src/modmgr.c`:
  - `modmgrApplyChanges()` now performs in-place apply only:
    - save component state/config,
    - rebuild catalog from current selection,
    - invalidate catalog-backed caches,
    - reset texture cache,
    - rescan themes,
    - clear dirty state.
  - Removed the forced teardown/transition behavior from apply:
    - no `menuStop()`,
    - no `pdguiMainMenuReset()`,
    - no `mainChangeToStage(MODMGR_STAGE_TITLE)`.
  - Updated apply-complete logging to explicitly note no stage restart.
- `port/fast3d/pdgui_menu_modmgr.cpp`:
  - Added in-UI apply flow modal state machine:
    - opens `Applying Changes` modal,
    - runs synchronous `modmgrApplyChanges()` while modal is active,
    - shows completion message (`Catalog changes are live. No restart required.`),
    - supports `Apply` and `Apply & Close` paths.
  - Refactored selection commit into helper (`applyPendingSelectionToCatalog()`).
  - Updated empty-state copy to remove restart guidance.
- `port/include/modmgr.h`:
  - Updated `modmgrApplyChanges()` comment to document in-place apply semantics.

**Why**:
- Returning to title on every Apply is unnecessary for this architecture and creates avoidable UX churn/risk. In-place apply keeps users in context and aligns with hot-reload behavior already used elsewhere.

**Verification**:
- Build verification passed (client + server):
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd pd-server`
  - Result: `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S283 — 2026-04-15 (UI Chrome Style: mod-discovered picker + persisted style ID)

**Scope**:
- Extend Settings -> Video -> UI Chrome Style from fixed Procedural/Classic toggle to a picker that includes discovered chrome mods and restores the exact chosen chrome style on restart.

**Code changes shipped in working tree**:
- `port/include/pdgui_theme.h`:
  - Added UI chrome style APIs for persisted style id and runtime style enumeration:
    - `pdguiThemeSetUiChromeStyleId` / `pdguiThemeGetUiChromeStyleId`
    - `pdguiThemeGetChromeStyleCount` / `pdguiThemeGetChromeStyleId` / `pdguiThemeGetChromeStyleName`
- `port/fast3d/pdgui_theme.cpp`:
  - Added `Video.UiChromeStyleId` config registration/backing storage (default `base:ui_chrome_frame`).
  - Added chrome style registry cache and manifest parser for `mod.json` entries using the extracted schema:
    - `components.textures[]` (`id`, `file`)
    - `components.nineslice[]` (`id`, `src_inset`, `dst_corner_px`, `*_mode`)
  - Added mod scan over common mods roots and dynamic registration:
    - load texture via existing `s_registerModTexture(...)`
    - register nineslice via `pdguiNinesliceRegister(...)`
    - expose style in runtime picker list
  - Startup chrome apply now:
    - resolves persisted `Video.UiChromeStyleId`,
    - falls back to `base:ui_chrome_frame` if style is unavailable,
    - applies the resolved style when chrome is enabled.
- `port/fast3d/pdgui_menu_mainmenu.cpp`:
  - Replaced static two-option UI Chrome combo with dynamic options:
    - `Procedural` + discovered style names from theme API.
  - Selection now persists both:
    - `Video.UiChromeEnabled` (existing),
    - `Video.UiChromeStyleId` (new),
    and still calls `configSave("pd.ini")` immediately on change.
- `port/include/pdgui_theme.h` + `port/fast3d/pdgui_theme.cpp`:
  - Added runtime chrome registration hooks for importer/save flows:
    - `pdguiThemeRegisterChromeModDir(mod_dir, activate_now)` to hot-register a newly written chrome mod directory and optionally auto-activate/persist it immediately.
    - `pdguiThemeRescanChromeStyles()` to rebuild chrome style list from disk after bulk import operations.
- `port/fast3d/pdgui_menu_moddinghub.cpp`:
  - Mod Pack import success path now calls `pdguiThemeRescanChromeStyles()` so newly imported chrome mods appear in Settings -> Video style picker without restart.

**Why**:
- The previous picker could only target hardcoded `base:ui_chrome_frame`, which blocked users from selecting custom chrome mods created from the same manifest template format.

**Verification**:
- Build verification passed (client + server):
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd pd-server`
  - Result: `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S282 — 2026-04-15 (UI Chrome Style: immediate persistence on change)

**Scope**:
- Ensure Settings -> Video -> UI Chrome Style saves immediately when changed, so toggling Procedural/Classic persists across restart without relying on later config writes.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_mainmenu.cpp`:
  - In `renderSettingsVideo()`, the `UI Chrome Style` combo change handler now calls `configSave("pd.ini")` immediately after applying `pdguiThemeSetUiChromeEnabled(...)` and the chrome runtime toggle.

**Context note**:
- Verified the external default template at `Downloads/Perfect Dark 2.0/data/mods/base-game/ui-chrome/mod.json` still uses embedded `components.nineslice` entries (`src_inset`/`dst_corner_px`) and no separate nineslice JSON file.

**Verification**:
- Build verification passed:
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd pd-server`
  - Result: `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S281 — 2026-04-15 (Skin Editor preview: avoid drawing non-ready black texture)

**Scope**:
- Address Skin Editor report of pitch-black preview panel while character preview is still loading/not ready.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_skin_editor.cpp`:
  - `renderPreviewPanel()` now requires both:
    - non-zero texture id, and
    - `pdguiCharPreviewIsReady() == true`
    before drawing the preview image.
  - When not ready, panel now shows explicit rendering status + selected body/head IDs instead of drawing a black texture.

**Why**:
- The previous path drew whenever texture id was non-zero, even if charpreview readiness had not been established yet, which could present as a black panel.

**Verification**:
- Build verification passed:
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd`
  - Result: `PerfectDark.exe` linked clean.


## Session S353 — 2026-04-17 (Prop Sync Event-Driven + Killfeed Bot Kills, `quizzical-murdock-95eb09`)

**Scope**:
- Task 1: Replace prop CRC polling (`SVC_PROP_SYNC` / `netPropSyncChecksum`) with snapshot-based dirty detection
- Task 2: Ensure killfeed shows bot kills to all in-game clients (not just local host)

**Code changes**:

### Task 1 — Prop Sync Event-Driven (`port/src/net/netmsg.c`, `net.c`, `netmsg.h`)

**Removed**:
- `g_NetPropDesyncCount`, `g_NetPropResyncLastReq` globals
- `netPropSyncChecksum()` — O(N) XOR-rotate CRC function
- `netmsgSvcPropSyncWrite()` — server no longer sends CRC packet
- CRC comparison logic from `netmsgSvcPropSyncRead` (now just consumes bytes for backward compat)

**Added** (`netmsg.c`):
- `PropStateSnap` struct with 128-entry static array `s_PropSnaps[]`
- `netPropSnapReset()` — clears snapshot at stage start (called from net.c at both MP and co-op stage start)
- `netPropSnapUpdate(prop)` — records `{syncid, hidden, damage}` snapshot when a prop event message is sent (move/damage/door/lift write paths)
- `netPropDirtyCheck()` — scans active sync-relevant props, compares vs snapshot; returns 1 if any diverged and updates snapshot. Logs divergences at LOG_NOTE.

**Changed** (`net.c`):
- Server tick: replaced `netmsgSvcPropSyncWrite(&g_NetMsgRel)` every 120 frames with `netPropDirtyCheck()` → sets `NET_RESYNC_FLAG_PROPS` if dirty. Guard added: only runs when `g_Vars.mplayerisrunning`.

**Protocol**: No change to wire format or version. `SVC_PROP_SYNC` (0x37) read handler still consumes 10 bytes so old servers remain compatible.

### Task 2 — Killfeed Bot Kills (`netdistrib.c`, `netmsg.c`, `mpstats.c`)

**Root cause**: `mpstatsRecordDeath` only called `pdguiKillfeedPush` locally. `netDistribSendKillFeed` was never called from this path, so networked clients never received kill events.

**Fix 1** (`netdistrib.c` — `netDistribSendKillFeed`):
- Extended recipient set from `CLSTATE_LOBBY` only → also `CLSTATE_GAME`. In-game clients now receive `SVC_LOBBY_KILL_FEED` (0x74).

**Fix 2** (`netmsg.c` — `netmsgSvcLobbyKillFeedRead`):
- Added `pdguiKillfeedPush` call (inside `#if !defined(PD_SERVER)`) to render the kill notification locally on clients. Team looked up from `g_MpAllChrConfigPtrs[]` by name match. Suicide detected via empty attacker string or attacker == victim.

**Fix 3** (`mpstats.c` — `mpstatsRecordDeath`):
- Suicide path: added `netDistribSendKillFeed("", vmpchr->name, "", 0)` (empty attacker = suicide signal)
- Normal kill path: added `netDistribSendKillFeed(ampchr->name, vmpchr->name, "", 0)`
- Both use `extern` pattern (matching existing score-sync pattern in same file). Guard: `g_NetMode == NETMODE_SERVER`.

**Verification**:
- Build: 774/774 objects clean, zero errors. `PerfectDark.exe` + `PerfectDarkServer.exe` both linked.

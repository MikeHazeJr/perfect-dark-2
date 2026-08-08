# Catalog System

## 2026-08-08 typed character and voice handoff

Top-level `.pdcharacter` rows now retain public body/head catalog IDs and a
display name through scanning, base walking, and runtime binding. The room
picker selects the exact pair, roster display consumes the character name, and
an optional declared portrait loads through the active source path with
fail-closed behavior. `.pdvoice` rows now retain actor, transcript, language,
and context from public `voice.ini`; typed audio resolution exposes those
fields and scenario dialogue uses matching-locale transcript text. Optional
`subtitle.json` and locale-specific sample members remain partial under
Workbench `T-ASSETS-021`.

> Single source of truth for asset identity. String-keyed, namespace-scoped, hash-indexed. Every asset reference uses a human-readable catalog ID.

---

## What it is

The asset catalog is a string-keyed FNV-1a hash table that holds an entry for every asset the game can resolve: maps, characters, skins, bodies, heads, weapons, models, props, audio, textures, animations, HUD elements, language banks, bot profiles, and more. Each entry carries identity (`namespace:asset_type_readable_name`), type, source provider handle, load state, payload, refcount, and metadata. The catalog ID is the asset reference. Numeric ROM/file/model/sound/body/head/animation slots are migration metadata only and must not be used as catalog identity or modder-facing live-file names.

Code: [port/include/assetcatalog.h](../../port/include/assetcatalog.h), [port/src/assetcatalog.c](../../port/src/assetcatalog.c), and 8 sibling files (`_api`, `_base`, `_base_extended`, `_cache`, `_deps`, `_load`, `_resolve`, `_scanner`).

---

## Architecture (three layers + a transition)

The catalog is built from a layered structure that has been migrating since 2026-03 and is partially complete.

**Layer A: legacy static data.** The original N64-era static C arrays (`g_Weapons[]` at [src/game/invitems.c:5700](../../src/game/invitems.c:5700), `g_AibotWeaponPreferences[]` at [src/game/botinv.c:23](../../src/game/botinv.c:23), `g_HeadsAndBodies[]`, `g_Stages[]`, `g_ModelStates[]`, `g_MpWeapons[]`). These hold the actual data (damage, fire rate, animations, etc.) for base game content. Layer A is being retired in stages, but is still the source of truth for most content fields today.

**Layer B: catalog rows.** Every asset has a row in the catalog (`asset_entry_t`, [port/include/assetcatalog.h:186](../../port/include/assetcatalog.h:186)) carrying identity (`id[CATALOG_ID_LEN=64]`), type (`asset_type_e`, see Asset Types below), display name, type-specific extension struct, source handle, load state, and refcount. Layer B owns the identity and metadata. Any remaining runtime index fields are temporary migration hooks, not asset-reference fields.

**Manager layer.** A type-specific manager (`catalog_mgr_<type>.c`) owns the typed accessors and validates inputs. The first manager is the weapons manager ([port/src/catalog_mgr_weapons.c](../../port/src/catalog_mgr_weapons.c), 162 lines) which is currently a thin pass-through router over `g_Weapons[]` (line 59), but provides the API surface for the future data move. A globals-free pure validator file ([port/src/catalog_mgr_weapons_pure.c](../../port/src/catalog_mgr_weapons_pure.c), 65 lines) is testable in `pd-tests`.

**.pdbase format (F11+F12+F13 shipped 2026-04-30, lane CLOSED).** External JSON archive format for base-game content data, paralleling `.pdmod` for mod content. F10 (S484) shipped the loader scaffold; F11 (S591) shipped the generated archive [base/weapons.pdbase](../../base/weapons.pdbase) (~12.8k lines, 86 weapon records + 110 animation records) authored via [devtools/extract_weapons_pdbase.py](../../devtools/extract_weapons_pdbase.py); F12 (S591) shipped the runtime loader; **F13 (S591) retired Layer A**: `g_Weapons[]`, `g_AibotWeaponPreferences[]`, `invaimsettings_default`, `invnoisesettings_silent`, all 75+ `invitem_*`/150+ `invfunc_*`/80+ `invammo_*`/13 `invaimsettings_*`/8 `invnoisesettings_*`/4 `invrecoilsettings_*` static records, all 110 `invanim_*` opcode arrays, all 14 `gunviscmds_*` arrays, all 14 `invpartvisibility_*` arrays, and the `vibrationstart/max_reaper` arrays are gone. `src/game/invitems.c` reduced from 5789 lines to ~50 lines (header comment + grep-trail). The catalog manager + .pdbase is the **sole source of truth** for weapon DATA; reading anything weapon-related anywhere in the codebase goes through `catalogManagerGetWeapon*()`. The extractor parses `src/game/invitems.c` + `src/game/botinv.c`, resolves `#define` constants from `constants.h` + `gunscript.h`, decodes `gunscript_*` / `gunviscmd_*` macro calls into JSON opcode arrays, and inlines sub-records (functions, ammos, aimsettings, noise, recoil, gunviscmds, partvis) per design Section C. The runtime loader ([port/src/loader_pdbase.c](../../port/src/loader_pdbase.c), ~1400 lines) reads `base/weapons.pdbase`, populates pools (`struct weapon[86]`, `guncmd[3000]`, `gunviscmd[500]`, `modelpartvisibility[500]`, `inventory_ammo[120]`, `invaimsettings[120]`, `noisesettings[120]`, `recoilsettings[120]`, `weaponfunc_any_t[256]`, animation name table[256]), and switches the catalog manager's accessors to the pool-backed pointers. F12 also generates `port/src/loader_pdbase_enums.c` (~1500 lines, 5400+ entries across ANIM/SFX/FILE/L_GUN families) for symbol-string -> integer resolution at JSON parse time. A startup self-test (`loaderPdbaseRunParityCheck`) compares 12 scalar fields per weapon against the legacy `g_Weapons[]` table and emits `LOADER.PDBASE.WEAPON.PARITY_FAIL:` on mismatches; this validates the loader's correctness while parity-period bridge to `g_Weapons[]` is still in place. Per Mike's 2026-04-30 directives: Path B (data-driven animations) was chosen for an IK runway, integers for flag bitfields, strings for enum names, inline-duplicate shared settings, and registration is unconditional on unlock state (selectors filter `catalog union unlocked` separately, not at registration). Structure + API pinned by [tests/test_loader_pdbase_scan.cpp](../../tests/test_loader_pdbase_scan.cpp) ([catalog-mgr-weapon][s484][f11/f12], 14 cases / 80 assertions). F13 retires the legacy `g_Weapons[]`, the 110 animation arrays, and supporting records once Mike's playtest verifies the parity check passes at runtime.

The full migration path is documented in [designs/catalog/catalog-full-pipeline-weapons.md](../designs/catalog/catalog-full-pipeline-weapons.md) and tracked in [tasks.md](../tasks.md) under the Catalog Weapons F11-F13 lane.

**Heads (F1-F13 shipped 2026-05-01 as S596 at dev `a2ad421e`).** Reused the weapons template for the 152 HEAD slots in `g_HeadsAndBodies[]`. Manager [port/src/catalog_mgr_heads.c](../../port/src/catalog_mgr_heads.c) (383 lines) owns the `s_Heads[152]` mirror; pure validators in [port/src/catalog_mgr_heads_pure.c](../../port/src/catalog_mgr_heads_pure.c) (40 lines) are the testable surface (`CATALOG_MGR_HEAD_COUNT_PURE = 152`, `CATALOG_MGR_HEAD_RANDOM_GENDER_PURE = 1000`). [base/heads.pdbase](../../base/heads.pdbase) (930 lines) holds 84 head records: 75 named `base:head_*` plus 9 SP-fallback `base:sp_head_*`. Generated by [devtools/extract_heads_pdbase.py](../../devtools/extract_heads_pdbase.py) (411 lines) from `robot.c` + `mplayer.c` + `assetcatalog_base.c` + `constants.h`. F2 routed `catalogGetHeadIsMale/Type/Height` through `catalogManagerGetHeadByIndex`; F3+F4 migrated the modeldef cache; F5 replaced `bodyAllocateModel`'s `g_HeadsAndBodies[h].modeldef == NULL` with `catalogManagerHeadIsModeldefLoaded(h)` (S593g `head_canon=NULL` warning gate preserved); F6 retired `g_MpMaleHeads[]` / `g_MpFemaleHeads[]` static literals (`mpDefaultHeadForBody` calls manager pickers; PD_SERVER guard on rng-using paths); F7 added `pdbase_path/offset/size` to `ext.head`; F9+F12+F13 extended the loader with the heads section, retiring the parity bridge once the structure was wired. Body reads keep the legacy `g_HeadsAndBodies` pattern -- bodies session (next sequential auto-merge, `local_f16fd720`) migrates them. F13 grep-guard pin: [tests/test_catalog_mgr_heads_api.cpp](../../tests/test_catalog_mgr_heads_api.cpp) (`[catalog-mgr-head][gate3][f1..f13]`, 12 cases / 96 assertions) ensures no new direct `g_HeadsAndBodies[h].<head-field>` reads appear in `pdgui_menu_*`, `chraction.c`, `player.c`, `netmanifest.c`. Phase 1 audit doc: [audits/catalog-gate3-heads-data-2026-05-01.md](../audits/catalog-gate3-heads-data-2026-05-01.md).

**Body self-contained metadata note (2026-05-19, B-345 second pass).** BODY_CHICROB/base:sp_body_118 is self-contained (`unk00_01=1`) even though it is a robot body rather than a classic integrated-head character. Solo setup uses `BODY_CHICROB, 0x00` in Chicago and Skedar Ruins; headnum 0x00 is a placeholder, not a separate catalog head. Keep this row self-contained so `bodyAllocateModel` does not request or warn about a phantom head.

---

## Catalog ID convention

All asset references use full catalog ID strings in `[namespace]:[asset_type]_[readable_name]` format:

- `base:stage_dark_combat` (a stage)
- `base:falcon2` (a weapon)
- `base:body_carrington` (a body)
- `base:arena_felicity` (an arena)
- `mod_redmund:skin_my_skin` (a mod-supplied skin)

The leading namespace is `base` for original Perfect Dark content, or a mod-scoped namespace for mod content. The trailing readable_name is human-friendly and stable across versions.

Integer indices are NEVER asset references. They must not appear in catalog IDs, authored descriptors, dependency manifests, modding UI dropdowns, generated live file names, wire messages, saves, public APIs, or runtime structs that mean "this asset." Existing numeric fields are migration debt only. The deprecated `net_hash u32` compact form was removed from the wire in v27 and from save format around the same time; do not reintroduce it.

**Generated-ID implementation guard (2026-05-22).** `port/include/catalog_readable_ids.h` owns readable fallback generation for base assets and ROM extractors. Base catalog registration and `.pdanim` / `.pdsfx` / `.pdvoice` / `.pdsong` / `.pdmesh` / `.pdhead` / `.pdbody` / `.pdweapon` dependency generation must call that helper instead of formatting raw slots into IDs. `tests/test_catalog_provider_static.cpp` pins the ban on patterns such as `base:model_%04x`, `base:sfx_%04x`, `base:voice_%04x`, and `base:rom_g_%04x`.

**Actual catalog-name guard (2026-05-27).** `tools/asset_archive_conformance.py` now validates catalog-looking references against the catalog IDs declared by root and nested typed archives. A public payload that names `base:*` or `mod:*` is not accepted merely because the string has the right shape; the referenced catalog ID must exist in the validated archive set or embedded dependency closure. Weapon audio extraction also normalizes high-bit SFX aliases through `g_AudioRussMappings` before graph, binding, dependency, or manifest emission so generated refs point at real `.pdsfx` catalog entries.

**JSON asset-ref numeric/legacy parity (2026-06-09, B-907).** The conformance JSON scanner (`scan_json_asset_refs`) now rejects numeric/legacy asset references on the same hard catalog-ref selector the delimited scanner uses (`is_delimited_catalog_ref_column`), closing a gap where catalog-ID keys outside the narrow `FORBIDDEN_NUMERIC_ASSET_REF_KEYS` set (`model_catalog_id`, `*_catalog_id`, `stage_id`, etc.) silently accepted `42` or `MODEL_*`/`SFX_*` values in the primary public JSON format. Generic structural `*_ref` (`pad_ref`/`room_ref`) and intra-archive member/dependency paths (`texture.png`, `dependencies/.../default.pdmaterial`) remain allowed; the tool's `--selftest` mode pins JSON<->delimited parity.

**Catalog health checkpoint consumer (2026-06-09, B-908, Gate 5).** `g_CatalogFailure` was a write-only flag: the load-site helpers (`catalogGetBodyFilenumByIndex`, `catalogGetHeadFilenumByIndex`, `catalogGetBodyScaleByIndex`, `catalogGetStageResultByIndex`, `catalogResolveModelByModelnum`, ...) set it on a miss and substituted a default, but nothing read it. `catalogAssertHealthy(checkpoint)` (in `assetcatalog_api.c`, wired at `lv.c` stage-load entry) is the consumer: it emits a single `CATALOG.HEALTH: FAIL at <checkpoint>` report and, under source-only enforcement (`assetSourceDebugOnlyType() != ASSET_NONE`), `sysFatalError`s -- a catalog miss after extraction is an asset-chain failure, not a tolerated default. The escalation decision is the pure `catalogHealthShouldFatal(failure, enforcement_active)` in `catalog_checked.c` (pinned in `[catalog][checked]`). Normal play (enforcement off) stays a loud report + clear. Follow-up: the in-range-unregistered body/head field accessors (`catalogGetBodyType` etc.) still return zeros silently and do not set the flag.

**Scenario/Mission graph source activation (2026-05-28, B-385; JSON-source updated 2026-06-07).** Stage setup now validates and loads `.pdscenario::level.graph.json` and the matching `.pdmission::mission.graph.json` through public FileProvider/archive-member paths. Active level-graph table refs now choose the public archive members consumed by runtime portals, pads, volumes, setup fields, objects, objectives, waypoints, waygroups, and covers loaders, with semantic JSON members such as `portals.json`, `volumes.json`, `setup.fields.json`, and `objects.json` parsed into graph-owned source rows. Explicit `scenario.trigger.volume.source` nodes validate against those rows, and Enter Room / Throw In Room pad-room checks route through those rows when active. Setup behavior-link registration is validated against graph setup-link source collected from `setup.fields.json`. Generated `.pdmission` archives now carry objective source and parity-backend graph nodes instead of empty graph placeholders. This keeps catalog identity and source accessibility aligned while mission/level behavior graph execution parity remains open under `c3844`.

**Mission phase graph lifecycle (2026-05-28, B-385).** Generated `.pdmission` archives now carry explicit `mission.phase.source` nodes, strict conformance requires them, and runtime records `load`, `active`, `complete`/`failed`, and `end` through the active graph source. Broader trigger behavior, any remaining global behavior beyond settings source validation, and AI action modules still own the remaining parity work before the OG backend can be retired.

**Level-global settings graph source (2026-05-28, B-385).** Generated `.pdscenario` archives now carry a required `scenario.global.settings.source` node in `level.graph.json`, stale scenario archives are rejected if that node is absent, strict conformance enforces the node, and stage activation validates/logs the node from the public archive-member graph before accepting the level graph. Broader trigger behavior, any remaining global behavior beyond settings source validation, and AI action modules still own the remaining parity work before the OG backend can be retired.

---

## Asset types (32 enumerated)

`asset_type_e` at [port/include/assetcatalog.h:77](../../port/include/assetcatalog.h:77) defines all asset categories:

```
ASSET_NONE, ASSET_MAP, ASSET_CHARACTER, ASSET_SKIN, ASSET_BOT_VARIANT,
ASSET_WEAPON, ASSET_TEXTURES, ASSET_SFX, ASSET_MUSIC, ASSET_PROP,
ASSET_VEHICLE, ASSET_MISSION, ASSET_UI, ASSET_TOOL, ASSET_ARENA,
ASSET_BODY, ASSET_HEAD, ASSET_ANIMATION, ASSET_TEXTURE, ASSET_GAMEMODE,
ASSET_AUDIO, ASSET_HUD, ASSET_EFFECT, ASSET_MODEL, ASSET_LANG,
ASSET_BOT_PROFILE, ASSET_PROJECTILE, ASSET_ENTITY, ASSET_MATERIAL,
ASSET_FONT, ASSET_SCENARIO, ASSET_THEME
```

Each type has its own typed `catalog_<type>_result_t` and resolver (`catalogResolveBody`, `catalogResolveHead`, `catalogResolveWeapon`, etc.) declared at [port/include/assetcatalog.h:975](../../port/include/assetcatalog.h:975). Use the typed resolver, not generic lookup.

---

## Identity helpers (typed, per integer space)

Per [constraints.md](../constraints.md) (S487, 2026-04-27), the low-level cache primitive `catalogIdByRuntime(type, n)` is internal-only. Callers outside catalog internals must use the typed helper for the integer space they actually hold:

- `catalogStageIdByStageTableIndex(idx)` for `g_Stages[]` indices (0-86)
- `catalogStageIdBySoloStageIndex(idx)` for `g_SoloStages[]` indices (0-20)
- `catalogStageIdByStagenum(num)` for logical stagenum values (e.g. 0x5e)
- `catalogModelIdByModelnum(num)` for `MODEL_*` / `g_ModelStates[]` indices
- `catalogBodyIdByBodynum(num)` for body indices
- `catalogHeadIdByHeadnum(num)` for head indices
- `catalogWeaponIdByRuntimeWeaponNum(num)` for `WEAPON_*` enum values
- `catalogWeaponIdByMpWeaponId(num)` for `MPWEAPON_*` selector values
- `catalogGameModeIdByScenarioIndex(num)` for game mode / scenario indices

These helpers exist because three index spaces (stage table, solo stage, stagenum) must not be confused (see [constraints.md](../constraints.md) Index Domain Warning) and because the same numeric value can mean different things across MPWEAPON vs WEAPON enums.

Mod stages have stage table indices 61-86 but no solo stage index. Flowing a stage table index into `g_SoloStages[]` is OOB; the typed helpers prevent this class of bug.

---

## Lifecycle (typed, payload-aware)

Per [constraints.md](../constraints.md) (S488, S569, 2026-04-28), stage diffs, screen mini-manifests, and match/stage manifests must call typed lifecycle functions:

- `catalogLoadTypedAsset(type, id)` to load
- `catalogReleaseTypedAsset(type, id)` to release
- `catalogRetainTypedAsset(type, id)` to retain

The untyped wrappers `catalogLoadAsset` / `catalogUnloadAsset` / `catalogRetainAsset` were retired from the public surface. Entry-level internals inside `assetcatalog_load.c` are the only allowed implementation path.

**Payload kinds** (per S490/S493, 2026-04-28). Catalog entries that store `loaded_data` set `payload_kind` to describe ownership and release semantics:

| Kind | Owner / release behavior |
|------|--------------------------|
| `ASSET_PAYLOAD_SYSMEM_BYTES` | Freed via `sysMemFree` on release |
| `ASSET_PAYLOAD_STAGE_MODELDEF` | Activated/promoted modeldef from the model/stage-pool path; detached from catalog on release; subsystem owns the memory |
| `ASSET_PAYLOAD_RUNTIME_ACTIVE` | Runtime subsystem activation marker (audio, lang, HUD, bot profile, arena, gamemode, skin, bot variant, effect); detached on release; subsystem releases on its normal reset/reload |

Any new typed payload path must add a matching payload kind or explicitly reuse an existing one.

2026-06-11 c3844 lifecycle fix: MP/client-manifest-owned match assets must not
be released by the older stage-category diff during a base stage load. The
custom-body live smoke showed `example:tri_body`, `example:tri_head`, and
`example:tri_weapon` being loaded by the match manifest and then immediately
released by `CATALOG: stage 0x1d diff -- load:0 unload:3`. The fix skips the
old stage-category diff while `g_ClientManifest` owns MP match assets and clears
body/head manager cached modeldef slots before a catalog-owned generated
modeldef is freed. Static/build verification pins this ownership boundary; the
remaining custom-body smoke failure is later in generated-body render handoff.

---

## Source handles and providers

Per [constraints.md](../constraints.md) (S346/S496, S577, S578, 2026-04-17 to 2026-04-28), source handles are catalog/provider-owned. Game code in `src/game/` must NEVER call `romProviderHandle()` directly or include `assetprovider_internal.h`.

- **ROM source assignment**: `catalogSetPrimaryRomFilenum(entry, filenum)` (catalog-internal bridge to RomProvider).
- **File source assignment**: `catalogSetPrimaryFile(entry, path)` (creates FileProvider handle, stores on `entry->source.primary`).
- **Override**: `catalogSetOverride(entry, handle)` and `catalogClearOverride(entry)` for mod-supplied or session-time overrides.

For ROM file loads, game code uses `assetLoadRomToNew(filenum, method, loadtype)` ([port/include/assetload.h](../../port/include/assetload.h)) or `fileLoadToNew(filenum, method, loadtype)` ([src/include/game/file.h](../../src/include/game/file.h)). For stage / body / head / model handles, game code reads from `catalog_*_result_t` fields (`bg_handle`, `pads_handle`, `setup_handle`, `mpsetup_handle`, `tile_handle`) or uses `catalogGetBodyHandle()` / `catalogGetHeadHandle()` / `catalogGetPropHandle()` / `catalogGetModelHandle()`.

---

## Static data store

Hash table primitives at [port/src/assetcatalog.c:67](../../port/src/assetcatalog.c:67) (FNV-1a hash) and `:85` (reflected-CRC32 secondary). Open addressing with linear probing, 70% load factor rehash trigger ([port/src/assetcatalog.c:175](../../port/src/assetcatalog.c:175)). Generation counter at `:56` invalidates stale lookups after rehash.

`s_EntryPool` at `:49` is the array of `asset_entry_t` records. `assetCatalogRegister(id, type)` at `:402` is the registration entry point; `assetCatalogRegisterMap`, `assetCatalogRegisterBody`, etc. are typed wrappers that also fill the type-specific extension fields.

Base-game registration: [port/src/assetcatalog_base.c](../../port/src/assetcatalog_base.c) and [port/src/assetcatalog_base_extended.c](../../port/src/assetcatalog_base_extended.c). The extended file holds the table of 41 MPWEAPON entries (`s_BaseWeapons[]` at line 57) along with 1207 animations, 3503 textures, 1545 audio entries.

---

## Active invariants (current as of 2026-04-30)

These are pulled from [constraints.md](../constraints.md). Any change to a catalog invariant updates that file in the same commit.

- **Catalog registers ALL assets** (including SP-only heads/bodies). Unlock is a separate gameplay layer; catalog registration is universal.
- **Catalog-owned asset lifecycle** (S485, strengthened 2026-05-22). The catalog is the single source of truth for declared asset identity, metadata, references, source handles, dependencies, load state, loaded payloads, refcounts, and release/unload behavior. Static Layer A tables are migration sources only, not identity stores.
- **Catalog ID strings for all asset references** (since 2026-04-02, strengthened 2026-05-22). No integer asset identity on the wire, in saves, in public APIs, in runtime reference structs, in authored archives, in generated live-file names, or in modding tool selectors.
- **Catalog-first pattern for stage identity** (S165). `g_MissionConfig.stage_id` is authoritative; `stagenum` is resolved from `stage_id` only at the consumption point, never stored or passed as primary identity.
- **Random / Fiesta spawn-weapon pool sources from match-manifest** (S483). `MANIFEST_TYPE_WEAPON` entries drive the eligible pool, not `g_MpSetup.weapons[]`.
- **Sentinel-audit discipline + `spawnWeaponNumIsResolved`** (S483c). Single source of truth for "is this a real resolved WEAPON_* enum?" returns 0 for {0, 0xFF, 0xFE}.

---

## What is done (per [audits/infrastructure-pillars-status-2026-04-27.md](../audits/infrastructure-pillars-status-2026-04-27.md) Section 1)

- Hash-table catalog with FNV-1a + reflected-CRC32 secondary, open addressing, 70% rehash trigger, generation counter.
- 32 asset types enumerated; 8 typed resolvers covering all heavily-used types.
- 41 MPWEAPON entries registered with `runtime_index` linkage.
- Asset Provider Phases 1+2+3 shipped (RomProvider + FileProvider + asset_source_t with primary/override/flags).
- Typed identity helpers (S487) covering stage, model, body, head, weapon, gamemode.
- Typed lifecycle wrappers (S488+S569) replace untyped public API.
- Payload kinds (S490+S493) classify owner/release for every loaded data path.
- FileProvider source handles centralized through `catalogSetPrimaryFile` (S577).
- RomProvider source handles centralized through `catalogSetPrimaryRomFilenum` (S578).
- F1-F10 of catalog full-pipeline weapons shipped: weapon manager skeleton (F1), `weaponFindById` migrated (F2), 3 direct-read sites migrated (F3-F4), EYESPY mutators migrated (F5), modelmgrreset migrated (F6), `g_AibotWeaponPreferences` reads migrated (F7), default fallbacks + I.1 mutator removal (F8), ext.weapon I.2 drop + pdbase fields (F9), .pdbase loader scaffold (F10).

---

## Phase 3 ROM-once-then-disk migration (CLOSED 2026-05-02)

Mike's 2026-05-01 directive: "ROM is an initial asset source and then we use the extracted assets for loading, sans ROM." Shipped 2026-05-02 across Passes A through D.

- **Pass A.1-A.5**: `data/<romid>/` tier accessors, first-launch ROM extraction (`romExtractAllFiles` + `romExtractAllSegments`), SHA-256 sidecars + verify (`romExtractVerifyAll` + `romExtractVerifyAllSegments`), `LOUDFAIL` log channel convention.
- **Pass B Slices 1-13**: per-asset-class catalog migration from RomProvider to FileProvider (weapon models, sfx/music banks, lang, character models, animations, props, stage scenes, voice retag, music sequences, SFX residual ACCEPTED LIMIT, UI chrome). All shipped sequentially.
- **Pass C** (`b15cc701`): `romdataReleaseRom()` frees `g_RomFile` after the verify pair, migrates SRC_ROM segments to disk-backed buffers, NULLs lazy fileSlot pointers into ROM range, emits `LOAD.PASSC` LOUD-FAIL on any missing fallback. Runtime never touches the ROM mapping post-extraction.
- **Pass D** (S604, this commit lane): self-heal hardening on top of Pass A.4 + segment verify. Per-file system toasts on `corrected` / `failed` outcomes (5-cap), aggregated boot integrity report (`DATA INTEGRITY: V validated, R re-extracted, U unrecoverable`), deferred toast queue + drain (`romExtractToastDrain` after `gameInit`), quarantine path migrated to user-visible `data/_quarantine/<romid>/<unixtime>_<basename>`.
- **2026-05-27 strengthening (`c3844`)**: Any remaining runtime ROM/RomProvider fallback after extraction is an asset-chain failure, not a valid fallback. `c3844` owns the removal/fatal-audit pass so bootstrap ROM input cannot leak back into gameplay/runtime loading.
- **2026-05-28 weapon source-gate verification (`B-376`)**: Generated `.pdweapon` archives bind back into catalog rows as FileProvider primary sources during the universal weapon walker pass. Runtime `WEAPON_*` ids stay in `runtime_index`; MPWEAPON selection slots stay in `ext.weapon.weapon_id`/`mp_index`. Final smoke verified `base:dy357` loading/spawning through extracted `.pdweapon` source.
- **2026-05-27 public named-selector cleanup (`B-377`)**: Public typed archive descriptors now express metadata selectors as named keys instead of numeric ids for game modes, bot profiles, HUD, effects, props, arenas, and scenarios. The scanner and distribution paths resolve those names into runtime values internally; legacy numeric fields are reader fallback only and conformance rejects them in new public payloads.
- **2026-05-28 universal archive-member source binding (`B-378`)**: Universal walkers for scenario, arena, mesh, font, lang, UI, SFX, voice, song, animation, body, and head now bind catalog rows to public archive-member FileProvider paths such as `data/<romid>/scenarios/base_scenario_airbase.pdscenario::scene.glb`. Scenario activation consumes that source by compiling collision from `scene.glb` or an optional collision override.
- **2026-05-28 weapon label enum repair (`B-379`)**: The DY-357 `Falcon 2` fire-mode label was caused by missing source `L_GUN_050` through `L_GUN_057` language rows and unstable reverse enum lookup, not by a bad weapon catalog row or behavior graph. The restored language rows and formula resolver keep catalog-backed weapon function label IDs aligned.
- **2026-05-28 generated body catalog modeldef repair (`B-380`)**: Player body/head spawn paths now accept generated public-source catalog modeldefs as character-root modeldefs instead of treating them as invalid static bodies or raw ROM-sized model payloads. This keeps source-backed character assets from falling back while asset-family source gates are active.
- **2026-05-28 scenario stage handle source gate (`B-381`)**: `setupLoadBriefing`, stage setup loading, pads loading, and tile loading now pass their stage handles through the source-only guard when `ASSET_SCENARIO` is selected. The guard rejects RomProvider-backed handles before runtime load, and `setupLoadBriefing()` now uses the catalog stage setup handle instead of direct `assetLoadRomToAddr(setupfilenum, ...)`.
- **2026-05-28 raw FileProvider cache rejection (`B-382`)**: The source-only guard now distinguishes clean public FileProvider source from raw extracted ROM cache. FileProvider paths under `data/<romid>/files`, `data/<romid>/segments`, or ending in `.bin` do not satisfy selected-family source-only checks, preventing catalog/provider migrations from hiding c3844 by pointing runtime at raw dump files.
- **2026-05-28 scenario pads public-source runtime (`B-383`; JSON-source updated 2026-06-07, B-783)**: The Scenario walker binds `pads.json` and related public members from `.pdscenario` archives as FileProvider archive-member paths, and `setupLoadFiles()` compiles public `pads.json` into the runtime padfile layout before legacy pad-handle fallback.
- **2026-05-28 scenario navigation public-source runtime (`B-384`; semantic JSON updated 2026-06-07, B-785; catalog submember propagation updated B-821)**: `.pdscenario` now requires `navigation/waypoints.json`, `navigation/waygroups.json`, and `navigation/covers.json`, and stage load compiles those archive-member FileProvider sources into runtime waypoint/group/cover tables beside `pads.json`. Scenario catalog rows and runtime bindings preserve the declared `waypoints_file`, `waygroups_file`, `covers_file`, and `paths_file` members explicitly, rather than relying only on hidden canonical member names. These JSON rows are local scenario structure, not asset references; asset references in public payloads still use actual catalog IDs only. Setup/tiles, room-portal source-derived runtime, and mission/level graph behavior remain active under `c3844-s1`.
- **2026-05-28 scenario setup/tile/objective graph source cutover (`B-385`; JSON-source updated 2026-06-07, B-780 through B-789)**: Stage load now activates the active `.pdscenario` catalog row and uses its public `scene.glb`/optional collision source as the source-derived world mesh and tile-cache input when available. Semantic JSON source members such as `objects.json`, `setup.fields.json`, `objectives.json`, `volumes.json`, and `pads.json` feed runtime setup compilation and the source-gated weapon match / SP-in-MP overlay path. Active `level.graph.json` table refs select the runtime source members for pads/volumes/setup/navigation/objectives, trigger-volume graph nodes must match public `volumes.json` rows, Enter Room / Throw In Room objective criteria ask those rows to resolve active pad-room matches, setup behavior links validate against graph setup-link source before live registration, and `.pdmission` generation now emits per-objective/per-criteria mission graph nodes from decoded public objective rows. `objectiveCheck()` records against the active mission graph before returning the current OG parity result. Mission/level graph behavior runtime parity remains the open cutover; the catalog/provider layer must still reject raw setup dumps, `.bin` cache payloads, stale TSV members, and fake source handles.
- **2026-05-28 source pad corruption fix (`B-386`)**: MP setup AI-list conversion/sorting is now bounded by the loaded setup blob size before source-compiled `.pdscenario` pads are prepared. This prevents stage AI-list sorting from writing past the setup AI-list table into the following stage-pool allocation, which could corrupt public-source pad/navigation data during source-gated match start.
- **2026-05-28 scenario model closure fix (`B-387`; JSON-source updated 2026-06-07, B-787)**: Public scenario `objects.json` `model_catalog_id` refs must resolve to actual catalog archives on disk, not only syntactically valid names. `.pdmesh` extraction now emits all `NUM_MODELS` catalog model archives through `catalogModelIdByModelnum()`, so scenario model refs such as `base:model_chravenger` have real `.pdmesh` closure under strict conformance.
- **2026-05-28 pdmesh skeleton metadata fix (`B-388`)**: Generated modeldef skeleton metadata export now treats small legacy skeleton tokens such as `SKEL_HEAD` as IDs instead of pointers and resolves nested `.pdhead::mesh.pdmesh` manifests correctly. The source-gated DY-357 match now reaches runtime source loading and proves `base:model_dy357_hi` plus `base:model_cartridge_rifle` from extracted `.pdmesh` public sources with no raw bin fallback.
- **2026-05-28 modeldef/held weapon source cutover (`c3844`)**: `modeldefLoad*` now resolves model source handles through catalog/FileProvider source first and refuses ROM fallback when the public source chain is missing. Queued held weapon model loading resolves catalog model source handles, so DY-357 and cartridge modeldefs load from `.pdmesh::model.obj` archive members instead of raw ROM model files. Verification: `c3844model` all-target build, `[catalog][provider][static]`, `[modding][pdxxx][c3844]`, native-source guard, strict generated-output conformance, and `weapon_match_source_gate_smoke` 35/35.

Test surface: `[catalog][passd]` 9 cases / ~25 assertions in `tests/test_romextract_passd.cpp` plus the existing Pass A / Pass B pins.

Audit: [audits/catalog-phase3-passd-self-heal-2026-05-02.md](../audits/catalog-phase3-passd-self-heal-2026-05-02.md). Plan: [designs/catalog/catalog-rom-once-phase3-plan-2026-05-02.md](../designs/catalog/catalog-rom-once-phase3-plan-2026-05-02.md).

---

## Utilization program state (c3849, 2026-06-17)

The measured-gaps audit ([audits/migration-utilization-measurement-2026-06-10.md](../audits/migration-utilization-measurement-2026-06-10.md)) drove a 7-wave closure program. As of 2026-06-17, Waves 1-7 are SHIPPED and verified for the current tree:

- **Waves 1-4** (telemetry, soundnum/stagenum/animnum/texnum allocators, FONT consumer, texture emitter extraction + Slice B bind verification/slug unification): see the session log entries of 2026-06-10 and [designs/catalog/c3849-wave-implementation-maps.md](../designs/catalog/c3849-wave-implementation-maps.md).
- **Wave 5 (dead-IR consumers)**: the ~115 weapon-graph IR fields now have production consumers feeding the existing OG execution routines through `*ForGameplay` accessors and custom-slot guards (binding specs B1-B8 in the maps doc). weaponTick has a combined custom PROJECTILE arm (wall-hugger -> contact-impact -> fuse timer) and a custom ENTITY arm (remote w/ detonator provenance, timed, proxy w/ LOS, storm); stick gate honors sticky_attach/sticky-device records; bounce/trail/impact filter/hit-sound/spark/explosion refs consume; guidance gains/fbw numerics/trajectory clamp consume; deployed-autogun cadence/muzzles/beam/ffsuppress + transition-to-entity + owner-cleanup + interaction consume via the g_ThrownLaptopLatch sidecar; settings/variables/presentation parse with $name substitution and defaults layering; camera_effect xray consumes.
- **Wave 6a (meta families)**: first 3 of 11 assetRuntimeFind* gameplay consumers (botprofile, gamemode, theme), value-identical to native mirrors with once-per-session `CATALOG.<FAM>.RUNTIME_MISS` fallbacks. Remaining 8 ranked + deferred with reasons in the maps doc.
- **Wave 6b (.pdeffect runtime)**: full compiler (canonical `pd.effect_graph.v1`, legacy accepted loudly) + effect_graph_runtime records + the four `effectGraphResolve*` bridges consumed at the Wave-5 WAVE6-EFFECT-HANDOFF seams + the custom spark-row registry (pink Needler spark achievable; explosion fireball tint is NOT OG-parameterizable - renderer slice deferred). Nested `.pdeffect` ingestion fixed at both levels (weapon -> projectile -> effect).
- **Wave 7 (complete 2026-06-17)**: weapon graph runtime is product-default ON; `Debug.WeaponGraphRuntime`, the Settings debug checkbox, and `MPOPTION_WEAPONGRAPH` are retired; normal-play fallback cutover is fatal for the selected source-owned families (texture, animation, SFX, music, Scenario, model); and `NET_PROTOCOL_VER` is bumped to 51 so mixed v50/v51 peers are refused at auth. `weaponGraphRuntimeSetEnabled()` remains a `PD_TESTS` hook only.

  2026-06-17 live proof: `needler_graph_runtime_visual_smoke` passed 40/40 in `.claude/smoke-verify-runs/results-20260617T193054Z.json` against isolated session build `wave7`. The log proves `mod_needler:needler` match spawn, held and projectile mesh ingestion from `.pdmod::needler.pdweapon` nested `.pdmesh::model.gltf` paths, `.pdeffect` ingestion, `BONDGUN.SOURCE` loading private source filenum 2016, and `MODASSET.RENDER` for skeletonless first-person Needler source geometry (12 vertices / 4 tris).

Bug ledger from the program: B-915/916/917 fixed (Unit 0), B-918 fixed (test pin repair), B-919 open (decomp quirk, verify-before-fix).

---

## What is in flight

- **Asset Provider Phase 4.** Filenum retirement (~23 game-code sites). Blocked on three prerequisite API migrations: handle-aware `assetGetSize`, handle-aware `modeldefLoad`, `MENUMODELPARAMS_SET_HANDLE`. Design at [designs/catalog/catalog-asset-provider-future-phases.md](../designs/catalog/catalog-asset-provider-future-phases.md) [TBD doc].
- **Catalog post-migration polish (non-blocking).** Scenarios / game modes and bot profiles + bot variants do not yet have a typed manager + `.pdbase`. Each is a future micro-lane following the F1-F13 template proven by weapons / heads / bodies / arenas. The architectural endpoint (ROM-once-then-disk + catalog-fronted accessors) is achieved without these.
- **Catalog Universality Pivot COMPLETE, with readability guard active.** Per [designs/catalog/universality-pivot-schemas.md](../designs/catalog/universality-pivot-schemas.md). Step 0 (schema lock-down) at dev `00fdb8b7`; Step 1 (`.pdwpn` + `.pdmesh` + `.pdanim` for weapons) at dev `7e0d0791`; Step 2 (`.pdhead` + `.pdbody` + `.pdarena` + `.pdscenario`) shipped 2026-05-03; Steps 3a/3b/4/5 completed the universal directory walker and retired the aggregate `.pdbase` tier. Emitters live in `port/src/romextract_pd*.c`. As of 2026-05-22, generated catalog IDs from these emitters use readable catalog IDs and keep raw FILE_*/SFX_*/ANIM_*/seq/model slots as provenance metadata only.

---

## Known gaps

- **41 vs 86 slot asymmetry.** Catalog covers 41 MPWEAPON entries (`MPWEAPON_*` slots 0x00..0x28); slots 0x29..0x55 (`WEAPON_PSYCHOSISGUN` through `WEAPON_SUICIDEPILL`) have no string-keyed lookup. `catalogManagerGetWeaponById("base:falcon2")` resolves through `runtime_index = g_MpWeapons[mpw].weaponnum`; solo or mission-only weapons cannot be string-resolved today. Either grow the catalog to all 86 WEAPON slots or document the asymmetry. Source: [audits/infrastructure-pillars-status-2026-04-27.md](../audits/infrastructure-pillars-status-2026-04-27.md) Section 1.
- **F10 `.pdbase` loader is scaffold-only.** Header [loader_pdbase.h:49-51](../../port/include/loader_pdbase.h:49) says "the directory scan is implemented but archives are not yet decoded"; body [loader_pdbase.c:37](../../port/src/loader_pdbase.c:37) says "Phase 2 (F10): scaffold only. The directory enumeration is not implemented yet." Header lies to its implementation. F11 fixes both.
- **F10 tests are static-text shape only.** [tests/test_loader_pdbase_scan.cpp](../../tests/test_loader_pdbase_scan.cpp) has 3 cases, all static text checks; none call `loaderPdbaseScan` or assert returns. Tests would pass even if implementation were stripped. F11 needs behavioral coverage.
- **EYESPY mutator still mutates Layer A static storage.** [catalog_mgr_weapons.c:132-134](../../port/src/catalog_mgr_weapons.c:132) writes `w->name`, `w->shortname`, `w->flags` directly on the struct retrieved from `g_Weapons[WEAPON_EYESPY]`. When data moves to `.pdbase` in F11+, this needs a different mechanism (loaded record will not be permanent static).
- **Const-cast violation.** [catalog_mgr_weapons.c:99-107](../../port/src/catalog_mgr_weapons.c:99) returns `const struct invaimsettings *`; the call site at [src/game/game_0b0fd0.c:120](../../src/game/game_0b0fd0.c:120) casts away const into a non-const pointer. Inherited from legacy `&invaimsettings_default` usage. F12 cleanup.

---

## Live g_Weapons[] direct reads

Three sites remain, all inside the manager (the chokepoint):

- [port/src/catalog_mgr_weapons.c:59](../../port/src/catalog_mgr_weapons.c:59) - `catalogManagerGetWeaponById` returns `g_Weapons[weapon_id]`
- [port/src/catalog_mgr_weapons.c:67](../../port/src/catalog_mgr_weapons.c:67) - iterator
- [port/src/catalog_mgr_weapons.c:96](../../port/src/catalog_mgr_weapons.c:96) - by mp index

The two `src/game/setup.c` references (line 2776, 2865) are inside block comments narrating the B-263 sentinel bug; not live code. The `loader_pdbase.c:10` reference is in a comment.

Static text scan in [tests/test_weapon_direct_reads_audit.cpp](../../tests/test_weapon_direct_reads_audit.cpp) `REQUIRE`s that specific source files contain zero `g_Weapons[` substrings; this catches reintroduction at compile time.

---

## Active design references

- [designs/catalog/catalog-full-pipeline-weapons.md](../designs/catalog/catalog-full-pipeline-weapons.md) - F1-F10 shipped, F11+ data move design.
- [designs/catalog/catalog-asset-provider-future-phases.md](../designs/catalog/catalog-asset-provider-future-phases.md) - Asset Provider Phase 4 prerequisite work [TBD doc].

---

## Where to look

- For catalog IDs and the registration model: this doc + [port/include/assetcatalog.h](../../port/include/assetcatalog.h).
- For catalog invariants: [constraints.md](../constraints.md) entries on catalog (S485, S487, S488, S569, S490, S493, S577, S578).
- For mod content registration: [pillars/modding.md](modding.md).
- For wire / save references to catalog IDs: [pillars/save-wire-format.md](save-wire-format.md).
- For typed lifecycle at manifest/screen/stage boundaries: [pillars/server.md](server.md) (manifest pipeline) and [pillars/connectivity.md](connectivity.md) (CLC_LOBBY_START / SVC_STAGE_START flow).

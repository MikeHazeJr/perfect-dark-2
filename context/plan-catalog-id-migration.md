# Catalog ID Migration Plan — Full Codebase

> **Mandate**: Zero integer-to-catalog-ID conversion anywhere, ever. Catalog ID (`char[64]` string) is the sole identity for every asset. No intermediate integer index step. Catalog ID goes all the way through — including engine internals.
>
> **Scope**: ~4,100 references to integer asset identity across `port/`, `src/game/`, `src/include/`.
>
> **Date**: 2026-04-06 (updated with game director decisions)
>
> Back to [index](README.md) | See also [constraints.md](constraints.md)

---

## Game Director Decisions (2026-04-06)

| Decision | Ruling | Impact |
|----------|--------|--------|
| **D-1: Weapon Enums** | **FULL migration.** Every `WEAPON_xxx` / `weaponnum` becomes catalog ID. All ~660 references in `src/game/`. | +660 refs, +35 files |
| **D-2: Model Numbers** | **FULL migration.** Add `ASSET_MODEL` to catalog (already exists!), register all models, audit completeness. | +83 refs, +15 files |
| **D-3: `mainChangeToStage`** | **Full engine refactor.** `mainChangeToStage(const char *stage_id)` — catalog ID all the way through. No boundary conversion. | ~15 callers, engine refactor |

---

## Current State

The codebase has a **dual identity** problem. Phases A–G of catalog universality gave us catalog ID strings as the *primary* identity in network wire, save/load, and config structs — but integer indices remain everywhere as "derived" fields that the engine still needs at runtime. Conversion functions bridge the gap.

**Active conversion functions** (all must be eliminated):
- `catalogResolveBodyByMpIndex(s32 mpbodynum)` — mpbody index → catalog ID string
- `catalogResolveHeadByMpIndex(s32 mpheadnum)` — mphead index → catalog ID string
- `catalogBodynumToMpBodyIdx(s32 bodynum)` — g_HeadsAndBodies[] index → g_MpBodies[] index
- `catalogHeadnumToMpHeadIdx(s32 headnum)` — g_HeadsAndBodies[] index → g_MpHeads[] index
- `catalogResolveStageByStagenum(s32 stagenum)` — stagenum hex ID → catalog ID string
- `catalogResolveArenaByStagenum(s32 stagenum)` — stagenum hex ID → catalog ID string (arena)
- `catalogResolveWeaponByGameId(s32 weapon_id)` — WEAPON_xxx enum → catalog ID string
- `catalogGetSafeBody(s32 bodynum)` — clamp bodynum to valid range
- `catalogGetSafeBodyPaired(s32 bodynum, s32 *out_mpheadnum)` — safe body + paired head
- `catalogGetSafeHead(s32 headnum)` — clamp headnum to valid range

**Where catalog IDs already work** (completed in Phases A–G):
- `g_MatchConfig.stage_id` — primary field, `stagenum` is DERIVED
- `g_MatchConfig.slots[].body_id` / `head_id` — primary fields, `bodynum`/`headnum` DERIVED
- `g_MpSetup.stage_id` — primary field
- Network wire: `SVC_STAGE_START` sends catalog IDs for stage, weapons, bot bodies/heads
- `SVC_LOBBY_STATE` sends `stage_id` string
- Save/load: writes catalog ID strings, falls back to integer on old saves
- Identity system (`identity.h`): `head_id`/`body_id` as `char[CATALOG_ID_LEN]`
- `net_client_t.settings.body_id` / `head_id` as `char[CATALOG_ID_LEN]`

**Where integer identity persists** (this plan):
- `mpchrconfig.mpheadnum` / `mpbodynum` (u8) — core character config struct
- `match_slot_t.headnum` / `bodynum` (u8) — DERIVED match slot fields
- `lobby_settings_t.stagenum` / `headnum` / `bodynum` (u8) — lobby state
- `net_server_info_t.stagenum` (u8) — server browser
- `mission_config_t.stagenum` (u8) — mission config
- `netmove_t.weaponnum` (s8) — per-frame user command
- `weaponobj.weaponnum` (u8) — prop/object identity
- `aibot.weaponnum` (s8) — bot current weapon
- `gunctrl.weaponnum` (s32) — player gun controller
- All `WEAPON_xxx` enum usage in game logic (~660 references across 35 files)
- `modelnum` (s16) — model identity in props/objects (~83 references across 15 files)
- `mainChangeToStage(s32 stagenum)` — 15 call sites
- All `g_MpBodies[]` / `g_MpHeads[]` array indexed lookups
- UI bridge functions: `mpPlayerConfigGetHead/Body`, `pdguiCharPreviewRequest(u8, u8)`
- Callback interface: `netcb_OnPlayerJoin(u8 headnum, u8 bodynum)` etc.

---

## Phase 0: Catalog Completeness — Live Source of Truth

**Goal**: Every asset type is registered in the catalog at startup. The catalog stays current when mods are loaded/unloaded. No gaps, no stale entries.

### 0.1 — Asset Type Registration Audit

| Asset Type | Registered? | Where | Count | Gap? |
|-----------|-------------|-------|-------|------|
| **ASSET_BODY** | YES | `assetcatalog_base.c` — from `g_MpBodies[]` | ~63 | None |
| **ASSET_HEAD** | YES | `assetcatalog_base.c` — from `g_MpHeads[]` | ~76 | None |
| **ASSET_MAP** | YES | `assetcatalog_base.c` — from `g_Stages[]` | ~60 | None |
| **ASSET_ARENA** | YES | `assetcatalog_base.c` — from `g_MpArenas[]` | ~30 | None |
| **ASSET_WEAPON** | YES | `assetcatalog_base_extended.c` — from `s_BaseWeapons[]` | 47 | **Gap: only MP weapons registered (MPWEAPON_* 0x01–0x2f). Engine has WEAPON_xxx enums (0x00–0x38) which include WEAPON_NONE, WEAPON_UNARMED, etc. that are not MP weapons. Need to register ALL weapon enums, including non-MP ones (unarmed, disabled, special).** |
| **ASSET_MODEL** | YES | `assetcatalog_base_extended.c` — from `g_ModelStates[]` | ~441 (NUM_MODELS) | **Gap: audit needed — are ALL models registered, or just "prop models"? Comment says "base prop models" which may not be all NUM_MODELS. Verify loop covers 0..NUM_MODELS-1 completely.** |
| **ASSET_TEXTURE** | YES | `assetcatalog_base_extended.c` | ~3503 | Verify count |
| **ASSET_ANIMATION** | YES | `assetcatalog_base_extended.c` | ~1207 | N/A for this plan |
| **ASSET_SFX** | YES | `assetcatalog_base_extended.c` (registered as type) | — | N/A for this plan |
| **ASSET_MUSIC** | YES | registered type exists | — | N/A for this plan |
| **ASSET_AUDIO** | YES | `assetcatalog_base_extended.c` | ~1545 | N/A for this plan |
| **ASSET_LANG** | YES | `assetcatalog_base_extended.c` | ~68 | N/A for this plan |
| **ASSET_PROP** | YES | `assetcatalog_base_extended.c` | 8 categories | N/A for this plan |
| **ASSET_GAMEMODE** | YES | `assetcatalog_base_extended.c` | 6 | N/A for this plan |
| **ASSET_HUD** | YES | `assetcatalog_base_extended.c` | 6 | N/A for this plan |

### 0.2 — Weapon Registration Gap Fix

| Item | Detail |
|------|--------|
| **File** | `port/src/assetcatalog_base_extended.c` |
| **What** | `s_BaseWeapons[]` only covers MPWEAPON_* range (0x01–0x2f, 47 entries). The engine uses WEAPON_xxx enums (0x00–0x38+), which include WEAPON_NONE (0x00), WEAPON_UNARMED (0x01), and higher values up to WEAPON_COMBATBOOST. These are different numbering from MPWEAPON_*. |
| **Should become** | Register ALL WEAPON_xxx enum values as ASSET_WEAPON. Two ID spaces exist: MPWEAPON_* (MP slot indices) and WEAPON_* (engine weapon enum). Both need catalog coverage. The `weapon_id` field in ASSET_WEAPON ext data currently stores MPWEAPON_*; we need a parallel `engine_weapon_id` for WEAPON_* or unify the ID space. |
| **Complexity** | **Moderate** — need to map between MPWEAPON_* and WEAPON_* cleanly. |

### 0.3 — Model Registration Completeness Audit

| Item | Detail |
|------|--------|
| **File** | `port/src/assetcatalog_base_extended.c:511–525` |
| **What** | Loop registers `g_ModelStates[0..NUM_MODELS-1]` as ASSET_MODEL. NUM_MODELS = 0x1b9 (441) or 0x1bb (443 JPN). |
| **Action** | Verify the loop actually runs 0..NUM_MODELS-1 with no skips. Check if any models are conditionally excluded. Check mod-added models get ASSET_MODEL entries too. |
| **Complexity** | **Trivial** — audit only |

### 0.4 — Catalog Liveness: Mod Load/Unload Freshness

**Game director question**: "How are we ensuring the catalog is always up to date?"

**Current lifecycle**:
1. `assetCatalogInit()` — allocate pool
2. `assetCatalogRegisterBaseGame()` → `assetCatalogRegisterBaseGameExtended()` — register ROM assets
3. `assetCatalogScanComponents(modsdir)` — scan mod directories, parse INIs, register mod assets
4. `modmgrLoadMod()` — called per enabled mod
5. `catalogClear()` / `catalogClearMods()` — declared in header but **implementation needs audit**

**Gaps identified**:
- **Hot reload**: If a mod is enabled/disabled at runtime, does the catalog update? `modmgrLoadMod()` exists but there's no `modmgrUnloadMod()` / `modmgrReloadMod()` that re-scans the catalog.
- **Mod asset removal**: `catalogClearMods()` is declared but needs verification that it correctly removes only mod entries and that subsequent re-scan rebuilds correctly.
- **Runtime staleness**: After initial startup, the catalog is static. If mods are toggled in the mod manager UI, the catalog must reflect changes before any asset resolution.
- **Server-side**: `server_main.c` calls `assetCatalogInit()` + `assetCatalogRegisterBaseGame()` but no mod scanning — intentional, but needs documentation.

**Required work**:

| Task | Description | Complexity |
|------|-------------|------------|
| **0.4a** | Audit `catalogClear()` / `catalogClearMods()` implementations — verify correctness | Trivial |
| **0.4b** | Implement `catalogRefreshMods()` — clear mod entries, re-scan, re-register. Call from mod manager toggle. | Moderate |
| **0.4c** | Add catalog generation counter (`g_CatalogGeneration`) — incremented on any catalog mutation. Consumers can cache-invalidate by checking generation. | Trivial |
| **0.4d** | Hook mod enable/disable in modmgr UI to call `catalogRefreshMods()` | Trivial |
| **0.4e** | Add startup validation: after all registration, log warning for any ASSET_WEAPON with no catalog entry, any MODEL_* with no entry, any stagenum with no entry | Trivial |

### 0.5 — Catalog Resolver Performance

**Concern**: With catalog ID as the sole identity, every asset lookup that currently does `g_MpBodies[idx]` (O(1) array index) becomes `assetCatalogResolve(id)` (O(n) string scan or hash lookup).

**Current state**: `assetCatalogResolve()` does O(n) linear scan.

**Required work**:

| Task | Description | Complexity |
|------|-------------|------------|
| **0.5a** | Add hash table index to catalog: `catalogResolve()` becomes O(1) amortized. Critical for weapon lookups in hot paths (per-frame). | Moderate |
| **0.5b** | Cached resolution: functions that resolve the same ID every frame (e.g., current weapon) should cache the resolved `asset_entry_t*` and invalidate on catalog generation change. | Moderate |

---

## Phase 1: Network Wire — Integer Asset IDs on the Wire

**Goal**: Every network message that currently sends an integer asset identity sends a catalog ID string instead.

### 1.1 — `CLC_USERCMD` weaponnum (netmsg.c:219,239)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:219` (write), `:239` (read) |
| **What** | `netbufWriteS8(buf, in->weaponnum)` — sends weapon switch as `s8` enum |
| **Should become** | `netbufWriteStr(buf, weapon_id)` where `weapon_id` is the catalog ID from the struct. Per D-1, `netmove_t.weaponnum` itself becomes `char weapon_id[CATALOG_ID_LEN]`. |
| **Complexity** | **Moderate** — high-frequency message (every frame), bandwidth concern. Currently sends `-1` when no change — that optimization stays. Only the `!= -1` path needs catalog ID string. |

### 1.2 — `SVC_PLAYERSTATE` weaponnum (netmsg.c:1333)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:1333` |
| **What** | `netbufWriteS8(dst, pl->gunctrl.weaponnum)` — server broadcasts player weapon state |
| **Should become** | `netbufWriteStr(dst, pl->gunctrl.weapon_id)` — per D-1, gunctrl gets `weapon_id` field |
| **Complexity** | **Moderate** — same bandwidth concern as 1.1 |

### 1.3 — `SVC_PROPSTATE` weaponnum + modelnum (netmsg.c:1651–1661)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:1651–1661` |
| **What** | Sends `prop->weapon->base.modelnum` (s16), `prop->weapon->weaponnum` (u8), `prop->weapon->dualweaponnum` (s8), `prop->obj->modelnum` (s16) |
| **Should become** | Catalog ID strings for weapon and model. Per D-2, `catalogResolveByRuntimeIndex(ASSET_MODEL, modelnum)` already works — use it for wire. |
| **Complexity** | **Complex** — multiple fields, prop creation on receive side needs catalog→runtime resolution |

### 1.4 — `SVC_PROPDAMAGE` weaponnum (netmsg.c:1902)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:1902` |
| **What** | `netbufWriteS8(dst, weaponnum)` — damage source weapon |
| **Should become** | `netbufWriteStr(dst, weapon_id)` — catalog ID string |
| **Complexity** | **Trivial** — low frequency |

### 1.5 — `SVC_CHRDISARM` weaponnum (netmsg.c:2235)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:2235` |
| **What** | `netbufWriteU8(dst, weaponnum)` |
| **Should become** | Catalog ID string |
| **Complexity** | **Trivial** |

### 1.6 — `SVC_BOTSTATE` / `SVC_BOTFULLSTATE` weaponnum (netmsg.c:2465, 2832)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:2465, 2832` |
| **What** | `netbufWriteS8(dst, aibot->weaponnum)` |
| **Should become** | `netbufWriteStr(dst, aibot->weapon_id)` — per D-1, `aibot.weaponnum` becomes `weapon_id` |
| **Complexity** | **Trivial** |

### 1.7 — `SVC_CLIENT_SETTINGS` mpbodynum/mpheadnum (netmsg.c:824–827)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:824–827` |
| **What** | Resolves `bc->base.mpbodynum`/`mpheadnum` → catalog ID via `catalogResolveBodyByMpIndex`. Already sends catalog IDs on wire but **reads from integer fields**. |
| **Should become** | Read directly from `body_id`/`head_id` string fields (already exist on `net_client_t.settings`) |
| **Complexity** | **Trivial** |

### 1.8 — `SVC_CLIENT_SETTINGS` read path: integer write-back (netmsg.c:1078–1143)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:1078–1143` |
| **What** | After receiving catalog IDs from wire, resolves back to mpbodynum/mpheadnum integers and writes to `g_PlayerConfigsArray[].base.mpbodynum`. |
| **Should become** | Store catalog ID strings directly in `mpchrconfig.body_id`/`head_id`. No integer write-back. |
| **Complexity** | **Complex** — this is the central fan-out point. Cascades to all consumers. |

### 1.9 — `SVC_STAGE_START` bot body/head resolution (netmsg.c:4155–4182)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:4155–4182` |
| **What** | Resolves catalog ID → integer → `g_BotConfigsArray[bi].base.mpbodynum` |
| **Should become** | Store catalog ID strings in `g_BotConfigsArray[bi].base.body_id` directly |
| **Complexity** | **Moderate** |

---

## Phase 2: Config/Data Structs — Integer Asset Fields

**Goal**: Every struct field that stores asset identity as an integer becomes catalog ID string. No "derived" integer fields kept.

### 2.1 — `mpchrconfig.mpheadnum` / `mpbodynum` (types.h:4007–4008)

| Item | Detail |
|------|--------|
| **File** | `src/include/types.h:4007–4008` |
| **What** | `u8 mpheadnum; u8 mpbodynum;` — the single most-used integer asset identity |
| **Should become** | `char body_id[CATALOG_ID_LEN]; char head_id[CATALOG_ID_LEN];` — replaces the integers entirely |
| **Consumers** | `g_PlayerConfigsArray[]`, `g_BotConfigsArray[]`, savefile.c, netmsg.c, matchsetup.c, netmenu.c, pdgui_bridge.c, botmgr.c, challenge.c, mplayer.c, menu.c |
| **Complexity** | **Complex** — ~80 direct references. Keystone change. |

### 2.2 — `match_slot_t.headnum` / `bodynum` (matchsetup.h:50–51)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/matchsetup.h:50–51` |
| **What** | `u8 headnum; u8 bodynum;` — DERIVED fields |
| **Should become** | Remove entirely. Callers use `body_id`/`head_id` (already present at :48–49). |
| **Complexity** | **Moderate** — ~15 references |

### 2.3 — `match_config_t.stagenum` (matchsetup.h:63)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/matchsetup.h:63` |
| **What** | `u8 stagenum;` — DERIVED from `stage_id` |
| **Should become** | Remove entirely. All consumers use `stage_id`. Per D-3, engine resolves stage_id internally. |
| **Complexity** | **Moderate** — ~20 references |

### 2.4 — `g_MpSetup.stagenum` (types.h:4088) → FULL REMOVAL per D-3

| Item | Detail |
|------|--------|
| **File** | `src/include/types.h:4088` |
| **What** | `u8 stagenum;` — used by `mainChangeToStage()` and engine stage loading |
| **Should become** | **Remove entirely.** Per D-3, `mainChangeToStage()` takes `const char *stage_id`. All code that reads `g_MpSetup.stagenum` reads `g_MpSetup.stage_id` instead. Engine resolves internally. |
| **Complexity** | **Complex** — requires engine refactor (see Phase 4.7) |

### 2.5 — `lobby_settings_t` (netlobby.h:23–32)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/netlobby.h:23–32` |
| **What** | `u8 headnum; u8 bodynum; u8 stagenum;` |
| **Should become** | `char body_id[CATALOG_ID_LEN]; char head_id[CATALOG_ID_LEN]; char stage_id[CATALOG_ID_LEN];` |
| **Complexity** | **Trivial** |

### 2.6 — `net_server_info_t.stagenum` (net.h:68)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/net.h:68` |
| **What** | `u8 stagenum;` |
| **Should become** | `char stage_id[CATALOG_ID_LEN];` |
| **Complexity** | **Trivial** |

### 2.7 — `mission_config_t.stagenum` (net.h)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/net.h` |
| **What** | `stagenum` for co-op/solo mission config |
| **Should become** | `char stage_id[CATALOG_ID_LEN];` — per D-3, no integer fallback |
| **Complexity** | **Moderate** |

### 2.8 — `netmove_t.weaponnum` (net.h:139) → FULL per D-1

| Item | Detail |
|------|--------|
| **File** | `port/include/net/net.h:139` |
| **What** | `s8 weaponnum;` — weapon switch in user command |
| **Should become** | `char weapon_id[CATALOG_ID_LEN];` — empty string means no change (replaces `-1` sentinel) |
| **Complexity** | **Moderate** — high frequency struct, size increase from 1 byte to 64 bytes per field |

### 2.9 — `mpchrconfig.mpheadnum`/`mpbodynum` in `g_ChallengeSimulants[]` (types.h:5003–5004)

| Item | Detail |
|------|--------|
| **File** | `src/include/types.h:5003–5004` |
| **What** | Challenge config simulant body/head as `u8` |
| **Should become** | `char body_id[CATALOG_ID_LEN]; char head_id[CATALOG_ID_LEN];` |
| **Complexity** | **Moderate** — challenge.c references ~10 sites |

### 2.10 — `weaponobj.weaponnum` / `gunctrl.weaponnum` / `aibot.weaponnum` (types.h) → FULL per D-1

| Item | Detail |
|------|--------|
| **File** | `src/include/types.h:726, 965, 1572, 2071, 2323, 3780, 5015, 5651` |
| **What** | Weapon identity as `s32`/`s8`/`u8` across 8 struct definitions |
| **Should become** | `char weapon_id[CATALOG_ID_LEN]` in each. The WEAPON_xxx enum values are eliminated as identity — catalog ID becomes the sole weapon identity throughout the engine. |
| **Impact** | ~660 references across 35 files in `src/game/`. Top files: bondgun.c (374), propobj.c (217), botinv.c (95), bot.c (64), chraction.c (88), inv.c (61), training.c (27), player.c (21). |
| **Complexity** | **MASSIVE** — largest single item in this plan. Every `WEAPON_xxx` comparison becomes string comparison or catalog lookup. |
| **Migration strategy** | See Phase 4.5 for the detailed weapon migration approach. |

### 2.11 — `defaultobj.modelnum` / `weaponobj.base.modelnum` (types.h:1460) → FULL per D-2

| Item | Detail |
|------|--------|
| **File** | `src/include/types.h:1460`, `port/include/preprocess/setup.h:27` |
| **What** | `s16 modelnum` — model identity for all props/objects |
| **Should become** | `char model_id[CATALOG_ID_LEN]` — catalog ID for the 3D model |
| **Impact** | ~83 references across 15 files. Top: propobj.c (40), setup.c (11), chraicommands.c (7), bondgun.c (5), lv.c (5). |
| **Complexity** | **MASSIVE** — model system is deeply embedded. Prop creation, collision, rendering all use modelnum. |
| **Migration strategy** | See Phase 4.6 for the detailed model migration approach. |

### 2.12 — `s_ReadyGate.stagenum` (netmsg.c:88)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:88` |
| **What** | `u8 stagenum;` in ready-gate struct |
| **Should become** | `char stage_id[CATALOG_ID_LEN];` — per D-3 |
| **Complexity** | **Trivial** |

---

## Phase 3: Function APIs — Integer Parameters/Returns

**Goal**: Every function that accepts or returns an integer asset identity takes a catalog ID string instead.

### 3.1 — `catalogResolveBodyByMpIndex` / `catalogResolveHeadByMpIndex` (assetcatalog_api.c:363–384)

| Item | Detail |
|------|--------|
| **What** | Takes integer index, returns catalog ID string |
| **Should become** | Eliminated — callers already have catalog ID strings |
| **Callers** | 15 call sites: identity.c, matchsetup.c, net.c, netmsg.c, savefile.c, pdgui_menu_agentcreate.cpp, pdgui_menu_room.cpp |
| **Complexity** | **Moderate** |

### 3.2 — `catalogBodynumToMpBodyIdx` / `catalogHeadnumToMpHeadIdx` (assetcatalog_api.c:389–405)

| Item | Detail |
|------|--------|
| **What** | Takes g_HeadsAndBodies[] index, returns g_MpBodies[] index |
| **Should become** | Eliminated |
| **Callers** | 18 call sites: matchsetup.c, netmsg.c, savefile.c, mplayer.c |
| **Complexity** | **Moderate** |

### 3.3 — `catalogResolveStageByStagenum` / `catalogResolveArenaByStagenum` (assetcatalog_api.c:634–674)

| Item | Detail |
|------|--------|
| **What** | Takes stagenum hex ID, returns catalog ID string |
| **Should become** | Eliminated — callers use `stage_id` directly |
| **Callers** | 6 call sites |
| **Complexity** | **Trivial** |

### 3.4 — `catalogResolveWeaponByGameId` (assetcatalog_api.c:676)

| Item | Detail |
|------|--------|
| **What** | Takes WEAPON_xxx enum, returns catalog ID string |
| **Should become** | Eliminated — per D-1, all callers have catalog ID directly |
| **Callers** | 6 call sites: netmanifest.c, netmsg.c, savefile.c, scenario_save.c |
| **Complexity** | **Trivial** once weapon structs migrated |

### 3.5 — `catalogGetSafeBody` / `catalogGetSafeBodyPaired` / `catalogGetSafeHead` (modelcatalog.c)

| Item | Detail |
|------|--------|
| **What** | Validates integer body/head index, clamps to safe default |
| **Should become** | `catalogValidateBodyId(const char *body_id)` — returns valid catalog ID or `"base:dark_combat"` default |
| **Callers** | netmsg.c:1078–1079, 1136–1138, pdmain.c:345 |
| **Complexity** | **Moderate** |

### 3.6 — `mpGetBodyId` / `mpGetHeadId` / `mpGetBodyName` / `mpGetMpheadnumByMpbodynum` (mplayer.h)

| Item | Detail |
|------|--------|
| **File** | `src/include/game/mplayer/mplayer.h:54–61` |
| **Should become** | `catalogGetDisplayName(body_id)`, `catalogGetBodyDefaultHead(body_id)` (already exists) |
| **Callers** | ~20 call sites across botmgr.c, menu.c, netmenu.c, server_stubs.c, and 5 pdgui_menu_*.cpp files |
| **Complexity** | **Moderate** |

### 3.7 — `mpPlayerConfigGetHead` / `mpPlayerConfigGetBody` (pdgui_bridge.c:66–82)

| Item | Detail |
|------|--------|
| **File** | `port/fast3d/pdgui_bridge.c:66–82` |
| **What** | Returns `u8` mpheadnum/mpbodynum |
| **Should become** | `const char *mpPlayerConfigGetBodyId(s32 playernum)` — returns catalog ID string |
| **Complexity** | **Trivial** |

### 3.8 — `pdguiCharPreviewRequest(u8 headnum, u8 bodynum)` (pdgui_charpreview.h:23)

| Item | Detail |
|------|--------|
| **File** | `port/include/pdgui_charpreview.h:23`, `port/fast3d/pdgui_charpreview.c:93` |
| **Should become** | `pdguiCharPreviewRequest(const char *head_id, const char *body_id)` |
| **Callers** | 4 sites: agentcreate, agentselect, moddinghub, room |
| **Complexity** | **Moderate** — needs internal resolution to model data for rendering |

### 3.9 — `netcb_OnPlayerJoin/OnPlayerSettingsChanged` (net_interface.h:15,17)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/net_interface.h:15,17` |
| **What** | `u8 headnum, u8 bodynum` parameters |
| **Should become** | `const char *head_id, const char *body_id` |
| **Complexity** | **Trivial** |

### 3.10 — `netcb_OnMatchStart(u8 stagenum, ...)` / `netcb_OnStageChange(u8 stagenum)` (net_interface.h:20,29)

| Item | Detail |
|------|--------|
| **Should become** | `const char *stage_id` |
| **Complexity** | **Trivial** |

### 3.11 — `netmsgClcLobbyStartWrite` (netmsg.h:174)

| Item | Detail |
|------|--------|
| **What** | `u8 stagenum` parameter |
| **Should become** | `const char *stage_id` |
| **Complexity** | **Trivial** |

### 3.12 — `netmsgSvcPropDamageWrite` / `netmsgSvcChrDisarmWrite` (netmsg.h:119,131)

| Item | Detail |
|------|--------|
| **What** | `s32 weaponnum` / `u8 weaponnum` parameters |
| **Should become** | `const char *weapon_id` |
| **Complexity** | **Trivial** |

### 3.13 — `netServerCoopStageStart(u8 stagenum, u8 difficulty)` (net.h:251)

| Item | Detail |
|------|--------|
| **Should become** | `netServerCoopStageStart(const char *stage_id, u8 difficulty)` |
| **Complexity** | **Trivial** |

### 3.14 — `manifestBuildMission(s32 stagenum, ...)` / `manifestSPTransition(s32 stagenum)` (netmanifest.h)

| Item | Detail |
|------|--------|
| **Should become** | `const char *stage_id` |
| **Complexity** | **Moderate** |

### 3.15 — `mainChangeToStage(s32 stagenum)` → FULL REFACTOR per D-3

| Item | Detail |
|------|--------|
| **File** | `port/src/pdmain.c:765`, `port/src/server_stubs.c:287`, `src/include/lib/main.h:15` |
| **What** | `void mainChangeToStage(s32 stagenum)` — 15 call sites across netmsg.c, net.c, modmgr.c, chraicommands.c |
| **Should become** | `void mainChangeToStage(const char *stage_id)` — resolves to stagenum internally via catalog lookup. All callers pass catalog ID strings. |
| **Complexity** | **Complex** — engine stage loading deeply uses stagenum for table lookups, allocation tables, etc. Internal resolution needed. |

### 3.16 — All `weaponnum`-parameter game functions (~60 functions in src/include/game/)

| Item | Detail |
|------|--------|
| **Files** | bondgun.h (~20 functions), botact.h (~8), botinv.h (~12), inv.h (~10), propobj.h (~15), prop.h (~3), gunfx.h, training.h, game_0b0fd0.h |
| **What** | All take `s32 weaponnum` as WEAPON_xxx enum |
| **Should become** | All take `const char *weapon_id`. Internal lookup: `weapon_entry_t *w = catalogResolve(weapon_id);` then use `w->ext.weapon` for any data. |
| **Complexity** | **MASSIVE** — ~60 function signatures × N call sites each |

### 3.17 — All `modelnum`-parameter game functions

| Item | Detail |
|------|--------|
| **Files** | propobj.h (~8 functions taking `s32 modelnum`), setup.h, setuputils.h |
| **What** | `hatCreateForChr(chr, modelnum, flags)`, `weaponCreateForChr(chr, modelnum, weaponnum, flags, ...)`, `scenarioCreateObj(modelnum, ...)`, etc. |
| **Should become** | All take `const char *model_id`. |
| **Complexity** | **Complex** — prop creation is a core engine path |

---

## Phase 4: Game Logic — Integer Comparisons/Conditionals

**Goal**: Eliminate integer asset identity from all game logic.

### 4.1 — Body/head feature checks in challenge.c (challenge.c:540–633)

| Item | Detail |
|------|--------|
| **File** | `src/game/challenge.c:540–633` |
| **What** | `config->simulants[i].mpbodynum < modmgrGetTotalBodies()` bounds checks; `modmgrGetBody(mpbodynum)->requirefeature` lookups |
| **Should become** | `catalogGetBodyRequireFeature(body_id)` — catalog-based feature query |
| **Complexity** | **Moderate** |

### 4.2 — Body-cycling in mplayer.c (mplayer.c:789–860)

| Item | Detail |
|------|--------|
| **What** | Iterates bodies/heads by integer index |
| **Should become** | Catalog iteration: `catalogIterateBodies(callback)` |
| **Complexity** | **Moderate** |

### 4.3 — Bot spawn in botmgr.c (botmgr.c:44–50)

| Item | Detail |
|------|--------|
| **What** | `headnum = mpGetHeadId(g_BotConfigsArray[aibotnum].base.mpheadnum)` |
| **Should become** | `headnum = catalogBodyIdToRuntimeIndex(g_BotConfigsArray[aibotnum].base.body_id)` |
| **Complexity** | **Moderate** |

### 4.4 — Menu character display in menu.c (menu.c:1872–1875)

| Item | Detail |
|------|--------|
| **What** | `mpGetBodyId(mpbodynum)` for legacy menu |
| **Should become** | Catalog ID → runtime index at display time |
| **Complexity** | **Trivial** — may be removed by D5.8 |

### 4.5 — FULL Weapon Engine Migration (D-1) — ~660 references across 35 files

This is the single largest migration item. Strategy:

**Approach**: Introduce `weapon_entry_t *weaponResolve(const char *weapon_id)` that returns a pointer to weapon data. All code that currently does `switch(weaponnum)` or `weaponnum == WEAPON_FALCON2` becomes `strcmp(weapon_id, "base:falcon2") == 0` or, better, pointer comparison via cached resolved entries.

**Per-file breakdown**:

| File | Refs | Nature | Approach |
|------|------|--------|----------|
| `bondgun.c` | 374 | Weapon behavior dispatch, fire modes, ammo, reloading | Resolve once per frame via `weapon_entry_t*`, access fields. Replace `WEAPON_xxx` switch cases with catalog property queries. |
| `propobj.c` | 217 | Prop creation, pickup, damage, weapon-specific behavior | Same pattern — resolve at function entry, use properties. |
| `botinv.c` | 95 | Bot weapon scoring, inventory management | Resolve at entry, query properties. |
| `chraction.c` | 88 | Character actions, weapon-specific animations | Resolve at entry. |
| `bot.c` | 64 | Bot weapon selection, ammo management | Resolve at entry. |
| `inv.c` | 61 | Inventory system — has/give/remove weapon | Key lookup by catalog ID instead of enum. |
| `training.c` | 27 | Firing range weapon tracking | Straightforward replacement. |
| `player.c` | 21 | Player weapon state | Resolve at entry. |
| `bondgun.c` supp. | 16 | Model number references | Follows 4.6 |
| `gunfx.c` | 19 | Gun visual effects | Resolve at entry. |
| `game_0b0fd0.c` | 17 | Weapon data queries (flags, ammo, file nums) | These become catalog property queries. |
| `chr.c` | 15 | Character weapon assignment | Resolve at entry. |
| `setup.c` | 15 | Setup/spawn weapon placement | Follows prop creation migration. |
| Other 22 files | ~46 | Scattered references | Individual migration. |

**Key migration patterns**:
1. **`weaponnum == WEAPON_FALCON2`** → `catalogWeaponHasTag(weapon_id, "falcon2")` or string compare
2. **`switch(weaponnum)`** → Property-based dispatch or string-keyed lookup table
3. **`g_Weapons[weaponnum]`** → `catalogResolveWeapon(weapon_id)->ext.weapon.*`
4. **`VALIDWEAPON()` macro** → `weapon_id[0] != '\0'` validity check
5. **`weaponnum >= X && weaponnum <= Y`** range checks → catalog tag/property queries

**Complexity**: **MASSIVE** — estimated 15–20 sessions of focused work.

### 4.6 — FULL Model Engine Migration (D-2) — ~83 references across 15 files

**Per-file breakdown**:

| File | Refs | Nature |
|------|------|--------|
| `propobj.c` | 40 | Prop creation with specific model |
| `setup.c` | 11 | Stage setup, object placement |
| `chraicommands.c` | 7 | AI scripting — give model to chr |
| `bondgun.c` | 5 | Weapon model loading |
| `lv.c` | 5 | Level loading |
| Other 10 files | 15 | Scattered |

**Approach**: `model_id` string replaces `s16 modelnum`. `catalogResolveModel(model_id)->runtime_index` provides the integer for engine-internal array indexing when needed (e.g., `g_ModelStates[]` access). The key difference from weapons: models are less behavior-dispatched and more data-looked-up, so the migration is more mechanical.

**Complexity**: **Complex** — 8–10 sessions.

### 4.7 — FULL Engine Stage Refactor (D-3) — `mainChangeToStage`

**Current callers** (15 sites):

| File | Call | What passes |
|------|------|-------------|
| `netmsg.c:1046` | Co-op stage start | `stagenum` |
| `netmsg.c:1165` | MP match start (solo) | `g_MpSetup.stagenum` |
| `netmsg.c:3933` | Ready gate fire | `s_ReadyGate.stagenum` |
| `netmsg.c:4359` | Server match start | `g_MpSetup.stagenum` |
| `netmsg.c:4372` | Server co-op start | `g_MpSetup.stagenum` |
| `net.c:805` | Stage transition | `stagenum` param |
| `net.c:970` | Disconnect → CI | `STAGE_CITRAINING` |
| `modmgr.c:947` | Mod hotswap → title | `MODMGR_STAGE_TITLE` |
| `modmgr.c:1111` | Mod reset → title | `MODMGR_STAGE_TITLE` |
| `chraicommands.c:4824` | AI: go to title | `STAGE_TITLE` |
| `pdmain.c:765` | Implementation | — |
| `server_stubs.c:287` | Server stub | — |

**Approach**:
1. `mainChangeToStage(const char *stage_id)` — new signature
2. Internally resolves: `asset_entry_t *ae = assetCatalogResolve(stage_id);` → `g_StageNum = ae->ext.map.stagenum;` (or arena variant)
3. All callers pass catalog ID: `mainChangeToStage(g_MpSetup.stage_id)` or `mainChangeToStage("base:ci_training")`
4. Special constants like `STAGE_TITLE`, `STAGE_CITRAINING` become `STAGE_ID_TITLE = "base:title"`, `STAGE_ID_CITRAINING = "base:ci_training"` string constants
5. Internal engine stage table lookups (`bgGetStageIndex`, etc.) continue to use integer stagenum but only as engine-internal — resolved from catalog entry, never passed across API boundaries

**Dependent functions** that also take `s32 stagenum` and feed into stage loading:
- `bgReset(stagenum)`, `bgBuildTables(stagenum)`, `lvReset(stagenum)`, `langReset(stagenum)`, `envSetStageNum(stagenum)`, `envChooseAndApply(stagenum, ...)`, `musicSetStageAndStartMusic(stagenum)`, `setupLoadFiles(stagenum)`, `setupCreateProps(stagenum)`, `zbufReset(stagenum)`, `viReset(stagenum)`, `bodiesReset(stagenum)`, `stageGetIndex(stagenum)`, `soloStageGetIndex(stagenum)`, `stageGetPrimaryTrack(stagenum)`, etc.

**Decision**: These deep engine functions can continue taking integer stagenum internally — the refactor boundary is `mainChangeToStage()`. The integer stagenum becomes a local variable inside the stage-load pipeline, derived from the catalog entry at the single entry point.

**Complexity**: **Complex** — but well-contained. The refactor is at one function signature + 15 callers + 2 implementations.

---

## Phase 5: UI Layer — Preview APIs, Bridge Functions, Display Code

**Goal**: All UI code uses catalog ID strings for asset identity.

### 5.1 — `pdguiBridgeSetHead/Body` (pdgui_bridge.c:55–82)

| Item | Detail |
|------|--------|
| **Should become** | Set/get via `body_id`/`head_id` string fields |
| **Complexity** | **Trivial** |

### 5.2 — Agent create screen body/head iteration (pdgui_menu_agentcreate.cpp:179–257)

| Item | Detail |
|------|--------|
| **What** | Iterates by integer index, calls `catalogResolveHeadByMpIndex(i)` |
| **Should become** | `catalogIterateByType(ASSET_HEAD, callback)` |
| **Complexity** | **Moderate** |

### 5.3 — Room menu body display (pdgui_menu_room.cpp:1380–2517)

| Item | Detail |
|------|--------|
| **What** | ~10 sites using integer body index for display |
| **Should become** | Use `slots[j].body_id` directly, `catalogGetDisplayName(body_id)` |
| **Complexity** | **Moderate** |

### 5.4 — Lobby player view (pdgui_menu_lobby.cpp:245–246)

| Item | Detail |
|------|--------|
| **Should become** | `pv.body_id[0] != '\0'` validity check, `catalogGetDisplayName(pv.body_id)` |
| **Complexity** | **Trivial** |

### 5.5 — Modding hub preview (pdgui_menu_moddinghub.cpp:552)

| Item | Detail |
|------|--------|
| **Should become** | Pass catalog ID after 3.8 migration |
| **Complexity** | **Trivial** |

### 5.6 — SVC_LOBBY_STATE read path (netmsg.c:4405–4428)

| Item | Detail |
|------|--------|
| **Should become** | Store `stage_id` string in lobby settings (after 2.5) |
| **Complexity** | **Trivial** |

---

## Phase 6: Save/Load — Remaining Integer Fields

**Goal**: Save files use catalog ID strings exclusively. Integer fallback only for reading legacy saves.

### 6.1 — Player config save/load (savefile.c:616–710)

| Item | Detail |
|------|--------|
| **What** | **Write**: resolves `mpheadnum`/`mpbodynum` → catalog ID. **Read**: resolves catalog ID → integer. |
| **Should become** | **Write**: read from `body_id`/`head_id` string fields. **Read**: store to string fields. Legacy integer fallback stays for old saves. |
| **Complexity** | **Moderate** |

### 6.2 — Stage save/load (savefile.c:781–846)

| Item | Detail |
|------|--------|
| **Should become** | Write `g_MpSetup.stage_id` directly. Read stores to `stage_id`. |
| **Complexity** | **Trivial** |

### 6.3 — Weapon save/load (savefile.c:798–878)

| Item | Detail |
|------|--------|
| **What** | `g_MpSetup.weapons[i]` is currently `u8` (MPWEAPON_* constant) |
| **Should become** | `char weapon_ids[NUM_MPWEAPONSLOTS][CATALOG_ID_LEN]` — per D-1, full migration |
| **Complexity** | **Moderate** — struct change + save format change |

### 6.4 — Scenario save/load (scenario_save.c:267–537)

| Item | Detail |
|------|--------|
| **Should become** | `g_MatchConfig.stage_id` direct, weapons as catalog ID strings |
| **Complexity** | **Moderate** |

---

## Phase 7: Conversion Function Elimination

**Goal**: Delete all integer↔catalog-ID bridge functions once all callers are migrated.

| Function | File | Call Sites | Remove After |
|----------|------|-----------|-------------|
| `catalogResolveBodyByMpIndex` | assetcatalog_api.c:363 | 15 | Phase 2.1 + 3.1 |
| `catalogResolveHeadByMpIndex` | assetcatalog_api.c:374 | 12 | Phase 2.1 + 3.1 |
| `catalogBodynumToMpBodyIdx` | assetcatalog_api.c:389 | 18 | Phase 2.1 + 3.2 |
| `catalogHeadnumToMpHeadIdx` | assetcatalog_api.c:400 | 15 | Phase 2.1 + 3.2 |
| `catalogResolveStageByStagenum` | assetcatalog_api.c:634 | 6 | Phase 2.3 + 2.4 + 3.3 |
| `catalogResolveArenaByStagenum` | assetcatalog_api.c:654 | 2 | Phase 2.3 |
| `catalogResolveWeaponByGameId` | assetcatalog_api.c:676 | 6 | Phase 4.5 |
| `catalogGetSafeBody` | modelcatalog.c:536 | 2 | Phase 3.5 |
| `catalogGetSafeBodyPaired` | modelcatalog.c:576 | 2 | Phase 3.5 |
| `catalogGetSafeHead` | modelcatalog.c:601 | 2 | Phase 3.5 |
| `catalogGetBodyDefaultHead` | assetcatalog_api.c:413 | 1 | **Keep** — already takes catalog ID |

Also remove:
- `mpGetBodyId(u8 bodynum)` / `mpGetHeadId(u8 headnum)` — replaced by catalog queries
- `mpGetBodyName(u8 mpbodynum)` — replaced by `catalogGetDisplayName()`
- `mpGetMpheadnumByMpbodynum(s32)` — replaced by `catalogGetBodyDefaultHead(body_id)`
- `mpPlayerConfigGetHead(s32)` / `mpPlayerConfigGetBody(s32)` — replaced by catalog ID getters

Protocol version bump to **v28**.

---

## New APIs Needed

| API | Purpose | Phase |
|-----|---------|-------|
| `catalogGetDisplayName(const char *id)` | Human-readable name from any catalog ID | 5.3 |
| `catalogGetBodyRequireFeature(const char *body_id)` | Feature flag for body | 4.1 |
| `catalogGetHeadRequireFeature(const char *head_id)` | Feature flag for head | 4.1 |
| `catalogIterateBodies(callback)` | Iterate all available bodies | 4.2, 5.2 |
| `catalogIterateHeads(callback)` | Iterate all available heads | 5.2 |
| `catalogIterateWeapons(callback)` | Iterate all weapons | 4.5 |
| `catalogValidateBodyId(const char *body_id)` | Returns valid body_id or default | 3.5 |
| `catalogValidateHeadId(const char *head_id)` | Returns valid head_id or default | 3.5 |
| `catalogBodyIdToRuntimeIndex(const char *body_id)` | Catalog ID → g_HeadsAndBodies[] index (engine internal) | 4.3 |
| `catalogHeadIdToRuntimeIndex(const char *head_id)` | Same for heads | 4.3 |
| `catalogWeaponIdToRuntimeIndex(const char *weapon_id)` | Catalog ID → WEAPON_* internal index (engine internal) | 4.5 |
| `catalogModelIdToRuntimeIndex(const char *model_id)` | Catalog ID → g_ModelStates[] index (engine internal) | 4.6 |
| `catalogWeaponGetProperty(const char *weapon_id, ...)` | Query weapon properties by catalog ID | 4.5 |
| `catalogWeaponHasFlag(const char *weapon_id, u32 flag)` | Check weapon flags by catalog ID | 4.5 |
| `catalogRefreshMods(void)` | Clear mod entries + re-scan + re-register | 0.4b |
| `catalogGetGeneration(void)` | Return catalog generation counter for cache invalidation | 0.4c |
| `catalogResolveHash(const char *id)` | O(1) hash-based catalog lookup | 0.5a |

---

## Execution Order

**Critical path**: Phase 0 (completeness) → Phase 2.1 (mpchrconfig keystone) → Phase 4.5 (weapons) → Phase 4.6 (models) → Phase 4.7 (engine stage).

### Batch 0: Catalog Foundation
1. **0.2** — Weapon registration gap fix (register ALL weapon enums)
2. **0.3** — Model registration completeness audit
3. **0.4a–e** — Catalog liveness: `catalogRefreshMods()`, generation counter, validation
4. **0.5a–b** — Hash table index + cached resolution (performance foundation)

### Batch 1: Small Struct Migrations (no code change risk)
5. **2.5** — `lobby_settings_t` → catalog IDs
6. **2.6** — `net_server_info_t` → catalog ID
7. **2.12** — `s_ReadyGate.stagenum` → `stage_id`
8. **3.9** — `netcb_On*` callback signatures
9. **3.10** — `netcb_OnMatchStart/OnStageChange` signatures
10. **3.11** — `netmsgClcLobbyStartWrite` signature

### Batch 2: Struct Keystone — Body/Head
11. **2.1** — `mpchrconfig` gets `body_id`/`head_id` as sole fields (remove `mpbodynum`/`mpheadnum`)
12. **2.2** — `match_slot_t.headnum`/`bodynum` removed
13. **2.9** — Challenge simulant structs

### Batch 3: Network Wire — Body/Head
14. **1.7** — `SVC_CLIENT_SETTINGS` reads from string fields
15. **1.8** — `SVC_CLIENT_SETTINGS` stores to string fields (no integer write-back)
16. **1.9** — `SVC_STAGE_START` stores to string fields

### Batch 4: Stage Migration (D-3)
17. **2.3** — `match_config_t.stagenum` removed
18. **2.4** — `g_MpSetup.stagenum` removed
19. **2.7** — `mission_config_t.stagenum` → `stage_id`
20. **3.15** — `mainChangeToStage(const char *stage_id)` refactor
21. **4.7** — All callers pass catalog ID

### Batch 5: Weapon Migration (D-1) — LARGEST BATCH
22. **2.8** — `netmove_t.weaponnum` → `weapon_id`
23. **2.10** — All weapon struct fields → `weapon_id`
24. **3.16** — All weapon function signatures → `const char *weapon_id`
25. **4.5** — All 660 game logic references migrated
26. **1.1–1.6** — Network wire weapon messages → catalog ID strings

### Batch 6: Model Migration (D-2)
27. **2.11** — `modelnum` struct fields → `model_id`
28. **3.17** — Model function signatures
29. **4.6** — All 83 game logic references
30. **1.3** — `SVC_PROPSTATE` modelnum → catalog ID

### Batch 7: Save/Load
31. **6.1** — Player config → string fields
32. **6.2** — Stage → string fields
33. **6.3** — Weapons → catalog ID strings
34. **6.4** — Scenarios → catalog ID strings

### Batch 8: UI Layer
35. **3.7** — `mpPlayerConfigGetHead/Body` → catalog ID
36. **3.8** — `pdguiCharPreviewRequest` → catalog ID params
37. **5.1–5.5** — All UI display code
38. **3.6** — Replace `mpGetBodyName` etc.

### Batch 9: Cleanup
39. **7** — Delete ALL conversion functions
40. **3.5** — Replace `catalogGetSafe*` with string-based validation
41. Protocol version bump (v28)

---

## Metrics

| Category | Integer ID refs | Files | Estimated effort |
|----------|----------------|-------|-----------------|
| Catalog completeness (Phase 0) | — | 3 | 2–3 sessions |
| Network wire (Phase 1) | ~30 | 1 | 2–3 sessions |
| Config structs (Phase 2) | ~120 | 8 | 3–4 sessions |
| Function APIs (Phase 3) | ~90+ | 12+ | 3–4 sessions |
| Game logic — body/head/stage (Phase 4.1–4.4, 4.7) | ~50 | 8 | 3–4 sessions |
| Game logic — **weapons** (Phase 4.5) | **~660** | **35** | **15–20 sessions** |
| Game logic — **models** (Phase 4.6) | **~83** | **15** | **8–10 sessions** |
| UI layer (Phase 5) | ~25 | 5 | 2 sessions |
| Save/load (Phase 6) | ~30 | 2 | 2 sessions |
| Cleanup (Phase 7) | — | ~5 | 1 session |
| **TOTAL** | **~1,088+** | **~50+** | **~40–50 sessions** |

**Note**: The weapon migration (4.5) dominates the timeline. Consider whether it can be parallelized by file (bondgun.c independent of propobj.c independent of bot.c, etc.) to allow multiple sessions to work different files concurrently.

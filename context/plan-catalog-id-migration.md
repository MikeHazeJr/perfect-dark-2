# Catalog ID Migration Plan — Full Codebase

> **Mandate**: Zero integer-to-catalog-ID conversion anywhere, ever. Catalog ID (`char[64]` string) is the sole identity for every asset. No intermediate integer index step.
>
> **Scope**: 3,366 references to integer asset identity across `port/`, `src/game/`, `src/include/`.
>
> **Date**: 2026-04-06
>
> Back to [index](README.md) | See also [constraints.md](constraints.md)

---

## Current State

The codebase has a **dual identity** problem. Phases A–G of catalog universality gave us catalog ID strings as the *primary* identity in network wire, save/load, and config structs — but integer indices remain everywhere as "derived" fields that the engine still needs at runtime. Conversion functions bridge the gap:

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
- All `WEAPON_xxx` enum usage in game logic (~660 references)
- `modelnum` (s16) — model identity in props/objects
- All `g_MpBodies[]` / `g_MpHeads[]` array indexed lookups
- UI bridge functions: `mpPlayerConfigGetHead/Body`, `pdguiCharPreviewRequest(u8, u8)`
- Callback interface: `netcb_OnPlayerJoin(u8 headnum, u8 bodynum)` etc.

---

## Phase 1: Network Wire — Integer Asset IDs on the Wire

**Goal**: Every network message that currently sends an integer asset identity sends a catalog ID string instead.

### 1.1 — `CLC_USERCMD` weaponnum (netmsg.c:219,239)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:219` (write), `:239` (read) |
| **What** | `netbufWriteS8(buf, in->weaponnum)` — sends weapon switch as `s8` enum |
| **Should become** | `netbufWriteStr(buf, catalogResolveWeaponByGameId(in->weaponnum))` on write; read resolves back via `assetCatalogFindByCanonId()` |
| **Struct change** | `netmove_t.weaponnum` stays `s8` internally (engine still uses WEAPON_xxx enums for frame logic), but wire sends string |
| **Complexity** | **Moderate** — high-frequency message (every frame), bandwidth concern. May need short-form catalog ID or hash. |
| **Note** | This is the highest-bandwidth message. Consider: send catalog ID only on weapon *change*, not every frame. Currently sends `-1` when no change — that optimization already exists. Only the `!= -1` path needs catalog ID. |

### 1.2 — `SVC_PLAYERSTATE` weaponnum (netmsg.c:1333)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:1333` |
| **What** | `netbufWriteS8(dst, pl->gunctrl.weaponnum)` — server broadcasts player weapon state |
| **Should become** | `netbufWriteStr(dst, catalogResolveWeaponByGameId(pl->gunctrl.weaponnum))` |
| **Complexity** | **Moderate** — same bandwidth concern as 1.1 |

### 1.3 — `SVC_PROPSTATE` weaponnum + modelnum (netmsg.c:1651–1661)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:1651–1661` |
| **What** | Sends `prop->weapon->base.modelnum` (s16), `prop->weapon->weaponnum` (u8), `prop->weapon->dualweaponnum` (s8), `prop->obj->modelnum` (s16) |
| **Should become** | Catalog IDs for weapon and model. Model catalog ID requires new resolver (`catalogResolveModelByModelnum`). |
| **Complexity** | **Complex** — modelnum has no catalog resolver yet; needs new ASSET_MODEL type or model ID mapping |

### 1.4 — `SVC_PROPDAMAGE` weaponnum (netmsg.c:1902)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:1902` |
| **What** | `netbufWriteS8(dst, weaponnum)` — damage source weapon |
| **Should become** | `netbufWriteStr(dst, catalogResolveWeaponByGameId(weaponnum))` |
| **Complexity** | **Trivial** — low frequency, straightforward |

### 1.5 — `SVC_CHRDISARM` weaponnum (netmsg.c:2235)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:2235` |
| **What** | `netbufWriteU8(dst, weaponnum)` — disarmed weapon |
| **Should become** | Catalog ID string |
| **Complexity** | **Trivial** |

### 1.6 — `SVC_BOTSTATE` / `SVC_BOTFULLSTATE` weaponnum (netmsg.c:2465, 2832)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:2465, 2832` |
| **What** | `netbufWriteS8(dst, aibot->weaponnum)` — bot weapon state |
| **Should become** | Catalog ID string |
| **Complexity** | **Trivial** — same pattern as 1.4 |

### 1.7 — `SVC_CLIENT_SETTINGS` mpbodynum/mpheadnum (netmsg.c:824–827)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:824–827` |
| **What** | Resolves `bc->base.mpbodynum`/`mpheadnum` → catalog ID via `catalogResolveBodyByMpIndex`. Already sends catalog IDs on wire but **reads from integer fields**. |
| **Should become** | Read directly from `body_id`/`head_id` string fields (already exist on `net_client_t.settings`) |
| **Complexity** | **Trivial** — the strings are already there, just stop reading from the integer |

### 1.8 — `SVC_CLIENT_SETTINGS` read path: integer write-back (netmsg.c:1078–1143)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:1078–1143` |
| **What** | After receiving catalog IDs from wire, resolves back to mpbodynum/mpheadnum integers via `catalogGetSafeBodyPaired`/`catalogGetSafeHead` and writes to `g_PlayerConfigsArray[].base.mpbodynum`/`mpheadnum` and `g_BotConfigsArray[].base.mpbodynum`/`mpheadnum` |
| **Should become** | Store catalog ID strings directly; integer resolution deferred to engine handoff only |
| **Complexity** | **Complex** — this is the central fan-out point. Changing what gets stored here cascades to all consumers of `mpbodynum`/`mpheadnum`. |

### 1.9 — `SVC_STAGE_START` bot body/head resolution (netmsg.c:4155–4182)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:4155–4182` |
| **What** | Resolves catalog ID → `catalogBodynumToMpBodyIdx` → writes to `g_BotConfigsArray[bi].base.mpbodynum`. Already receives catalog IDs from wire. |
| **Should become** | Store catalog ID strings in bot config; resolve to integer only at spawn time |
| **Complexity** | **Moderate** — same cascade as 1.8 |

---

## Phase 2: Config/Data Structs — Integer Asset Fields

**Goal**: Every struct field that stores asset identity as an integer gets a catalog ID string field as primary, with the integer becoming engine-only or eliminated.

### 2.1 — `mpchrconfig.mpheadnum` / `mpbodynum` (types.h:4007–4008)

| Item | Detail |
|------|--------|
| **File** | `src/include/types.h:4007–4008` |
| **What** | `u8 mpheadnum; u8 mpbodynum;` — the single most-used integer asset identity in the codebase |
| **Should become** | `char body_id[CATALOG_ID_LEN]; char head_id[CATALOG_ID_LEN];` as primary; `mpheadnum`/`mpbodynum` become private derived cache, set only at engine handoff |
| **Consumers** | `g_PlayerConfigsArray[]`, `g_BotConfigsArray[]`, savefile.c, netmsg.c, matchsetup.c, netmenu.c, pdgui_bridge.c, botmgr.c, challenge.c, mplayer.c, menu.c |
| **Complexity** | **Complex** — ~80 direct references. This is the keystone change. |

### 2.2 — `match_slot_t.headnum` / `bodynum` (matchsetup.h:50–51)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/matchsetup.h:50–51` |
| **What** | `u8 headnum; u8 bodynum;` — DERIVED fields, already marked as such |
| **Should become** | Remove entirely once all consumers use `body_id`/`head_id` (already present at :48–49) |
| **Complexity** | **Moderate** — ~15 references, all in port/ code |

### 2.3 — `match_config_t.stagenum` (matchsetup.h:63)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/matchsetup.h:63` |
| **What** | `u8 stagenum;` — DERIVED, already marked. `stage_id` is primary at :62 |
| **Should become** | Remove once all consumers use `stage_id` |
| **Complexity** | **Moderate** — ~20 references in matchsetup.c, netmsg.c, scenario_save.c, pdmain.c |

### 2.4 — `g_MpSetup.stagenum` (types.h:4088)

| Item | Detail |
|------|--------|
| **File** | `src/include/types.h:4088` |
| **What** | `u8 stagenum;` — DERIVED from `stage_id`. Used by `mainChangeToStage()` and engine stage loading |
| **Should become** | Keep as engine-internal derived field, but eliminate all *identity* uses (comparisons, saves, display). Only used for `mainChangeToStage()` call. |
| **Complexity** | **Complex** — `mainChangeToStage(s32 stagenum)` is deeply embedded in engine. Full removal requires engine stage-load refactor. |
| **Recommendation** | Phase this as engine-handoff-only: `g_MpSetup.stagenum` set from `stage_id` at one canonical point, never read for identity purposes. |

### 2.5 — `lobby_settings_t` (netlobby.h:23–32)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/netlobby.h:23–32` |
| **What** | `u8 headnum; u8 bodynum; u8 stagenum;` — lobby display state |
| **Should become** | `char body_id[CATALOG_ID_LEN]; char head_id[CATALOG_ID_LEN]; char stage_id[CATALOG_ID_LEN];` |
| **Complexity** | **Trivial** — small struct, few consumers |

### 2.6 — `net_server_info_t.stagenum` (net.h:68)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/net.h:68` |
| **What** | `u8 stagenum;` — server browser display |
| **Should become** | `char stage_id[CATALOG_ID_LEN];` |
| **Complexity** | **Trivial** |

### 2.7 — `mission_config_t.stagenum` (net.h, types.h)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/net.h` / referenced via `g_MissionConfig.stagenum` |
| **What** | `stagenum` for co-op/solo mission config |
| **Should become** | `char stage_id[CATALOG_ID_LEN];` primary, stagenum derived for engine |
| **Complexity** | **Moderate** — feeds into `mainChangeToStage()` |

### 2.8 — `netmove_t.weaponnum` (net.h:139)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/net.h:139` |
| **What** | `s8 weaponnum;` — weapon switch in user command |
| **Should become** | Keep as engine-internal (WEAPON_xxx enum is the engine's native weapon identity). Wire serialization (Phase 1) converts to/from catalog ID. |
| **Complexity** | **N/A** — this is engine-internal, not asset identity. Weapon enums are ROM constants, not mod-extensible yet. |
| **Decision needed** | Game director: are WEAPON_xxx enums considered "integer asset identity" that must go? Or are they engine constants like GAMEMODE_COMBAT? If weapons become mod-extensible, enums must go. If not, they can stay. |

### 2.9 — `mpchrconfig.mpheadnum`/`mpbodynum` in `g_ChallengeSimulants[]` (types.h:5003–5004)

| Item | Detail |
|------|--------|
| **File** | `src/include/types.h:5003–5004` |
| **What** | Challenge config simulant body/head as `u8` |
| **Should become** | Catalog ID strings |
| **Complexity** | **Moderate** — challenge.c references ~10 sites |

### 2.10 — `weaponobj.weaponnum` / `gunctrl.weaponnum` / `aibot.weaponnum` (types.h)

| Item | Detail |
|------|--------|
| **File** | `src/include/types.h:726, 965, 1572, 2071, 2323, 3780, 5015, 5651` |
| **What** | Weapon identity as `s32`/`s8`/`u8` across multiple engine structs |
| **Should become** | See 2.8 decision. These are all WEAPON_xxx enum consumers — deep engine code (~660 references in `src/game/`). |
| **Complexity** | **Massive** if full migration. **N/A** if weapon enums stay as engine constants. |

### 2.11 — `defaultobj.modelnum` / `weaponobj.base.modelnum` (types.h:1460, preprocess/setup.h:27)

| Item | Detail |
|------|--------|
| **File** | `src/include/types.h:1460`, `port/include/preprocess/setup.h:27` |
| **What** | `s16 modelnum` — model identity for all props/objects |
| **Should become** | Catalog ID for modded models; ROM models stay as engine index |
| **Complexity** | **Massive** — 75 references, deeply embedded in prop/object system |
| **Decision needed** | Game director: is modelnum in scope for this migration? Models are not currently in the catalog system. |

---

## Phase 3: Function APIs — Integer Parameters/Returns

**Goal**: Every function that accepts or returns an integer asset identity takes a catalog ID string instead.

### 3.1 — `catalogResolveBodyByMpIndex` / `catalogResolveHeadByMpIndex` (assetcatalog_api.c:363–384)

| Item | Detail |
|------|--------|
| **What** | Takes integer index, returns catalog ID string |
| **Should become** | Eliminated — callers already have catalog ID strings |
| **Callers** | identity.c:228–229, matchsetup.c:118–119,346, net.c:337–339, netmsg.c:824–827, savefile.c:616–617, pdgui_menu_agentcreate.cpp:179, pdgui_menu_room.cpp:1432–2334 |
| **Complexity** | **Moderate** — 15 call sites, each needs to switch to using the catalog ID they already have |

### 3.2 — `catalogBodynumToMpBodyIdx` / `catalogHeadnumToMpHeadIdx` (assetcatalog_api.c:389–405)

| Item | Detail |
|------|--------|
| **What** | Takes g_HeadsAndBodies[] index, returns g_MpBodies[] index |
| **Should become** | Eliminated — callers use catalog ID → `assetCatalogResolve()` → `runtime_index` directly |
| **Callers** | matchsetup.c:417,424,471,476,547–610, netmsg.c:1118–1127,4160–4167, savefile.c:689–702, mplayer.c:789,851,860 |
| **Complexity** | **Moderate** — 18 call sites |

### 3.3 — `catalogResolveStageByStagenum` / `catalogResolveArenaByStagenum` (assetcatalog_api.c:634–674)

| Item | Detail |
|------|--------|
| **What** | Takes stagenum hex ID, returns catalog ID string |
| **Should become** | Eliminated — callers use `stage_id` directly |
| **Callers** | savefile.c:784, scenario_save.c:271, mplayer.c:308, netmanifest.c:786, pdgui_menu_room.cpp:2195,2208 |
| **Complexity** | **Trivial** — 6 call sites |

### 3.4 — `catalogResolveWeaponByGameId` (assetcatalog_api.c:676)

| Item | Detail |
|------|--------|
| **What** | Takes WEAPON_xxx enum, returns catalog ID string |
| **Should become** | Eliminated once weapon identity is catalog-native |
| **Callers** | netmanifest.c:451,616, netmsg.c:745,3724, savefile.c:798, scenario_save.c:288 |
| **Complexity** | **Trivial to moderate** — 6 call sites, but depends on weapon enum decision (2.8/2.10) |

### 3.5 — `catalogGetSafeBody` / `catalogGetSafeBodyPaired` / `catalogGetSafeHead` (modelcatalog.c)

| Item | Detail |
|------|--------|
| **What** | Validates integer body/head index, clamps to safe default |
| **Should become** | Catalog-ID-based validation: `catalogGetSafeBodyId(const char *body_id)` returns valid catalog ID or default |
| **Callers** | netmsg.c:1078–1079,1136–1138, pdmain.c:345 |
| **Complexity** | **Moderate** — needs new string-based validation API |

### 3.6 — `mpGetBodyId` / `mpGetHeadId` / `mpGetBodyName` / `mpGetMpheadnumByMpbodynum` (mplayer.h)

| Item | Detail |
|------|--------|
| **File** | `src/include/game/mplayer/mplayer.h:54–61` |
| **What** | `mpGetBodyId(u8 bodynum)` → g_HeadsAndBodies index. `mpGetBodyName(u8 mpbodynum)` → display name string. `mpGetMpheadnumByMpbodynum(s32)` → default head for body. |
| **Should become** | `catalogGetBodyDisplayName(const char *body_id)`, `catalogGetBodyDefaultHead(body_id)` (already exists!) |
| **Callers** | botmgr.c:44–45, menu.c:1872–1875, netmenu.c:152,175,381,399, server_stubs.c:379,381, pdgui_menu_agentcreate.cpp:223, pdgui_menu_lobby.cpp:246, pdgui_menu_room.cpp:1265,1429,2312,2323,2517 |
| **Complexity** | **Moderate** — ~20 call sites, `catalogGetBodyDefaultHead` already exists |

### 3.7 — `mpPlayerConfigGetHead` / `mpPlayerConfigGetBody` (pdgui_bridge.c:66–82)

| Item | Detail |
|------|--------|
| **File** | `port/fast3d/pdgui_bridge.c:66–82` |
| **What** | Returns `g_PlayerConfigsArray[].base.mpheadnum`/`mpbodynum` as `u8` |
| **Should become** | Return `const char *` catalog ID from the string field |
| **Callers** | pdgui_menu_agentselect.cpp:498 |
| **Complexity** | **Trivial** |

### 3.8 — `pdguiCharPreviewRequest(u8 headnum, u8 bodynum)` (pdgui_charpreview.h:23)

| Item | Detail |
|------|--------|
| **File** | `port/include/pdgui_charpreview.h:23`, `port/fast3d/pdgui_charpreview.c:93` |
| **What** | Takes mphead/mpbody integers for 3D character preview |
| **Should become** | `pdguiCharPreviewRequest(const char *head_id, const char *body_id)` |
| **Callers** | pdgui_menu_agentcreate.cpp:257, pdgui_menu_agentselect.cpp:498, pdgui_menu_moddinghub.cpp:552, pdgui_menu_room.cpp:2498 |
| **Complexity** | **Moderate** — needs internal resolution to model data for rendering |

### 3.9 — `netcb_OnPlayerJoin/OnPlayerSettingsChanged` (net_interface.h:15,17)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/net_interface.h:15,17` |
| **What** | `u8 headnum, u8 bodynum` parameters |
| **Should become** | `const char *head_id, const char *body_id` |
| **Complexity** | **Trivial** — callback interface, 2 call sites + 2 impl sites |

### 3.10 — `netcb_OnMatchStart(u8 stagenum, ...)` / `netcb_OnStageChange(u8 stagenum)` (net_interface.h:20,29)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/net_interface.h:20,29` |
| **What** | `u8 stagenum` parameter |
| **Should become** | `const char *stage_id` |
| **Complexity** | **Trivial** |

### 3.11 — `netmsgClcLobbyStartWrite` (netmsg.h:174)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/netmsg.h:174` |
| **What** | `u8 stagenum` parameter |
| **Should become** | `const char *stage_id` |
| **Complexity** | **Trivial** |

### 3.12 — `netmsgSvcPropDamageWrite` / `netmsgSvcChrDisarmWrite` (netmsg.h:119,131)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/netmsg.h:119,131` |
| **What** | `s32 weaponnum` / `u8 weaponnum` parameters |
| **Should become** | `const char *weapon_id` |
| **Complexity** | **Trivial** — function signature + wire write |

### 3.13 — `netServerCoopStageStart(u8 stagenum, u8 difficulty)` (net.h:251)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/net.h:251` |
| **What** | `u8 stagenum` parameter |
| **Should become** | `const char *stage_id` |
| **Complexity** | **Trivial** |

### 3.14 — `manifestBuildMission(s32 stagenum, ...)` / `manifestSPTransition(s32 stagenum)` (netmanifest.h:312,379)

| Item | Detail |
|------|--------|
| **File** | `port/include/net/netmanifest.h:312,379` |
| **What** | `s32 stagenum` parameters |
| **Should become** | `const char *stage_id` |
| **Complexity** | **Moderate** — internal manifest building needs stage_id-based lookup |

---

## Phase 4: Game Logic — Integer Comparisons/Conditionals

**Goal**: Eliminate integer asset identity from game logic comparisons.

### 4.1 — Body/head feature checks in challenge.c (challenge.c:540–633)

| Item | Detail |
|------|--------|
| **File** | `src/game/challenge.c:540–633` |
| **What** | `config->simulants[i].mpbodynum < modmgrGetTotalBodies()` bounds checks; `modmgrGetBody(mpbodynum)->requirefeature` lookups |
| **Should become** | `catalogGetBodyRequireFeature(body_id)` — catalog-based feature query |
| **Complexity** | **Moderate** — needs new catalog query API for feature flags |

### 4.2 — `mpSetCurrentPlayerBodyByMpbodynum` and body-cycling in mplayer.c (mplayer.c:789–860)

| Item | Detail |
|------|--------|
| **File** | `src/game/mplayer/mplayer.c:789–860` |
| **What** | Iterates bodies/heads by integer index, resolves to mpbody index via `catalogBodynumToMpBodyIdx` |
| **Should become** | Catalog iteration: `assetCatalogIterateByType(ASSET_BODY, ...)` |
| **Complexity** | **Moderate** |

### 4.3 — Bot spawn in botmgr.c (botmgr.c:44–50)

| Item | Detail |
|------|--------|
| **File** | `src/game/botmgr.c:44–50` |
| **What** | `headnum = mpGetHeadId(g_BotConfigsArray[aibotnum].base.mpheadnum)` — resolves mpheadnum → g_HeadsAndBodies index for model loading |
| **Should become** | `catalogResolveToRuntimeIndex(body_id)` — catalog ID → model runtime index |
| **Complexity** | **Moderate** — this is the engine handoff point for bot models |

### 4.4 — Menu character display in menu.c (menu.c:1872–1875)

| Item | Detail |
|------|--------|
| **File** | `src/game/menu.c:1872–1875` |
| **What** | `mpGetBodyId(mpbodynum)` / `mpGetHeadId(mpheadnum)` for legacy menu character display |
| **Should become** | Catalog ID → runtime index at display time |
| **Complexity** | **Trivial** — legacy menu, may be removed by D5.8 |

### 4.5 — Weapon slot/identity in bot AI (bot.c:413–428, botinv.c:382–447)

| Item | Detail |
|------|--------|
| **File** | `src/game/bot.c:413–428`, `src/game/botinv.c:382–447` |
| **What** | `g_MpWeapons[wi].weaponnum == resolvedWeaponNum` — matches weapon enum to MP weapon slot |
| **Should become** | Depends on weapon enum decision (2.8). If enums stay as engine constants, these remain. |
| **Complexity** | **Massive** if weapon enums go; **N/A** if they stay |

### 4.6 — Active menu weapon display (activemenu.c:333–530)

| Item | Detail |
|------|--------|
| **File** | `src/game/activemenu.c:333–530` |
| **What** | Weapon identity checks using `weaponnum` enum values |
| **Should become** | Same decision as 4.5 |

---

## Phase 5: UI Layer — Preview APIs, Bridge Functions, Display Code

**Goal**: All UI code uses catalog ID strings for asset identity.

### 5.1 — `pdguiBridgeSetHead/Body` (pdgui_bridge.c:55–60)

| Item | Detail |
|------|--------|
| **File** | `port/fast3d/pdgui_bridge.c:55–82` |
| **What** | Sets/gets `g_PlayerConfigsArray[].base.mpheadnum`/`mpbodynum` as `u8` |
| **Should become** | Set/get via `body_id`/`head_id` string fields |
| **Complexity** | **Trivial** |

### 5.2 — Agent create screen body/head iteration (pdgui_menu_agentcreate.cpp:179–257)

| Item | Detail |
|------|--------|
| **File** | `port/fast3d/pdgui_menu_agentcreate.cpp:179–257` |
| **What** | Iterates by integer index, calls `catalogResolveHeadByMpIndex(i)` for each head |
| **Should become** | `assetCatalogIterateByType(ASSET_HEAD, callback)` — catalog-based iteration |
| **Complexity** | **Moderate** — needs catalog iteration API |

### 5.3 — Room menu body display (pdgui_menu_room.cpp:1380–2517)

| Item | Detail |
|------|--------|
| **File** | `port/fast3d/pdgui_menu_room.cpp:1380–2517` |
| **What** | Multiple sites: `g_MatchConfig.slots[j].bodynum` for display, `catalogResolveBodyByMpIndex((s32)b)` for names, `mpGetBodyName` for labels |
| **Should become** | Use `slots[j].body_id` directly, `catalogGetDisplayName(body_id)` for names |
| **Complexity** | **Moderate** — ~10 sites in one file |

### 5.4 — Lobby player view (pdgui_menu_lobby.cpp:245–246)

| Item | Detail |
|------|--------|
| **File** | `port/fast3d/pdgui_menu_lobby.cpp:245–246` |
| **What** | `pv.bodynum < mpGetNumBodies()` bounds check, `mpGetBodyName(pv.bodynum)` |
| **Should become** | `pv.body_id[0] != '\0'` validity check, `catalogGetDisplayName(pv.body_id)` |
| **Complexity** | **Trivial** |

### 5.5 — Modding hub preview (pdgui_menu_moddinghub.cpp:552)

| Item | Detail |
|------|--------|
| **File** | `port/fast3d/pdgui_menu_moddinghub.cpp:552` |
| **What** | `pdguiCharPreviewRequest(0, bodynum)` — integer body index |
| **Should become** | Use catalog ID after 3.8 migration |
| **Complexity** | **Trivial** — follows from 3.8 |

### 5.6 — Lobby settings display in SVC_LOBBY_STATE (netmsg.c:4405–4428)

| Item | Detail |
|------|--------|
| **File** | `port/src/net/netmsg.c:4405–4428` |
| **What** | Already sends `stage_id` string on wire. Read path sets `g_Lobby.settings.stagenum`. |
| **Should become** | Read path stores `stage_id` string in lobby settings (after 2.5) |
| **Complexity** | **Trivial** — follows from 2.5 |

---

## Phase 6: Save/Load — Remaining Integer Fields

**Goal**: Save files use catalog ID strings exclusively. Integer fallback only for reading legacy saves.

### 6.1 — Player config save/load (savefile.c:616–710)

| Item | Detail |
|------|--------|
| **File** | `port/src/savefile.c:616–710` |
| **What** | **Write**: resolves `mpheadnum`/`mpbodynum` → catalog ID via `catalogResolveHeadByMpIndex`. **Read**: resolves catalog ID → mpheadnum/mpbodynum via `catalogHeadnumToMpHeadIdx`. Fallback: raw integer. |
| **Should become** | **Write**: read from `body_id`/`head_id` string fields directly. **Read**: store to string fields directly. Legacy integer fallback stays for old saves. |
| **Complexity** | **Moderate** — bidirectional, fallback logic |

### 6.2 — Stage save/load (savefile.c:781–846)

| Item | Detail |
|------|--------|
| **File** | `port/src/savefile.c:781–846` |
| **What** | **Write**: `catalogResolveStageByStagenum(g_MpSetup.stagenum)`. **Read**: resolves catalog ID → stagenum. |
| **Should become** | **Write**: use `g_MpSetup.stage_id` directly (already exists). **Read**: store to `stage_id` directly. |
| **Complexity** | **Trivial** |

### 6.3 — Weapon save/load (savefile.c:798–878)

| Item | Detail |
|------|--------|
| **File** | `port/src/savefile.c:798–878` |
| **What** | **Write**: `catalogResolveWeaponByGameId(g_MpSetup.weapons[i])`. **Read**: resolves catalog ID → weapon_id integer. |
| **Should become** | Depends on weapon enum decision (2.8). If `g_MpSetup.weapons[]` becomes string array, direct write. |
| **Complexity** | **Moderate** if weapons migrate; **Trivial** if only wire changes |

### 6.4 — Scenario save/load (scenario_save.c:267–537)

| Item | Detail |
|------|--------|
| **File** | `port/src/scenario_save.c:267–537` |
| **What** | Saves `g_MatchConfig.stagenum` as integer + catalog ID. Loads with fallback. |
| **Should become** | Save `g_MatchConfig.stage_id` directly. Keep integer fallback for legacy. |
| **Complexity** | **Trivial** |

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
| `catalogResolveWeaponByGameId` | assetcatalog_api.c:676 | 6 | Phase 6.3 + weapon decision |
| `catalogGetSafeBody` | modelcatalog.c:536 | 2 | Phase 3.5 |
| `catalogGetSafeBodyPaired` | modelcatalog.c:576 | 2 | Phase 3.5 |
| `catalogGetSafeHead` | modelcatalog.c:601 | 2 | Phase 3.5 |
| `catalogGetBodyDefaultHead` | assetcatalog_api.c:413 | 1 | **Keep** — takes catalog ID, returns catalog ID. Already clean. |

---

## Decisions Required from Game Director

### D-1: Weapon Enums (WEAPON_xxx)
**Question**: Are `WEAPON_FALCON2`, `WEAPON_SHOTGUN`, etc. (enum values 0–56) considered "integer asset identity" that must become catalog IDs?

**Impact**: ~660 references in `src/game/` (bondgun.c, botact.c, botinv.c, inv.c, propobj.c, etc.). This is the single largest block of integer identity in the codebase. The game's weapon system is deeply built around these enums for function dispatch, ammo types, fire modes, etc.

**Options**:
- **A) Full migration**: Every WEAPON_xxx becomes a catalog ID string. Massive effort, touches nearly every game file. Required if weapons become mod-extensible.
- **B) Wire-only migration**: Catalog IDs on network wire and save files, but engine-internal code keeps WEAPON_xxx enums. Conversion happens at serialization boundary only. Reasonable if weapons are a fixed set.
- **C) Defer**: Weapons are a fixed ROM set for now. Migrate when mod-extensible weapons are implemented.

**Recommendation**: **B** for now, **A** when modding needs it.

### D-2: Model Numbers (modelnum)
**Question**: Is `s16 modelnum` (prop/object model identity) in scope?

**Impact**: 75 references. Models are not currently in the catalog system. Would require new `ASSET_MODEL` type.

**Options**:
- **A) Full migration**: Add ASSET_MODEL to catalog, migrate all modelnum references
- **B) Defer**: Models become catalog-native when model modding is implemented

**Recommendation**: **B** — modelnum is engine-internal, no user-facing identity.

### D-3: `g_MpSetup.stagenum` and `mainChangeToStage(s32 stagenum)`
**Question**: The engine's stage loading is deeply built around integer stagenum (hex IDs like 0x5e). Full elimination requires refactoring `mainChangeToStage()` and all engine stage references.

**Options**:
- **A) Full refactor**: `mainChangeToStage(const char *stage_id)` — engine resolves internally
- **B) Boundary conversion**: Keep `stagenum` as engine-internal, set from `stage_id` at exactly one canonical point
- **C) Current state**: Already doing B, just needs cleanup

**Recommendation**: **B** — `stagenum` is an engine dispatch value, not user-facing identity. Set it from `stage_id` at one point, never expose it.

---

## Execution Order

**Critical path**: Phase 2.1 (mpchrconfig struct) is the keystone — almost everything depends on it.

### Batch 1: Foundation (no code change risk)
1. **2.5** — `lobby_settings_t` → catalog IDs (small, isolated)
2. **2.6** — `net_server_info_t` → catalog ID (small, isolated)
3. **3.9** — `netcb_On*` callback signatures (interface-only)
4. **3.10** — `netcb_OnMatchStart/OnStageChange` signatures
5. **3.11** — `netmsgClcLobbyStartWrite` signature

### Batch 2: Struct Keystone
6. **2.1** — `mpchrconfig` gets `body_id[64]`/`head_id[64]` as primary fields
7. **2.2** — `match_slot_t.headnum`/`bodynum` removed (use `body_id`/`head_id`)
8. **2.3** — `match_config_t.stagenum` eliminated (use `stage_id`)
9. **2.7** — `mission_config_t` gets `stage_id`

### Batch 3: Network Wire
10. **1.7** — `SVC_CLIENT_SETTINGS` reads from string fields
11. **1.8** — `SVC_CLIENT_SETTINGS` stores to string fields
12. **1.9** — `SVC_STAGE_START` stores to string fields
13. **1.1–1.6** — remaining wire messages (weaponnum → catalog ID)

### Batch 4: Save/Load
14. **6.1** — Player config save/load → string fields
15. **6.2** — Stage save/load → string fields
16. **6.4** — Scenario save/load → string fields

### Batch 5: UI Layer
17. **3.7** — `mpPlayerConfigGetHead/Body` → return catalog ID
18. **3.8** — `pdguiCharPreviewRequest` → catalog ID params
19. **5.1–5.5** — All UI display code → catalog IDs
20. **3.6** — Replace `mpGetBodyName` etc. with catalog queries

### Batch 6: Game Logic
21. **4.1** — Challenge feature checks → catalog-based
22. **4.2** — Body cycling → catalog iteration
23. **4.3** — Bot spawn → catalog ID resolution
24. **4.4** — Menu character display → catalog ID

### Batch 7: Cleanup
25. **7** — Delete all conversion functions
26. **3.5** — Replace `catalogGetSafe*` with string-based validation
27. Protocol version bump (v28)

---

## New APIs Needed

| API | Purpose | Phase |
|-----|---------|-------|
| `catalogGetDisplayName(const char *id)` | Human-readable name from any catalog ID | 5.3 |
| `catalogGetBodyRequireFeature(const char *body_id)` | Feature flag for body | 4.1 |
| `catalogGetHeadRequireFeature(const char *head_id)` | Feature flag for head | 4.1 |
| `catalogIterateBodies(callback)` | Iterate all available bodies | 4.2, 5.2 |
| `catalogIterateHeads(callback)` | Iterate all available heads | 5.2 |
| `catalogValidateBodyId(const char *body_id)` | Returns valid body_id or default | 3.5 |
| `catalogValidateHeadId(const char *head_id)` | Returns valid head_id or default | 3.5 |
| `catalogBodyIdToRuntimeIndex(const char *body_id)` | Catalog ID → g_HeadsAndBodies[] index (engine handoff) | 4.3 |
| `catalogHeadIdToRuntimeIndex(const char *head_id)` | Same for heads | 4.3 |

---

## Metrics

| Category | Integer ID refs | Files | Estimated effort |
|----------|----------------|-------|-----------------|
| Network wire (Phase 1) | ~30 | 1 (netmsg.c) | Moderate |
| Config structs (Phase 2) | ~120 | 8 | Complex |
| Function APIs (Phase 3) | ~90 | 12 | Moderate |
| Game logic (Phase 4) | ~40 (excl. weaponnum) | 6 | Moderate |
| UI layer (Phase 5) | ~25 | 5 | Moderate |
| Save/load (Phase 6) | ~30 | 2 | Moderate |
| Weapon enums (if D-1=A) | ~660 | ~30 | **Massive** |
| Model nums (if D-2=A) | ~75 | ~10 | **Massive** |
| **Total (excl. weapon/model)** | **~335** | **~25** | **Large** |

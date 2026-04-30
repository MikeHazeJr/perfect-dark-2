# Catalog Migration -- Heads Selector (Phase 1 Audit)

> **Date**: 2026-04-26
> **Scope**: Character HEAD selector pool only. Bodies and maps are sibling sessions and out of scope.
> **Architectural directive (Mike, prior session)**: "It should build the weapons list from the weapons listed in the catalog, filtering by weapons that are either not unlocked yet or are disabled (which technically should mean they aren't in the catalog at all, since it's supposed to be dynamically constructed). This same thing should apply for character heads and bodies, weapons, maps, etc."
> **Project memory anchor**: `catalog-builds-all-selector-pools` -- selector pool = catalog INTERSECT unlock-state.
> **Constraint anchor (`context/constraints.md` line 32)**: "Catalog registers ALL assets (unlock is a separate layer)."
> **Working precedent**: spawn-weapon picker at `port/fast3d/pdgui_menu_room.cpp:488-522` (build) and arena picker at `port/fast3d/pdgui_menu_room.cpp:343-436` (build + unlock filter via `challengeIsFeatureUnlocked(e->ext.arena.requirefeature)`).
> **Phase**: 1 of 3 -- AUDIT only. No code changes. Phase 2 = migration commits, Phase 3 = verification.

---

## Section A -- Layer A inventory (data tables and direct accessors)

Five distinct head pool surfaces exist as data today. They are NOT all equivalent: `g_HeadsAndBodies` is the canonical mixed table, the other four are subsets indexing INTO it.

### A.1 Tables (data)

| # | Symbol | File:line | Size | Element type | Purpose |
|---|---|---|---|---|---|
| 1 | `g_HeadsAndBodies[]` | [src/game/modeldata/robot.c:64](src/game/modeldata/robot.c) | 152 | `struct headorbody` | **Canonical mixed body+head table.** Fields: `ismale:1`, `unk00_01:1` (1=standalone head, 0=full body), `canvaryheight:1`, `type:3` (HEADBODYTYPE_*), `height:8`, `filenum`, `scale`, `animscale`, `modeldef*`, `handfilenum`. Heads occupy mixed slots (0x04-0x55 are heads, 0x56-0x96 are bodies, with exceptions). Sentinel: last slot 0x97 has `filenum == 0`. |
| 2 | `g_MpHeads[]` | [src/game/mplayer/mplayer.c:2053](src/game/mplayer/mplayer.c) | 76 (75 on JP) | `struct mphead { s16 headnum; u8 requirefeature; }` | **MP-eligible head subset.** `headnum` indexes `g_HeadsAndBodies`; `requirefeature` is the MPFEATURE_CHR_* unlock gate. |
| 3 | `g_BotHeads[]` | [src/game/mplayer/mplayer.c:2135](src/game/mplayer/mplayer.c) | 52 (51 on JP) | `u32` (HEAD_* enum -- a `g_HeadsAndBodies` index) | **Random pool for AI bot head pick.** Subset of MpHeads, all with `requirefeature == 0` (always available). |
| 4 | `g_MpBeauHeads[]` | [src/game/mplayer/mplayer.c:2044](src/game/mplayer/mplayer.c) | (small) | `struct mphead` | **Photographic / "Perfect Head" pool** (legacy N64 photo-head feature). Used only by the legacy carousel handler, **NOT** by any new ImGui picker. |
| 5 | `g_MpMaleHeads[]` / `g_MpFemaleHeads[]` | [src/game/mplayer/mplayer.c:2282, 2331](src/game/mplayer/mplayer.c) | 43 / 7 | `u32` (HEAD_*) | **Random-gender resolution pool** for bodies whose default head is the `HEAD_RANDOM_GENDER` (1000) sentinel. Read by `mpDefaultHeadForBody` only. |

`headorbody` struct definition: [src/include/types.h:3085-3096](src/include/types.h:3085).
`mphead` struct definition: [src/include/types.h:3138-3141](src/include/types.h:3138).
External declarations: [src/include/data.h:391, 495](src/include/data.h:391).
Server-side stubs (zero-init, no model data): [port/src/server_stubs.c:109, 405](port/src/server_stubs.c:109).

### A.2 Layer A direct iteration sites (other than registration)

| File:line | Function | What it iterates |
|---|---|---|
| [src/game/training.c:2357](src/game/training.c:2357) | `ciGetNumUnlockedChrBios` | full `g_HeadsAndBodies[]` -- counts CI training bio entries |
| [src/game/training.c:2371](src/game/training.c:2371) | `ciGetChrBioBodynumBySlot` | full `g_HeadsAndBodies[]` -- maps bio slot to bodynum |
| [src/game/mplayer/mplayer.c:3008](src/game/mplayer/mplayer.c:3008) | `mpDefaultHeadForBody` | `for (i = 0; i < totalheads; i++)` over `modmgrGetHead(i)` (catalog-fronted) -- find mp_idx by HEAD_* match |
| [port/src/assetcatalog_api.c:417](port/src/assetcatalog_api.c:417) | `catalogBuildRuntimeCaches` Pass 3 | `g_MpHeads[0..75]` -- pre-cache mp_idx -> catalog ID |
| [port/src/assetcatalog_base.c:565-606](port/src/assetcatalog_base.c:565) | `assetCatalogRegisterBaseGame` MP head loop | `g_MpHeads[0..75]` -- registration |
| [port/src/assetcatalog_base.c:769-775](port/src/assetcatalog_base.c:769) | base SP-fallback coverage mask | `g_MpHeads[]` headnums -- mark covered |
| [port/src/assetcatalog_base.c:777-830](port/src/assetcatalog_base.c:777) | base SP-fallback registration | full `g_HeadsAndBodies[]` -- register stragglers as `base:sp_head_<idx>` |
| [port/src/modelcatalog.c:351-370, 588](port/src/modelcatalog.c:351) | `modelCatalogInit`, `findMphead*` | `g_HeadsAndBodies[]`, `g_MpHeads[]` scans |
| [src/game/mplayer/mplayer.c:3606](src/game/mplayer/mplayer.c:3606) | `mpCreateBotFromProfile` | `g_BotHeads[rngRandom() % ARRAYCOUNT(g_BotHeads)]` -- random AI head pick |
| [src/game/mplayer/mplayer.c:3001-3003](src/game/mplayer/mplayer.c:3001) | `mpDefaultHeadForBody` | `g_MpMaleHeads[]` / `g_MpFemaleHeads[]` -- HEAD_RANDOM_GENDER body resolution |

### A.3 Public Layer A accessor functions in `src/game/mplayer/mplayer.c`

| Function | Line | Today reads | Purpose |
|---|---|---|---|
| `mpGetNumHeads()` | 2897 | `modmgrGetTotalHeads()` | Total MP head count (mod-aware). Equivalent to `mpGetNumHeads2()`. |
| `mpGetNumHeads2()` | 2892 | `modmgrGetTotalHeads()` | Same as `mpGetNumHeads()` (deduplicated). |
| `mpGetHeadId(headnum)` | 2902 | `modmgrGetHead(headnum)->headnum` | mp_idx -> g_HeadsAndBodies[] index |
| `mpGetHeadRequiredFeature(headnum)` | 2907 | `modmgrGetHead(headnum)->requirefeature` | mp_idx -> MPFEATURE_* |
| `mpGetBeauHeadId(headnum)` | 2912 | `g_MpBeauHeads[headnum].headnum` | photo-head id (legacy carousel only) |
| `mpGetNumBeauHeads()` | 2917 | `ARRAYCOUNT(g_MpBeauHeads)` | photo-head count |
| `mpDefaultHeadForBody(mpbodynum)` | 2987 | catalog (fast path) + male/female pools (random-gender bodies) | resolve a body's default head as mp_idx |
| `mpchrSetHeadByIndex(cfg, mpheadnum)` | 3026 | `catalogMpHeadId` -> writes `cfg->head_id` AND `cfg->mpheadnum` | unifying setter (writes both layers) |

`modmgrGetHead(n)` itself (in `port/src/modmgr.c:2844`) reads back from the catalog entry's `e->ext.head` -- so even Layer-A-shaped accessors are catalog-fronted today. The remaining problem is at the SELECTOR level (iteration without unlock filter), not the per-element resolution level.

---

## Section B -- Layer B inventory (catalog registrations and accessors)

### B.1 Registration

`assetCatalogRegisterBaseGame()` at [port/src/assetcatalog_base.c:437](port/src/assetcatalog_base.c:437) registers heads in two passes:

**Pass 1 -- MP heads** ([port/src/assetcatalog_base.c:565-610](port/src/assetcatalog_base.c:565)):
- For `mpidx` in `[0..75]`: register one `ASSET_HEAD` entry with `id = "base:" + s_BaseHeads[mpidx].name` (or `"base:head_<mpidx>"` fallback if name table miss).
- Sets `e->ext.head.headnum = g_MpHeads[mpidx].headnum` (the g_HeadsAndBodies[] index).
- Sets `e->ext.head.requirefeature = g_MpHeads[mpidx].requirefeature` (MPFEATURE_* unlock id).
- Sets `e->ext.head.rig_class = rigClassForHeadBodyType(g_HeadsAndBodies[headnum].type)` (compatibility key for body pairing).
- Sets `e->category = "base"`, `e->bundled = 1`, `e->enabled = 1`, `e->mp_index = mpidx`, `e->runtime_index = headnum`.
- Logs: `"assetcatalog: registered %d base heads (all g_MpHeads[])"` (76 expected on non-JP).

**Pass 2 -- SP fallback** ([port/src/assetcatalog_base.c:777-830](port/src/assetcatalog_base.c:777)):
- Walks `g_HeadsAndBodies[0..151]` skipping any index already covered by Pass 1, the null sentinel, and `BODY_TESTCHR`.
- For entries with `unk00_01 == 1` (standalone head): register `base:sp_head_<i>` as `ASSET_HEAD` with `runtime_index = i`, `category = "sp"`, no `requirefeature` gate (always 0), and `rig_class` from the same `HEADBODYTYPE_*` bucket.
- These are the SP-only heads referenced by the constraint at `constraints.md` line 32.

### B.2 Accessors (`port/src/assetcatalog_api.c` and `port/include/assetcatalog.h`)

| Accessor | Defined | Returns | Use |
|---|---|---|---|
| `catalogMpHeadId(mp_idx)` | api.c:438 | catalog ID string for `g_MpHeads[mp_idx]` | mp_idx -> "base:head_*" (single resolve) |
| `catalogIdByRuntime(ASSET_HEAD, headnum)` | api.c:444 | catalog ID for a runtime g_HeadsAndBodies[] index | headnum -> id (Pass 1 OR Pass 2 entry) |
| `catalogGetBodyDefaultHead(body_id)` | api.c:454 | catalog head ID stored on a body's `ext.body.headnum` | body -> default head |
| `catalogGetBodyDefaultMpHeadIdx(mpbodynum)` | api.c:613 | mp_idx of default head, or -1 (sentinel/random-gender) | mp_idx -> mp_idx (carousel use) |
| `catalogGetBodyValidHeadIds(body_id, &count)` | api.c:534 | array of catalog IDs whose `rig_class` matches `body.rig_class` | body -> compatible heads |
| `catalogPickRandomHeadIdForBody(body_id)` | api.c:598 | random pick from `catalogGetBodyValidHeadIds` set | body -> random compatible head |
| `assetCatalogGetByIndex(i)` | catalog.c | linear iteration over all entries -- caller filters on `e->type == ASSET_HEAD` | full pool iteration (the migration target) |
| `assetCatalogIterateByType(ASSET_HEAD, cb, userdata)` | catalog.c | callback per type-matched entry | preferred type-filtered iteration |
| `assetCatalogGetCountByType(ASSET_HEAD)` | catalog.c | count of ASSET_HEAD entries | for sizing |
| `catalogGetHeadHandle(headnum)` | catalog.h:996 | provider handle for load | (load path, not selector) |
| `catalogGetHeadFilenumByIndex(headnum)` | catalog.h:1021 | DEPRECATED filenum for legacy load APIs | (load path, not selector) |
| `catalogGetHeadIsMale(headnum)` / `catalogGetHeadType(headnum)` / `catalogGetHeadHeight(headnum)` | api.c:911-923 | property reads (still indirect through `g_HeadsAndBodies[]`) | property lookup |
| `catalogGetHeadModeldef(headnum)` | api.c:990 | lazy-load modeldef | (load path) |

### B.3 `ext.head` struct fields ([port/include/assetcatalog.h:266-273](port/include/assetcatalog.h:266))

```c
struct {
    s16 headnum;            /* global head ID in g_HeadsAndBodies[] -- runtime_index */
    u8  requirefeature;     /* unlock check (0 = always available) */
    char rig_class[32];     /* body<->head compatibility key */
} head;
```

### B.4 Counts (working baseline)

- `assetcatalog: registered 76 base heads (all g_MpHeads[])` -- NTSC; 75 on JP.
- `assetcatalog: registered N sp bodies, M sp heads from g_HeadsAndBodies[152]` -- M is the count of `unk00_01 == 1` entries in `g_HeadsAndBodies` not already in `g_MpHeads[]`. Empirically confirmed in S452 work and the included pdserver.log lines.

---

## Section C -- Selector consumers (every code path that iterates "the available heads")

Grouped by selector category. Each row: file:line, function, what it reads, what filter is applied today, ITERATION vs RESOLUTION, brief description. The migration target is converting every ITERATION row to read Layer B (`assetCatalogIterateByType(ASSET_HEAD, ...)`) with an unlock-state filter applied.

### C.1 Agent Creator (front-of-game character creator)

**File**: [port/fast3d/pdgui_menu_agentcreate.cpp](port/fast3d/pdgui_menu_agentcreate.cpp).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| 264 | `renderAgentCreate` | `s_NumHeads = mpGetNumHeads2()` (= total MP heads, 76) | **NONE** | ITERATION (sets bound) | Frame-refreshed total. |
| 179-197 | `rebuildHeadSortMap` | `for i in [0..s_NumHeads): catalogMpHeadId(i)` | **NONE** | ITERATION | Build name map for sorted carousel. |
| 241 | `autoSelectHead` | `catalogGetBodyDefaultMpHeadIdx(s_SelectedBody)` | n/a | RESOLUTION | Auto-pick head when body changes. |
| 493, 572 | (head pick commit) | `catalogMpHeadId(s_SortedHeadIndices[s_SelectedHead])` | n/a | RESOLUTION | Commit chosen head to config. |

**Filter gap**: zero unlock filter. Locked SP-only / DLC heads are presented to the user in the carousel.

### C.2 Character Select / Player Config (in-lobby self-edit)

**File**: [port/fast3d/pdgui_menu_playerconfig.cpp](port/fast3d/pdgui_menu_playerconfig.cpp).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| 600-640 | (Head carousel) | `numHeads` from carousel handler `menuhandlerMpCharacterHead` (= `mpGetNumHeads2()`); per-step `car_Set` | **partial** -- the legacy handler at `setup.c:2408` filters MENUOP_21 (next-valid step) by `challengeIsFeatureUnlocked(mpGetHeadRequiredFeature(...))` but `MENUOP_GETOPTIONCOUNT` returns `mpGetNumHeads2()` unfiltered. Net effect: total count ignores unlocks; arrow-step skips locked entries on traversal. | ITERATION (count) + step-resolution | Numeric carousel "i / N (catalog_id)" with arrow buttons. |
| 624, 656 | (display) | `catalogMpHeadId(curHead)` | n/a | RESOLUTION | Show the catalog ID hint for the current head. |

**Filter gap**: the count is wrong (shows N=76 even when fewer are unlocked). The "next valid" step skips locked entries silently, so users see the count flap and indices jump.

### C.3 Bot Setup -- Simulant Character (per-bot edit)

**File**: [port/fast3d/pdgui_menu_botsetup.cpp](port/fast3d/pdgui_menu_botsetup.cpp).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| 944-947 | `renderMpSimulantCharacter` | `nHead = car_GetCount(menuhandlerMpSimulantHead, 0)` (= `mpGetNumHeads2()`) | **NONE** at MENUOP_GETOPTIONCOUNT | ITERATION (sets bound) | Combo size. |
| 1011-1024 | (Head dropdown body) | `for (i = 0; i < nHead; i++): bot_FormatHeadName(i, ...)` then `Selectable` | **NONE** | ITERATION | Full list shown in combo. |
| 425 | `bot_FormatHeadName` | `catalogMpHeadId(mpheadnum)` | n/a | RESOLUTION | Format display string. |
| 964 | (Live preview) | `catalogMpHeadId(curHead)` / `catalogMpBodyId(curBody)` | n/a | RESOLUTION | 3D preview of selection. |

**Filter gap**: dropdown shows all 76 heads regardless of unlock.

### C.4 Room screen -- per-bot context menu (Set Body submenu)

**File**: [port/fast3d/pdgui_menu_room.cpp](port/fast3d/pdgui_menu_room.cpp).

The room screen does NOT have an explicit head dropdown -- head selection is implicit. Picking a body via the context menu or bot edit modal triggers `catalogPickRandomHeadIdForBody(bid)` to pick a fresh head from the body's rig_class-compatible pool.

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| 2280 | (Set Body bulk submenu) | `catalogPickRandomHeadIdForBody(bid)` | rig_class match (catalog-side) -- **no unlock filter** | RESOLUTION (single pick) | Bulk-apply body to all selected bots, fresh random head each. |
| 3477 | (single-bot edit modal) | same | same | RESOLUTION (single pick) | Single-bot body change, fresh random head. |
| 1372-1377 | `pdgui_bridge.c::pdguiResolveDefaultHeadForBody` | `catalogGetBodyDefaultMpHeadIdx(mpbodynum)` -> `catalogMpHeadId(mpheadnum)` | n/a | RESOLUTION | Default-head resolution helper used by room. |

**Filter gap**: `catalogPickRandomHeadIdForBody` does NOT consult `challengeIsFeatureUnlocked`. A locked head can be picked and assigned to a bot even when the user can't pick that head for themselves through other UI.

### C.5 Random AI bot head pick (creation path, no UI)

**File**: [src/game/mplayer/mplayer.c](src/game/mplayer/mplayer.c).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| 3605-3618 | `mpCreateBotFromProfile` | `g_BotHeads[rngRandom() % ARRAYCOUNT(g_BotHeads)]` (52 entries, all `requirefeature == 0`); uniqueness pass via `catalogMpHeadId` string compare | **implicit** -- the pool is hand-curated to exclude locked heads | RESOLUTION (single pick) + retry | Create-from-profile bot init. |
| 2987-3012 | `mpDefaultHeadForBody` | catalog (fast path); `g_MpMaleHeads` / `g_MpFemaleHeads` (random-gender bodies) | **implicit** -- pools hand-curated | RESOLUTION (single pick) + scan | Resolve a body's default head as mp_idx. |

**Filter gap**: implicit-only. Migrating these to the catalog with an explicit unlock filter would let `g_BotHeads` and `g_MpMaleHeads` / `g_MpFemaleHeads` be retired entirely (the pool becomes "all unlocked heads compatible with this body" or "all unlocked heads matching gender"). See Section H for the trade-off.

### C.6 Legacy menu carousel (probably dead per P10 D5.7, but verify)

**File**: [src/game/mplayer/setup.c](src/game/mplayer/setup.c).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| 2408 | `mpCharacterHeadMenuHandler` MENUOP_21 | `mpGetHeadRequiredFeature(carousel.value)` | **YES** -- `challengeIsFeatureUnlocked(...)` | step-validation | "Skip to next valid head" step gate -- this is the existing template for what NEW pickers should be doing. |
| 2378-2425 | same handler MENUOP_GETOPTIONCOUNT, MENUOP_GETSELECTEDINDEX, MENUOP_SET, MENUOP_FOCUS | `mpGetNumHeads2()` etc. | NONE on COUNT | ITERATION (count) | Count is unfiltered. |
| 3415, 3456 | (other carousel paths) | `mpGetNumHeads()` | n/a | RESOLUTION | Bounds checks. |

**Filter gap**: even the legacy code has the same bug -- count is unfiltered. The MENUOP_21 step gate masks the symptom for arrow-only nav.

### C.7 Save / load (identity profile persistence)

**File**: [port/src/identity.c](port/src/identity.c).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| 232 | identity v1 load -- legacy integer index migration | `catalogMpHeadId(headnum)` | n/a | RESOLUTION | Migrate old v1 saves to catalog ID strings. |
| (v2 path) | identity v2 load | `head_id` string read directly | n/a | (no iteration) | New format already uses catalog ID strings. |

**Filter gap**: none here -- save/load is RESOLUTION not ITERATION. Listed for completeness.

### C.8 Wire serialization (lobby state, bot configs)

**Files**: [port/src/net/netmsg.c](port/src/net/netmsg.c), [port/src/net/netmanifest.c](port/src/net/netmanifest.c), [port/src/net/matchsetup.c](port/src/net/matchsetup.c).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| netmsg.c:1285 | bot config wire write | `catalogIdByRuntime(ASSET_HEAD, bc->base.mpheadnum)` -- **WARNING**: confuses domains. `bc->base.mpheadnum` is mp_idx (g_MpHeads[] position), but `catalogIdByRuntime(ASSET_HEAD, ...)` indexes by runtime g_HeadsAndBodies[] position. This is exactly the off-by-one class B-235 fixed for the room body picker. **Likely active bug.** | n/a | RESOLUTION | Wire-encode head identity. **Flag to Mike.** |
| netmanifest.c:1055, 1412 | manifest entry build | `catalogIdByRuntime(ASSET_HEAD, headnum)` -- callers pass headnum (g_HeadsAndBodies[] index) here, so it is correct. | n/a | RESOLUTION | Manifest entry head id. |
| matchsetup.c:340-356 | `matchConfigPickHeadIdForBody` | `catalogPickRandomHeadIdForBody(body_id)` -- LEADER-side pick, broadcast to peers via `head_id` | rig_class match -- **no unlock filter** | RESOLUTION (single pick) | Match-start head resolution. |
| matchsetup.h:60 | `mpchrconfig::headnum` | DEPRECATED u8 mp index | n/a | (data) | Marked deprecated alongside `head_id`. |
| scenario_save.c:569 | scenario save -> JSON | `catalogIdByRuntime(ASSET_HEAD, head)` | n/a | RESOLUTION | Saved scenarios reference head by catalog ID string. |

**Filter gap**: leader-side `matchConfigPickHeadIdForBody` doesn't apply unlock filter. If the leader has all unlocks and a peer doesn't, the peer is told "you are wearing head X" with no consent. The constraint at `constraints.md` line 32 explicitly says SP-only characters are unlockable for MP per-player; in MP today the catalog ID is the wire identity and the receiver does not validate unlock status. Phase 2 must clarify whether unlock is per-host or per-client (Section I question I.4).

**Suspected B-235 sibling -- `netmsg.c:1285`**: see flag above. This is RESOLUTION, not iteration, but it's the same domain-confusion class. Should be re-read in Phase 2 and fixed in the same patch series if it's truly broken.

### C.9 Misc resolution sites (not selectors -- listed for boundary clarity)

These RESOLVE heads but do not iterate the pool. Migrating selectors (C.1-C.5) does not require touching them, but they are in scope for understanding the head-id surface:

- `port/fast3d/pdgui_charpreview.c:240` -- `catalogGetBodyDefaultHead(id2)` for preview fallback when no head_id is set.
- `port/fast3d/pdgui_skin_editor.cpp:967, 981, 994` -- `catalogGetBodyDefaultHead(...)` for skin editor preview head.
- `port/src/net/netmenu.c:176, 403` -- `catalogGetBodyDefaultMpHeadIdx(...)` for net menu defaults.
- `port/src/modelcatalog.c:697, 808` -- internal modelcatalog scan / cache.
- `src/game/body.c:365, 522` -- `catalogIdByRuntime(ASSET_HEAD, headnum)` for body load logic.
- `src/game/botmgr.c:52-63` -- `he->ext.head.headnum` for bot manager head resolve.
- `src/game/player.c:1957, 2008` -- `_he->ext.head.headnum` and `_he->ext.head.rig_class` in `B234_RESOLVE_CHARCONFIG` macro.
- `src/game/trainingmenus.c:1532` -- `catalogGetBodyDefaultMpHeadIdx((s32)mpbodynum)`.

---

## Section D -- Unlock-state surface

### D.1 The query function

[src/game/challenge.c:851](src/game/challenge.c:851) -- `bool challengeIsFeatureUnlocked(s32 featurenum)`.

Sibling helpers in the same file:
- `challengeIsFeatureUnlockedByPlayer(numplayers, featurenum)` (line 860)
- `challengeIsFeatureUnlockedByDefault(featurenum)` (line 869)

Header: [src/include/game/challenge.h:49](src/include/game/challenge.h:49).

### D.2 Where it's queried for HEADS today

| File:line | Context | Notes |
|---|---|---|
| [src/game/mplayer/setup.c:2408](src/game/mplayer/setup.c:2408) | Legacy `mpCharacterHeadMenuHandler` MENUOP_21 (next-valid step) | `if (!challengeIsFeatureUnlocked(mpGetHeadRequiredFeature(data->carousel.value))) return 1;` -- **the canonical existing pattern**. |

That is the ONLY current site that gates a head selector by unlock. The new ImGui pickers (`pdgui_menu_agentcreate.cpp`, `pdgui_menu_playerconfig.cpp`, `pdgui_menu_botsetup.cpp`) do not query it for heads. The room screen body picker queries it for arenas (templates D.3) but not for heads.

### D.3 Reference templates -- ARENAS already do this

[port/fast3d/pdgui_menu_room.cpp:343-393](port/fast3d/pdgui_menu_room.cpp:343) -- `catalogArenaCollect`:

```c
static void catalogArenaCollect(const asset_entry_t *e, void *userdata)
{
    ...
    if (!challengeIsFeatureUnlocked((u8)e->ext.arena.requirefeature)) {
        return;
    }
    ...
}

static void buildArenaListFromCatalog(void)
{
    ...
    assetCatalogIterateByType(ASSET_ARENA, catalogArenaCollect, NULL);
    ...
}
```

This is the SHAPE the heads selectors must adopt. The exact same `e->ext.head.requirefeature` gate field exists on the head catalog entry ([assetcatalog.h:268](port/include/assetcatalog.h:268)) and is populated at registration ([assetcatalog.c:640](port/src/assetcatalog.c:640)). No new data is needed.

[pdgui_menu_mainmenu.cpp:4362](port/fast3d/pdgui_menu_mainmenu.cpp:4362) is a second arena-filtering site using the same pattern.

### D.4 Cheat / debug bypass

`challengeIsFeatureUnlocked` is the single point. To audit the bypass paths, grep `challenge.c`:

- "Unlock all" cheat: implemented inside `challengeIsFeatureUnlocked` itself (see [src/game/challenge.c:851](src/game/challenge.c:851)+) -- if a "everything unlocked" cheat-like override is present, it lives there. The constraint doc says "OR the Unlock All cheat is active" -- expectation is that the helper already returns 1 in that case so callers do not need a separate bypass.
- Dev-build bypass: not located in this audit pass. PD_DEV_BUILD F-key toggles exist for invincibility / forge but no F-key for unlock-all in the dev surface (S430 F7 is invincibility). If Mike wants a dev-only "show all heads" toggle, it should be added to `challengeIsFeatureUnlocked` (one place, not per-selector), or to a wrapper used only by selectors.

### D.5 Save format

The unlock state lives in the challenge save data structure. The wire identity for a head is the catalog ID string -- the unlock state is per-machine, queried at the moment of selector presentation. Save migration is NOT required for the heads migration -- catalog IDs already exist in saves (identity v2, scenario_save, mpchrconfig.head_id).

### D.6 Wire / cross-client validation

Today: zero. Leader picks `head_id` and broadcasts; followers honor it without re-checking against their own challenge save. The current behavior is "leader can dress you in a head you haven't unlocked." Phase 2 design needs to decide: (a) leave as-is (host authority for cosmetics), (b) followers silently swap to a default if their challenge data says the head is locked, (c) followers always trust host (current behavior). See Section I.4.

---

## Section E -- Catalog ID conformance

### E.1 MP head IDs (Pass 1, `s_BaseHeads[]`)

The name table at [port/src/assetcatalog_base.c:325-401](port/src/assetcatalog_base.c:325) provides 75 entries (mpidx 0..74), all human-readable in the form `head_<character>` (e.g., `base:head_carrington`, `base:head_president`, `base:head_elvis_gogs`).

**Conformance**: 75 of 76 conform. Mpidx 75 (`HEAD_GREY` -- "Joanna (JP version)") has no entry in `s_BaseHeads[]`, so it falls through the `head_<mpidx>` integer fallback at line 576 -> registered as `base:head_75`. **Non-conforming -- one ID needs renaming** in a follow-up session.

### E.2 SP head IDs (Pass 2, sp fallback)

`base:sp_head_<i>` where `<i>` is the runtime g_HeadsAndBodies[] index. **Non-conforming by design** -- these are integer-indexed because the SP heads have no human names in `s_BaseHeads[]`.

**Recommended Phase 2.5 follow-up (NOT in this session per scope rules)**: extend `s_BaseHeads[]` (or add a parallel `s_BaseSpHeads[]` table) to map each SP head's runtime index to a human name (e.g., `sp_head_67` -> `sp_theking`, `sp_head_70` -> `sp_testchr` (or skip), etc.). Tracked here, deferred to a sibling session per scope.

### E.3 Mod heads

Mod heads enter the catalog via `assetcatalog_scanner.c` and use IDs from their `mod.json` manifest. Out of scope for this audit -- mod IDs follow whatever the modder chose.

---

## Section F -- Gap characterization per consumer

For each ITERATION row in Section C, what specifically must change to read Layer B with unlock filter applied?

| Consumer | Today | Target |
|---|---|---|
| **C.1 Agent Creator** | `s_NumHeads = mpGetNumHeads2()`; `for (i in [0..s_NumHeads)): catalogMpHeadId(i)` | Build a static `s_HeadList[N]` of `{ catalog_id, display_name, mpheadnum }` via `assetCatalogIterateByType(ASSET_HEAD, ...)` filtered on `e->ext.head.requirefeature` via `challengeIsFeatureUnlocked`. Refresh on `IsWindowAppearing` (or on a catalog dirty signal). Carousel index space becomes `[0..N)` over the filtered list, NOT `[0..76)`. Body-default-head auto-pick must clamp into the filtered set. |
| **C.2 Player Config head carousel** | Inherits `mpGetNumHeads2()` count from legacy carousel handler; arrow step skips locked entries silently | Same pattern as C.1. The carousel's "i / N" label now reads "i / N_unlocked". Either build a parallel filtered list and do not call the legacy handler at all (preferred), or fix the legacy `mpCharacterHeadMenuHandler` MENUOP_GETOPTIONCOUNT to apply the filter (more risk -- handler is shared between PC and legacy paths). |
| **C.3 Bot Setup -- Simulant Character dropdown** | `nHead = car_GetCount(...)`; `for (i in [0..nHead)): bot_FormatHeadName` | Same pattern as C.1. Build filtered list once, drive combo from it. |
| **C.4 Room screen body context menu / bot edit modal** | `catalogPickRandomHeadIdForBody(bid)` -- no unlock filter | Either (a) extend `catalogPickRandomHeadIdForBody` itself to filter the valid set by `challengeIsFeatureUnlocked(e->ext.head.requirefeature)` (preferred -- it's one fix in `assetcatalog_api.c` that propagates to every caller), or (b) wrap each caller. Option (a) requires resolving Section I.4 first (does the unlock filter apply leader-side, follower-side, or both?). |
| **C.5 Random AI bot head pick (`mpCreateBotFromProfile`)** | `g_BotHeads[rngRandom() % ARRAYCOUNT(g_BotHeads)]` | Replace static `g_BotHeads[]` pool with `assetCatalogIterateByType(ASSET_HEAD, ...)` filtered by `requirefeature`. Optionally also filter by some `is_simulant_eligible` predicate (today's `g_BotHeads[]` excludes the famous-named heads -- Joanna, Elvis, Carrington -- so bots get "rank-and-file" faces; that intent has to be either preserved or removed -- see Section I.5). |
| **C.6 Legacy carousel `mpCharacterHeadMenuHandler`** | MENUOP_GETOPTIONCOUNT returns `mpGetNumHeads2()` unfiltered; MENUOP_21 filters per-step | Either kill the handler entirely (P10 D5.7 has removed all legacy menu rendering -- check whether anyone still calls this) OR fix MENUOP_GETOPTIONCOUNT to return the unlocked count. Resolve via Section I.6. |
| **C.7 Identity save / load** | RESOLUTION only -- no change required | (no-op) |
| **C.8a Wire `netmsg.c:1285` bot config write** | `catalogIdByRuntime(ASSET_HEAD, bc->base.mpheadnum)` -- **likely B-235 sibling bug**, mp_idx -> runtime_index domain confusion | Phase 2 task: confirm bug, change to `catalogMpHeadId(bc->base.mpheadnum)` if confirmed. Out of strict heads-selector migration scope but correlated and worth fixing in the same series. |
| **C.8b Wire `matchConfigPickHeadIdForBody`** | `catalogPickRandomHeadIdForBody(body_id)` -- no unlock filter | Same fix as C.4 (extend `catalogPickRandomHeadIdForBody` to filter). |

---

## Section G -- Migration sequencing (per consumer, surgical)

Each step is a self-contained, build-verifiable commit. Sequenced so a failed step is bisectable.

**Step 1 -- Add filtered iteration helper.**
- Add `assetCatalogIterateUnlockedByType(asset_type_e type, void (*cb)(const asset_entry_t *e, void *user), void *user)` in `port/src/assetcatalog_api.c`. Implementation: wraps `assetCatalogIterateByType` and skips entries whose `ext.<type>.requirefeature != 0 && !challengeIsFeatureUnlocked(...)`. Header in `assetcatalog.h`.
- Plus a count helper: `assetCatalogGetUnlockedCountByType(type)`.
- Build verify both `pd` and `pd-server` (server needs the helper but never reaches it because no challenge data on server -- behavior matches arenas today which also use the same pattern).

**Step 2 -- Fix `catalogPickRandomHeadIdForBody` to apply unlock filter.**
- Locked-out heads are never picked for body assignment by the LEADER side (`matchConfigPickHeadIdForBody` matchsetup.c:340) and never by the room context menu / bot edit modal (pdgui_menu_room.cpp:2280, 3477).
- Single-line addition inside `port/src/assetcatalog_api.c::catalogPickRandomHeadIdForBody` after the `catalogGetBodyValidHeadIds` call -- skip entries whose `ext.head.requirefeature` is locked.
- (Server-side: `challengeIsFeatureUnlocked` returns true for everything when no save -- safe.)

**Step 3 -- Migrate Agent Creator (`pdgui_menu_agentcreate.cpp`).**
- Replace `s_NumHeads = mpGetNumHeads2()` with a one-time-per-frame rebuild via `assetCatalogIterateUnlockedByType(ASSET_HEAD, ...)` populating `s_SortedHeadIndices[]` directly (no integer mp_idx mapping needed -- store `ext.head.mp_index` from the entry directly).
- Adjust `autoSelectHead` to map default-head's mp_idx to the position in the filtered list.

**Step 4 -- Migrate Player Config (`pdgui_menu_playerconfig.cpp`).**
- Replace `numHeads` from `car_GetCount(menuhandlerMpCharacterHead, 0)` with the filtered count.
- Step buttons drive a list index; commit calls `mpchrSetHeadByIndex(... mp_idx ...)` after looking up mp_idx from the filtered list.

**Step 5 -- Migrate Bot Setup (`pdgui_menu_botsetup.cpp`).**
- Same as Step 4 for the dropdown body.

**Step 6 -- Migrate `mpCreateBotFromProfile` random AI head pick.**
- Replace `g_BotHeads[rngRandom() % ARRAYCOUNT(g_BotHeads)]` with `assetCatalogPickRandomUnlockedHead()` (new helper that wraps unlocked iteration + rng).
- Decide whether to keep the "exclude famous-named heads" behavior of `g_BotHeads` (Section I.5).
- After this step `g_BotHeads[]` is dead and can be removed in a follow-up commit.

**Step 7 (optional, ties to C.8a) -- Fix `netmsg.c:1285` bot config wire write.**
- Independently confirm B-235 sibling bug. If confirmed, change to `catalogMpHeadId(bc->base.mpheadnum)`. Verifies as a separate commit.

**Step 8 (optional, ties to C.6) -- Audit / remove dead legacy carousel handler.**
- Confirm `mpCharacterHeadMenuHandler` is no longer called (P10 D5.7). If dead, remove it + `mpGetHeadRequiredFeature` if no longer needed. If still called, fix MENUOP_GETOPTIONCOUNT to apply the filter.

**Verification after each step**: `source devtools/build-env.sh && ninja -C Build pd pd-server` (full link, not per-file compile -- per `feedback_build_verify`). Visual playtest after Step 3 to confirm Agent Creator carousel size matches unlocked count; Step 5 for bot dropdown size; Step 6 with the Unlock All cheat off + a fresh save confirms bot-creation only picks unlocked heads.

---

## Section H -- Cross-cuts

### H.1 Wire format

- The on-wire identity for heads is the catalog ID string (`mpchrconfig::head_id[64]`, `slot::head_id[64]`). Catalog IDs are stable across builds for `base:` entries (Mike's standing rule -- `catalog-id-everywhere`, `catalog-id-not-hash`).
- Protocol bump **NOT required** for the heads selector migration. The wire format already carries strings.
- Suspected bug at `netmsg.c:1285` is a *receiver-side* hazard (writes wrong catalog ID for a bot config), not a wire-format change.

### H.2 Save format

- Identity v2 already stores `head_id` as a catalog ID string ([port/src/identity.c](port/src/identity.c)).
- Scenario save uses catalog IDs ([port/src/scenario_save.c:569](port/src/scenario_save.c:569)).
- `mpchrconfig::head_id` and `mpbotconfig::head_id` are runtime structs (not serialized to disk beyond identity / scenario / wire).
- **No save migration required.**

### H.3 Bot configs

- `g_BotConfigsArray[].base.head_id` carries the catalog ID for each bot config slot ([src/include/types.h:5028+](src/include/types.h:5028)).
- `g_MpSimulants` (preset bot list) -- not touched by this migration.
- Both feed through `catalogMpHeadId(...)` for resolution. Migrate the SELECTORS that POPULATE these (Section C.3 Bot Setup); the storage and read-back paths are already catalog-ID-native.

### H.4 Default selections

- `catalogGetBodyDefaultMpHeadIdx(mpbodynum)` ([port/src/assetcatalog_api.c:613](port/src/assetcatalog_api.c:613)) is the canonical "what head goes with this body" lookup. Exists, used by the room screen and legacy setup.
- `catalogGetBodyDefaultHead(body_id)` ([port/src/assetcatalog_api.c:454](port/src/assetcatalog_api.c:454)) -- string variant, used by char preview and skin editor.
- A "no head selected" state resolves to the body's default head via these helpers. Migrating selectors does NOT change the default-resolution path.

### H.5 Bodies with integrated heads (`unk00_01 == 1`)

- [src/game/mplayer/setup.c:2457-2470](src/game/mplayer/setup.c:2457) -- `mpBodyHasIntegratedHead(mpbodynum)` returns true for bodies like Dr. Carroll, Eye Spy, Skedar that don't support a swappable head.
- The legacy carousel locks the head selector to count=1 in this case.
- New ImGui pickers (Agent Creator, Player Config, Bot Setup) **do not check this**. After the migration, the head selector should be hidden / disabled when `mpBodyHasIntegratedHead(currentBody)` is true. Track this as a Phase 2 must-include since locked SP heads + integrated-head bodies are both filter conditions on the same selector.

### H.6 The B-235 anti-pattern

- B-235 (S452) -- the room body picker passed body mp_index to `catalogMpHeadId()` causing Maian bots to wear President's head.
- The general pattern: `catalogMpHeadId(mp_idx)` and `catalogIdByRuntime(ASSET_HEAD, runtime_index)` index into DIFFERENT spaces. Mixing them is the same bug class as the weapon anti-pattern documented at [src/game/setup.c:2739](src/game/setup.c:2739).
- After the migration, all selectors will iterate via `assetCatalogIterateByType(ASSET_HEAD, ...)` and read the catalog ID directly off the entry -- the off-by-one risk goes away by construction (no integer index arithmetic in selector code).
- Sibling anti-pattern comment for HEADS does not exist in source. Adding one in the migration commit message + a pin in `port/src/assetcatalog_api.c::catalogMpHeadId` doc is recommended.

### H.7 Server build

- `port/src/server_stubs.c:109, 405` provides empty `g_MpHeads[76]` and `g_HeadsAndBodies[152]` for the dedicated server build.
- `assetCatalogRegisterBaseGame` is NOT called on the dedicated server (no ROM data). Catalog head entries do not exist server-side.
- Selector code in `port/fast3d/pdgui_*` is `pd`-target only (not built into `pd-server`).
- `challengeIsFeatureUnlocked` is in `src/game/challenge.c` -- compiled into both targets, but the server has no save data to consult.
- The migration helpers must build clean on both targets. The arena precedent (`pdgui_menu_room.cpp` is `pd`-only; `assetcatalog_*` is shared) demonstrates the layering works.

### H.8 Mod heads

- Mod heads enter via `assetcatalog_scanner.c::registerComponent` and use IDs from manifest. They get a `requirefeature` of 0 by default (always unlocked) -- a mod-authored head never needs to be unlocked.
- Migration is forward-compatible with mods: mod heads will appear in selectors automatically because `assetCatalogIterateByType(ASSET_HEAD, ...)` returns them without any special-casing.

---

## Section I -- Decisions for Mike (do NOT proceed to Phase 2 until resolved)

### I.1 Sentinel question -- empty / "head_75" fallback

`s_BaseHeads[]` has 75 named entries; mpidx 75 (HEAD_GREY, "Joanna (JP version)") falls through to `base:head_75`. Acceptable for Phase 2? Or rename in this session vs deferred follow-up?

**Recommendation**: defer to a follow-up session per scope rule "Don't proactively rename catalog IDs". Track here.

### I.2 SP head IDs not human-readable

`base:sp_head_<idx>` IDs (Pass 2 fallback) are integer-indexed. After the migration, these will appear in selector lists (because they're ASSET_HEAD with `requirefeature == 0`). Three options:

**(a)** Leave as-is. Users see "sp_head_67" as a head name -- ugly.
**(b)** Defer the rename to a follow-up session, and during this session add a display_name table for SP heads similar to what bodies got (`s_BaseBodies[].desc`). One commit, ~30 lines.
**(c)** Filter SP heads out of MP selectors entirely (return to N64-era behavior).

**Recommendation**: (b). The constraint at line 32 explicitly says SP heads should be selectable in MP (with unlock gate). Adding human names is a small Phase 2 sub-step.

### I.3 Random gender pools (`g_MpMaleHeads` / `g_MpFemaleHeads`)

After the migration, can these be retired in favor of "iterate ASSET_HEAD where unlocked && rig_class matches body && (no gender mismatch via HEADBODYTYPE_*)"?

The catalog's `rig_class` system already handles this for the body-pairing case. The "random gender" sentinel (HEAD_RANDOM_GENDER == 1000) is used by some bodies whose default head IS the random pool result. After the migration, `mpDefaultHeadForBody` could call `catalogPickRandomHeadIdForBody` instead of `g_MpMaleHeads`/`g_MpFemaleHeads` lookup.

**Recommendation**: yes, retire in Phase 2 Step 6 alongside `g_BotHeads`. Both are vestigial under the catalog model. Worth flagging as a one-line comment for Mike before doing it.

### I.4 Wire / cross-client unlock validation policy

Today: leader picks `head_id`, follower honors it. If follower has not unlocked that head, it still wears the assigned head. Three options:

**(a) Status quo** -- host authority for cosmetics.
**(b) Follower silently swaps** to default body head if locked.
**(c) Reject at lobby join** -- "you have not unlocked this head".

**Recommendation**: (a) status quo for Phase 2. The unlock system is per-machine save data; the wire identity is the cosmetic catalog ID. Forcing follower-side validation creates a UX where two players see different heads on the same chr -- worse than the current behavior. Document the policy in `constraints.md` after Phase 2 lands.

### I.5 Bot AI head pool restriction (`g_BotHeads`)

`g_BotHeads[52]` deliberately excludes famous-named characters (Joanna variants, Elvis, Carrington, Cassandra, Trent, Mr. Blonde, etc.) -- bots get "rank-and-file" rare-staff faces. Should the migration:

**(a)** Preserve this exclusion via a new `is_simulant_eligible` flag on the catalog entry.
**(b)** Drop the restriction -- bots can wear any unlocked head. Player has Joanna; bot can also have Joanna.

**Recommendation**: (b) is the simpler path and aligns with "catalog as single source of truth -- selector pool = catalog INTERSECT unlock-state". The "famous names" exclusion is a pre-modder design choice. If users complain, add the predicate later. **Open question for Mike.**

### I.6 Legacy carousel handler `mpCharacterHeadMenuHandler` -- still called?

P10 D5.7 (S184) removed all legacy native rendering. The handler is C-side though, and may still be invoked by the carousel's `MENUOP_GETOPTIONCOUNT` if any ImGui carousel goes through `car_GetCount(menuhandlerMpCharacterHead, 0)` (player config does -- pdgui_menu_playerconfig.cpp). So the handler is not dead.

**Recommendation**: Phase 2 Step 4 reads the filtered count directly via the new helper, bypassing the handler for size queries. The handler stays for backward compatibility but is no longer authoritative. Do NOT delete it.

### I.7 `catalogPickRandomHeadIdForBody` -- where to apply unlock filter

Two equivalent options:

**(a)** Inside `catalogPickRandomHeadIdForBody` -- one fix, propagates to every caller.
**(b)** At each caller -- explicit but repetitive.

**Recommendation**: (a). The function is named "pick a head for this body"; "pick a LOCKED head" is never the right answer for a player-facing UI. Match the arena precedent which folds the filter into a single helper.

### I.8 Phase 2 commit scope

Eight steps in Section G; recommend bundling into 4-6 commits:
- Commit 1: Step 1 (helper + count helper)
- Commit 2: Step 2 (catalogPickRandomHeadIdForBody filter)
- Commit 3: Steps 3 + H.5 (Agent Creator + integrated-head guard)
- Commit 4: Step 4 (Player Config)
- Commit 5: Step 5 (Bot Setup)
- Commit 6: Step 6 (random AI bot head + retire `g_BotHeads`)
- Commit 7 (optional): Step 7 (netmsg.c:1285 fix)
- Commit 8 (optional): Step 8 (legacy handler audit)

Each builds clean and is bisectable. **Confirm bundling shape with Mike?**

---

---

## Section J -- Decisions confirmed (2026-04-26, delegated by Mike via parent session)

All eight Section I decisions approved + B-235 sibling fold-in.

| # | Decision |
|---|---|
| **I.1** | Catalog ID renames for non-conforming entries (`base:head_75`, `base:sp_head_<idx>` set) -- DEFER to a follow-up session. Track in audit, do NOT rename in this session. |
| **I.2** | Keep mixed `g_HeadsAndBodies[]` array intact. Bodies migration session needs it. Migrate the head-related callers; leave the array alone. |
| **I.3** | New `assetCatalogIterateUnlockedByType` helper lands in `assetcatalog_api.c` alongside the existing iterators. Document in the function block comment style the existing iterators use. |
| **I.4** | Wire / cross-client unlock validation policy: STATUS QUO. Host authority for cosmetics. No follower-side unlock validation. Cosmetics are non-cheating-sensitive. |
| **I.5** | Drop the `g_BotHeads` "famous-named character" exclusion. Bots can wear any unlocked head, including Joanna's face if unlocked. |
| **I.6** | Random head selection uses the unlock-filtered pool. Preserve a graceful "no unlocked head" sentinel fallback so a body that cannot resolve any unlocked head still has a defined state. |
| **I.7** | Apply the unlock filter inside `catalogPickRandomHeadIdForBody` (one-shot fix, propagates to all callers). Single point of truth. |
| **I.8** | Bundle the 8 migration steps into 4-6 commits. Sequential, bisectable, build-verified per commit. |
| **Bonus** | Fold the suspected B-235 sibling fix at `port/src/net/netmsg.c:1285` into the migration commit series. Document in `context/bugs.md` as a B-235 sibling fix. |

## Phase 2 commit plan (6 commits)

1. **Step 1** -- helper + count helper in `assetcatalog_api.c` + Section J record (this commit).
2. **Step 2** -- filter `catalogPickRandomHeadIdForBody` by unlock.
3. **Step 3** -- migrate Agent Creator + integrated-head guard (H.5).
4. **Steps 4 + 5** -- migrate Player Config + Bot Setup pickers (same shape).
5. **Step 6** -- migrate `mpCreateBotFromProfile` random AI head pick + retire `g_BotHeads`. Honor I.6 graceful fallback.
6. **Step 7** -- fix `netmsg.c:1285` B-235 sibling + `bugs.md` entry.

Step 8 (legacy-handler audit) deferred -- the new pickers no longer route through the legacy handler for size queries, so the handler becomes vestigial without needing a removal commit.

---

## Stop conditions encountered (none)

- Unlock-state lookup is NOT more invasive than expected -- `challengeIsFeatureUnlocked` is the single point and the catalog already carries `ext.head.requirefeature`.
- Wire format does NOT need a protocol bump -- catalog ID strings already in place.
- Catalog ID renames are NOT needed for the migration to function (the human-readability gap is cosmetic, not functional).
- Layer A head consumers can be cleanly migrated -- no iteration order dependency.
- Head/body separation in `g_HeadsAndBodies` is NOT a blocker -- catalog Pass 2 already handles it via `unk00_01` discrimination.

## Surface area summary

- **Layer A tables**: 5 (g_HeadsAndBodies + 4 subset pools).
- **Layer A direct iterations to remove or audit**: 9 sites (Section A.2).
- **Layer A accessor functions**: 8 (Section A.3) -- most can stay; only the iteration ones get retired.
- **Layer B accessors already in place**: 13 (Section B.2). No new public API needed beyond Step 1's helper.
- **Selector consumers requiring migration**: 5 categories (C.1 Agent Creator / C.2 Player Config / C.3 Bot Setup / C.4 Room body picker / C.5 Random AI bot pick).
- **Cross-cut sites (RESOLUTION only, no iteration)**: 8 sites (C.9) -- listed for completeness, not migrated.
- **Decisions for Mike**: 8 (Section I).
- **Suspected B-235 sibling bug**: 1 (`netmsg.c:1285`) -- verify in Phase 2.

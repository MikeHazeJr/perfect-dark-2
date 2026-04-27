# Catalog Migration -- Bodies Selector (Phase 1 Audit)

> **Date**: 2026-04-26
> **Scope**: Character BODY selector pool only. Heads migrated earlier today (dev `132a883c`), maps/arenas is the next sibling session.
> **Architectural directive (Mike, prior session)**: "It should build the weapons list from the weapons listed in the catalog, filtering by weapons that are either not unlocked yet or are disabled (which technically should mean they aren't in the catalog at all, since it's supposed to be dynamically constructed). This same thing should apply for character heads and bodies, weapons, maps, etc."
> **Project memory anchor**: `catalog-builds-all-selector-pools` -- selector pool = catalog INTERSECT unlock-state.
> **Constraint anchor (`context/constraints.md` line 32)**: "Catalog registers ALL assets (unlock is a separate layer)."
> **Working precedent**: heads audit + 6-commit migration at `context/audits/catalog-migration-heads-2026-04-26.md` (dev `132a883c`).
> **Phase**: 1 of 4 -- AUDIT only. Phase 2 = migration, Phase 3 = verify, Phase 4 = auto-merge.

---

## Section A -- Layer A inventory (data tables and direct accessors)

### A.1 Tables (data)

| # | Symbol | File:line | Size | Element type | Purpose |
|---|---|---|---|---|---|
| 1 | `g_HeadsAndBodies[]` | [src/game/modeldata/robot.c:64](src/game/modeldata/robot.c) | 152 | `struct headorbody` | Canonical mixed body+head table (heads occupy slots 0x04-0x55, bodies 0x56-0x96 with exceptions). Sentinel at 0x97 (`filenum == 0`). Heads session decision I.2: leave untouched -- migrate the body callers, not the table. |
| 2 | `g_MpBodies[]` | [src/game/mplayer/mplayer.c:2166](src/game/mplayer/mplayer.c) | 63 | `struct mpbody { s16 bodynum; s16 name; s16 headnum; u8 requirefeature }` | **MP-eligible body subset** -- the migration target. `bodynum` indexes `g_HeadsAndBodies`; `requirefeature` is the MPFEATURE_CHR_* unlock gate. |
| 3 | (no `g_BotBodies`) | n/a | n/a | n/a | Unlike heads, there is no static "AI bot body pool". Bots pull from `g_BotProfiles[].body` (a fixed 18-entry profile table) at create time, then the room screen / random-body path picks from `g_MpBodies[]`. |

`mpbody` struct definition: [src/include/types.h](src/include/types.h) (alongside `mphead`).
External declaration: [src/include/data.h:497](src/include/data.h:497) -- `extern struct mpbody g_MpBodies[63];`.
Server-side stub (zero-init, no model data): [port/src/server_stubs.c:108](port/src/server_stubs.c:108) -- `struct mpbody g_MpBodies[63];`.

### A.2 Layer A direct iteration sites

| File:line | Function | What it iterates | Filter? |
|---|---|---|---|
| [src/game/training.c](src/game/training.c) | `ciGetNumUnlockedChrBios` / `ciGetChrBioBodynumBySlot` | full `g_HeadsAndBodies[]` for CI training bio entries | n/a (campaign content, not a selector pool) |
| [port/src/assetcatalog_api.c:404-414](port/src/assetcatalog_api.c:404) | `catalogBuildRuntimeCaches` Pass 2 | `g_MpBodies[0..62]` -- pre-cache mp_idx -> catalog ID | one-shot init, not a selector |
| [port/src/assetcatalog_base.c:486-549](port/src/assetcatalog_base.c:486) | `assetCatalogRegisterBaseGame` MP body loop | `s_BaseBodies[0..62]` -> register `ASSET_BODY` | registration |
| [port/src/assetcatalog_base.c:762-768](port/src/assetcatalog_base.c:762) | base SP-fallback coverage mask | `g_MpBodies[].bodynum` -> mark covered | registration prep |
| [port/src/assetcatalog_base.c:777-830](port/src/assetcatalog_base.c:777) | base SP-fallback registration | full `g_HeadsAndBodies[0..151]` -- registers stragglers as `base:sp_body_<idx>` (or `sp_head_<idx>`) | registration |
| [port/src/modelcatalog.c](port/src/modelcatalog.c) | `modelCatalogInit`, body scans | `g_HeadsAndBodies[]`, `g_MpBodies[]` for the validation/thumbnail subsystem | one-shot init |
| [port/src/net/matchsetup.c:359-406](port/src/net/matchsetup.c:359) | `pickRandomBodyHead` | `mpGetNumBodies()` random pick + `catalogMpBodyId(idx)` | **NONE** -- the random body for bots is unfiltered |

### A.3 Public Layer A accessor functions

| Function | File:line | Today reads | Purpose |
|---|---|---|---|
| `mpGetNumBodies()` | [src/game/mplayer/mplayer.c:2873](src/game/mplayer/mplayer.c:2873) | `modmgrGetTotalBodies()` | Total MP body count (mod-aware). |
| `mpGetBodyId(bodynum)` | [src/game/mplayer/mplayer.c:2879](src/game/mplayer/mplayer.c:2879) | `modmgrGetBody(bodynum)->bodynum` | mp_idx -> g_HeadsAndBodies[] index. |
| `mpGetBodyName(mpbodynum)` | [src/game/mplayer/mplayer.c:2904](src/game/mplayer/mplayer.c:2904) | catalog override (B-226) -> langbank fallback | display name. |
| `mpGetBodyRequiredFeature(mpbodynum)` | [src/game/mplayer/mplayer.c:2924](src/game/mplayer/mplayer.c:2924) | `modmgrGetBody(mpbodynum)->requirefeature` | unlock gate id. |
| `mpGetMpbodynumByBodynum(bodynum)` | [src/game/mplayer/mplayer.c:2890](src/game/mplayer/mplayer.c:2890) | scan `g_MpBodies[]` for matching `bodynum` | reverse-lookup. |
| `mpchrSetBodyByIndex(cfg, mpbodynum)` | [src/game/mplayer/mplayer.c:2965](src/game/mplayer/mplayer.c:2965) | `catalogMpBodyId` -> writes `cfg->body_id` AND `cfg->mpbodynum` | unifying setter (writes both layers). |
| `mpchrSetBodyById(cfg, body_id)` | [src/game/mplayer/mplayer.c:2989](src/game/mplayer/mplayer.c:2989) | reverse via `assetCatalogResolve` | catalog-ID-first setter. |

`modmgrGetBody(n)` itself is catalog-fronted (`port/src/modmgr.c`). The remaining problem is at the SELECTOR level (iteration without unlock filter), not the per-element resolution level -- exactly mirroring the heads finding.

---

## Section B -- Layer B inventory (catalog registrations and accessors)

### B.1 Registration

`assetCatalogRegisterBaseGame()` registers bodies in two passes:

**Pass 1 -- MP bodies** ([port/src/assetcatalog_base.c:486-549](port/src/assetcatalog_base.c:486)):
- For each entry in `s_BaseBodies[]` (63 entries, mpidx 0..62): register one `ASSET_BODY` with `id = "base:" + s_BaseBodies[mpidx].name`.
- Sets `e->ext.body.bodynum = g_MpBodies[idx].bodynum` (the g_HeadsAndBodies[] index).
- Sets `e->ext.body.headnum = g_MpBodies[idx].headnum` (default head id, may be `HEAD_RANDOM_GENDER` 1000).
- Sets `e->ext.body.requirefeature = g_MpBodies[idx].requirefeature` (MPFEATURE_* unlock id).
- Sets `e->ext.body.rig_class = rigClassForHeadBodyType(g_HeadsAndBodies[bodynum].type)` (compatibility key).
- Sets `e->ext.body.display_name = s_BaseBodies[idx].desc` (B-226: catalog-authoritative for the 4 Bond bodies + Skedar/DrCaroll).
- Sets `e->category = "base"`, `e->bundled = 1`, `e->enabled = 1`, `e->mp_index = idx`, `e->runtime_index = bodynum`.
- Logs: `"assetcatalog: registered 63 base bodies"`.

**Pass 2 -- SP fallback** ([port/src/assetcatalog_base.c:777-830](port/src/assetcatalog_base.c:777)):
- Walks `g_HeadsAndBodies[0..151]` skipping covered indices, the null sentinel, and `BODY_TESTCHR`.
- For entries with `unk00_01 == 0` (full body): register `base:sp_body_<i>` as `ASSET_BODY` with `runtime_index = i`, `category = "sp"`, `requirefeature = 0` (always available), and `rig_class` from the same `HEADBODYTYPE_*` bucket.
- Default head set via the third arg to `assetCatalogRegisterBody(idbuf, (s16)i, 0, -1, 0)` -- `headnum = -1` means "no declared default head" (caller falls back to gender-pool resolution).

### B.2 Accessors

| Accessor | Defined | Returns | Use |
|---|---|---|---|
| `catalogMpBodyId(mp_idx)` | [api.c:433](port/src/assetcatalog_api.c:433) | catalog ID for `g_MpBodies[mp_idx]` | mp_idx -> "base:body_*" |
| `catalogIdByRuntime(ASSET_BODY, bodynum)` | [api.c:445](port/src/assetcatalog_api.c:445) | catalog ID for runtime g_HeadsAndBodies index | runtime_index -> id |
| `catalogGetBodyDefaultHead(body_id)` | [api.c:516](port/src/assetcatalog_api.c:516) | head ID stored on body's `ext.body.headnum` | body -> default head |
| `catalogGetBodyDisplayName(mpbodynum)` | [api.c:528](port/src/assetcatalog_api.c:528) | catalog-authoritative display name (B-226) | display polish |
| `catalogGetBodyValidHeadIds(body_id, &count)` | [api.c:596](port/src/assetcatalog_api.c:596) | array of catalog IDs whose `rig_class` matches | body -> compatible heads |
| `catalogPickRandomHeadIdForBody(body_id)` | [api.c:660](port/src/assetcatalog_api.c:660) | random pick from valid set, **unlock-filtered** (heads Step 2) | body -> random head |
| `catalogGetBodyHandle(bodynum)` | [api.c:880](port/src/assetcatalog_api.c:880) | provider handle for load | load path |
| `catalogGetBodyFilenumByIndex(bodynum)` | [api.c:762](port/src/assetcatalog_api.c:762) | DEPRECATED filenum for legacy load APIs | load path |
| `catalogGetBodyIsMale(bodynum)` / `catalogGetBodyType(bodynum)` / `catalogGetBodyHeight(bodynum)` / `catalogGetBodyAnimScale(bodynum)` / `catalogGetBodyCanVaryHeight(bodynum)` / `catalogGetBodyIsComplete(bodynum)` / `catalogGetBodyHandFilenum(bodynum)` | [api.c:963-999](port/src/assetcatalog_api.c:963) | property reads | property lookup |
| `catalogGetBodyModeldef(bodynum)` | [api.c:1076](port/src/assetcatalog_api.c:1076) | lazy-load modeldef | load path |
| `assetCatalogIterateByType(ASSET_BODY, ...)` | catalog.c | iterate every body | full pool iteration |
| `assetCatalogIterateUnlockedByType(ASSET_BODY, ...)` | [api.c:490](port/src/assetcatalog_api.c:490) | **unlock-filtered iteration -- shipped in heads Step 1** | the migration target API |
| `assetCatalogGetUnlockedCountByType(ASSET_BODY)` | [api.c:506](port/src/assetcatalog_api.c:506) | count of unlocked bodies | size for caches |

### B.3 `ext.body` struct fields ([port/include/assetcatalog.h:239-265](port/include/assetcatalog.h:239))

```c
struct {
    s16  bodynum;             /* g_HeadsAndBodies[] index = runtime_index */
    s16  name_langid;         /* legacy langbank string id */
    s16  headnum;             /* default head, or HEAD_RANDOM_GENDER (1000) */
    u8   requirefeature;      /* unlock gate (0 = always available) */
    char display_name[64];    /* B-226 catalog-authoritative override */
    char rig_class[32];       /* Issue 10 head pairing key */
} body;
```

### B.4 Counts (working baseline)

- `assetcatalog: registered 63 base bodies` -- NTSC.
- `assetcatalog: registered N sp bodies, M sp heads from g_HeadsAndBodies[152]` -- N is the count of `unk00_01 == 0` body entries in `g_HeadsAndBodies` not in `g_MpBodies[]`.

---

## Section C -- Selector consumers (every code path that iterates "the available bodies")

Same shape as the heads audit. Each row: file:line, function, what it reads, what filter is applied today, ITERATION vs RESOLUTION, brief description.

### C.1 Agent Creator (front-of-game character creator)

**File**: [port/fast3d/pdgui_menu_agentcreate.cpp](port/fast3d/pdgui_menu_agentcreate.cpp).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| 289 | `renderAgentCreate` | `s_NumBodies = mpGetNumBodies()` | **NONE** | ITERATION (sets bound) | Frame-refreshed total. |
| 419-446 | (body carousel arrows) | indices `[0, s_NumBodies)` | **NONE** | ITERATION | Cycle bodies via mp_idx arithmetic. |
| 448 | (display) | `(%d/%d)` shows total over unfiltered count | **NONE** | display | Counter shows N=63. |
| 244-251 | `getBodyDisplayName(bodyIdx)` | `mpGetBodyName((u8)bodyIdx)` | n/a | RESOLUTION | Per-frame name fetch. |
| 547, 628 | (commit / preview) | `catalogMpBodyId(s_SelectedBody)` | n/a | RESOLUTION | Resolves chosen mp_idx to catalog ID for commit/preview. |
| 231-239 | `s_bodyHasIntegratedHead(mpbodynum)` | `catalogMpBodyId` -> `catalogGetBodyIsComplete(runtime_index)` | n/a | RESOLUTION | Integrated-head guard (Dr. Carroll, Skedar, Eye Spy). |

**Filter gap**: zero unlock filter on the body list. Locked SP-only / unlock-gated bodies appear in the carousel.

### C.2 Player Config -- in-lobby self-edit body list

**File**: [port/fast3d/pdgui_menu_playerconfig.cpp](port/fast3d/pdgui_menu_playerconfig.cpp).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| 613-615 | `renderMpCharacter` | `numBodies = list_GetOptionCount(mpCharacterBodyListHandler, 0)` (returns `mpGetNumBodies()`); `curBody = list_GetSelectedIndex(...)` | **partial** -- legacy handler at `setup.c:2926` filters MENUOP_21 (next-valid step) by `challengeIsFeatureUnlocked`, but MENUOP_GETOPTIONCOUNT returns `mpGetNumBodies()` unfiltered | ITERATION (count) + step-resolution | Body scrollable list. |
| 665-689 | (list rows) | `list_GetOptionText(mpCharacterBodyListHandler, 0, i)` -> `mpGetBodyName` | **NONE** at GETOPTIONCOUNT | ITERATION | Selectable rows. |
| 753 | (live preview) | `catalogMpBodyId(curBody)` | n/a | RESOLUTION | 3D preview. |

**Filter gap**: list shows N=63 even when fewer are unlocked. The MENUOP_21 step gate skips locked entries on `next-valid` traversal but the list itself iterates `[0..63)`. Same exact bug as the heads carousel pre-migration.

### C.3 Bot Setup -- Simulant Character (per-bot edit)

**File**: [port/fast3d/pdgui_menu_botsetup.cpp](port/fast3d/pdgui_menu_botsetup.cpp).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| 1015-1016 | `renderMpSimulantCharacter` | `nBody = car_GetCount(menuhandlerMpSimulantBody, 0)` (= `mpGetNumBodies()`) | **NONE** at MENUOP_GETOPTIONCOUNT | ITERATION (sets bound) | Combo size. |
| 1118-1142 | (Body dropdown body) | `for (i = 0; i < nBody; i++): mpGetBodyName((u8)i)` then `Selectable` -> `car_Set(menuhandlerMpSimulantBody, 0, i)` | **NONE** | ITERATION | Full list shown in combo. |
| 1045 | (Live preview) | `catalogMpBodyId(curBody)` | n/a | RESOLUTION | 3D preview. |

**Filter gap**: dropdown shows all 63 bodies regardless of unlock. Note the head selector here was migrated in heads Step 5; bodies kept legacy.

### C.4 Room screen -- bulk Set Body submenu

**File**: [port/fast3d/pdgui_menu_room.cpp](port/fast3d/pdgui_menu_room.cpp).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| 2225-2289 | (Set Character submenu, bulk apply) | `numBodies = mpGetNumBodies()` then sort `[0..numBodies)` by display name | **NONE** | ITERATION | Bulk-apply body to all selected bots. Body-id assigned via `catalogMpBodyId(b)`; head via `catalogPickRandomHeadIdForBody` (already unlock-filtered). |

### C.5 Room screen -- single-bot edit modal

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| 3436-3487 | (Bot edit modal Character combo) | `numBodies = mpGetNumBodies()` then iterate `[0..numBodies)` | **NONE** | ITERATION | Single-bot body change. Body assigned via `catalogMpBodyId(b)`; head via `catalogPickRandomHeadIdForBody`. |

### C.6 Random body pick for bots (`pickRandomBodyHead`)

**File**: [port/src/net/matchsetup.c](port/src/net/matchsetup.c).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| 359-406 | `pickRandomBodyHead` | `numBodies = mpGetNumBodies()` -> `catalogMpBodyId(rand() % numBodies)` with up-to-10 dup-avoidance retries | **NONE** | RESOLUTION (single pick) + retry | Used for re-roll body+head paths and any caller that wants a fresh body for a bot. The unlock filter applies to player UI; this leader-side server pick currently ignores it. |

**Filter gap**: bots can be rolled into locked bodies. The leader picks a body the local player has unlocked; remote peers without that unlock still wear it (cosmetic, no gameplay impact).

### C.7 Room screen self-edit body picker

The room screen does NOT have an explicit player-self body picker -- the player edits their own body via the **Player Config** screen (C.2). The room screen only edits bot slots (C.4, C.5) and reads the local player's `body_id` via `g_PlayerConfigsArray[0].base.body_id`.

### C.8 Legacy carousel handlers (probably dead per P10 D5.7, but verify)

**Files**: [src/game/mplayer/setup.c](src/game/mplayer/setup.c), [port/src/net/netmenu.c](port/src/net/netmenu.c).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| setup.c:828-933 | `mpCharacterBodyMenuHandler` | `mpGetNumBodies()` at MENUOP_GETOPTIONCOUNT; `challengeIsFeatureUnlocked` at MENUOP_21 (step gate) | partial | step-validation | Used by Player Config body list (still called via `list_*` accessors). |
| setup.c:2887-2934 | `mpCharacterBodyListHandler` | same shape, list-flavored | partial | step-validation | Same handler, list variant. |
| netmenu.c:145-187 | `menuhandlerCoopCharacter` | `ARRAYCOUNT(g_MpBodies) + 1` (compile-time 64) | **NONE** | ITERATION (count) | Legacy native co-op character dropdown. P10 D5.7 made native rendering dead but the handler is still wired to the dialog def. |
| netmenu.c:374-409 | `menuhandlerJoinCharacter` | same shape | **NONE** | ITERATION (count) | Legacy native join character dropdown. Same dead-handler status. |

**Filter gap**: even the legacy code path has the same bug. P10 D5.7 (S184) removed all legacy native rendering, so the handlers are vestigial -- they do not render anymore. Same heads decision applies: leave the legacy handlers alone, just bypass their MENUOP_GETOPTIONCOUNT path from the migrated ImGui pickers.

### C.9 Wire serialization

**Files**: [port/src/net/netmsg.c](port/src/net/netmsg.c), [port/src/net/netmanifest.c](port/src/net/netmanifest.c), [port/src/net/matchsetup.c](port/src/net/matchsetup.c).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| netmsg.c:1297 | bot config wire write fallback | `catalogMpBodyId((s32)bc->base.mpbodynum)` -- correct mp_idx -> id mapping (heads Step 7 already fixed the matching head call at line 1300) | n/a | RESOLUTION | Wire-encode bot body identity. **Already correct -- no B-235 sibling bug for bodies on the wire.** |
| netmanifest.c | manifest entry build | `catalogIdByRuntime(ASSET_BODY, bodynum)` -- callers pass `bodynum` (g_HeadsAndBodies[] index) | n/a | RESOLUTION | Manifest entry body id. |

**Filter gap**: leader-side writes still don't apply unlock filter -- if leader has unlocks the follower doesn't, the follower wears the body without unlock validation. Same heads decision I.4 applies: status quo for cosmetics, host authority.

### C.10 Misc resolution sites (not selectors -- listed for completeness)

These RESOLVE bodies but do not iterate the pool. Migrating selectors does not require touching them:

- `port/fast3d/pdgui_charpreview.c:230,240` -- `catalogGetBodyIsComplete` + `catalogGetBodyDefaultHead` for preview fallback.
- `port/fast3d/pdgui_skin_editor.cpp:909-925` -- `assetCatalogIterateByType(ASSET_BODY, charCollector, NULL)` for the **authoring** Skin Editor character selector. Deliberately **does not** apply unlock filter -- this is a creator tool, not a player-facing selector. Out of migration scope (matches heads scope decision: skin editor is authoring, not selector).
- `port/fast3d/pdgui_skin_uv.cpp:183` -- `catalogGetBodyModeldef(be->mp_index)` for skin UV editor (authoring).
- `port/src/net/netmenu.c:176, 403` -- `catalogGetBodyDefaultMpHeadIdx(mpbodynum)` after a body change. Default-head resolution, not iteration.
- `port/src/modelcatalog.c:697,808` -- internal modelcatalog scan / cache.
- `src/game/body.c` -- `catalogIdByRuntime(ASSET_BODY, ...)` for body load logic.
- `src/game/botmgr.c:79` -- `mpGetBodyId(g_BotConfigsArray[aibotnum].base.mpbodynum)` for bot manager body resolve.
- `src/game/menu.c:2048` -- `mpGetBodyId(mpbodynum)` for legacy menu render.
- `src/game/player.c:1979` -- `mpGetBodyId((cfg_).mpbodynum)` in `B234_RESOLVE_CHARCONFIG` macro.
- `src/game/mplayer/setup.c:2503` -- `mpGetBodyName(g_PlayerConfigsArray[g_MpPlayerNum].base.mpbodynum)` for header text.

---

## Section D -- Unlock-state surface (already shared with heads)

### D.1 The query function

[src/game/challenge.c:851](src/game/challenge.c:851) -- `bool challengeIsFeatureUnlocked(s32 featurenum)`. Same as heads. The MPFEATURE_CHR_* enum covers both heads (line 17 constraint) and bodies; `g_MpBodies[].requirefeature` and `g_MpHeads[].requirefeature` reference the same id space.

### D.2 Where it's queried for BODIES today

| File:line | Context | Notes |
|---|---|---|
| [src/game/mplayer/setup.c:859](src/game/mplayer/setup.c:859) | `mpCharacterBodyMenuHandler` MENUOP_21 (next-valid step) | `if (!challengeIsFeatureUnlocked(mpGetBodyRequiredFeature(data->carousel.value))) return 1;` |
| [src/game/mplayer/setup.c:2927](src/game/mplayer/setup.c:2927) | `mpCharacterBodyListHandler` MENUOP_21 (next-valid step) | Same shape, list variant. |

That is the ONLY current site that gates a body selector by unlock. The new ImGui pickers do not query it for bodies. The arena builder + heads pickers do (heads landed today).

### D.3 Reference templates

After the heads migration, the canonical template is `assetCatalogIterateUnlockedByType(ASSET_HEAD, ...)` at [port/fast3d/pdgui_menu_agentcreate.cpp:200-209](port/fast3d/pdgui_menu_agentcreate.cpp:200) and the cache-invalidation pattern at line 290-301. Bodies will land the same shape with `ASSET_BODY` swapped in.

### D.4 Cheat / debug bypass

`challengeIsFeatureUnlocked` returns 1 when the Unlock All cheat is active -- already verified in heads audit D.4. No body-specific bypass.

### D.5 Save format

Unlock state lives in the challenge save. The wire identity for a body is the catalog ID string; unlock state is per-machine, queried at presentation time. **No save migration required.**

### D.6 Wire / cross-client validation

Same as heads I.4 -- status quo. Host authority for cosmetics. The follower honors the leader's body pick without re-validating.

---

## Section E -- Catalog ID conformance

### E.1 MP body IDs (Pass 1, `s_BaseBodies[]`)

The name table at [port/src/assetcatalog_base.c:236-309](port/src/assetcatalog_base.c:236) provides 63 entries (mpidx 0..62), all human-readable. **All 63 conform** to the `"base:" + readable_name` convention (e.g. `base:dark_combat`, `base:carrington`, `base:skedar`).

**Conformance**: 63/63. Better than heads (which had 75/76 with `head_75` falling through to integer fallback). No catalog ID rename follow-up needed for bodies.

### E.2 SP body IDs (Pass 2, sp fallback)

`base:sp_body_<i>` where `<i>` is the runtime g_HeadsAndBodies[] index. **Non-conforming by design** -- SP bodies have no human names in `s_BaseBodies[]`. Same status as `sp_head_<i>`. Per heads decision I.1, **defer the rename** to a future polish session. Not blocking the migration.

### E.3 Mod bodies

Mod bodies enter the catalog via `assetcatalog_scanner.c::registerComponent` and use IDs from their `mod.json` manifest. Default `requirefeature = 0` (always unlocked). Forward-compatible with the migration.

---

## Section F -- Gap characterization per consumer

Same shape as the heads audit Section F. For each ITERATION row in Section C, what specifically must change to read Layer B with unlock filter applied?

| Consumer | Today | Target |
|---|---|---|
| **C.1 Agent Creator body carousel** | `s_NumBodies = mpGetNumBodies()`; iterate `[0..s_NumBodies)` | Build a static `s_BodyList[N]` of `{id, display, mp_index}` via `assetCatalogIterateUnlockedByType(ASSET_BODY, ...)`. Refresh on `IsWindowAppearing` or unlocked-count delta. Carousel index space becomes `[0..N)` over the filtered list, NOT `[0..63)`. Selection commits via the entry's `id` to `mpPlayerConfigSetHeadBody`; legacy `s_SelectedBody` becomes a list index, NOT mp_idx. Auto-head-pick (`autoSelectHead`) reads `s_BodyList[s_SelectedBody].id`. Integrated-head guard switches to look up the entry directly. |
| **C.2 Player Config body list** | `numBodies = list_GetOptionCount(mpCharacterBodyListHandler, 0)` | Same pattern as C.1 but list-flavored. Replace `list_GetOptionCount` / `list_GetOptionText` calls with iteration over the unlocked-pool list. Commit via `mpchrSetBodyById` (catalog-ID-first setter); the auto-head-pick chain at `setup.c:2906-2915` keeps working because `mpchrSetHeadById` resolves catalog IDs identically. |
| **C.3 Bot Setup body dropdown** | `nBody = car_GetCount(menuhandlerMpSimulantBody, 0)` | Same pattern as C.1 dropdown-flavored. Build filtered list once per dialog open, drive the combo from it. Commit via `car_Set(menuhandlerMpSimulantBody, 0, mp_index)` so the legacy `mpchrSetBodyByIndex` writer keeps the body_id (PRIMARY) and mpbodynum (DEPRECATED) in sync. |
| **C.4 Room screen bulk Set Body submenu** | `numBodies = mpGetNumBodies()` -> sort `[0..numBodies)` | Iterate via `assetCatalogIterateUnlockedByType(ASSET_BODY, ...)`. Sort by display name. Per row, body_id comes directly from the entry; head_id continues to come from `catalogPickRandomHeadIdForBody` (already unlock-filtered). |
| **C.5 Room screen single-bot edit modal** | same | same as C.4 |
| **C.6 `pickRandomBodyHead`** | `mpGetNumBodies()` random pick | Replace random pick over `mpGetNumBodies()` with a collector-based pick: build a transient list of unlocked body IDs via `assetCatalogIterateUnlockedByType(ASSET_BODY, ...)`, then `rand() % count` into that list. Decision I.2: drop the `mp_index >= 0` constraint -- bots can wear any unlocked body (matches the `g_BotHeads` retirement decision from heads I.5). Graceful "no unlocked body" fallback (matches heads I.6). |
| **C.7 Identity save / load** | n/a -- RESOLUTION only | (no-op) |
| **C.8 Legacy carousel handlers** | unfiltered MENUOP_GETOPTIONCOUNT | Per heads decision: leave alone, do not delete (P10 D5.7 made native rendering dead; the handlers are vestigial but harmless). |
| **C.9 Wire writes** | already-correct `catalogMpBodyId` | (no-op) |

---

## Section G -- Migration sequencing (per consumer, surgical)

Each step is a self-contained, build-verifiable commit. Sequenced so a failed step is bisectable.

**Step 1 -- Migrate Agent Creator body carousel** ([port/fast3d/pdgui_menu_agentcreate.cpp](port/fast3d/pdgui_menu_agentcreate.cpp)).
- Add `BodyEntry { char id[CATALOG_ID_LEN]; char display[64]; s32 mp_index }` and a static `s_BodyList[MAX_BODY_COUNT]` + `s_BodyListCount` + `s_BodyListCountKnown` triple, mirroring `s_HeadList[]`.
- Add a collector callback + rebuild function. Use `mpGetBodyName(e->mp_index)` for display when available (catalog already overrides for B-226 cases via `mpGetBodyName`'s catalog path); fall back to `formatCatalogId` for SP / mod bodies whose mp_index is -1.
- Replace `s_NumBodies = (s32)mpGetNumBodies()` with `s32 unlockedBodyCount = assetCatalogGetUnlockedCountByType(ASSET_BODY)` + the rebuild trigger.
- Carousel arrows step `s_SelectedBody` over the filtered list. `getBodyDisplayName` reads from `s_BodyList`. `autoSelectHead` reads `s_BodyList[s_SelectedBody].id`. Integrated-head guard takes a list-index and resolves the entry directly.
- Selection commits use `s_BodyList[s_SelectedBody].id` as the catalog ID; the matched mp_index can come from the entry too.
- Reset of `s_SelectedBody = 0` on Cancel still valid (position 0 is guaranteed unlocked).

**Step 2 -- Migrate Player Config body list** ([port/fast3d/pdgui_menu_playerconfig.cpp](port/fast3d/pdgui_menu_playerconfig.cpp)).
- Mirror Step 1's pattern, list-flavored. Add `s_PcBodyList[]` + collector + rebuild + count-known-tracker.
- Replace `numBodies = list_GetOptionCount(mpCharacterBodyListHandler, 0)` with the unlocked-count delta + rebuild + `numBodies = s_PcBodyListCount`.
- The for-loop over rows iterates `s_PcBodyList[]`. Each row's text is `s_PcBodyList[i].display`; click commits `mpchrSetBodyById(g_PlayerConfigsArray[g_MpPlayerNum].base, s_PcBodyList[i].id)` so the body_id PRIMARY field updates correctly.
- After body change, the existing default-head auto-pick chain at the legacy handler keeps firing (the legacy MENUOP_SET path is preserved by `list_Set`).
- Live preview reads `bodyId = s_PcBodyList[curBody].id`, replacing `catalogMpBodyId(curBody)`.

**Step 3 -- Migrate Bot Setup body dropdown** ([port/fast3d/pdgui_menu_botsetup.cpp](port/fast3d/pdgui_menu_botsetup.cpp)).
- Same pattern as Step 2 dropdown-flavored. Add `s_BsBodyList[]` (alongside the heads-migration `s_BsHeadList[]`).
- Replace `nBody = car_GetCount(menuhandlerMpSimulantBody, 0)` with the unlocked-count delta + rebuild.
- Combo body iterates `s_BsBodyList[]`. Click commits via `car_Set(menuhandlerMpSimulantBody, 0, s_BsBodyList[i].mp_index)` so the legacy mpchrSetBodyByIndex writer fires.

**Step 4 -- Migrate Room screen bulk + single body pickers** ([port/fast3d/pdgui_menu_room.cpp](port/fast3d/pdgui_menu_room.cpp)).
- Two sites (lines 2225 and 3436). Both currently read `mpGetNumBodies()` and iterate `[0..numBodies)`.
- Build a local-stack `BodyEntry sorted[MAX_BODY_ENTRIES]` array via `assetCatalogIterateUnlockedByType(ASSET_BODY, ...)` and sort by `mpGetBodyName` (or the display field on the entry's `ext.body.display_name`).
- Body-id assignment per slot uses `sorted[si].id` (entry's catalog ID) instead of `catalogMpBodyId(b)`.
- Leave the per-row `catalogPickRandomHeadIdForBody` head pick path untouched -- it's already unlock-filtered (heads Step 2).
- These sites do not have multi-frame caches; rebuild per dialog open (matching the existing per-render iteration pattern).

**Step 5 -- Migrate `pickRandomBodyHead`** ([port/src/net/matchsetup.c](port/src/net/matchsetup.c)).
- Build a transient pool of unlocked body IDs via `assetCatalogIterateUnlockedByType(ASSET_BODY, ...)`.
- Random pick from the pool with the same up-to-10 dup-avoidance retry. Drop the integer-mp_idx-based loop entirely.
- Graceful fallback when pool is empty: keep the existing `"base:dark_combat"` constant fallback (matches heads Decision I.6 graceful sentinel).
- Note: this function runs in BOTH client and server `pd-server` builds. `assetCatalogIterateUnlockedByType` falls through to a no-op on the server (no entries registered), which keeps the original `"base:dark_combat"` fallback behaviour for dedicated servers. Verify by reading the iterator's server-side path.

**Step 6 (optional) -- audit / remove dead legacy carousel handler.**
- Confirm `mpCharacterBodyMenuHandler` is no longer authoritative (the new pickers bypass `list_GetOptionCount` etc.).
- Per heads decision I.6, leave the handler alone -- it's a vestigial C-callback that does not render anymore but is still wired to legacy dialog defs.

**Verification after each step**: `source devtools/build-env.sh && ninja -C Build pd pd-server pd-tests` (full link, plus tests target since pd-tests landed today). Visual playtest after Step 1 to confirm the Agent Creator carousel size matches unlocked count; Steps 2 + 3 for Player Config + Bot Setup body dropdown sizes; Step 4 for room screen bulk + single bot bodies; Step 5 with the Unlock All cheat off + a fresh save confirms re-roll only picks unlocked bodies.

---

## Section H -- Cross-cuts

### H.1 Wire format

- `mpchrconfig::body_id[64]` is the catalog ID. Stable, mod-aware, no protocol bump.
- The B-235 sibling check at `netmsg.c:1297` already uses `catalogMpBodyId((s32)bc->base.mpbodynum)` correctly. **No B-235 sibling bug to fix for bodies.**
- Mod bodies go on the wire via the catalog ID with no special-casing.

### H.2 Save format

- `mpchrconfig::body_id` is a runtime field (not directly serialized to disk except via identity v2 / scenario_save, both already string-keyed).
- **No save migration required.**

### H.3 Bot configs

- `g_BotConfigsArray[].base.body_id` (PRIMARY) + `mpbodynum` (DEPRECATED). Already catalog-ID-native.
- `g_BotProfiles[].body` is a fixed 18-entry mp_idx table for bot creation defaults. Not a selector pool -- bots get those bodies as their initial assignment, then re-rolls / room screen edits go through C.4 / C.5 / C.6.
- `mpCreateBotFromProfile` (heads Step 6) uses `catalogMpBodyId(g_BotProfiles[profilenum].body)` to resolve the initial body_id. Each profile body is hardcoded -- not user-selected -- so applying the unlock filter here is wrong: the bot type definition asserts a specific body for that profile (e.g. BOTTYPE_KAZE always uses Pres Security). **Leave `g_BotProfiles[].body` resolution unchanged.**

### H.4 Default selections

- Body's default head: `catalogGetBodyDefaultHead(body_id)` (existing accessor). Unchanged by the bodies migration.
- Auto-head-pick on body change (Player Config setup.c:2906-2915, Bot Setup setup.c:3440-3461) keeps working -- they receive the new body_id via `mpchrSetBodyById` and look up the default head themselves.

### H.5 Bodies with integrated heads (`unk00_01 == 1`)

- `catalogGetBodyIsComplete(bodynum)` returns 1 for bodies whose `unk00_01 == 1` (Dr. Carroll, Eye Spy, Skedar). The Agent Creator integrated-head guard reads through this accessor (line 238: `catalogGetBodyIsComplete(be->runtime_index)`). Migrating the bodies surface preserves the guard automatically -- the entry-driven lookup still resolves to the same `runtime_index`. **No semantic change to the guard.**
- Player Config + Bot Setup do NOT currently apply the integrated-head guard. After the heads migration the head selector for those screens reads through `s_PcHeadList[]` / `s_BsHeadList[]`, but does not check `mpBodyHasIntegratedHead`. Out of scope for the bodies migration -- track as a polish follow-up if Mike wants symmetry.

### H.6 The B-235 anti-pattern (mp_idx vs runtime_index domain confusion)

- B-235 fixed in heads Step 7. Body site at netmsg.c:1297 already uses `catalogMpBodyId(mp_idx)` correctly.
- Migration removes integer index arithmetic from selectors (every consumer reads catalog IDs directly off entries), so the off-by-one risk for bodies goes away by construction.

### H.7 Server build

- `assetCatalogIterateUnlockedByType(ASSET_BODY, ...)` falls through to a zero-iteration no-op on `pd-server` (no entries registered; `assetCatalogRegisterBaseGame` not called server-side). `pickRandomBodyHead` server path retains the `"base:dark_combat"` constant fallback.
- `pdgui_menu_*` files are `pd`-target only.
- Migration helpers must build clean on both targets.

### H.8 Mod bodies

- Mod bodies enter via `assetcatalog_scanner.c::registerComponent` and use `mod.json` IDs. `requirefeature = 0` by default. Forward-compatible.

---

## Section I -- Decisions (deferred to defaults per `make-decisions-delegation` memory)

The heads migration produced 8 confirmed decisions (I.1-I.8) that mostly transfer 1:1 to bodies. Re-evaluating each:

| # | Heads decision | Bodies application |
|---|---|---|
| **I.1** | Catalog ID renames deferred. | **Same.** Bodies already conform 63/63 (E.1); SP body IDs (`sp_body_<i>`) follow heads decision -- defer. |
| **I.2** | `g_HeadsAndBodies[]` kept intact. | **Same.** This bodies session also leaves the array alone -- migrate the body-related callers only. |
| **I.3** | Helper lands in `assetcatalog_api.c`. | **Already shipped** in heads Step 1 -- supports `ASSET_BODY` natively. No new helper needed. |
| **I.4** | Status quo -- host authority for cosmetics. | **Same.** No follower-side body validation. |
| **I.5** | Drop `g_BotHeads` famous-name exclusion. | **N/A** -- there is no `g_BotBodies` static pool to retire. The closest analog (`pickRandomBodyHead`) already pulls from `g_MpBodies[]`; just adding the unlock filter is the bodies-equivalent decision and is straightforward. |
| **I.6** | Graceful "no unlocked body" sentinel fallback. | **Same.** Step 5's `pickRandomBodyHead` keeps `"base:dark_combat"` as a final fallback when the unlocked body pool is empty. |
| **I.7** | Apply unlock filter inside `catalogPickRandomHeadIdForBody`. | **Already shipped** in heads Step 2 -- bodies migration does not alter this function. |
| **I.8** | Bundle into 4-6 commits. | **Apply same shape.** 5 commits planned: |

### Bodies-specific items that need explicit decisions

None require Mike's call. The audit confirms:

- No B-235 sibling site for bodies on the wire (already correct).
- Catalog ID conformance is 63/63 for MP bodies (no rename gap to flag).
- Server build behaviour falls out cleanly via the type-generic iterator.
- `g_BotProfiles[].body` is intentional bot-archetype definition data (not a selector pool); leave alone.
- Skin Editor `assetCatalogIterateByType(ASSET_BODY, ...)` is authoring tool, not a player-facing selector; leave alone.
- Player Config + Bot Setup integrated-head guard asymmetry is a head-selector polish, out of scope for bodies migration.

---

## Section J -- Decisions confirmed (2026-04-26, default per `make-decisions-delegation` memory)

All decisions transfer from heads with the bodies-specific notes above. No items surfaced for Mike's call.

| # | Decision |
|---|---|
| **J.1** | Catalog ID renames deferred (matches heads I.1). |
| **J.2** | `g_HeadsAndBodies[]` kept intact (matches heads I.2). |
| **J.3** | Reuse the existing `assetCatalogIterateUnlockedByType` + `assetCatalogGetUnlockedCountByType` helpers. No new public API. |
| **J.4** | Wire / cross-client unlock validation = STATUS QUO (matches heads I.4). |
| **J.5** | `pickRandomBodyHead` adopts the unlock filter; `g_BotProfiles[].body` resolution stays unchanged (deliberate per-archetype assignment). |
| **J.6** | Graceful fallback to `"base:dark_combat"` when the unlocked body pool is empty (matches heads I.6 spirit). |
| **J.7** | The integrated-head guard at Agent Creator stays catalog-driven (already routes through `catalogGetBodyIsComplete`); no change required. |
| **J.8** | 5 commits, sequential, bisectable, build-verified per step. |

## Phase 2 commit plan (5 commits)

1. **Step 1** -- migrate Agent Creator body carousel ([pdgui_menu_agentcreate.cpp](port/fast3d/pdgui_menu_agentcreate.cpp)).
2. **Step 2** -- migrate Player Config body list ([pdgui_menu_playerconfig.cpp](port/fast3d/pdgui_menu_playerconfig.cpp)).
3. **Step 3** -- migrate Bot Setup body dropdown ([pdgui_menu_botsetup.cpp](port/fast3d/pdgui_menu_botsetup.cpp)).
4. **Step 4** -- migrate Room screen bulk + single bot body pickers ([pdgui_menu_room.cpp](port/fast3d/pdgui_menu_room.cpp)).
5. **Step 5** -- migrate `pickRandomBodyHead` ([port/src/net/matchsetup.c](port/src/net/matchsetup.c)).

Step 6 (legacy-handler audit) deferred -- the new pickers no longer route through the legacy handler for size queries, so the handler becomes vestigial without needing a removal commit. Same disposition as heads Step 8.

---

## Stop conditions encountered (none)

- Helper API for unlock-filtered iteration -- already shipped in heads Step 1.
- B-235 sibling for bodies on wire -- not present (bodies use `catalogMpBodyId` correctly).
- Catalog ID conformance -- 63/63 for MP bodies (no `body_75` integer-fallback like heads).
- Wire format -- catalog IDs already in place.
- Layer A body consumers can be cleanly migrated -- no iteration order dependency.
- Server build -- type-generic iterator falls through cleanly on dedicated server.
- Integrated-head guard -- already catalog-driven, no semantic change required.

## Surface area summary

- **Layer A tables**: 1 to migrate the callers of (`g_MpBodies[63]`); 1 left intact (`g_HeadsAndBodies[152]`).
- **Layer A direct iterations to remove or audit**: 5 sites (Section A.2 lines 1, 4 are non-selector).
- **Layer A accessor functions**: 7 (Section A.3) -- all stay; only the iteration ones get bypassed.
- **Layer B accessors already in place**: 13 (Section B.2). No new public API needed.
- **Selector consumers requiring migration**: 5 (C.1 Agent Creator / C.2 Player Config / C.3 Bot Setup / C.4 Room bulk / C.5 Room single + C.6 random body for bots).
- **Cross-cut sites (RESOLUTION only, no iteration)**: 8 sites (C.10) -- listed for completeness, not migrated.
- **Decisions for Mike**: 0 (defaults from heads transfer per memory `make-decisions-delegation`).
- **Suspected B-235 sibling bugs**: 0 (already correct on the wire).

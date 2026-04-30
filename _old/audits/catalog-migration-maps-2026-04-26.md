# Catalog Migration -- Maps / Arenas Selector (Phase 1 Audit)

> **Date**: 2026-04-26
> **Scope**: MP arena selector pool only.  Heads (dev `132a883c`) and bodies (dev `e7c4e702`) merged earlier today.  This is the final selector-pool migration in the catalog chain.
> **Architectural directive (Mike, prior session)**: "It should build the weapons list from the weapons listed in the catalog, filtering by weapons that are either not unlocked yet or are disabled (which technically should mean they aren't in the catalog at all, since it's supposed to be dynamically constructed). This same thing should apply for character heads and bodies, weapons, maps, etc."
> **Project memory anchor**: `catalog-builds-all-selector-pools` -- selector pool = catalog INTERSECT unlock-state.
> **Constraint anchor (`context/constraints.md` line 32)**: "Catalog registers ALL assets (unlock is a separate layer)."
> **Working precedent**: heads audit `context/audits/catalog-migration-heads-2026-04-26.md` (dev `132a883c`); bodies audit `context/audits/catalog-migration-bodies-2026-04-26.md` (dev `e7c4e702`).
> **B-225 precedent**: the original B-225 fix recommendation explicitly named this same architectural pattern for arenas: "data-driven probe at mpInit (walk g_MpArenas, resolve each stage's required files via catalogResolveFile, cache a per-arena .available bit) so the three tables cannot drift."  The structural note at `src/game/mplayer/setup.c:187-194` carries the same recommendation forward.
> **Phase**: 1 of 4 -- AUDIT only.  Phase 2 = migration, Phase 3 = verify, Phase 4 = auto-merge.

---

## Section A -- Layer A inventory (data tables and direct accessors)

### A.1 Tables (data)

| # | Symbol | File:line | Size | Element type | Purpose |
|---|---|---|---|---|---|
| 1 | `g_MpArenas[]` (client) | [src/game/mplayer/setup.c:115](src/game/mplayer/setup.c) | 47 | `struct mparena { s16 stagenum; u8 requirefeature; s16 name; }` | **Authoritative client-side MP arena table.**  Post-AllInOne / GEX / Goldfinger cull (2026-04-26).  Group layout: Dark (0-12), Solo Missions (13-26), Classic (27-31), Bonus (32-44), Random (45-46).  `stagenum` is the logical stage ID (`STAGE_MP_*` / `STAGE_*` solo); `name` is a langbank string ID; `requirefeature` is the MPFEATURE_* unlock gate. |
| 2 | `g_MpArenas[]` (server stub) | [port/src/server_stubs.c:113](port/src/server_stubs.c) | 47 | same | Mirror of the client table for `pd-server`.  Names + requirefeature both set to 0 -- only `stagenum` matters server-side (no langbank, no challenge save).  Size and stagenum order MUST match the client table; the comment at server_stubs.c:138 pins the contract. |
| 3 | `s_ArenaNames[47]` | [port/src/assetcatalog_base.c:632](port/src/assetcatalog_base.c) | 47 | `const char *const` | **Slug shadow** for catalog ID generation -- `"base:arena_<slug>"`.  Maps `g_MpArenas[]` index to slug.  NULL slot = "skip this arena from registration" canonical marker (none used post-cull -- all 47 are registered). |
| 4 | `g_ArenaGroupDefs[7]` | [src/game/mplayer/setup.c:368](src/game/mplayer/setup.c) | 7 | `{ s32 offset; u16 name; }` | Legacy collapsible-group offset table for the legacy `mpArenaMenuHandler` carousel.  Group offsets: 0/13/27/32/43/55/71.  Note: 6 of the 7 group offsets do not match the post-cull table (the table only has 47 entries, group offsets 43/55/71 are stale).  Used by `arenaMapIndex` / `arenaCountVisible` / `arenaFindSelected`.  This table is only consulted via `mpArenaMenuHandler` which is in turn consulted by `pdgui_menu_mpsetup.cpp::renderMpArena` -- a migration target. |
| 5 | `s_ArenaGroupMap[5]` | [port/src/assetcatalog_base.c:665](port/src/assetcatalog_base.c) | 5 | `{ s32 first; s32 count; const char *category; }` | Layer B group definitions used at registration time only (not at runtime).  Maps arena index ranges to category strings stored in `e->category`: "Dark" / "Solo Missions" / "Classic" / "Bonus" / "Random". |

`mparena` struct definition: alongside `mphead` / `mpbody` in `src/include/types.h`.
External declaration: [src/include/data.h:459](src/include/data.h:459) -- `extern struct mparena g_MpArenas[];`.

### A.2 Layer A direct iteration sites

| File:line | Function | What it iterates | Filter? |
|---|---|---|---|
| [src/game/mplayer/setup.c:174](src/game/mplayer/setup.c:174) | `mpGetNumStages` | one-shot count via `modmgrGetTotalArenas()` | n/a |
| [src/game/mplayer/setup.c:201-232](src/game/mplayer/setup.c:201) | `mpArenaIndexIsUsable` | resolves a Layer A index to its arena and applies `challengeIsFeatureUnlocked` + name validity checks | unlock |
| [src/game/mplayer/setup.c:234-263](src/game/mplayer/setup.c:234) | `mpChooseRandomStage` | full Layer A `[0..71)`, gated by `mpArenaIndexIsUsable` | unlock |
| [src/game/mplayer/setup.c:265-294](src/game/mplayer/setup.c:265) | `mpChooseRandomMultiStage` | Layer A `[0..32)` with `(i <= 12 \|\| i >= 27)` -- excludes Solo Missions | unlock |
| [src/game/mplayer/setup.c:296-325](src/game/mplayer/setup.c:296) | `mpChooseRandomSoloStage` | Layer A `[0..27)` with `(i >= 13 && i <= 26)` -- only Solo Missions | unlock |
| [src/game/mplayer/setup.c:327-358](src/game/mplayer/setup.c:327) | `mpChooseRandomGexStage` | Layer A `[0..61)` with `((i >= 32 && i <= 54) \|\| (i >= 59 && i <= 60))` -- the OLD GEX range; **dead post-cull** (table now 47 entries; the bitmask hits both Bonus arenas and out-of-bounds slots) | unlock |
| [src/game/mplayer/setup.c:382-412](src/game/mplayer/setup.c:382) | `arenaMapIndex` | walks groups + arenas, returns visible -> arena index map | unlock (via `mpArenaIndexIsUsable`) |
| [src/game/mplayer/setup.c:414-433](src/game/mplayer/setup.c:414) | `arenaCountVisible` | walks groups + arenas, counts visible options | unlock |
| [src/game/mplayer/setup.c:435-460](src/game/mplayer/setup.c:435) | `arenaFindSelected` | walks groups + arenas, returns visible index of selected stagenum | unlock |
| [src/game/mplayer/setup.c:461-505](src/game/mplayer/setup.c:461) | `mpArenaMenuHandler` | full Layer A via the three helpers above; legacy MENUOP handler | unlock |
| [src/game/mplayer/setup.c:2631-2643](src/game/mplayer/setup.c:2631) | `mpMenuTextSetupName` (overview save text) | linear scan to find arena by stagenum | unlock |
| [src/game/mplayer/setup.c:5462-5469](src/game/mplayer/setup.c:5462) | `mpMenuTextArenaName` | linear scan to find arena by stagenum | unlock |
| [src/game/challenge.c:485-494](src/game/challenge.c:485) | `challengeForceUnlockSetup` | linear scan to find arena by stagenum, force-unlock its requirefeature | n/a |
| [port/fast3d/pdgui_bridge.c:656-668](port/fast3d/pdgui_bridge.c:656) | `pdguiPauseGetStageName` | linear scan to find arena by stagenum, return langbank name | n/a |
| [port/src/modmgr.c:2885-2891](port/src/modmgr.c:2885) | `modmgrRebuildArenaCache` | one-shot via `assetCatalogIterateByType(ASSET_ARENA, ...)` -- the **catalog -> Layer A bridge** | n/a |
| [port/src/assetcatalog_base.c:674-735](port/src/assetcatalog_base.c:674) | `assetCatalogRegisterBaseGame` arena pass | one-shot registration walk over `s_ArenaGroupMap[]` -> `s_ArenaNames[]` -> `g_MpArenas[]` | n/a |
| [src/game/spawnpool.c:1818](src/game/spawnpool.c:1818) | `smokeOfflineBuildPool` | offline smoke-test arena walk via `assetCatalogIterateByType(ASSET_ARENA, ...)` | n/a (smoke test) |

### A.3 Public Layer A accessor functions

| Function | File:line | Today reads | Purpose |
|---|---|---|---|
| `mpGetNumStages` | [src/game/mplayer/setup.c:174](src/game/mplayer/setup.c:174) | `modmgrGetTotalArenas()` | total arena count (mod-aware via catalog cache). |
| `mpArenaIndexIsUsable(idx)` | [src/game/mplayer/setup.c:201](src/game/mplayer/setup.c:201) | `modmgrGetArena(idx)` -> stagenum + requirefeature + name validity | the canonical "is this Layer A index usable for the current player" check. |
| `mpChooseRandomStage` / `Multi` / `Solo` / `Gex` | [src/game/mplayer/setup.c:234-358](src/game/mplayer/setup.c:234) | iterate Layer A | random arena resolution for `STAGE_MP_RANDOM*` tokens at match start. |
| `mpArenaMenuHandler` | [src/game/mplayer/setup.c:461](src/game/mplayer/setup.c:461) | full Layer A via group helpers | legacy MENUOP handler still consumed by ImGui mpsetup picker. |
| `mpMenuTextArenaName(item)` | [src/game/mplayer/setup.c:5458](src/game/mplayer/setup.c:5458) | linear `g_MpArenas[]` scan | display the current arena's name for the MP advanced setup hub row. |
| `modmgrGetTotalArenas()` | [port/src/modmgr.c:2941](port/src/modmgr.c:2941) | catalog-fronted cache | total arena count.  **Already catalog-backed** -- the cache is rebuilt from `assetCatalogIterateByType(ASSET_ARENA, ...)` whenever `modmgrCatalogChanged()` fires. |
| `modmgrGetArena(idx)` | [port/src/modmgr.c:2947](port/src/modmgr.c:2947) | catalog-fronted cache | by-index arena lookup.  **Already catalog-backed.** |

`modmgrGetArena` / `modmgrGetTotalArenas` are catalog-fronted (same as bodies / heads).  The remaining problem is at the SELECTOR level (iteration shape -- "iterate `[0..N)` and apply unlock filter via `mpArenaIndexIsUsable`") rather than the per-element resolution level.

---

## Section B -- Layer B inventory (catalog registrations and accessors)

### B.1 Registration

`assetCatalogRegisterBaseGame()` registers arenas in a single pass at [port/src/assetcatalog_base.c:612-735](port/src/assetcatalog_base.c:612):

- Walks `s_ArenaGroupMap[5]` -> for each group, walks the count of arenas at that offset.
- For each entry: skips NULL `s_ArenaNames[idx]` (canonical "exclude" marker; none post-cull).
- Builds id `"base:arena_<slug>"` and calls `assetCatalogRegisterArena(idbuf, stagenum, requirefeature, name_langid)`.
- Sets `e->category = group->category` ("Dark" / "Solo Missions" / "Classic" / "Bonus" / "Random").
- Sets `e->bundled = 1`, `e->enabled = 1`, `e->runtime_index = idx` (Layer A index).
- B-254 (2026-04-25): Solo Missions group (idx 13-26) sets `e->ext.arena.load_mode = ARENA_LOADMODE_CANVAS`.  All other groups keep the default `ARENA_LOADMODE_PLAYABLE`.
- Logs: `"assetcatalog: registered N base arenas"` (47 expected post-cull).

### B.2 Accessors

| Accessor | Defined | Returns | Use |
|---|---|---|---|
| `assetCatalogIterateByType(ASSET_ARENA, ...)` | catalog.c | callback per arena | full iteration |
| `assetCatalogIterateUnlockedByType(ASSET_ARENA, ...)` | [api.c:490](port/src/assetcatalog_api.c:490) | callback per unlocked arena | **the migration target API** -- shipped in heads Step 1 |
| `assetCatalogGetCountByType(ASSET_ARENA)` | catalog.c | total registered arena count | for sizing |
| `assetCatalogGetUnlockedCountByType(ASSET_ARENA)` | [api.c:506](port/src/assetcatalog_api.c:506) | unlocked count | for cache delta |
| `catalogIdByRuntime(ASSET_ARENA, idx)` | api.c:445 | catalog ID by Layer A index | reverse-lookup |
| `assetCatalogResolve("base:arena_*")` | catalog.c | full asset_entry by ID | by-ID lookup |
| `pdguiResolveStageId(stage_id)` | [pdgui_bridge.c:961-980](port/fast3d/pdgui_bridge.c:961) | stagenum from catalog ID (handles ASSET_ARENA + ASSET_MAP) | UI-side resolution |

### B.3 `ext.arena` struct fields ([port/include/assetcatalog.h:223-238](port/include/assetcatalog.h:223))

```c
struct {
    s32 stagenum;          /* logical stage ID this arena loads */
    u8  requirefeature;    /* unlock check (0 = always available) */
    s32 name_langid;       /* language string ID for display name */
    u8  load_mode;         /* B-254: 0=PLAYABLE, 1=CANVAS (Grid suppress) */
} arena;
```

### B.4 Counts (working baseline)

- `assetcatalog: registered 47 base arenas` -- post-cull.  Group split: Dark 13, Solo Missions 14, Classic 5, Bonus 13, Random 2.
- The catalog count is authoritative for the migration -- the legacy `mpGetNumStages()` returns the same value via `modmgrGetTotalArenas()` (catalog-fronted).

---

## Section C -- Selector consumers (every code path that iterates "the available arenas")

Same shape as the heads / bodies audits.  Each row: file:line, function, what it reads, what filter is applied today, ITERATION vs RESOLUTION, brief description.

### C.1 CS / Room screen arena picker (already migrated)

**File**: [port/fast3d/pdgui_menu_room.cpp](port/fast3d/pdgui_menu_room.cpp).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| 343-393 | `catalogArenaCollect` | catalog ASSET_ARENA entries | **YES** -- `challengeIsFeatureUnlocked` | ITERATION | Builds `s_Arenas[]` with name/id/stagenum/requirefeature/category/section.  Section split: MP_BASE / CAMPAIGN / MOD. |
| 396-402 | `arenaCompare` | s_Arenas | n/a | sort | Sort by section, then alphabetical. |
| 404-436 | `buildArenaListFromCatalog` | catalog | unlock | iter wrap | `assetCatalogIterateByType(ASSET_ARENA, catalogArenaCollect, NULL)` + sort + section-boundary compute. |
| 1026 | (catalog browser tag) | n/a | n/a | (display) | Type label for ASSET_ARENA in catalog tab. |
| 1388-1389 | (room body picker stage check) | catalog ASSET_ARENA | n/a | RESOLUTION | Stage-id consumption when changing arena. |

**Filter status**: APPLIED.  This is the canonical **template** for the migration -- `catalogArenaCollect` shows the exact unlock-filter shape the other selectors must adopt.

**Migration status**: DONE (pre-existing).  No work in this audit.

### C.2 Forge / Grid arena picker (already migrated)

**File**: [port/fast3d/pdgui_menu_mainmenu.cpp](port/fast3d/pdgui_menu_mainmenu.cpp).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| 4351-4401 | `gridArenaCollect` | catalog ASSET_ARENA | **YES** -- `challengeIsFeatureUnlocked` | ITERATION | Builds `s_GridArenas[]` with name/id/stagenum/category/load_mode.  Category derived from `e->category`. |
| 4403-4410 | `gridArenaCompare` | s_GridArenas | n/a | sort | Blank Map first, then alphabetical. |
| 4412-4446 | `gridArenaListBuild` | catalog | unlock | iter wrap | `assetCatalogIterateByType(ASSET_ARENA, gridArenaCollect, NULL)` + sort + Blank Map prepend. |

**Filter status**: APPLIED.

**Migration status**: DONE (pre-existing, AUDIT-24-M5).  No work in this audit.

### C.3 MP setup arena picker (the BIG migration target)

**File**: [port/fast3d/pdgui_menu_mpsetup.cpp](port/fast3d/pdgui_menu_mpsetup.cpp).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| 147 | (forward decl) | `mpArenaMenuHandler` | n/a | (decl) | Legacy handler delegate. |
| 505-558 | `renderMpArena` | full `list_*(mpArenaMenuHandler, ...)` calls | unlock (via `mpArenaIndexIsUsable` inside the legacy handler) | ITERATION | Renders the MP advanced setup hub's Arena picker by delegating EVERY operation (`GETOPTIONCOUNT` / `GETOPTIONTEXT` / `SET` / `GETSELECTEDINDEX`) to the legacy `mpArenaMenuHandler`. |

**Filter status**: APPLIED INDIRECTLY -- the legacy handler's `arenaMapIndex` / `arenaCountVisible` / `arenaFindSelected` helpers all gate via `mpArenaIndexIsUsable(a)` which calls `challengeIsFeatureUnlocked(arena->requirefeature)`.

**Filter gap**: not the unlock filter (which IS applied), but the **architecture** -- the picker reads Layer A indices and depends on the legacy collapsible-group handler.  Migration target = build a Layer-B-driven list directly in `pdgui_menu_mpsetup.cpp`, mirror `pdgui_menu_room.cpp::buildArenaListFromCatalog`.

**Migration status**: REQUIRED.  Step 1 of Phase 2.

### C.4 Random meta arena resolution (4 functions in setup.c)

**File**: [src/game/mplayer/setup.c](src/game/mplayer/setup.c).  **Callers**: [src/game/mplayer/mplayer.c:284-292](src/game/mplayer/mplayer.c:284) (`mpStartMatch`), [port/fast3d/pdgui_menu_mainmenu.cpp:4546-4550](port/fast3d/pdgui_menu_mainmenu.cpp:4546) (Grid commit).

| Line | Function | Reads | Filter today | Shape | Description |
|---|---|---|---|---|---|
| 234-263 | `mpChooseRandomStage` | Layer A `[0..71)` | unlock | RESOLUTION (single pick) | Random over ALL arenas (no group filter).  Bound 71 includes the Random meta entries (45-46), but `mpArenaIndexIsUsable` rejects them (their stagenum is the meta token itself which won't have a usable arena). |
| 265-294 | `mpChooseRandomMultiStage` | Layer A `[0..32)` with `(i <= 12 \|\| i >= 27)` | unlock | RESOLUTION (single pick) | Random over Dark + Classic.  Bound 32 means Bonus / Random groups never participate -- post-cull this is broken: the legacy bound assumed pre-cull layout. |
| 296-325 | `mpChooseRandomSoloStage` | Layer A `[0..27)` with `(i >= 13 && i <= 26)` | unlock | RESOLUTION (single pick) | Random over Solo Missions only. |
| 327-358 | `mpChooseRandomGexStage` | Layer A `[0..61)` with `((i >= 32 && i <= 54) \|\| (i >= 59 && i <= 60))` | unlock | RESOLUTION (single pick) | **Dead post-cull** -- the GEX block (32-54) was deleted; current arenas at 32-44 are the Bonus group.  The bitmask runs out of bounds at 55+ (table is 47 entries).  Caller `mpStartMatch` only invokes this when `g_MpSetup.stagenum == STAGE_MP_RANDOM_GEX`, but `STAGE_MP_RANDOM_GEX` no longer appears in the catalog or `g_MpArenas[]`, so the function is unreachable through normal UI.  Stale save files could in theory still trigger it. |

**Filter status**: APPLIED (`mpArenaIndexIsUsable` gates all four).

**Filter gaps**: pre-cull bounds (71 / 32 / 61) carried forward.  `Multi` excludes the now-Bonus arenas and excludes the Random group bonus arenas without an architectural reason.  `Gex` is dead.

**Migration target**: each function should iterate Layer B with category filter (`Dark+Classic+Bonus` for `Multi`, `Solo Missions` for `Solo`, no filter for `Stage`, retire `Gex`).  Architectural directive: "Random arena selection only ever picks from catalog-resolved arenas."

**Migration status**: REQUIRED.  Step 2 of Phase 2.

### C.5 Legacy carousel handler `mpArenaMenuHandler` (vestigial after Step 1)

**File**: [src/game/mplayer/setup.c](src/game/mplayer/setup.c).

After Step 1 (`renderMpArena` rewritten to read Layer B directly), `mpArenaMenuHandler` retains exactly one consumer: itself, plus three helper functions (`arenaMapIndex` / `arenaCountVisible` / `arenaFindSelected`) which become unused.

**Migration status**: DEFERRED.  Per the heads I.6 / bodies decision: leave the handler in place (defensive, and `g_ArenaGroupCollapsed` static state persists across mpsetup opens).  Removal is a follow-up cleanup, not blocking.

### C.6 RESOLUTION-only sites (not selectors -- listed for completeness)

These RESOLVE arenas by stagenum but do not iterate the pool to PRESENT options to the user.  Per the heads / bodies pattern, RESOLUTION sites are NOT migrated:

- [src/game/mplayer/setup.c:2631-2643](src/game/mplayer/setup.c:2631) `mpMenuTextSetupName` -- resolve setup-overview arena name by stagenum.
- [src/game/mplayer/setup.c:5462-5469](src/game/mplayer/setup.c:5462) `mpMenuTextArenaName` -- resolve hub-row arena name by stagenum.
- [src/game/challenge.c:485-494](src/game/challenge.c:485) `challengeForceUnlockSetup` -- find arena by stagenum to force-unlock its requirefeature.
- [port/fast3d/pdgui_bridge.c:656-668](port/fast3d/pdgui_bridge.c:656) `pdguiPauseGetStageName` -- pause-menu stage name by stagenum.
- [port/src/net/netmsg.c:4881, 5511-5514](port/src/net/netmsg.c:4881) -- wire reads of `stage_id` -> resolve to ext.arena.stagenum.
- [port/src/net/matchsetup.c:94-95, 686-688](port/src/net/matchsetup.c:94) -- match start `stage_id` -> stagenum.
- [src/game/mplayer/mplayer.c:830-840](src/game/mplayer/mplayer.c:830) `mpInit` -- resolve `"base:arena_mp_skedar"` for the default stagenum at engine init.
- [src/game/spawnpool.c:1770-1820](src/game/spawnpool.c:1770) -- offline smoke-test arena walk (development tool, never runs in shipped game).

These all look up an arena by stagenum or catalog ID and read scalar fields (`name`, `name_langid`, `requirefeature`).  No migration needed.

### C.7 Server-side selector surface (none -- by design)

The dedicated server has no UI and no challenge save data.  `pdgui_menu_*` files are `pd`-target only; `assetCatalogIterateUnlockedByType(ASSET_ARENA, ...)` falls through to a no-op on `pd-server` because `assetCatalogRegisterBaseGame` is not called there.  Random meta resolution happens client-side; the server reads the resolved stagenum off the wire.  Server-side `g_MpArenas[]` exists only to satisfy the linker for code that references the array in conditional / dead branches.

**Migration status**: NO server-side changes needed.

---

## Section D -- Unlock-state surface (already shared with heads / bodies)

### D.1 The query function

[src/game/challenge.c:851](src/game/challenge.c:851) -- `bool challengeIsFeatureUnlocked(s32 featurenum)`.  Same as heads / bodies.  `g_MpArenas[].requirefeature` references the same MPFEATURE_* id space.

### D.2 Where it's queried for ARENAS today

| File:line | Context | Notes |
|---|---|---|
| [src/game/mplayer/setup.c:218](src/game/mplayer/setup.c:218) | `mpArenaIndexIsUsable` | `if (!challengeIsFeatureUnlocked(arena->requirefeature)) return false;` -- the canonical Layer A gate. |
| [port/fast3d/pdgui_menu_room.cpp:365](port/fast3d/pdgui_menu_room.cpp:365) | `catalogArenaCollect` | Same shape, catalog-driven. |
| [port/fast3d/pdgui_menu_mainmenu.cpp:4362](port/fast3d/pdgui_menu_mainmenu.cpp:4362) | `gridArenaCollect` | Same shape, catalog-driven. |

### D.3 Reference templates

After heads / bodies migrations, the canonical template for arenas is `pdgui_menu_room.cpp::catalogArenaCollect` -- an unlock-filtered catalog walk that builds a sorted display list and exposes a section split.  Step 1's mpsetup migration should mirror this template; Step 2's random-meta migration should mirror the heads `pickRandomBodyHead` shape (collect transient pool + `rand() % count` + graceful fallback).

### D.4 Cheat / debug bypass

Same as heads / bodies -- `challengeIsFeatureUnlocked` is the single point.  No arena-specific bypass.

### D.5 Save format

Unlock state lives in the challenge save.  The wire identity for an arena is the catalog ID string (`mpsetup.stage_id` -- already in place since v32, see constraints.md ENet protocol entry).  Unlock state is per-machine, queried at presentation time.  **No save migration required.**

### D.6 Wire / cross-client validation

Same as heads / bodies I.4 -- status quo.  Host authority for arena selection.  Followers honor the host's `stage_id` without re-checking.  If a follower has not unlocked the chosen arena, they still load it -- this matches the standing decision that the unlock layer is presentation-only, not enforcement.

---

## Section E -- Catalog ID conformance

### E.1 MP arena IDs (`s_ArenaNames[47]`)

All 47 entries are non-NULL and human-readable.  IDs use `"base:arena_<slug>"` form: `base:arena_mp_skedar`, `base:arena_villa`, `base:arena_test_lam`, etc.  Conformance: 47 / 47.

A few slugs are still test-style (`test_arch` -> "Suburb", `test_dest` -> "Training Day", `test_lam` -> "Grand Library") -- the slugs predate the user-facing renames in the langbank, and renaming them now would invalidate save files referencing those catalog IDs.  Per the heads I.1 / bodies J.1 decision, **catalog ID renames are deferred** -- not blocking the migration.

### E.2 Mod arenas

Mod arenas enter the catalog via `assetcatalog_scanner.c::registerComponent` and use IDs from their `mod.json` manifest.  Default `requirefeature = 0` (always unlocked).  Forward-compatible.

---

## Section F -- Gap characterization per consumer

For each ITERATION row in Section C.3 + C.4, what specifically must change?

| Consumer | Today | Target |
|---|---|---|
| **C.3 mpsetup arena picker (`renderMpArena`)** | Delegates to `mpArenaMenuHandler` for COUNT / TEXT / SET / SELECTED | Build a static `s_MpsetupArenaList[N]` of `{ id, display, stagenum, category }` via `assetCatalogIterateUnlockedByType(ASSET_ARENA, ...)`.  Mirror `pdgui_menu_room.cpp::catalogArenaCollect` shape (with `arenaGetName`-driven display and section split).  Refresh on `IsWindowAppearing` and on `assetCatalogGetUnlockedCountByType(ASSET_ARENA)` delta.  Selection commits write `g_MpSetup.stagenum = entry->stagenum` and `g_MpSetup.stage_id = entry->id` (keeps wire identity in sync per the v32 constraint).  Dialog still pops via the legacy `MENUDIALOGFLAG_CLOSEONSELECT`. |
| **C.4 `mpChooseRandomStage`** | Layer A walk gated by `mpArenaIndexIsUsable` | Build a transient pool of unlocked arena stagenums via `assetCatalogIterateUnlockedByType(ASSET_ARENA, ...)`, EXCLUDING entries whose category is "Random" (they are meta entries, not target arenas) and EXCLUDING `ARENA_LOADMODE_CANVAS` if the caller is gameplay (campaign canvases under Grid only).  `rand() % count` into the pool. |
| **C.4 `mpChooseRandomMultiStage`** | Layer A bound 32 with `(i <= 12 \|\| i >= 27)` | Iterate via `assetCatalogIterateUnlockedByType(ASSET_ARENA, ...)`, KEEP entries whose category is not `"Solo Missions"` and not `"Random"` (i.e. Dark + Classic + Bonus).  This is the post-cull spirit of the original "non-Solo" filter.  `rand() % count`. |
| **C.4 `mpChooseRandomSoloStage`** | Layer A `(i >= 13 && i <= 26)` | Iterate, KEEP entries whose category is `"Solo Missions"`.  `rand() % count`. |
| **C.4 `mpChooseRandomGexStage`** | Layer A pre-cull GEX range, dead | RETIRE the function.  Remove from `setup.h` + `setup.c` + the dead `mpStartMatch` `STAGE_MP_RANDOM_GEX` branch.  No behavioural change (the function was already unreachable through normal UI). |

**Graceful fallback**: each of `mpChooseRandomStage` / `Multi` / `Solo` keeps its current "no arena available" sentinel (`STAGE_MP_SKEDAR` for Multi/Stage, `STAGE_DEFECTION` for Solo) per the heads I.6 / bodies J.6 graceful-fallback decision.

---

## Section G -- Migration sequencing (per consumer, surgical)

Each step is a self-contained, build-verifiable commit.  Sequenced so a failed step is bisectable.

**Step 1 -- Migrate `pdgui_menu_mpsetup.cpp::renderMpArena`** to read Layer B directly with unlock filter.

- Add a static `MpsetupArenaEntry { char id[64]; char display[64]; s32 stagenum; char category[32]; }` and `s_MpsetupArenaList[MAX_ARENA_COUNT]` + `s_MpsetupArenaListCount` + `s_MpsetupArenaListCountKnown` mirroring the heads / bodies pattern.
- Add a collector callback that appends entries via `assetCatalogIterateUnlockedByType(ASSET_ARENA, ...)`.  Mirror `catalogArenaCollect` shape from `pdgui_menu_room.cpp` -- use `arenaGetName(e->ext.arena.name_langid)` for display, fall back to `formatCatalogId` for langid-less mod arenas.
- Sort by section (MP_BASE / CAMPAIGN / MOD) then alphabetical; same `arenaCompare` shape as room.cpp.
- Cache invalidation triggered by `assetCatalogGetUnlockedCountByType(ASSET_ARENA)` delta + `IsWindowAppearing`.
- Selection commits write `g_MpSetup.stagenum = entry->stagenum` and `g_MpSetup.stage_id` synced via `catalogIdByRuntime(ASSET_ARENA, ...)` or directly from the entry.
- Drop the four `list_*(mpArenaMenuHandler, ...)` calls.  Keep the dialog's `MENUDIALOGFLAG_CLOSEONSELECT` plumbing -- selection still calls `mp_CloseCurrentDialog()` directly.
- Build verify: `pd` + `pd-server` + `pd-tests`.

**Step 2 -- Migrate `mpChooseRandomStage` / `Multi` / `Solo` to Layer B; retire `Gex`.**

- Replace each function's Layer A loop with `assetCatalogIterateUnlockedByType(ASSET_ARENA, ...)` + a category-string match callback.
- Pool collector writes stagenum into a stack `u8 pool[MAX_ARENA_COUNT]` plus a counter; `rand() % count` picks the index.
- Retire `mpChooseRandomGexStage` (delete declaration + definition + the `STAGE_MP_RANDOM_GEX` branch in `mpStartMatch`).  This is dead post-cull and was already unreachable.
- Update the structural note at `setup.c:187-194` to reflect that the migration has landed.
- Build verify.

**Optional Step 3 (defer) -- Retire the legacy `mpArenaMenuHandler` carousel helpers.**

After Step 1, `mpArenaMenuHandler` and its three helpers (`arenaMapIndex` / `arenaCountVisible` / `arenaFindSelected`) have zero callers.  Per heads / bodies decision: leave alone.  Track here, do not remove this session.

**Verification after each step**: `source devtools/build-env.sh && ninja -C Build pd pd-server pd-tests` (full link) + `./Build/pd-tests` (77 cases / 770 assertions, all green).  Visual playtest after Step 1 to confirm the MP setup Arena picker renders the catalog-driven list (Mike's call).  Step 2 functionally invisible -- the four random-meta tokens still resolve, just via the catalog.

---

## Section H -- Cross-cuts

### H.1 Wire format

- `mpsetup.stage_id[64]` is the catalog ID.  Stable, mod-aware, **no protocol bump**.
- The wire reads at `netmsg.c:4881, 5511-5514` and `matchsetup.c:94, 686-688` already use catalog ID -> stagenum resolution.  **No B-235 sibling bug present** (the lookups all pass through `assetCatalogResolve` which is type-correct).
- The wire identity carries from leader to follower; followers honor the chosen arena without re-validating unlock (status quo per H.6 / heads I.4).

### H.2 Save format

- `mpsetup.stage_id` is runtime; the on-disk save format stores the legacy `stagenum` byte in `mpsetupfile.bytes[]` (compressed).  See `mpsetupfileGetOverview` at `src/game/mplayer/mplayer.c:4474-4493`.
- Save files reference arenas by stagenum.  When loaded, the stagenum is resolved back to `stage_id` via `catalogIdByRuntime(ASSET_ARENA, ...)` at the next match start (`mpStartMatch` line 304).
- AllInOne cull (2026-04-26) bumped `MPSETUP_VERSION 1 -> 2` precisely to handle save files referencing now-deleted arenas.  Saves with stale stagenums are migrated by the v1 -> v2 clamp rule.
- **No new save migration required** for this audit.

### H.3 Bot / botprofile data

- Arenas have no bot-profile coupling.  `g_BotProfiles[].body` is per-bot-archetype (per bodies J.5); arenas don't get baked into bot configs.
- **No bot-config interaction** for the arena migration.

### H.4 Default selection

- `mpInit` at `mplayer.c:830-840` resolves `"base:arena_mp_skedar"` to the default stagenum.  Already catalog-driven.  No change.

### H.5 Random meta arena tokens (`STAGE_MP_RANDOM*`)

- `STAGE_MP_RANDOM_MULTI 0x02` and `STAGE_MP_RANDOM_SOLO 0x03` are alive in `s_ArenaNames[45-46]` and surface via the Random group.  When the user picks one, the resolution at `mpStartMatch` line 287-292 calls the corresponding `mpChooseRandom*Stage`.
- `STAGE_MP_RANDOM_GEX 0x04` is no longer in `g_MpArenas[]` post-cull.  The token cannot be set through any post-cull UI path; only stale saves or buggy code could leak it.  Step 2 retires the resolver; the `mpStartMatch` branch is removed alongside.
- The catalog category `"Random"` therefore contains exactly two arenas (Multi + Solo).

### H.6 ARENA_LOADMODE_CANVAS interaction

- Solo Missions (Layer A 13-26) carry `ARENA_LOADMODE_CANVAS`.  Used by the Grid editor to suppress chr / AI / cutscene side-effects when flying around a campaign mission as a build canvas.
- The MP setup picker (C.3) does NOT differentiate by load_mode -- in MP a Solo Missions arena simply loads as a playable MP map (the canvas mode applies only when entered via Grid).
- `mpChooseRandomStage` / `Multi` / `Solo` -- the Random meta resolution in `mpStartMatch` -- runs in the MP path, so `ARENA_LOADMODE_CANVAS` is irrelevant there.  No filter needed.
- Grid's own random pickers (called from `pdgui_menu_mainmenu.cpp:4546-4550` Grid commit) call `mpChooseRandomMultiStage` / `SoloStage`.  Post-Step-2 these read Layer B with the right category filter and Grid handles canvas-mode dispatch downstream via `pdguiForgeStartSessionOnCanvas` based on the resolved entry's `load_mode`.

### H.7 Server build

- `assetCatalogIterateUnlockedByType(ASSET_ARENA, ...)` falls through to a zero-iteration no-op on `pd-server` (no entries registered).  Step 2's random functions then return their graceful-fallback sentinel (`STAGE_MP_SKEDAR` / `STAGE_DEFECTION`).  Server-side never invokes random meta resolution (clients send already-resolved `stage_id`), so the server-side behaviour is academic.
- `pdgui_menu_mpsetup.cpp` is `pd`-target only -- not built into `pd-server`.
- Server build: no observable change.

### H.8 Mod arenas

- Mod arenas enter via `assetcatalog_scanner.c::registerComponent` with `requirefeature = 0` and `category = "Mod"` (or whatever the manifest specifies).  After Step 1, mod arenas appear in the MP setup picker automatically.  After Step 2, mod arenas participate in random selection if their category does not put them in the Solo Missions group (mod authors who want their arena treated as a Solo Mission can set `category = "Solo Missions"` in the manifest).
- Forward-compatible with mods.

### H.9 The B-225 structural note

`src/game/mplayer/setup.c:179-194` carries the structural note: "the authoring contract is split across three tables (g_MpArenas client, g_MpArenas server in server_stubs.c, s_ArenaNames in assetcatalog_base.c). Any divergence leaks orphan entries into the UI. A future pass should consolidate playability into a data-driven probe..."

This audit + Step 1 / 2 migration is **the start of that consolidation**:
- After Step 1, the live UI selector reads Layer B directly -- the three-table divergence stops affecting the user-facing picker.
- After Step 2, random selection also reads Layer B directly -- same.
- The three tables still exist (Layer A `g_MpArenas` x2 + `s_ArenaNames` slug shadow) but are reduced to **catalog seed data** (`assetCatalogRegisterBaseGame` reads them once at startup).  Their role narrows to "stage-binding seed for the registration pass".  Future cleanup could collapse to a single declarative table, but that is orthogonal cleanup to the selector migration.

Update the structural note at Step 2 to reflect the progress.

---

## Section I -- Decisions (defaults from heads / bodies transfer)

| # | Heads / bodies decision | Arenas application |
|---|---|---|
| **I.1** | Catalog ID renames deferred. | **Same.**  Defer test-style slug renames (`test_arch` / `test_dest` / `test_lam`) -- functional, not blocking. |
| **I.2** | `g_HeadsAndBodies[]` kept intact. | **Equivalent: `g_MpArenas[]` kept intact.**  Migrate the consumers, not the Layer A seed data.  The structural note at `setup.c:187-194` already prescribes a future "data-driven probe" cleanup -- out of scope for this session. |
| **I.3** | Reuse the existing `assetCatalogIterateUnlockedByType` helper. | **Same** -- helper handles `ASSET_ARENA` natively (heads Step 1).  No new public API needed. |
| **I.4** | Wire / cross-client unlock validation = STATUS QUO. | **Same.**  Host authority for arena selection.  Followers honor the host's `stage_id`. |
| **I.5** | Drop `g_BotHeads` / `g_BotProfiles[].body` archetype filter. | **N/A** -- no bot-archetype coupling for arenas. |
| **I.6** | Graceful "no unlocked X" sentinel fallback. | **Same.**  `mpChooseRandomStage` / `Multi` -> `STAGE_MP_SKEDAR`.  `mpChooseRandomSoloStage` -> `STAGE_DEFECTION`.  Matches existing fallbacks. |
| **I.7** | Apply unlock filter inside the catalog pick helper. | **N/A directly** -- the migration adds the unlock filter at each iteration site.  The catalog itself doesn't have a `catalogPickRandomArenaIdByCategory` helper today; Step 2 inlines the pool-collect-then-pick pattern at each callsite (3 functions, small surface).  Could extract a helper if the inlining feels duplicative; defer judgment to Step 2. |
| **I.8** | Bundle into 4-6 commits. | **2 commits.**  Step 1 (mpsetup picker) + Step 2 (random meta resolvers + retire Gex).  Plus the audit commit (this file). |

### Arenas-specific items

- **`mpChooseRandomGexStage` retirement**: removing dead post-cull code.  Audit confirms the function is unreachable through any post-cull UI path.  Save-file route is also blocked by the v1 -> v2 weapon-cull save migration that bumps `MPSETUP_VERSION`.  **Decision: retire in Step 2.**
- **Category-based random filtering**: post-cull layer A bounds (71 / 32 / 61) are obsolete.  Replace with explicit catalog category match.  **Decision: use `e->category` string match in Step 2.**
- **Legacy `mpArenaMenuHandler` retirement**: defer per heads I.6 / bodies disposition.  Vestigial after Step 1, harmless.  **Decision: leave in place.**

---

## Section J -- Decisions confirmed (2026-04-26, default per `make-decisions-delegation` memory)

All decisions transfer from heads / bodies with the arenas-specific items above.

| # | Decision |
|---|---|
| **J.1** | Catalog ID renames for test-style slugs deferred (matches heads I.1 / bodies J.1). |
| **J.2** | `g_MpArenas[]` (client + server stub) and `s_ArenaNames[]` kept intact (matches heads I.2). |
| **J.3** | Reuse the existing `assetCatalogIterateUnlockedByType` + `assetCatalogGetUnlockedCountByType` helpers.  No new public API. |
| **J.4** | Wire / cross-client unlock validation = STATUS QUO (matches heads I.4). |
| **J.5** | `mpChooseRandomGexStage` retired in Step 2 alongside the other random-meta migrations.  Dead post-cull and unreachable through any UI path. |
| **J.6** | Random arena selection adopts the unlock filter via category-keyed catalog walk; graceful sentinels preserved (`STAGE_MP_SKEDAR` for Multi / Stage, `STAGE_DEFECTION` for Solo). |
| **J.7** | Legacy `mpArenaMenuHandler` carousel + helpers stay in place after Step 1; vestigial but harmless.  Removal is a follow-up cleanup. |
| **J.8** | 2 migration commits (Step 1 + Step 2), sequential, bisectable, build-verified per step.  Plus the audit commit (this file). |

## Phase 2 commit plan (3 commits including this audit)

1. **Audit** (this file) -- `context/audits/catalog-migration-maps-2026-04-26.md`.
2. **Step 1** -- migrate `pdgui_menu_mpsetup.cpp::renderMpArena` to Layer B with unlock filter.
3. **Step 2** -- migrate `mpChooseRandomStage` / `Multi` / `Solo` to Layer B with category filter; retire `mpChooseRandomGexStage` + `STAGE_MP_RANDOM_GEX` branch in `mpStartMatch`; update structural note in `setup.c`.

Optional Step 3 (legacy-handler retirement) deferred per Section I.

---

## Stop conditions encountered (none)

- Helper API for unlock-filtered iteration -- already shipped in heads Step 1.
- B-235 sibling for arenas on wire -- not present (lookups type-correct via `assetCatalogResolve`).
- Catalog ID conformance -- 47 / 47 (test-style slug names are functional, not blocking).
- Wire format -- catalog IDs already in place (since v32).
- Save format -- already migrated for the AllInOne cull (MPSETUP_VERSION 1 -> 2).
- Layer A arena consumers can be cleanly migrated -- no iteration order dependency.
- Server build -- type-generic iterator falls through cleanly on dedicated server.

## Surface area summary

- **Layer A tables**: 1 to migrate the consumers of (`g_MpArenas[47]`); 4 left intact (server stub + slug shadow + group def + group map).
- **Layer A direct iterations to migrate**: 5 sites (3 random functions + 1 mpsetup picker delegate + 1 retired Gex resolver).  9 RESOLUTION-only sites left alone.
- **Layer A accessor functions**: 7 (Section A.3) -- all stay; only the iteration / selector ones get bypassed.
- **Layer B accessors already in place**: 7 (Section B.2).  No new public API needed.
- **Selector consumers requiring migration**: 2 (C.3 mpsetup arena picker + C.4 random meta resolvers).
- **Cross-cut sites (RESOLUTION only, no iteration)**: 8 sites (C.6) -- listed for completeness, not migrated.
- **Decisions for Mike**: 0 (defaults transfer per `make-decisions-delegation` memory).
- **Suspected B-235 sibling bugs**: 0 (already correct on the wire).

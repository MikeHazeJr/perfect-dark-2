# Catalog Gate 3 Arenas DATA Migration -- Phase 1 Audit

> **Date**: 2026-05-02
> **Session**: catalog-gate3-arenas (worktree `claude/condescending-ellis-248824`, dev base `6d990f66`)
> **Predecessors**: weapons F1-F13 (S484+S591, dev `S484`); heads `a2ad421e` (2026-05-01); bodies `64af7e0c` + `47f837d5` close-out (2026-05-02 dev `6d990f66`).
> **Scope**: migrate ASSET_ARENA static-array data (`g_MpArenas[47]`, `s_ArenaNames[47]`, `s_ArenaGroupMap[5]`) to a Manager-owned pool backed by `base/arenas.pdbase`. Mirrors the validated heads + bodies template.
> **Stop conditions watched**: arena identity on the wire requires a protocol bump; save format incompatibility; conflicts with the Universality Sweep + Phase 3 ROM-once Pass B Slice 9 work (in flight on a parallel session).
> **Mike's directive (activation)**: arenas are ALREADY accessor-migrated -- `modmgr.c:2876-2878` pulls every field from `ext.arena`. Only the data move remains. Mirror heads I.1-I.7 unless arena-specific concerns surface.

---

## Why this audit is short

The selector pool migration shipped 2026-04-26 in [catalog-migration-maps-2026-04-26.md](catalog-migration-maps-2026-04-26.md). All arena selectors (Combat Sim arena picker, Grid arena picker, mp setup arena picker, random meta resolvers, debug Test Scenarios menu) read through `assetCatalogIterateUnlockedByType(ASSET_ARENA, ...)` which inherits the Universality Sweep B-303 enabled-filter (`entry->enabled` skip). The accessor chain (`modmgrGetArena` -> `s_CatalogArenas[]` cache -> `ext.arena` fields) is already catalog-fronted; `s_CatalogArenas[]` is rebuilt from `assetCatalogIterateByType(ASSET_ARENA, ...)` whenever `modmgrCatalogChanged()` fires.

This session is the DATA migration: move the static C arrays that seed catalog row registration (`g_MpArenas[]` + `s_ArenaNames[]` + `s_ArenaGroupMap[]`) into `base/arenas.pdbase` JSON, populate a manager pool, and retire the static arrays after parity verification.

---

## Section A. Comprehensive Layer A Audit (Arenas)

### A.1 Source-of-truth data tables

| Symbol | File:line | Notes |
|---|---|---|
| `struct mparena g_MpArenas[47]` (client) | [src/game/mplayer/setup.c:115-172](src/game/mplayer/setup.c:115) | 47 entries post-AllInOne / GEX cull (2026-04-26). Group layout: Dark (0-12), Solo Missions (13-26), Classic (27-31), Bonus (32-44), Random (45-46). Each row carries `{stagenum, requirefeature, name_langid}`. The `name_langid` field uses `(VERSION == VERSION_JPN_FINAL ? L_OPTIONS_JP : L_OPTIONS_NTSC)` ternaries for the 14 Solo Missions entries. |
| `struct mparena g_MpArenas[]` (server stub) | [port/src/server_stubs.c:113-149](port/src/server_stubs.c:113) | Mirror of the client table for `pd-server`. `requirefeature` and `name_langid` set to 0 (server has no langbank, no challenge save). Stagenum order MUST match the client table; pinned by audit comment at line 138. |
| `static const char *const s_ArenaNames[47]` | [port/src/assetcatalog_base.c:632-660](port/src/assetcatalog_base.c:632) | Slug shadow for catalog ID generation (`base:arena_<slug>`). NULL slot is the canonical "skip from registration" marker (none used post-cull -- all 47 are non-NULL). |
| `static const struct { s32 first; s32 count; const char *category; } s_ArenaGroupMap[5]` | [port/src/assetcatalog_base.c:662-671](port/src/assetcatalog_base.c:662) | Group definitions consumed at registration time only. Values: `{0,13,"Dark"}`, `{13,14,"Solo Missions"}`, `{27,5,"Classic"}`, `{32,13,"Bonus"}`, `{45,2,"Random"}`. Stored in `e->category` after registration. |
| `g_ArenaGroupDefs[7]` | [src/game/mplayer/setup.c:350](src/game/mplayer/setup.c:350) | Legacy carousel offsets (7 groups including the pre-cull "GoldenEye X" + "GoldenEye X Bonus" entries). Read only by `mpArenaMenuHandler` / `arenaMapIndex` / `arenaCountVisible` / `arenaFindSelected` -- vestigial after the 2026-04-26 selector-pool migration. |
| `extern struct mparena g_MpArenas[]` | [src/include/data.h:411](src/include/data.h:411), [port/src/assetcatalog_base.c:411](port/src/assetcatalog_base.c:411) | Public extern decls. |

### A.2 Field schema

`struct mparena` ([src/include/types.h:4175-4179](src/include/types.h:4175)):

```c
struct mparena {
    s16 stagenum;       /* logical stage ID (STAGE_MP_*, STAGE_*) */
    u8  requirefeature; /* MPFEATURE_CHR_* unlock gate, 0 = always available */
    u16 name;           /* langbank ID for display name */
};
```

Three fields per row, 47 rows. The catalog's `ext.arena` already mirrors all three plus a `load_mode` byte ([port/include/assetcatalog.h:223-238](port/include/assetcatalog.h:223)):

```c
struct {
    s32 stagenum;
    u8  requirefeature;
    s32 name_langid;
    u8  load_mode;   /* B-254: ARENA_LOADMODE_PLAYABLE (0) / CANVAS (1) */
} arena;
```

### A.3 Layer A read sites

#### A.3.1 Direct `g_MpArenas[]` reads outside catalog internals

ZERO. The structural note at [src/game/mplayer/setup.c:187-194](src/game/mplayer/setup.c:187) confirms the 2026-04-26 migration eliminated all live UI selector and random meta resolver reads. The grep:

```bash
grep -rn "g_MpArenas\[" --include="*.c" --include="*.cpp" --include="*.h"
```

returns 8 sites:
- 5 in `port/src/assetcatalog_base.c` (registration body, comments, group walk -- the migration target).
- 1 declaration `port/src/server_stubs.c:113` (server stub literal).
- 1 declaration `src/game/mplayer/setup.c:115` (client literal).
- 1 extern in `src/include/data.h` (header).
- 4 comments in `src/game/mplayer/setup.c` historical notes -- not reads.

There are no consumers reading `g_MpArenas[idx].field` outside `assetcatalog_base.c::assetCatalogRegisterBaseGame`. The single registration loop is the migration target.

#### A.3.2 Direct `s_ArenaNames[]` reads

| File:line | Pattern | Notes |
|---|---|---|
| [port/src/assetcatalog_base.c:632](port/src/assetcatalog_base.c:632) | declaration | Static const inside `assetCatalogRegisterBaseGame` |
| [port/src/assetcatalog_base.c:678](port/src/assetcatalog_base.c:678) | bounds check + sentinel test | `if (!s_ArenaNames[idx])` skip |
| [port/src/assetcatalog_base.c:685](port/src/assetcatalog_base.c:685) | sentinel test | `if (!s_ArenaNames[idx])` skip |
| [port/src/assetcatalog_base.c:689](port/src/assetcatalog_base.c:689) | slug read | `snprintf(idbuf, "base:arena_%s", s_ArenaNames[idx])` |

Only the registration loop. After F12 ships the loader, this loop reads from the manager pool's `slug` field instead.

#### A.3.3 `s_ArenaGroupMap[]` reads

| File:line | Pattern | Notes |
|---|---|---|
| [port/src/assetcatalog_base.c:665](port/src/assetcatalog_base.c:665) | declaration | Static const inside `assetCatalogRegisterBaseGame` |
| [port/src/assetcatalog_base.c:676-677](port/src/assetcatalog_base.c:676) | group walk bounds | `for (s32 j = 0; j < s_ArenaGroupMap[g].count; j++) { s32 idx = s_ArenaGroupMap[g].first + j; ... }` |
| [port/src/assetcatalog_base.c:703](port/src/assetcatalog_base.c:703) | category write | `strncpy(e->category, s_ArenaGroupMap[g].category, ...)` |
| [port/src/assetcatalog_base.c:728](port/src/assetcatalog_base.c:728) | log line | references `s_ArenaGroupMap[g].category` |

Only the registration loop. After F12 the loop reads from the manager pool's `category` field per arena.

#### A.3.4 `g_ArenaGroupDefs[]` reads (vestigial)

| File:line | Pattern | Notes |
|---|---|---|
| [src/game/mplayer/setup.c:350](src/game/mplayer/setup.c:350) | declaration | Static `g_ArenaGroupDefs[7]` |
| [src/game/mplayer/setup.c:379](src/game/mplayer/setup.c:379) | `arenaMapIndex` | group offset walk |
| [src/game/mplayer/setup.c:381](src/game/mplayer/setup.c:381) | `arenaMapIndex` | range iter |
| [src/game/mplayer/setup.c:404](src/game/mplayer/setup.c:404) | `arenaCountVisible` | group offset walk |
| [src/game/mplayer/setup.c:406](src/game/mplayer/setup.c:406) | `arenaCountVisible` | range iter |
| [src/game/mplayer/setup.c:427](src/game/mplayer/setup.c:427) | `arenaFindSelected` | group offset walk |
| [src/game/mplayer/setup.c:429](src/game/mplayer/setup.c:429) | `arenaFindSelected` | range iter |
| [src/game/mplayer/setup.c:458](src/game/mplayer/setup.c:458) | `mpArenaMenuHandler` | group header label |

All 8 sites are inside `mpArenaMenuHandler` and its 3 helper functions. The 2026-04-26 selector-pool migration replaced the live UI consumer (`pdgui_menu_mpsetup.cpp::renderMpArena`) with a catalog-driven path; `mpArenaMenuHandler` and helpers are no longer consulted by ImGui pickers. **Vestigial after this migration.** Tracked for a future "retire legacy MP carousel" cleanup session per heads I.6 / bodies disposition.

#### A.3.5 Catalog API reads of arena fields (the migration target)

The catalog row fields `ext.arena.{stagenum, requirefeature, name_langid, load_mode}` are read directly by:

| File:line | Caller | Reads |
|---|---|---|
| [port/src/modmgr.c:2876-2878](port/src/modmgr.c:2876) | `modmgrArenaCollectCb` | all three writable fields (stagenum, requirefeature, name) -- catalog-fronted Layer A bridge |
| [port/fast3d/pdgui_menu_room.cpp:374-375](port/fast3d/pdgui_menu_room.cpp:374) | `catalogArenaCollect` (CS room arena picker) | `stagenum`, `requirefeature`, `name_langid` |
| [port/fast3d/pdgui_menu_mainmenu.cpp:4395-4398](port/fast3d/pdgui_menu_mainmenu.cpp:4395) | `gridArenaCollect` (Forge / Grid arena picker) | `stagenum`, `name_langid`, `load_mode` |
| [port/fast3d/pdgui_menu_mpsetup.cpp:573,584](port/fast3d/pdgui_menu_mpsetup.cpp:573) | `mpsetupArenaCollect` | `stagenum`, `name_langid` |
| [port/fast3d/pdgui_bridge.c:974](port/fast3d/pdgui_bridge.c:974) | `s_resolveStageIdToStagenum` (lobby start bridge) | `stagenum` |
| [port/src/net/netmsg.c:4881,5514](port/src/net/netmsg.c:4881) | wire decode (lobby start, stage start) | `stagenum` |
| [port/src/net/matchsetup.c:95,688](port/src/net/matchsetup.c:95) | match start | `stagenum` |
| [src/game/mplayer/mplayer.c:835](src/game/mplayer/mplayer.c:835) | `mpInit` default arena | `stagenum` |
| [src/game/mplayer/setup.c:300](src/game/mplayer/setup.c:300) | `randomPoolCollect` (Random meta resolver) | `stagenum` |
| [src/game/spawnpool.c:1770,1818](src/game/spawnpool.c:1770) | offline smoke-test pool walk | various |
| [port/fast3d/pdgui_forge.cpp:87](port/fast3d/pdgui_forge.cpp:87) | comment | n/a |
| [src/include/game/forgemode.h:69](src/include/game/forgemode.h:69) | comment | n/a |

These are all already catalog-API consumers; no migration changes needed for them. The migration only changes the SOURCE that populates `ext.arena` fields (registration today reads `g_MpArenas[]`; after F12 reads from the manager pool sourced from `arenas.pdbase`).

#### A.3.6 Indirect access through `modmgrGetArena` accessor

`modmgrGetArena(idx)` returns `struct mparena *` from the `s_CatalogArenas[]` cache (modmgr.c:2947). Cache is populated by `modmgrArenaCollectCb` via `assetCatalogIterateByType(ASSET_ARENA, ...)`. 11 callsites across:

- `src/game/challenge.c:487-488` - `challengeForceUnlockSetup` (force-unlock by stagenum lookup)
- `src/game/mplayer/setup.c:206,431,461,471,2632,2643,2653,5524,5525` - `mpArenaIndexIsUsable`, `mpMenuTextSetupName`, `mpMenuTextArenaName`, legacy `mpArenaMenuHandler`
- `port/fast3d/pdgui_bridge.c:683` - `pdguiPauseGetStageName`

These are catalog-fronted (the cache is built from catalog rows). They do not read `g_MpArenas[]` or `s_ArenaNames[]` directly. **No migration changes for them.**

### A.4 Mutation sites

ZERO. Arenas are read-only data. No EYESPY-style mutator analogue (weapons), no `currentPlayerSetWeaponPos` analogue, no body-side modeldef cache. The only writes to `ext.arena.load_mode` happen at registration in `assetcatalog_base.c:725` for the Solo Missions group (CANVAS load mode); after registration, all fields are read-only.

### A.5 Indirect access through `stagenum` passers

`stagenum` is the wire / save / runtime stage identity. Consumers pass it through `catalogResolveStageByStagenum` / `catalogResolveStage` / `catalogStageIdByStagenum` resolvers, which return catalog ID strings. These are NOT Layer A reads of arena DATA -- they are identity-index passers. Not migrated; identity continues unchanged.

### A.6 Site-count summary

| Site class | Count | Files |
|---|---|---|
| Direct `g_MpArenas[idx].<field>` read inside the registration loop | 6 | 1 (`assetcatalog_base.c`) |
| Direct `s_ArenaNames[idx]` read inside the registration loop | 4 | 1 (`assetcatalog_base.c`) |
| Direct `s_ArenaGroupMap[g].<field>` read inside the registration loop | 5 | 1 (`assetcatalog_base.c`) |
| `g_ArenaGroupDefs[]` reads (vestigial, post-selector-pool-migration) | 8 | 1 (`setup.c`) |
| Direct `g_MpArenas[]` extern decls / comments / declarations | 4 | 4 (`data.h`, `assetcatalog_base.c`, `setup.c`, `server_stubs.c`) |
| Indirect `modmgrGetArena` consumers (catalog-fronted) | 11 | 3 (`challenge.c`, `setup.c`, `pdgui_bridge.c`) |
| `ext.arena.<field>` direct reads (catalog API) | 12+ | 8 (already migrated, no change) |

**Total in-scope migration surface: 15 explicit edits in `assetcatalog_base.c` (the single registration loop) + 1 init-order hookup in `main.c`.** All 11 indirect consumers and 12+ catalog-API consumers inherit through the manager + parity bridge.

Compare to bodies (9 explicit edits + 10 inherited tier-2 = 19 sites) and heads (9 explicit + 10 inherited = 19 sites). Arenas are about half the scope.

---

## Section B. Manager Schema

### B.1 Catalog row (additive only)

Existing `asset_entry.ext.arena` carries the four fields registration already populates. F7 adds `pdbase_path[128]` + `pdbase_offset` + `pdbase_size` scaffold fields mirroring heads / bodies F7. No schema break.

### B.2 Manager-served `arena_data_t` (typed payload)

```c
typedef struct arena_data {
    /* Identity */
    s16  arena_index;          /* runtime_index in catalog row, equal to slot in g_MpArenas[] / s_Arenas[] */
    char catalog_id[64];       /* "base:arena_mp_skedar" / "base:arena_test_lam" etc. */
    char slug[32];             /* "mp_skedar" / "test_lam" -- the slug component of catalog_id */
    char category[32];         /* "Dark" / "Solo Missions" / "Classic" / "Bonus" / "Random" */

    /* Arena fields (mirrors ext.arena) */
    s16  stagenum;             /* logical stage ID (STAGE_MP_*, STAGE_*) */
    u8   requirefeature;       /* MPFEATURE_CHR_* unlock gate */
    s32  name_langid;          /* langbank ID */
    u8   load_mode;            /* ARENA_LOADMODE_PLAYABLE (0) / CANVAS (1) */
} arena_data_t;
```

Approx 116 bytes per arena, 47 arenas total = ~5.5 KB pool overhead. Trivial.

The slug + category strings duplicate information that is already in the catalog ID and `e->category`, but having them on the typed payload makes the manager self-contained for diagnostic logging (`CATALOG.MGR.ARENA.LOAD: slug=mp_skedar category=Dark stagenum=...`).

### B.3 `arena_data_t` vs sibling typed payloads

| Field | head_data_t | body_data_t | arena_data_t | Why |
|---|---|---|---|---|
| identity (catalog_id) | yes | yes | yes | Diagnostic logging |
| identity (back-ref idx) | headnum | bodynum | arena_index | Pool slot back-ref |
| ismale | yes | yes | NO | Heads/bodies only |
| unk00_01 | yes (standalone head) | yes (integrated head) | NO | Heads/bodies only |
| filenum | yes (CHEAD_*) | yes (CBODY_*) | NO | Arenas reference stages, not models |
| modeldef cache | yes | yes | NO | Arenas have no modeldef |
| stagenum | NO | NO | yes | Arenas only |
| requirefeature | NO (lives on catalog row) | NO (lives on catalog row) | yes | For arenas this is a manager-pool field for parity with the catalog row at audit time |
| name_langid | NO (lives on catalog row) | NO (lives on catalog row) | yes | Same -- arenas have a langid carried in the typed payload for parity |
| load_mode | NO | NO | yes | Arenas only (B-254 Grid canvas vs playable) |
| slug | NO | NO | yes | Arenas only -- the slug used to build catalog_id |
| category | NO | NO | yes | Arenas only -- the group classification used by random pickers + sorting |

The arena pool is a self-contained mirror of every datum needed to reconstruct the catalog row. This makes F12 trivial: parse one record, populate one pool slot, registration in `assetcatalog_base.c` reads from the pool slot.

### B.4 Field-by-field migration map

| Legacy field | Manager-served field | Notes |
|---|---|---|
| `g_MpArenas[idx].stagenum` | `s_Arenas[idx].stagenum` | unchanged semantics |
| `g_MpArenas[idx].requirefeature` | `s_Arenas[idx].requirefeature` | unchanged |
| `g_MpArenas[idx].name` | `s_Arenas[idx].name_langid` | renamed for symmetry with `ext.arena.name_langid` |
| `s_ArenaNames[idx]` | `s_Arenas[idx].slug` | per-arena slug |
| `s_ArenaGroupMap[g].category` (resolved per group via `for (j ...)` walk) | `s_Arenas[idx].category` | flattened: each arena carries its own category string |
| `s_ArenaGroupMap[g].first / count` (group bounds) | implicit in per-arena category | no separate map; iteration filters by category string when needed |
| Legacy `g_ArenaGroupDefs[]` (vestigial) | retire in F13 cleanup or defer | post-selector-pool migration vestige |

Each registration-loop read becomes `s_Arenas[idx].X`. Same semantics, single chokepoint at the registration loop in `assetcatalog_base.c`.

---

## Section C. `arenas.pdbase` File Format

### C.1 Container

`base/arenas.pdbase` is a deflate-compressed archive analogous to `base/heads.pdbase` and `base/bodies.pdbase` (validated F11 format). At the root: a `manifest.json` listing arena records.

Discovery (existing F11 path extends): startup walks `base/*.pdbase`. The loader recognises an `arenas` array key in the top-level JSON object alongside the existing `weapons`, `heads`, `bodies` keys.

### C.2 Arena record schema

```jsonc
{
  "id": "base:arena_mp_skedar",   // catalog ID; slug-derived (must match arena_index slot)
  "arena_index": 0,                // slot in g_MpArenas / s_Arenas[] (== runtime_index)
  "slug": "mp_skedar",             // slug component of catalog_id (== name in s_ArenaNames[])
  "category": "Dark",              // s_ArenaGroupMap entry: "Dark" / "Solo Missions" / "Classic" / "Bonus" / "Random"
  "stagenum": "STAGE_MP_SKEDAR",   // symbolic stage enum, decoded via STAGE_* enum table at parse
  "requirefeature": 0,             // unlock gate (0 = always available)
  "name_langid": "L_MPMENU_119",   // symbolic langbank id, decoded via L_* enum table at parse
  "load_mode": "ARENA_LOADMODE_PLAYABLE"  // symbolic load_mode enum (PLAYABLE/CANVAS)
}
```

Path B (heads decision; carries over) symbolic enum names, decoded via existing `loaderPdbaseEnums` infrastructure at parse time.

### C.3 Loader parsing path

Mirrors heads / bodies F11/F12:

1. **Discovery (eager):** `loaderPdbaseScan` extends to read `base/arenas.pdbase` (or recognise an `arenas` array in any `*.pdbase`).
2. **Parse:** new `parseArena(jstream_t *s)` parallel to `parseHead` / `parseBody`. Reads the 8 fields into a stack-local `arena_data_t`, validates `arena_index`, writes to `s_ArenasPool[arena_index]`, increments `s_ArenasRegistered`.
3. **Build:** new `loaderPdbaseBuildArenaManager()` runs after catalog scan. Sets `s_ArenasLoaderActive = 1`.
4. **Read (hot path):** `catalogManagerGetArenaByIndex(idx)` is O(1) array lookup; fields read directly from the pool slot.

### C.4 Validation rules

- `arena_index` in [0, 47) and unique per `.pdbase` namespace.
- `id` must match the slug-derived form (`base:arena_<slug>` for base arenas; `<modid>:arena_<slug>` for mod arenas).
- `slug` must be non-empty and consist of `[a-z0-9_]+`.
- `category` must be one of the 5 known values; unknowns default to `""` and emit `LOADER.PDBASE.ARENA.FIELD_UNKNOWN:` log.
- `stagenum` must resolve against the STAGE_* enum table. Unknown -> PER-ELEMENT failure, arena loaded with `stagenum = 0` and warning.
- `name_langid` must resolve against the L_* enum table.
- `load_mode` must resolve against the ARENA_LOADMODE_* enum table; unknown defaults to PLAYABLE.

### C.5 Versioning

Reuses `manifest.json::pdbase_version` from F11. Arenas add an `arenas:` array key; the version field is unchanged.

### C.6 Scope clarification

This `arenas.pdbase` carries ONLY arena identity / classification / unlock data. **It does NOT carry stage scene file references** (bg / tile / pads / setup / mpsetup), which are ASSET_MODEL entries owned by the Universality Sweep Phase 2 Commit 1 (`assetCatalogRegisterStageSceneFiles`) and the Phase 3 Pass B Slice 9 conversion (`base:stage_<class>_<filenum>` slugs, FileProvider handles to `data/<romid>/stages/<stagenum>/<class>.bin`). The arenas migration leaves stage scene file plumbing untouched.

---

## Section D. Manager API Spec

### D.1 Public accessors ([port/include/catalog_mgr_arenas.h](../../port/include/catalog_mgr_arenas.h), to be created)

```c
/* Hot-path accessor. O(1).  Returns NULL on out-of-range.
 * Logs CATALOG.MGR.ARENA.MISS: on out-of-range positive. */
const arena_data_t *catalogManagerGetArenaByIndex(s32 arena_index);

/* String-id accessor.  For boundary use; not a hot path. */
const arena_data_t *catalogManagerGetArenaById(const char *catalog_id);

/* Iterator support. */
s32 catalogManagerArenaCount(void);
const arena_data_t *catalogManagerGetArenaAt(s32 iter_index);

/* Lookup by stagenum.  Returns the FIRST arena whose .stagenum matches
 * (Random meta arenas with stagenum == STAGE_MP_RANDOM_* are special;
 * callers wanting a specific category should use the iterator + filter). */
const arena_data_t *catalogManagerGetArenaByStagenum(s16 stagenum);

/* Lifecycle. */
void catalogManagerArenaInit(void);
void catalogManagerRegisterArena(const char *id, const arena_data_t *data);
void catalogManagerUnregisterArena(const char *id);
void catalogManagerArenaShutdown(void);
```

No modeldef cache (arenas have no model). No mutators (arenas are read-only). No random-gender pool helpers. Smaller API than heads (which had random-gender pickers) and bodies (which had modeldef + IsModeldefLoaded helpers).

### D.2 Pure layer ([port/include/catalog_mgr_arenas_pure.h](../../port/include/catalog_mgr_arenas_pure.h))

```c
#define CATALOG_MGR_ARENA_COUNT_PURE 47

s32 catalogMgrArenaIsInRangePure(s32 arena_index);

/* Category-string -> bitmask helper, mirrors src/game/mplayer/setup.c::categoryToMask
 * but lives in the pure layer so pd-tests can pin random-pool semantics
 * without dragging globals. Returns RNDMASK_* bit per category, or 0 for
 * unknown / "Random" (Random meta entries never participate in random picks). */
#define CATALOG_MGR_ARENA_RNDMASK_DARK         (1u << 0)
#define CATALOG_MGR_ARENA_RNDMASK_CLASSIC      (1u << 1)
#define CATALOG_MGR_ARENA_RNDMASK_BONUS        (1u << 2)
#define CATALOG_MGR_ARENA_RNDMASK_SOLOMISSIONS (1u << 3)

u32 catalogMgrArenaCategoryToMaskPure(const char *category);
```

The pure layer surfaces the random-pool category-mask helper that today lives privately in `setup.c`. Sharing it is opportunistic: the pure layer makes the predicate testable, and the live `chooseRandomFromCatalog` helper in `setup.c` can switch to `catalogMgrArenaCategoryToMaskPure` for symmetry. Open question I.7 below.

### D.3 Reference counting / retention

Arenas are bundled (same as heads / bodies / weapons). `ref_count = ASSET_REF_BUNDLED`. No eviction during runtime; manager pool is static at process lifetime.

### D.4 Logging

Hierarchical channels under `CATALOG.MGR.ARENA.*` and `LOADER.PDBASE.ARENA.*`:

- `CATALOG.MGR.ARENA.MISS:` -- out-of-range positive arena_index or unregistered catalog_id (rate-limited).
- `CATALOG.MGR.ARENA.OVERRIDE:` -- mod overlay replaces base entry.
- `CATALOG.MGR.ARENA.LOAD:` -- startup completion log (count loaded vs expected).
- `LOADER.PDBASE.ARENA.SCAN_FAIL:` -- JSON parse error or file open error, archive level.
- `LOADER.PDBASE.ARENA.RESOLVE_FAIL:` -- enum reference unresolvable, arena level (TOTAL failure).
- `LOADER.PDBASE.ARENA.FIELD_UNKNOWN:` -- unrecognised enum value (PER-ELEMENT).
- `LOADER.PDBASE.ARENA.OK:` -- end-of-load summary.

---

## Section E. Loader Integration

### E.1 Eager build phase (startup)

Sequence (mirrors heads / bodies F12):

```
1. catalogInit()                                   // existing
2. stageTableInit()                                // existing
3. assetCatalogInit()                              // existing
4. assetCatalogRegisterBaseGame()                  // existing, registers 47 ASSET_ARENA rows
                                                   //   Today reads g_MpArenas[]+s_ArenaNames+s_ArenaGroupMap.
                                                   //   F12+ reads from manager pool when active.
5. assetCatalogRegisterStageSceneFiles()           // existing (Universality Sweep Phase 2 Commit 1)
6. assetCatalogScanComponents() / ScanBotVariants  // existing
7. catalogManagerHeadInit() / BodyInit() /
   catalogManagerArenaInit()                       // NEW: parity-period populate from g_MpArenas[]
8. loaderPdbaseScan("base/")                       // existing, extended to read arenas section
9. loaderPdbaseBuildWeaponManager() /
   BuildHeadManager() / BuildBodyManager() /
   BuildArenaManager()                             // NEW: flips arenas active flag
10. catalogBuildRuntimeCaches()                    // existing
11. catalogLoadInit()                              // existing
```

### E.2 Lazy read phase (runtime)

Arenas are eagerly loaded. Lazy reads via `catalogManagerGetArenaByIndex` are O(1) array lookup against `s_Arenas[CATALOG_MGR_ARENA_COUNT]`. No I/O at read time.

### E.3 Failure granularity

Same as heads / bodies F11/F12:

- **TOTAL failure (arena excluded):** stagenum unresolvable, JSON parse error, arena_index out of range, duplicate arena_index within a namespace.
- **PER-ELEMENT failure (partial arena):** unknown category, unknown load_mode (defaults to PLAYABLE), unknown name_langid (zeroed). Arena loads with the unrecognised piece zeroed.

---

## Section F. Migration Plan (F1-F13 mirror, with skip steps)

Sequential commits. Each commit lands code + the pd-tests case that pins its invariant.

| # | Step | Commit description | Build verify | Test |
|---|---|---|---|---|
| F1 | Manager skeleton | New `port/include/catalog_mgr_arenas.h` + `_pure.h`, `port/src/catalog_mgr_arenas.c` + `_pure.c`. Public API + pure validators + parity-period bridge that populates from `g_MpArenas[]` + `s_ArenaNames[]` + `s_ArenaGroupMap[]` at init. main.c calls `catalogManagerArenaInit()` after `catalogManagerBodyInit()`. CMakeLists wires both source files into pd + pd-server, plus the pure file + test into pd-tests. | pd + pd-server + pd-tests | `tests/test_catalog_mgr_arenas_api.cpp` (API contract: count == 47, in-range / out-of-range / negative; category-mask predicate). |
| F2 | (skip) | Arenas are already accessor-migrated -- no `catalogGetArenaX` wrappers or live `g_MpArenas[]` consumers outside the registration loop. The 11 indirect consumers go through `modmgrGetArena()` which reads `s_CatalogArenas[]` (catalog-fronted). No re-routing required. | n/a | n/a |
| F3 | (skip) | Arenas have no modeldef cache. | n/a | n/a |
| F4 | (skip) | Arenas have no modeldef reset. | n/a | n/a |
| F5 | (skip) | No body.c-equivalent NULL pre-check. | n/a | n/a |
| F6 | (skip) | Arenas have no random-gender pool analogue. The random meta resolvers in `setup.c` (`mpChooseRandomStage` / `Multi` / `Solo`) already iterate the catalog via `assetCatalogIterateUnlockedByType(ASSET_ARENA, ...)` per the 2026-04-26 selector-pool migration. | n/a | n/a |
| F7 | Catalog row pdbase scaffold | Add `pdbase_path[128]` + `pdbase_offset` + `pdbase_size` to `ext.arena` (mirrors heads / bodies F7). Registration code keeps the fields zero until F11 binds them. | pd + pd-server | extends F1 test (`pdbase_path` zero-default). |
| F8 | (skip) | No mutators or shadow fields to drop. | n/a | n/a |
| F9 | Loader scaffold extension | `loader_pdbase.c::parseTopLevel` recognises `arenas` array key (alongside `weapons`, `heads`, `bodies`). Static `s_ArenasPool[CATALOG_MGR_ARENA_COUNT]` + `s_ArenasLoaderActive` + `s_ArenasRegistered` declarations. New public API: `loaderPdbaseArenasActive()`, `loaderPdbaseGetArena(idx)`, `loaderPdbaseGetArenasRegistered()`, `loaderPdbaseBuildArenaManager()`. Empty `arenas:` section yields zero records. | pd + pd-server + pd-tests | `tests/test_loader_pdbase_arenas_empty.cpp` (asserts empty arenas section parses cleanly, zero registrations, no errors). |
| F10 | (skip) | Heads / bodies / arenas all share the same loader scan (collapsed into F9). | n/a | n/a |
| F11 | Python extractor + base/arenas.pdbase | New `devtools/extract_arenas_pdbase.py` reads `src/game/mplayer/setup.c::g_MpArenas[]`, `port/src/assetcatalog_base.c::s_ArenaNames[]`, and `s_ArenaGroupMap[]`. Resolves `STAGE_*` + `L_*` + `ARENA_LOADMODE_*` symbols via `loader_pdbase_enums.c` parity check. Emits `base/arenas.pdbase` JSON. Re-runs are deterministic. | pd + pd-server + pd-tests | `tests/test_loader_pdbase_arenas_load.cpp` (parses real archive, asserts 47 records, per-arena `arena_index` round-trips, sample IDs `base:arena_mp_skedar` / `base:arena_test_lam` / `base:arena_mp_random_multi` resolve). |
| F12 | Loader implementation + manager pool routing + parity bridge | `loader_pdbase.c::parseArena` per-record parser. Manager `s_get(idx)` checks `loaderPdbaseArenasActive()` and copies the loader-owned record into the manager slot when active. Adds `loaderPdbaseRunParityCheckArenas()` startup self-test comparing 6 scalar fields per arena vs `g_MpArenas[]` + `s_ArenaNames[]` + `s_ArenaGroupMap[]`. Logs `LOADER.PDBASE.ARENA.PARITY_FAIL:` on mismatch. | pd + pd-server + pd-tests | `tests/test_loader_pdbase_arenas_parity.cpp` (asserts parity check passes for every arena). |
| F13 | Retire parity bridge + grep-guard test | After Mike's playtest confirms `LOADER.PDBASE.ARENA.OK: parity check PASS`, retire the parity bridge. Manager accessors return pool-backed pointers unconditionally. Registration in `assetcatalog_base.c` migrates from `g_MpArenas[idx]` + `s_ArenaNames[idx]` + `s_ArenaGroupMap[g]` to `catalogManagerGetArenaByIndex(idx)` reads. The static arrays (`g_MpArenas[]` client + server stub, `s_ArenaNames`, `s_ArenaGroupMap`, vestigial `g_ArenaGroupDefs`) become dead weight; can be retired in a Phase 3 follow-up that re-orders init so the loader populates the catalog directly. | pd + pd-server + pd-tests | `tests/test_no_g_mparenas_field_reads.cpp` (compile-time grep: `g_MpArenas[idx].field` patterns appear ONLY in `assetcatalog_base.c` registration loop and the static literal definitions). |

Each commit independently builds and passes pd-tests. F11-F13 are the data-move + retire-parity-bridge commits.

---

## Section G. Wire / Save Format Implications

### G.1 Wire format

Arena identity already crosses the wire as catalog ID string `mpsetup.stage_id` (per ENet protocol v32+, see `context/constraints.md`). Arena DATA never crosses the wire; clients have their own catalog + manager.

**Conclusion: No `NET_PROTOCOL_VER` bump required.**

### G.2 Save format

Arena identity in saves uses catalog ID strings (identity v2 + scenario_save migrated long ago, per the 2026-04-26 selector-pool migration). Arena DATA is not saved.

**Conclusion: No save format change required.**

### G.3 Mod manifest

`.pdmod` mods can already declare ASSET_ARENA entries via their `mod.json` (existing code path). The `.pdbase` loader treats `base/arenas.pdbase` identically to mod archives at the registration layer; only precedence (base first) and namespace (`base:` vs mod's `<id>:`) differ.

---

## Section H. Cross-cuts

### H.1 ARENA_LOADMODE_CANVAS preservation (B-254 invariant)

The Solo Missions group (arena_index 13-26) sets `ext.arena.load_mode = ARENA_LOADMODE_CANVAS` so Grid arena entries spawn with chr / AI / cutscene side-effects suppressed. Currently set inside `assetCatalogRegisterBaseGame` at [port/src/assetcatalog_base.c:723-725](port/src/assetcatalog_base.c:723):

```c
if (idx >= 13 && idx <= 26) {
    e->ext.arena.load_mode = ARENA_LOADMODE_CANVAS;
}
```

After F11, `arenas.pdbase` carries `load_mode` per arena (symbolic `ARENA_LOADMODE_CANVAS` for Solo Missions, `ARENA_LOADMODE_PLAYABLE` for everything else). The hardcoded `idx >= 13 && idx <= 26` check is replaced with a per-record JSON value. **Test pin**: assert all 14 Solo Missions entries carry `load_mode == ARENA_LOADMODE_CANVAS` after extractor run.

### H.2 Random meta arena handling

Arena_index 45-46 (`STAGE_MP_RANDOM_MULTI` / `STAGE_MP_RANDOM_SOLO`) are special: their `stagenum` is a TOKEN that triggers re-resolution in `mpStartMatch` to a real stagenum. Category is `"Random"`. The `randomPoolCollect` in `setup.c::categoryToMask` returns 0 for "Random" (preventing self-reference). After migration, the manager's pool retains both meta entries with category `"Random"`; the random pool helper `catalogMgrArenaCategoryToMaskPure` returns 0 for the same string. Predicate parity preserved.

### H.3 Universality Sweep + Phase 3 ROM-once Slice 9 boundary

The Universality Sweep Phase 2 Commit 1 (`assetCatalogRegisterStageSceneFiles`) and Phase 3 Pass B Slice 9 (`stage scene files on disk`) converted ASSET_MODEL entries with `base:stage_<class>_<filenum>` slugs from RomProvider to FileProvider for per-stage scene files (bg, tile, pads, setup, mpsetup). **Those entries are distinct from ASSET_ARENA**.

The arena migration:
- DOES touch `assetCatalogRegisterBaseGame` registration of ASSET_ARENA rows.
- DOES NOT touch ASSET_MODEL stage scene file registration (separate function `assetCatalogRegisterStageSceneFiles`).
- DOES NOT touch FileProvider plumbing for stage scene files (Phase 3 Slice 9 territory).

The two work streams are orthogonal at the file boundary: arenas migrates lines 612-735 of `assetcatalog_base.c`; Slice 9 migrates a different function in the same file (already shipped).

### H.4 Three-table consistency contract (the deep refactor opportunity)

The structural note at [src/game/mplayer/setup.c:187-194](src/game/mplayer/setup.c:187) prescribes "consolidate playability into a data-driven probe (e.g. at mpInit: walk g_MpArenas, probe each stage's required files via catalogResolveFile, cache a per-arena .available bit) so the three tables cannot drift."

The three tables:
1. `g_MpArenas[]` (client, in `setup.c:115`)
2. `g_MpArenas[]` (server stub, in `server_stubs.c:113`)
3. `s_ArenaNames[]` (slug shadow, in `assetcatalog_base.c:632`)

After F12, the manager pool is the SOLE SOURCE OF TRUTH for arena typed payload. After F13 the parity bridge is retired. The three legacy tables persist as catalog row registration seed -- they CANNOT be retired in this session because re-ordering registration to follow the loader scan is a larger init-order refactor.

**Decision (this session)**: F13 retires the parity bridge but preserves the three tables. A follow-up session re-orders init (`loaderPdbaseScan` BEFORE `assetCatalogRegisterBaseGame`'s arena pass) and retires the tables. That follow-up is also the natural home for the structural-note's "data-driven probe" -- per-arena `.available` bit set by walking `catalogResolveFile` for each stage's required files.

### H.5 Server build

`g_MpArenas[]` is a server-stub mirror (port/src/server_stubs.c:113). The manager pool init populates `s_Arenas[]` from this zero-init array on the server (same shape as heads / bodies on server). Server has no langbank, no challenge save, no model data; arena field reads return zeros. The dedicated server never invokes `catalogManagerGetArenaByIndex` (no UI; arena identity arrives over the wire as catalog ID strings and gets resolved via `assetCatalogResolve`).

### H.6 Mod loading

Mod-supplied arenas enter via `assetCatalogScanComponents()` and register into the catalog row layer. Today they do NOT have an `arena_data_t` payload -- only an `ext.arena` row. F12 doesn't change this; mods continue to surface through the catalog row layer + their `.pdmod` component-INI loader, which is orthogonal to `arenas.pdbase`. Per heads I.6 (carries over): mods don't ship `.pdbase` archives; they ship `.pdmod` archives that reference catalog IDs.

### H.7 Vestigial legacy carousel cleanup

`g_ArenaGroupDefs[7]` and `mpArenaMenuHandler` + `arenaMapIndex` + `arenaCountVisible` + `arenaFindSelected` are post-selector-pool-migration vestiges in `setup.c`. Live ImGui pickers do not consult them. **Out of scope this session** per heads I.6 / bodies disposition; tracked here for a future "retire legacy MP carousel" cleanup session.

### H.8 Swarm session arena selector compatibility

The parallel swarm session (S593e and follow-ups) uses the catalog arena selector for the Debug Test Scenarios menu's spawn arena pick. The path: `pdgui_menu_mainmenu.cpp::gridArenaCollect` -> `assetCatalogIterateUnlockedByType(ASSET_ARENA, ...)` -> reads `e->ext.arena.{stagenum, name_langid, load_mode}` -> stores `slug + stage_id` for the swarm bot launch. The S593e fix routed the stage_id through `catalogStageIdByStagenum(arena_entry.stagenum)` instead of `base:arena_*` directly because launch resolves through `catalogResolveStage` which expects `base:mp_*` (stage IDs, not arena IDs).

After arena migration:
- `ext.arena.stagenum` value is identical (parity check enforces).
- `ext.arena.name_langid` value is identical.
- `ext.arena.load_mode` value is identical.
- `e->category` value is identical.

The swarm session's arena selector contract is preserved bitwise. **No coordination change required.**

---

## Section I. Decisions (mirror heads I.1-I.7 + arena-specific items)

| # | Heads decision (carries over to arenas unless noted) | Arenas application |
|---|---|---|
| I.1 | `arena_data_t` mirrors arena-relevant fields. Bitfields not applicable (none in `struct mparena`). | Manager pool stores 8 fields + identity (slug, category, catalog_id, arena_index). Drops nothing -- arenas have no head/body bitfield-equivalents. |
| I.2 | Manager pool sized to full arena range so `arena_index` indexes directly. | `CATALOG_MGR_ARENA_COUNT = 47` (post-AllInOne cull). |
| I.3 | `ext.arena` gains `pdbase_path[128]` + `pdbase_offset` + `pdbase_size`. | Same. |
| I.4 | F4 splits `catalogResetAllModeldefs`. | N/A (arenas have no modeldef cache). |
| I.5 | Pure layer split (`*_pure.c` + `*_pure.h`) so pd-tests stays globals-free. | Same. |
| I.6 | One archive per asset class (`base/heads.pdbase`, `base/bodies.pdbase`). Mods ship `.pdmod` not `.pdbase`. | Same: `base/arenas.pdbase`. |
| I.7 | Logging channel taxonomy `CATALOG.MGR.<TYPE>.{MISS,OVERRIDE,MUTATE,LOAD}` + `LOADER.PDBASE.<TYPE>.{SCAN_FAIL,RESOLVE_FAIL,FIELD_UNKNOWN,OK}`. | Same. (Mutate channel reserved; arenas have no mutators.) |

### Arena-specific items

| # | Item | Decision |
|---|---|---|
| Ia.1 | Pure-layer category-mask helper (`catalogMgrArenaCategoryToMaskPure`) shared with `setup.c::categoryToMask`. | Ship the helper in the pure layer. The live consumer in `setup.c::randomPoolCollect` can switch to the pure helper for symmetry, but it is not required -- the inline version is identical. **Decision: ship the helper, leave the live consumer's switch as a follow-up.** |
| Ia.2 | Three-table cleanup (`g_MpArenas[]` x2 + `s_ArenaNames` + `s_ArenaGroupMap` + vestigial `g_ArenaGroupDefs`). | Defer. F13 retires the parity bridge but keeps the static arrays as registration seed. A follow-up session re-orders init and finishes the cleanup. **Decision: defer. Track in Section J.1.** |
| Ia.3 | F11-F13 in this session vs split across sessions. | Single-session F1-F13. Arena schema is small (8 fields × 47 records), extractor is short (~150 lines vs ~500 for bodies / ~1300 for weapons), parity check is trivial (6 scalar fields per arena). **Decision: single session.** |
| Ia.4 | `arena_data_t` field set: include `slug` + `category` strings, or compute on demand from catalog ID + group iteration? | Include. The pool record is self-contained, mirrors the bodies pattern (which carries `catalog_id` for diagnostic logging), and saves an `assetCatalogResolve` call when the consumer wants the slug. ~64 bytes per arena, 47 arenas, ~3 KB total -- trivial. **Decision: include.** |
| Ia.5 | F11 extractor: read from `g_MpArenas[]` source code (line-by-line parse) or from a separate manifest? | Parse `g_MpArenas[]` source code, mirroring `extract_heads_pdbase.py` and `extract_bodies_pdbase.py`. `s_ArenaNames` and `s_ArenaGroupMap` are also in source code (`assetcatalog_base.c`). The extractor reads three source files, joins on arena_index. **Decision: source-code parse. Re-runs are deterministic given same source bytes.** |
| Ia.6 | Universality Sweep + Phase 3 Slice 9 conflict surface. | None. Section H.3 boundary. ASSET_ARENA (this session) and ASSET_MODEL stage scene files (Slice 9) are distinct catalog types touched in different functions of `assetcatalog_base.c`. **Decision: proceed in parallel. Surface immediately if grep shows conflict in shared lines.** |

No items require Mike's call. Proceeding with mirrored + arena-specific decisions.

---

## Section J. Arena-specific concerns surfaced

### J.1 F13 cannot fully retire `g_MpArenas[]` without an init-order refactor

`assetCatalogRegisterBaseGame` runs BEFORE `loaderPdbaseScan` in main.c. The arena registration loop at lines 691-705 reads `g_MpArenas[idx]` + `s_ArenaNames[idx]` + `s_ArenaGroupMap[g]`. If F13 wants to retire those static arrays, registration would need to read from the manager pool, which means moving `loaderPdbaseScan` + `loaderPdbaseBuildArenaManager` BEFORE `assetCatalogRegisterBaseGame`.

This is a non-trivial change because:
1. The loader currently runs after `assetCatalogRegisterBaseGame` completes, allowing it to populate manager pools that mirror catalog rows.
2. Re-ordering would require the loader to populate the manager BEFORE catalog rows exist -- which means the manager's `s_get` cannot use `assetCatalogResolve` for parity-period fallback; the loader must be the sole authority.
3. The same re-order would cascade to heads + bodies (their registration also reads `g_HeadsAndBodies[]` + `g_MpHeads[]` + `g_MpBodies[]`).

**Decision**: F13 retires the parity bridge for arenas (manager accessors return pool-backed pointers unconditionally) but keeps the three legacy tables alive as registration seed. A future follow-up session does the init-order refactor for ALL three asset classes (heads, bodies, arenas) simultaneously, and ALSO implements the structural-note's "data-driven probe" (per-arena `.available` bit). Filed in Section H.4.

### J.2 Vestigial `g_ArenaGroupDefs[]` cleanup window

`g_ArenaGroupDefs[7]` in `setup.c` and the legacy carousel handlers (`mpArenaMenuHandler`, `arenaMapIndex`, `arenaCountVisible`, `arenaFindSelected`) are not consulted by live UI. The 2026-04-26 selector-pool migration left them in place per heads I.6 / bodies disposition.

The arena migration does NOT touch them either. **Decision: defer to the same future session that does the three-table cleanup (J.1).** Removing the legacy carousel handlers and the 7-entry group def table together is cleaner than piecewise.

### J.3 The 5-group vs 7-group asymmetry

`s_ArenaGroupMap[5]` (in `assetcatalog_base.c`) is the post-cull canonical groups. `g_ArenaGroupDefs[7]` (in `setup.c`) carries pre-cull groups including the deleted GoldenEye X entries. The 7-entry table has stale offsets and is unreliable; live code paths bypass it via the catalog category strings (`assetCatalogIterateByType` + filter on `e->category`).

After F12, `arena_data_t.category` is the authoritative classification. After F13 (and the J.1 follow-up cleanup) the 7-entry table is gone.

**No correctness issue today.** Selector code reads `e->category` not `g_ArenaGroupDefs[]`. Tracked here so the future cleanup session knows which table to delete and why.

### J.4 Universality Sweep B-303 (`entry->enabled` filter) inheritance

The 2026-05-01 Universality Sweep added `if (!entry->enabled) continue;` to `assetCatalogIterateByType`. The unlock variant `assetCatalogIterateUnlockedByType` inherits the filter. Mod-disabled entries (via Mod Manager) no longer appear in selectors.

The arena manager pool is INDEPENDENT of `entry->enabled` -- the pool stores the typed payload, not the enabled state. Consumers that need enabled-filter semantics use `assetCatalogIterateByType(ASSET_ARENA, ...)` which goes through the catalog row layer (where `entry->enabled` lives). The manager iterator (`catalogManagerGetArenaAt(iter)`) returns ALL slots; consumers wanting the enabled subset filter via `assetCatalogResolve(arena_data->catalog_id)->enabled`.

This matches the heads / bodies pattern. **Decision: manager iterator is unfiltered; consumers filter via catalog row when needed.** The two existing arena selectors (`gridArenaCollect`, `mpsetupArenaCollect`) consume the catalog API directly, so they automatically respect the enabled filter.

### J.5 Phase 3 ROM-once Pass B Slice 9 surface

Pass B Slice 9 (stage scene files on disk) shipped at dev `87bc959a` + `0983b47c`. It converts ASSET_MODEL entries `base:stage_<class>_<filenum>` from RomProvider to FileProvider. Touched function: `assetCatalogRegisterStageSceneFiles` in `assetcatalog_base.c`.

The arena migration touches `assetCatalogRegisterBaseGame` arena registration loop in the SAME file, lines 612-735. Different functions, different lines, no overlap. **No conflict.**

---

## Section K. Stop conditions checked

- ASSET_ARENA catalog row schema change is additive (`pdbase_path`, `pdbase_offset`, `pdbase_size`); no break to mod loading or wire format.
- Wire format: arena identity already crosses as catalog ID string (since v32). No bump.
- Save format: arena identity already migrated to catalog IDs. No change.
- ARENA_LOADMODE_CANVAS invariant preserved by routing `ext.arena.load_mode` through the manager pool (test pin in F11).
- `entry->enabled` filter inheritance preserved by leaving catalog API consumers untouched.
- B-303 enabled filter is in `assetCatalogIterateByType`; arena manager iterator is unfiltered (per J.4 decision).
- Universality Sweep + Phase 3 Slice 9 boundary respected (different functions in same file, no shared lines).
- Three-table cleanup deferred (J.1) -- F13 retires parity bridge only, follow-up session does init-order refactor.
- Swarm session compatibility: arena selector contract preserved bitwise (J in H.8).

Phase 2 begins with F1 (manager skeleton). Sequential auto-merge per slice per `feedback_auto_merge_by_default`.

---

## Phase 2 commit plan (effective steps after skips)

1. **F1** -- manager skeleton (`catalog_mgr_arenas.{h,c}` + `_pure.{h,c}`) + parity-period bridge + main.c init order + CMakeLists wiring + `tests/test_catalog_mgr_arenas_api.cpp` API contract pin
2. **F7** -- `ext.arena` gains pdbase_path / pdbase_offset / pdbase_size scaffold fields
3. **F9** -- loader scaffold extension: `parseTopLevel` recognises `arenas` array key + new public API (`loaderPdbaseArenasActive` / `loaderPdbaseGetArena` / `loaderPdbaseBuildArenaManager`) + `tests/test_loader_pdbase_arenas_empty.cpp`
4. **F11** -- `devtools/extract_arenas_pdbase.py` + `base/arenas.pdbase` (47 arena records) + `tests/test_loader_pdbase_arenas_load.cpp`
5. **F12** -- `parseArena` implementation + manager pool routing via `s_get` + parity check + `tests/test_loader_pdbase_arenas_parity.cpp`
6. **F13** -- retire parity bridge (manager pool-backed unconditionally) + `tests/test_no_g_mparenas_field_reads.cpp` grep guard

6 effective commits. Sequential auto-merge per `feedback_auto_merge_by_default`.

Build verification per commit: queued `devtools\build-session.ps1` invocation (queued build only per standing rules).

**Phase 1 audit complete. Phase 2 begins next.**

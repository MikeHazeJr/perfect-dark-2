# Catalog Gate 3 -- Character Heads Data Migration (Phase 1 Audit)

> **Date**: 2026-05-01
> **Worktree**: `claude/catalog-gate3-heads-0501`
> **Phase**: 1 of 3 (audit). Phase 2 = F1-F13 migration commits, Phase 3 = verify + grep-guard.
> **Scope**: HEAD DATA only. Move struct headorbody fields for HEAD slots from `g_HeadsAndBodies[]` static array to a Manager-owned pool plus `base/heads.pdbase` archive. Bodies + Arenas migrate next (separate sessions, sequentially per `phase5-sequential`).
> **Companion docs**: [pillars/catalog.md](../pillars/catalog.md); [designs/catalog/catalog-full-pipeline-weapons.md](../designs/catalog/catalog-full-pipeline-weapons.md) (validated F1-F13 template); [audits/catalog-universality-sweep-2026-05-01.md](catalog-universality-sweep-2026-05-01.md) (B-303 iterator-enabled-filter shipped, my selectors inherit).
> **Mike's directive (activation message)**: catalog as single source of truth for head DATA, mirror weapons F11-F13 pattern, preserve integrated-head awareness (S593g body.c warning gate must stay).
> **Project memory anchors**: `catalog-architecture-future-decisions`, `all-heads-in-catalog`, `catalog-builds-all-selector-pools`, `phase5-sequential`, `human-readable-catalog-ids`.

---

## Why this audit is short

The selector pool migration (heads-side UI consumers) shipped 2026-04-26 in [catalog-migration-heads-2026-04-26.md](catalog-migration-heads-2026-04-26.md). All 5 head selector categories (Agent Creator / Player Config / Bot Setup / Room body picker / Random AI bot pick) now route through `assetCatalogIterateUnlockedByType(ASSET_HEAD, ...)`. The Universality Sweep (2026-05-01) added an `entry->enabled` filter to the iterator; my selectors inherit that filter automatically. **No selector work required this session.**

This session is the DATA migration: move struct headorbody fields for HEAD slots out of the legacy static array and into Manager-owned storage backed by `base/heads.pdbase`, mirroring the weapons F11-F13 template.

---

## Section A. Comprehensive Layer A Audit (Heads)

### A.1 Source-of-truth data tables

| Symbol | File:line | Notes |
|---|---|---|
| `struct headorbody g_HeadsAndBodies[152]` | [src/game/modeldata/robot.c:64](../../src/game/modeldata/robot.c:64) | Mixed bodies+heads. Head slots have `unk00_01==1` (standalone-head-model flag). Heads occupy 0x04..0x55 contiguously plus a few scattered. Non-head entries (bodies) at 0x00-0x03 and 0x56-0x96. **Bodies session retires the body half; this session retires only the head half.** |
| `struct mphead g_MpHeads[76]` | [src/game/mplayer/mplayer.c:2101](../../src/game/mplayer/mplayer.c:2101) | MP-eligible head subset. Each entry: `s16 headnum + u8 requirefeature`. `headnum` indexes into `g_HeadsAndBodies[]`. The catalog already mirrors all 76 entries as ASSET_HEAD rows; this table itself is read by 1 site outside catalog internals (the random-gender pool resolution in `mpDefaultHeadForBody`). |
| `struct mphead g_MpBeauHeads[]` | [src/game/mplayer/mplayer.c:2092](../../src/game/mplayer/mplayer.c:2092) | Photographic / "Perfect Head" pool. Used only by the legacy MENUITEMTYPE_CAROUSEL handler (`mpGetBeauHeadId` / `mpGetNumBeauHeads` accessors at mplayer.c:2913, 2918). New ImGui pickers do not consume Beau heads. **Out of scope; tracked for a future "retire legacy carousel" session.** |
| `u32 g_MpMaleHeads[]` / `u32 g_MpFemaleHeads[]` | [src/game/mplayer/mplayer.c:2281, 2330](../../src/game/mplayer/mplayer.c:2281) | Random-gender resolution pools used by `mpDefaultHeadForBody` for HEAD_RANDOM_GENDER bodies. Consumed at mplayer.c:3000-3002. Two sites total. **In scope as the only Layer A HEAD reads outside the catalog API.** |
| `extern struct headorbody g_HeadsAndBodies[152]` | [src/include/data.h:390](../../src/include/data.h:390) | Single extern decl. |
| `struct mphead g_MpHeads[76]` server stub | [port/src/server_stubs.c](../../port/src/server_stubs.c) | Zero-init stub for pd-server (no ROM model data). The migration retains the stub. |

### A.2 Field schema

`struct headorbody` ([src/include/types.h:3109-3120](../../src/include/types.h:3109)):

```c
struct headorbody {
    u16 ismale : 1;          /* sex flag (BodyChooseHead, chraction) */
    u16 unk00_01 : 1;        /* HEAD entry: standalone head model = 1; BODY entry: body has integrated head = 1 */
    u16 canvaryheight : 1;   /* used by body height variation (bodies side) */
    u16 type : 3;            /* HEADBODYTYPE_* (DEFAULT, FEMALE, FEMALEGUARD, MAIAN, CASS, MRBLONDE) */
    u16 height : 8;          /* head/body height -- player vv_headheight integration site */
    u16 filenum;             /* ROM file ID for the model (CHEAD* for heads, C* for bodies) */
    f32 scale;               /* model scale */
    f32 animscale;           /* per-body animation rate scaling */
    struct modeldef *modeldef;  /* lazy-loaded modeldef cache (heads + bodies share semantics) */
    u16 handfilenum;         /* hand model filenum (bodies-side; zero for HEAD entries) */
};
```

Heads-only fields: `ismale`, `unk00_01`, `type`, `height`, `filenum`, `scale`, `animscale`, `modeldef`. (`canvaryheight` and `handfilenum` belong to bodies.)

`struct mphead` ([src/include/types.h:3162-3165](../../src/include/types.h:3162)):

```c
struct mphead {
    s16 headnum;          /* g_HeadsAndBodies[] index */
    u8  requirefeature;   /* MPFEATURE_CHR_* unlock gate */
};
```

The catalog's existing `ext.head` already mirrors `headnum + requirefeature + rig_class` ([port/include/assetcatalog.h:273-281](../../port/include/assetcatalog.h:273)). No schema change needed there.

### A.3 Layer A read sites for HEAD field data

The catalog API is already the chokepoint. **Eight catalog accessors are the only sites that read `g_HeadsAndBodies[].field` for HEAD entries.** All callers outside the catalog API go through these eight functions.

#### Catalog accessors (the migration target -- internal `g_HeadsAndBodies[]` reads):

| Function | File:line | Field read | Direct or routed |
|---|---|---|---|
| `catalogGetHeadIsMale(headnum)` | [port/src/assetcatalog_api.c:1356-1359](../../port/src/assetcatalog_api.c:1356) | `ismale` | direct read |
| `catalogGetHeadType(headnum)` | [port/src/assetcatalog_api.c:1362-1365](../../port/src/assetcatalog_api.c:1362) | `type` | direct read |
| `catalogGetHeadHeight(headnum)` | [port/src/assetcatalog_api.c:1368-1371](../../port/src/assetcatalog_api.c:1368) | `height` | direct read |
| `catalogGetHeadModeldef(headnum)` | [port/src/assetcatalog_api.c:1442-1455](../../port/src/assetcatalog_api.c:1442) | `modeldef` (lazy-load + cache) | mutates `g_HeadsAndBodies[].modeldef` |
| `catalogGetHeadModeldefChecked(headnum, out)` | [port/src/assetcatalog_api.c:1655-1683](../../port/src/assetcatalog_api.c:1655) | sentinel via `filenum` then `modeldef` | wraps modeldef call |
| `catalogResetHeadModeldef(headnum)` | [port/src/assetcatalog_api.c:1466-1469](../../port/src/assetcatalog_api.c:1466) | writes `modeldef = NULL` | mutator |
| `catalogResetAllModeldefs()` | [port/src/assetcatalog_api.c:1474-1478](../../port/src/assetcatalog_api.c:1474) | walks until `filenum == 0`, writes `modeldef = NULL` | mutator (called from `bodiesReset`) |
| `catalogGetHeadFilenumByIndex(headnum)` | [port/src/assetcatalog_api.c:1104-1124](../../port/src/assetcatalog_api.c:1104) | DEPRECATED -- routes through catalog entry's `source_filenum`, NOT `g_HeadsAndBodies[].filenum` | catalog-routed |
| `catalogGetHeadHandle(headnum)` | [port/src/assetcatalog_api.c:1248-1268](../../port/src/assetcatalog_api.c:1248) | catalog entry's source handle | catalog-routed |

`catalogGetHeadFilenumByIndex` and `catalogGetHeadHandle` already route through the catalog entry, not through `g_HeadsAndBodies[]`. They do not need migration.

The other six accessors all read `g_HeadsAndBodies[headnum].field` directly. These are the migration target.

#### Tier-2 reads (callers of the catalog accessors):

| File:line | Caller | Accessor used |
|---|---|---|
| [src/game/body.c:837, 841, 847, 870, 913-922](../../src/game/body.c:837) | `body0f02d338` head/body compatibility checks | `catalogGetHeadType` |
| [src/game/chraction.c:3836](../../src/game/chraction.c:3836) | chr animation gating | `catalogGetHeadIsMale` |
| [src/game/player.c:2781, 2795](../../src/game/player.c:2781) | head modeldef for hand offset attach | `catalogGetHeadModeldef` |
| [src/game/player.c:2839](../../src/game/player.c:2839) | `vv_headheight` integration | `catalogGetHeadHeight` |
| [src/game/player.c:2844-2845](../../src/game/player.c:2844) | Mr Blonde max-headheight clamp | `catalogGetHeadHeight` |
| [src/game/body.c:276](../../src/game/body.c:276) | NULL-checked head modeldef preload | `catalogGetHeadModeldefChecked` |
| [src/game/mplayer/setup.c:2382, 2396](../../src/game/mplayer/setup.c:2382) | legacy carousel preview filenum | `catalogGetHeadFilenumByIndex` (catalog-routed) |
| [port/src/net/netmanifest.c:1980](../../port/src/net/netmanifest.c:1980) | manifest head modeldef preload | `catalogGetHeadModeldef` |

10 callers across 5 files. After F2 migrates the accessor bodies, all 10 inherit automatically.

#### Direct `g_HeadsAndBodies[]` reads outside the catalog API:

| File:line | Pattern | Notes |
|---|---|---|
| [src/game/body.c:184, 188](../../src/game/body.c:184) | `bodynum >= ARRAYCOUNT(g_HeadsAndBodies)` | bounds check only -- no field read |
| [src/game/body.c:274-275](../../src/game/body.c:274) | `g_HeadsAndBodies[headnum].modeldef == NULL` | modeldef pre-load NULL check (read-only). The actual lookup uses `catalogGetHeadModeldefChecked`. |
| [src/game/mplayer/setup.c:2465](../../src/game/mplayer/setup.c:2465) | `bodyid >= ARRAYCOUNT(g_HeadsAndBodies)` | bounds check only |
| [src/game/training.c:2356-2391](../../src/game/training.c:2356) | bounds checks + comments | bounds check only |
| [src/game/modeldef.c:263](../../src/game/modeldef.c:263) | comment | no read |

Only one read-with-mutation site: `body.c:274-275` reads `g_HeadsAndBodies[headnum].modeldef` to decide if a pre-load is needed. After the migration, this NULL-check moves to a manager accessor (`catalogManagerHeadIsModeldefLoaded(headnum)`) or routes through the existing `catalogGetHeadModeldefChecked` which already hides the NULL check.

### A.4 Layer A read sites for HEAD identity tables (mphead, gender pools)

Outside catalog internals (which the universality sweep + my prior selector migration already cleaned):

| File:line | Symbol read | Use |
|---|---|---|
| [src/game/mplayer/mplayer.c:2913](../../src/game/mplayer/mplayer.c:2913) | `g_MpBeauHeads[headnum].headnum` | legacy carousel `mpGetBeauHeadId` |
| [src/game/mplayer/mplayer.c:2918](../../src/game/mplayer/mplayer.c:2918) | `ARRAYCOUNT(g_MpBeauHeads)` | legacy carousel `mpGetNumBeauHeads` |
| [src/game/mplayer/mplayer.c:3000](../../src/game/mplayer/mplayer.c:3000) | `g_MpMaleHeads[rngRandom() % ARRAYCOUNT(g_MpMaleHeads)]` | random-gender body resolution |
| [src/game/mplayer/mplayer.c:3002](../../src/game/mplayer/mplayer.c:3002) | `g_MpFemaleHeads[rngRandom() % ARRAYCOUNT(g_MpFemaleHeads)]` | same |
| [port/src/assetcatalog_api.c:455](../../port/src/assetcatalog_api.c:455) | `g_MpHeads[i].headnum` | catalog cache build (legitimate) |
| [port/src/assetcatalog_base.c:583, 594, 599, 604](../../port/src/assetcatalog_base.c:583) | `g_MpHeads[mpidx].{headnum, requirefeature}` | catalog registration (legitimate) |

**Decision sites for this migration:**
- `g_MpBeauHeads[]` reads (2 sites): vestigial; kept until the legacy carousel handler retires entirely. Out of scope.
- `g_MpMaleHeads[]` / `g_MpFemaleHeads[]` reads (2 sites): used by `mpDefaultHeadForBody` for HEAD_RANDOM_GENDER bodies. **In scope -- migrate to manager.**

### A.5 Mutation sites

Heads have ZERO data-mutation sites outside the catalog API. The only writes to head fields are:

1. `g_HeadsAndBodies[].modeldef = <ptr>` (lazy load, in `catalogGetHeadModeldef`)
2. `g_HeadsAndBodies[].modeldef = NULL` (reset, in `catalogResetHeadModeldef` / `catalogResetAllModeldefs`)

Both are CACHE state, not data state. After the migration, the manager owns its own modeldef cache field per head_data_t entry; the legacy `g_HeadsAndBodies[].modeldef` stays alive only because bodies-side data still uses it.

No EYESPY-style data mutator. No `currentPlayerSetHeadPos` analogue. **Heads are read-only data + a lazy modeldef cache.**

### A.6 Indirect access through `headnum` passers

`headnum` is the wire / save / runtime head identity (`s32` or `u8`, same enum space as `g_HeadsAndBodies[]` index). It crosses every chr / mpchrconfig / bot config / wire / save boundary. These are NOT Layer A reads of head DATA -- they are identity-index passers that ultimately call `catalogGetHeadX(headnum)` or the matching wire / save resolver. **Not migrated; identity continues unchanged.**

Same shape as weapons audit Section A.5.

### A.7 Surprises and notes

- **All eight catalog accessors are pure pass-throughs to `g_HeadsAndBodies[]`.** This is much simpler than the weapons migration where 65 raw read sites scattered across 5 files. For heads, migrating the eight accessor bodies migrates everything.
- **Heads have no data mutators.** Compare weapons' EYESPY name swap, `currentPlayerSetWeaponPos`. No `catalogManagerHeadSet*` mutator API needed.
- **`g_HeadsAndBodies[]` cannot be retired this session.** The same array holds body data; bodies are a sibling migration. F13 for heads = manager owns the data, but the static array stays alive until bodies migration also retires it. This is an acceptable parity-period state matching the weapons F12 bridge.
- **The lazy `modeldef` cache is the only stateful field.** When the manager owns the head pool, the modeldef pointer lives on the manager's `head_data_t`, not on `g_HeadsAndBodies[]`. `bodiesReset` (which calls `catalogResetAllModeldefs`) gets a manager-side `catalogManagerHeadResetAllModeldefs` invocation alongside the bodies-side reset; both can coexist during the parity period.
- **Integrated-head invariant (S593g, body.c:413-414).** The warning gate `if (headnum >= 0 && headnum != HEAD_RANDOM_GENDER && !head_canon && !catalogGetBodyIsComplete(bodynum))` is bodies-side data (`catalogGetBodyIsComplete` reads body fields). Heads migration does not touch this gate. Bodies migration must preserve it. Tracked here so the bodies session has a continuity reminder.
- **`g_MpBeauHeads[]` (Beau / Perfect Head) is a vestigial photo-head pool.** Used only by legacy carousel. Out of scope. Tracked for a future "retire legacy MP carousel" session that also retires `mpGetBeauHeadId` / `mpGetNumBeauHeads`.
- **`g_MpMaleHeads[]` / `g_MpFemaleHeads[]` random-gender resolution pools.** Two sites in `mpDefaultHeadForBody`. Migrate to manager iteration of `s_Heads[]` filtered by `ismale`. Drops the static enum lists.

### A.8 Site-count summary

| Site class | Count | Files |
|---|---|---|
| Catalog accessor body that reads `g_HeadsAndBodies[].field` | 6 | 1 (`assetcatalog_api.c`) |
| Tier-2 caller of catalog accessor | 10 | 5 (body.c, chraction.c, player.c, setup.c, netmanifest.c) |
| Direct `g_HeadsAndBodies[]` HEAD-field read outside catalog | 1 (modeldef NULL pre-check) | 1 (body.c) |
| Bounds-check-only `ARRAYCOUNT(g_HeadsAndBodies)` | 4 | 3 (body.c, setup.c, training.c) |
| `g_MpMaleHeads[]` / `g_MpFemaleHeads[]` read | 2 | 1 (mplayer.c) |
| `g_MpBeauHeads[]` read (out of scope) | 2 | 1 (mplayer.c) |

**Total in-scope migration surface: 6 accessor bodies + 1 modeldef-precheck + 2 random-gender pool reads = 9 explicit edits.** The 10 tier-2 callers inherit through the accessor migration. Way below the > 50 phasing threshold; one bundled session ships the lot.

---

## Section B. Manager Schema

### B.1 Catalog row (no change)

Existing `asset_entry.ext.head` already covers MP-selector identity:

```c
struct {
    s16 headnum;            /* g_HeadsAndBodies[] index -- runtime_index */
    u8  requirefeature;     /* unlock gate */
    char rig_class[32];     /* body<->head pairing key */
} head;
```

`damage` / `fire_rate` / `ammo_type` style shadow fields (per weapons I.2) do not exist for heads. No row changes needed.

### B.2 Manager-served `head_data_t` (typed payload)

```c
typedef struct head_data {
    /* Field set mirrors the head-relevant subset of struct headorbody so
     * that a `struct headorbody *` and a `head_data_t *` can be aliased
     * by tier-2 callers without indirection changes.  After the
     * migration the typedef IS the struct headorbody for HEAD entries. */
    u8 ismale;            /* unpacked from bitfield */
    u8 unk00_01;          /* unpacked from bitfield (1 = standalone head model) */
    u8 type;              /* HEADBODYTYPE_* */
    u16 height;           /* unpacked from bitfield */
    u16 filenum;          /* CHEAD_* ROM file ID */
    f32 scale;
    f32 animscale;
    struct modeldef *modeldef;  /* lazy-loaded cache (manager owns this slot) */

    /* Manager-only fields (not in legacy struct headorbody): */
    char catalog_id[64];      /* for diagnostic logging */
    s16  headnum;             /* g_HeadsAndBodies[] index (back-ref) */
} head_data_t;
```

`canvaryheight` and `handfilenum` are bodies-side fields and are NOT part of `head_data_t`. They stay on `struct headorbody` for the bodies session to migrate.

The bitfields (`ismale:1`, `unk00_01:1`, `type:3`, `height:8`) are unpacked into byte/short fields in the manager. Removes legacy bit-packing and matches the weapons `weapon_data_t` shape. Costs ~5 bytes per head; 152 heads = ~760 bytes total. Trivial.

### B.3 Field-by-field migration map

| Legacy field | Manager-served field | Notes |
|---|---|---|
| `g_HeadsAndBodies[h].ismale` | `s_Heads[h].ismale` (manager pool) | unchanged semantics |
| `g_HeadsAndBodies[h].unk00_01` | `s_Heads[h].unk00_01` | semantic clarification: standalone-head-model flag for HEAD entries |
| `g_HeadsAndBodies[h].type` | `s_Heads[h].type` | HEADBODYTYPE_* |
| `g_HeadsAndBodies[h].height` | `s_Heads[h].height` | head height |
| `g_HeadsAndBodies[h].filenum` | `s_Heads[h].filenum` | CHEAD_* file ID |
| `g_HeadsAndBodies[h].scale` | `s_Heads[h].scale` | model scale |
| `g_HeadsAndBodies[h].animscale` | `s_Heads[h].animscale` | anim rate scaling |
| `g_HeadsAndBodies[h].modeldef` | `s_Heads[h].modeldef` | lazy-load cache, manager owns |
| `g_MpMaleHeads[]` | manager iteration of `s_Heads[]` filtered by `ismale && unk00_01` | retired array |
| `g_MpFemaleHeads[]` | manager iteration of `s_Heads[]` filtered by `!ismale && unk00_01` | retired array |

Each `catalogGetHeadX(headnum)` accessor becomes `s_Heads[headnum].X`. Same semantics, same chokepoints.

---

## Section C. `.pdbase` File Format (Heads-Specific Aspect)

### C.1 Container

`base/heads.pdbase` is a deflate-compressed archive analogous to `base/weapons.pdbase` (validated F11 format). At the root: a `manifest.json` listing head records. Same loader (`loaderPdbaseScan` + `loaderPdbaseBuildHeadManager`).

**Discovery** (existing F11 path extends):
- Startup walks `base/*.pdbase` (precedence 1: base game), then `mods/*.pdmod` (precedence 2: user overlays).
- Mod overlays can override base heads via `id` collision; precedence wins.

### C.2 Head record schema

```jsonc
{
  "id": "base:head_carrington",     // catalog ID (must match s_BaseHeads[mpidx].name)
  "headnum": 7,                     // g_HeadsAndBodies[] index (HEAD_CARRINGTON = 0x07)
  "ismale": 1,                      // 1 = male
  "unk00_01": 1,                    // 1 = standalone head model (always 1 for heads)
  "type": "HEADBODYTYPE_DEFAULT",   // symbolic, decoded via enum table at parse
  "height": 13,                     // head height (typically 13 for non-Maian, 27 for Maian)
  "filenum": "FILE_CHEADCARRINGTON", // symbolic CHEAD_* file ID, decoded via enum table
  "scale": 1.0,
  "animscale": 1.0
}
```

The `requirefeature` field already lives in the catalog row (`ext.head.requirefeature`). It does NOT belong in `heads.pdbase` -- it is selector-policy state, not head-data.

The `rig_class` field is computed at registration time from `type` ([assetcatalog_base.c:524](../../port/src/assetcatalog_base.c:524) `rigClassForHeadBodyType()`) and similarly stays in the catalog row. Not in the `.pdbase` schema.

### C.3 Loader parsing path

Mirrors weapons F11/F12:

1. **Discovery (eager):** `loaderPdbaseScan(base_dir)` already enumerates `base/*.pdbase`. Extending `loaderPdbaseScan` to recognize a `heads` array in `manifest.json` (alongside the existing `weapons` array) is one new branch in the JSON parser.
2. **Build (eager):** New `loaderPdbaseBuildHeadManager()` runs after catalog scan. Iterates `ASSET_HEAD` entries, loads each head's full `head_data_t` from `.pdbase`, registers with manager via `catalogManagerRegisterHead(id, &data)`. Missing model file or unresolved enum logs `LOADER.PDBASE.HEAD.RESOLVE_FAIL:` and the head is excluded.
3. **Read (lazy / hot path):** `catalogManagerGetHeadByIndex(headnum)` is O(1) array lookup against `head_data_t s_Heads[152]`.

### C.4 Validation rules

- `headnum` in [0, 152) and unique per `.pdbase` namespace.
- `id` must match the slug-derived form (`base:head_<slug>` for base heads; `<modid>:head_<slug>` for mod heads).
- `type` must resolve against the HEADBODYTYPE_* enum table. Unknown -> PER-ELEMENT failure, head loads with `type = HEADBODYTYPE_DEFAULT` and a `LOADER.PDBASE.HEAD.FIELD_UNKNOWN:` log.
- `filenum` must resolve against the FILE_C*_* enum table. Unknown -> PER-ELEMENT, `filenum = 0` and warning.
- `unk00_01` must be 0 or 1.

### C.5 Versioning

Reuses `manifest.json::pdbase_version` from F11. Heads add a `heads:` array; the version field is unchanged.

---

## Section D. Manager API Spec

### D.1 Public accessors ([port/include/catalog_mgr_heads.h](../../port/include/catalog_mgr_heads.h), to be created)

```c
/* Index-based hot-path accessor.  O(1).  Returns NULL on out-of-range
 * or unloaded entry; logs CATALOG.MGR.HEAD.MISS: on out-of-range positive,
 * silent on negative (matches weapons F2 parity policy). */
const head_data_t *catalogManagerGetHeadByIndex(s32 headnum);

/* String-id accessor.  For boundary use; not a hot path. */
const head_data_t *catalogManagerGetHeadById(const char *catalog_id);

/* Iterator over loaded head data (for selectors that need the typed payload). */
s32 catalogManagerHeadCount(void);
const head_data_t *catalogManagerGetHeadAt(s32 iter_index);

/* Modeldef cache management (mirrors catalogGetHeadModeldef + reset family). */
struct modeldef *catalogManagerGetHeadModeldef(s32 headnum);   /* lazy-load */
void catalogManagerResetHeadModeldef(s32 headnum);             /* nulls cache */
void catalogManagerResetAllHeadModeldefs(void);                /* called from bodiesReset alongside bodies-side reset */

/* Random-gender pool helpers.  Replaces g_MpMaleHeads / g_MpFemaleHeads. */
s32 catalogManagerHeadPickRandomMale(void);    /* returns headnum (g_HeadsAndBodies index) */
s32 catalogManagerHeadPickRandomFemale(void);  /* same */
```

Field-readers route through `catalogManagerGetHeadByIndex(headnum)` and field access on `head_data_t *`. The existing `catalogGetHeadX(headnum)` accessors continue to exist as thin wrappers (`return catalogManagerGetHeadByIndex(h)->X;`), preserving the chokepoint at `assetcatalog_api.c` so tier-2 callers (body.c, player.c, etc.) need NO source changes.

### D.2 Lifecycle

```c
void catalogManagerHeadInit(void);                                /* startup */
void catalogManagerRegisterHead(const char *id, const head_data_t *data);  /* loader + mod overlay */
void catalogManagerUnregisterHead(const char *id);                /* mod toggle off */
void catalogManagerHeadShutdown(void);                            /* shutdown */
```

### D.3 Reference counting / retention

Heads are bundled (same as weapons). `ref_count = ASSET_REF_BUNDLED`. No eviction during runtime; manager pool is static at process lifetime.

### D.4 Logging

Hierarchical channels under `CATALOG.MGR.HEAD.*` and `LOADER.PDBASE.HEAD.*`:

- `CATALOG.MGR.HEAD.MISS:` -- out-of-range positive headnum (rate-limited).
- `CATALOG.MGR.HEAD.OVERRIDE:` -- mod overlay replaced a base entry.
- `CATALOG.MGR.HEAD.LOAD:` -- startup completion log (count loaded vs expected).
- `LOADER.PDBASE.HEAD.SCAN_FAIL:` -- JSON parse error or file open error, archive level.
- `LOADER.PDBASE.HEAD.RESOLVE_FAIL:` -- model / enum reference unresolvable (TOTAL failure).
- `LOADER.PDBASE.HEAD.FIELD_UNKNOWN:` -- unrecognised field (PER-ELEMENT).
- `LOADER.PDBASE.HEAD.OK:` -- end-of-load summary.

### D.5 Pure layer

`port/src/catalog_mgr_heads_pure.c` -- pure validators (range check, enum decoders, schema-pin helpers). Compiled into `pd-tests` only. Mirrors `catalog_mgr_weapons_pure.c`.

---

## Section E. Loader Integration

### E.1 Eager build phase (startup)

Sequence:

```
1. assetCatalogInit()                              // existing
2. assetCatalogRegisterBaseGame()                  // existing, registers 76 ASSET_HEAD rows
3. loaderPdbaseScan("base/")                       // existing, extended to enumerate heads
   - parse heads array in manifest.json
   - register/generate runtime head payload records for all 152 g_HeadsAndBodies-derived rows
4. modmgrScanDirectory("mods/")                    // existing, walks mods/*.pdmod
5. catalogManagerHeadInit()                        // NEW
6. loaderPdbaseBuildHeadManager()                  // NEW
   - iterate ASSET_HEAD catalog rows
   - load head_data_t from .pdbase
   - register with manager
   - log CATALOG.MGR.HEAD.LOAD: count=N expected=N
7. catalogGetHeadX accessors now route through manager
```

Mirrors weapons F12 startup sequence. Adds `loaderPdbaseRunParityCheckHeads()` for the parity period (F12 ships with parity bridge to `g_HeadsAndBodies[]`; F13 retires the bridge).

### E.2 Lazy read phase (runtime)

Heads are eagerly loaded. Lazy reads via `catalogManagerGetHeadByIndex` are O(1). `modeldef` cache is lazy-loaded on first `catalogManagerGetHeadModeldef(headnum)` and held until `catalogManagerResetAllHeadModeldefs()` (called from `bodiesReset` at stage transition).

### E.3 Failure granularity

Same as weapons F11/F12:

- **TOTAL failure (head excluded):** model file unresolvable, JSON parse error, headnum out of range, duplicate headnum within a namespace.
- **PER-ELEMENT failure (partial head):** unknown enum value, unrecognized field. Head loads with the unrecognised piece zeroed.

---

## Section F. Migration Plan (Per Step, F1-F13 Mirror)

Sequential commits. Each commit lands code + the pd-tests case that pins its invariant. Same shape as weapons.

| # | Step | Commit description | Build verify | Test |
|---|---|---|---|---|
| F1 | Manager skeleton | New `port/include/catalog_mgr_heads.h` + `_pure.h`, `port/src/catalog_mgr_heads.c` + `_pure.c`. Public API + stub bodies that proxy to `g_HeadsAndBodies[].field` for parity. | pd + pd-server + pd-tests | `tests/test_catalog_mgr_heads_api.cpp` (API contract: index range, count, NULL on miss). |
| F2 | Catalog accessor migration | `assetcatalog_api.c::catalogGetHeadIsMale/Type/Height/HandFilenum/...` bodies changed to `return catalogManagerGetHeadByIndex(h)->X;`. The 10 tier-2 callers inherit. | pd + pd-server + pd-tests | `tests/test_head_accessors_route_to_manager.cpp` (asserts same value as legacy direct-read for valid range, NULL on out-of-range). |
| F3 | catalogGetHeadModeldef lazy-load migration | Lazy load + cache moved to manager. `g_HeadsAndBodies[].modeldef` field stays alive (bodies side) but heads-side accessors use `s_Heads[h].modeldef`. | pd + pd-server + pd-tests | `tests/test_head_modeldef_cache.cpp` (asserts repeated calls return cached pointer; reset clears it). |
| F4 | catalogResetHeadModeldef + catalogResetAllModeldefs migration | Reset functions split: heads-side resets `s_Heads[h].modeldef`, bodies-side stays on `g_HeadsAndBodies[h].modeldef` until bodies session migrates. `bodiesReset` calls both. | pd + pd-server + pd-tests | extends F3 test + `tests/test_head_modeldef_reset_split.cpp`. |
| F5 | body.c head modeldef NULL pre-check migration | `body.c:274-275` changes from `g_HeadsAndBodies[headnum].modeldef == NULL` to `catalogManagerHeadIsModeldefLoaded(headnum)`. New manager helper avoids exposing the cache pointer. | pd + pd-server | grep test confirms `g_HeadsAndBodies[` no longer appears in `body.c` HEAD context (bodies-side reads stay until bodies session). |
| F6 | g_MpMaleHeads / g_MpFemaleHeads migration | `mpDefaultHeadForBody` (mplayer.c:3000-3002) replaced with `catalogManagerHeadPickRandomMale()` / `catalogManagerHeadPickRandomFemale()`. Manager iterates `s_Heads[]` filtered by `ismale && unk00_01`. | pd + pd-server + pd-tests | `tests/test_random_gender_head_pool.cpp` (asserts pick is in expected set, both genders represented). |
| F7 | Head data fields ext.head shadow scaffold | Catalog row gains `pdbase_path` / `pdbase_offset` / `pdbase_size` scaffold fields (F11+ populates). `s_BaseHeads[]` table stays; rows continue to carry catalog identity. | pd + pd-server | extends F1 test. |
| F8 | (no I.1/I.2 equivalent) | Heads have no mutators or shadow fields to drop. Skip step. | n/a | n/a |
| F9 | Loader scaffold extension | `loader_pdbase.c` extended to recognize `heads` section in `manifest.json`. New API: `loaderPdbaseGetHead(headnum)` / `loaderPdbaseGetHeadCount()`. Empty `heads` section yields zero records (parity with `weapons` section). | pd + pd-server + pd-tests | `tests/test_loader_pdbase_heads_empty.cpp` (asserts empty heads section produces zero records, no errors). |
| F10 | Skip -- F9 covers loader scaffold | Heads use the same loader as weapons. | n/a | n/a |
| F11 | Python extractor + base/heads.pdbase | `devtools/extract_heads_pdbase.py` reads `src/game/modeldata/robot.c`, filters HEAD entries (`unk00_01==1`), resolves enum constants from `constants.h` + `files.h` + character-related headers, emits `base/heads.pdbase` JSON. Re-runs are deterministic. | pd + pd-server + pd-tests | extends F9 test with a real archive: `tests/test_loader_pdbase_heads_load.cpp` asserts each head's `headnum` field round-trips, count matches expected (~76 + ~30 SP heads). |
| F12 | Loader implementation + manager pool routing + parity bridge | `loader_pdbase.c::parseHead` per-record parser. Manager pool population from .pdbase. Parity-bridge gate: `loaderPdbaseHeadsActive()` returns 1 iff `base/heads.pdbase` parsed clean; manager accessors gate on this and fall back to `g_HeadsAndBodies[]` when inactive (parity period). New `loaderPdbaseRunParityCheckHeads` startup self-test compares 8 scalar fields per head against `g_HeadsAndBodies[headnum].X`; logs `LOADER.PDBASE.HEAD.PARITY_FAIL:` on mismatch. | pd + pd-server + pd-tests | `tests/test_loader_pdbase_heads_parity.cpp` (asserts parity check passes for every head). |
| F13 | Retire Layer A HEAD-side data + parity bridge | After Mike's playtest confirms `LOADER.PDBASE.HEAD.OK: parity check PASS`, retire the parity bridge. Manager accessors return pool-backed pointers unconditionally. `g_HeadsAndBodies[]` stays alive for bodies side; heads-side fields are now dead (the static array's HEAD entries are still data-source for parity check during bridge period; after F13 they're dead-weight). The static array literal in `robot.c` cannot be split (mixed bodies+heads); it stays as-is until bodies session retires the bodies half too. Grep-guard tests pin the absence of new direct `g_HeadsAndBodies[].field` reads outside the catalog API. | pd + pd-server + pd-tests | `tests/test_no_g_headsandbodies_field_reads.cpp` (compile-time greps; allowed sites: bounds checks + bodies-side reads). |

Each commit independently builds and passes pd-tests. F11-F13 are the "data move + retire" commits. After F13, head DATA is sole-sourced from the manager + `.pdbase`; the static `g_HeadsAndBodies[]` array still carries body data and is still allocated, but no head-field read goes through it.

---

## Section G. Wire / Save Format Implications

### G.1 Wire format

Head identity already crosses the wire as catalog session ref u16 (per `weapon identity uses catalog session refs u16` rule, applied symmetrically to heads). Head DATA never crosses the wire; clients have their own catalog + manager.

**Conclusion: No `NET_PROTOCOL_VER` bump required.**

### G.2 Save format

Head identity in saves uses catalog ID strings (identity v2 already migrated). Head DATA is not saved.

**Conclusion: No save format change required.**

### G.3 Mod manifest

`.pdmod` mods can already declare ASSET_HEAD entries via their `mod.json`. The `.pdbase` loader treats `base/heads.pdbase` identically to mod archives at the registration layer; only precedence (base first) and namespace (`base:` vs mod's `<id>:`) differ.

---

## Section H. Test Coverage

| Test case file | Invariant | Commit |
|---|---|---|
| `tests/test_catalog_mgr_heads_api.cpp` | API contract: index range, string id round-trip, NULL on miss, count == 152 (or whatever HEAD entries in g_HeadsAndBodies). | F1 |
| `tests/test_head_accessors_route_to_manager.cpp` | catalogGetHeadIsMale/Type/Height match catalogManagerGetHeadByIndex(h)->X for every valid headnum. | F2 |
| `tests/test_head_modeldef_cache.cpp` | Repeated calls return cached pointer; reset clears it. | F3 |
| `tests/test_head_modeldef_reset_split.cpp` | Heads-side reset doesn't clobber bodies-side modeldef cache and vice versa. | F4 |
| `tests/test_random_gender_head_pool.cpp` | Random-gender pickers return headnums from the expected gender pool. | F6 |
| `tests/test_loader_pdbase_heads_empty.cpp` | Empty heads section produces zero records, no errors. | F9 |
| `tests/test_loader_pdbase_heads_load.cpp` | base/heads.pdbase loads cleanly; expected count (~76 + SP); per-head headnum round-trips. | F11 |
| `tests/test_loader_pdbase_heads_parity.cpp` | Parity check passes (8 fields per head vs legacy g_HeadsAndBodies[]). | F12 |
| `tests/test_no_g_headsandbodies_field_reads.cpp` | Grep-guard: no new direct `.ismale / .type / .height / .filenum / .scale / .animscale / .modeldef / .unk00_01` reads outside the catalog API. | F13 |

---

## Section I. Decisions for Mike

### I.1 `head_data_t` shape: heads-only struct vs `struct headorbody` mirror

The weapons F11 chose to typedef `weapon_data_t = struct weapon` so existing `struct weapon *` chained reads in `bondgun.c::handweaponinfo.definition` continued to compile unchanged. Heads have NO equivalent cached-pointer chain pattern. Every head field read goes through a catalog accessor. Two options:

- **Option A: Heads-only struct.** `head_data_t` carries only `ismale, unk00_01, type, height, filenum, scale, animscale, modeldef`. Drops `canvaryheight` and `handfilenum` (bodies-side fields). Cleaner semantics; bitfields unpacked. ~5 bytes per head + manager fields.
- **Option B: Mirror `struct headorbody`.** `head_data_t = struct headorbody` typedef. Identical to legacy. Easier later if `g_HeadsAndBodies[]` is retired wholesale.

**My recommendation: Option A (heads-only).** No tier-2 chained-pointer pattern means no reason to mirror. Bitfield unpacking improves clarity. Bodies session can choose its own struct shape independently.

### I.2 Migrate `g_MpMaleHeads[]` / `g_MpFemaleHeads[]` this session?

These are two static enum lists feeding `mpDefaultHeadForBody` for HEAD_RANDOM_GENDER bodies. After the manager has the head pool, picking by gender is a one-line iteration filter. Two options:

- **Option A: Migrate.** New `catalogManagerHeadPickRandomMale/Female()` helpers iterate `s_Heads[]` filtered by `ismale && unk00_01`. The two static arrays retire. ~10 line code change.
- **Option B: Defer.** Keep the static arrays. Migrate them in the bodies session (where random-gender picking makes more sense semantically).

**My recommendation: Option A (migrate).** The pool predicate is heads-side data (`ismale`), so heads session is the natural home. Bodies session shouldn't carry heads-data concerns.

### I.3 `g_MpBeauHeads[]` (Beau / Perfect Head) handling

Two static reads in `mpGetBeauHeadId` / `mpGetNumBeauHeads`, used only by the legacy MENUITEMTYPE_CAROUSEL handler. ImGui pickers don't consume it.

**My recommendation: Out of scope.** Track for a future "retire legacy MP carousel handler" session that also retires the wrapper accessors. The Beau pool is small, vestigial, and has no live consumer in modern UI.

### I.4 `head_data_t.modeldef` ownership

The lazy modeldef cache currently lives on `g_HeadsAndBodies[h].modeldef`. After the migration, the manager pool's `s_Heads[h].modeldef` is the heads-side cache. Bodies-side `g_HeadsAndBodies[]` entries still carry `.modeldef` until bodies session migrates.

`bodiesReset` calls `catalogResetAllModeldefs` which walks `g_HeadsAndBodies[]`. After F4, that walk no longer affects head entries (their cache lives elsewhere). Two options:

- **Option A: Split-aware reset.** `bodiesReset` calls BOTH `catalogManagerResetAllHeadModeldefs()` AND `catalogResetAllModeldefs()`. Both run; first clears manager-side head caches, second clears legacy bodies-side caches.
- **Option B: Wrap.** `catalogResetAllModeldefs()` internally calls the manager reset first, then continues the legacy walk. Single call site at `bodiesReset`.

**My recommendation: Option B (wrap).** Single call site is less error-prone. Bodies session can later remove the legacy walk wrapper when it retires the bodies-side cache.

### I.5 Phase 2 scope: F1-F10 only or push through F13?

F1-F10 lands manager + accessor migration + scaffold while preserving `g_HeadsAndBodies[]` as the data source. F11-F13 retires legacy data and switches manager source to `.pdbase`.

The weapons F11-F13 was chosen to ship together (S591) because the data extractor was the heaviest part. For heads, the extractor is much simpler (~50 lines vs ~1300 lines for weapons; the field schema is tiny). Two options:

- **Option A: F1-F10 this session, F11-F13 follow-up.** Lower risk; manager pattern validates first.
- **Option B: F1-F13 this session.** Full migration in one shot. Mirrors weapons S591 cadence.

**My recommendation: Option B (F1-F13 this session).** Heads schema is small enough to extract + parity-check in a single session. The cumulative diff is much smaller than weapons. Splitting adds friction without reducing risk.

### I.6 `.pdbase` archive granularity: shared `base/weapons.pdbase` vs `base/heads.pdbase`

Mike's directive on per-asset-class extensions (per the rom-extraction-audit) suggests separate archives per asset class (`.pdwpn`, `.pdmesh`, etc. -- though those are per-FILE, not per-archive). For `.pdbase` content, two options:

- **Option A: Separate archives.** `base/weapons.pdbase`, `base/heads.pdbase`, `base/bodies.pdbase` (future), `base/arenas.pdbase` (future). Each archive owns its own asset class.
- **Option B: Single archive.** One `base/pd.pdbase` (or similar) holds all bundled base data with a section per asset class.

**My recommendation: Option A (separate archives).** Matches weapons F11 precedent; clean per-asset-class boundary; enables independent versioning per archive; mod overlays can target a specific asset class without touching others.

### I.7 SP-only / non-MP heads in heads.pdbase

The catalog registers ALL g_HeadsAndBodies head entries -- not just the 76 in g_MpHeads (per `all-heads-in-catalog`). The `.pdbase` archive must carry all of them so the manager pool covers every catalog row. Two options:

- **Option A: All HEAD entries in heads.pdbase.** Includes SP-only heads (`base:sp_head_<idx>` registrations). One archive entry per head row.
- **Option B: Only MP heads in heads.pdbase.** SP heads stay in `g_HeadsAndBodies[]` until bodies session.

**My recommendation: Option A (all HEAD entries).** Single source of truth for head data. SP heads are valid catalog rows; their data should live with the rest. The `id` namespacing handles the `base:head_*` vs `base:sp_head_*` distinction.

---

## Section J. Decisions Confirmed (2026-05-01, delegated by Mike via parent session)

All seven Section I items resolved per AI's recommendations.

| # | Decision |
|---|---|
| **I.1** | Heads-only `head_data_t` struct. Drop bodies-side fields (`canvaryheight`, `handfilenum`). Bitfields unpacked into byte/short fields. ~5 bytes per head; 152 heads = ~760 bytes total. |
| **I.2** | Migrate `g_MpMaleHeads[]` and `g_MpFemaleHeads[]` random-gender pools this session. New `catalogManagerHeadPickRandomMale/Female()` helpers iterate `s_Heads[]` filtered by `ismale && unk00_01`. Both static arrays retire. |
| **I.3** | Defer `g_MpBeauHeads[]` (Photo / Beau Head pool). Vestigial; no live ImGui consumer. Tracked here for a future "retire legacy MP carousel" session that also retires `mpGetBeauHeadId` / `mpGetNumBeauHeads`. |
| **I.4** | `catalogResetAllModeldefs()` calls manager-side reset first, then continues legacy walk for bodies-side. Single call site at `bodiesReset` keeps semantics intact. Bodies session removes the legacy walk wrapper when it retires the bodies-side cache. |
| **I.5** | F1-F13 in this session. Heads schema is small enough to extract + parity-check + retire in one shot. Mirrors weapons S591 cadence. |
| **I.6** | Separate `base/heads.pdbase` per asset class. Mirrors `base/weapons.pdbase` precedent. Enables independent versioning and clean per-asset-class boundary. |
| **I.7** | All HEAD entries (including `base:sp_head_*` fallback registrations) go into `heads.pdbase`. Single source of truth. |

Phase 2 plan:
1. F1 manager skeleton + pure layer + tests
2. F2 catalog accessor migration (6 functions reroute through manager)
3. F3 modeldef lazy-load migration
4. F4 modeldef reset split (heads-side + bodies-side)
5. F5 body.c NULL-precheck via new `catalogManagerHeadIsModeldefLoaded`
6. F6 random-gender pool migration; retire `g_MpMaleHeads` + `g_MpFemaleHeads`
7. F7 catalog row pdbase scaffold fields
8. F8 SKIP (no mutators / shadow fields to drop)
9. F9 loader scaffold extends to heads section
10. F10 SKIP (collapsed into F9)
11. F11 Python extractor + `base/heads.pdbase` archive
12. F12 loader implementation + manager pool routing + parity bridge
13. F13 retire parity bridge + grep-guard tests

---

## Stop conditions watched

- **> 50 read sites:** 9 explicit edits + 10 inherited tier-2 = 19 sites. **Phasing not needed.** Proceeding without phasing.
- **`.pdbase` format:** existing weapons F11 envelope reused; `heads:` section is a new array but the loader scaffold is shared.
- **Manager API decisions:** simpler than weapons (no mutators, no struct-pointer chain). API surface is 8 functions vs 12 for weapons.
- **Wire / save format incompatibility:** none.
- **Integrated-head invariant (S593g):** read-only; bodies-side data, untouched by this migration. Bodies session must preserve.
- **Random-gender pool:** new manager helper retires two static arrays in same session (per I.2).
- **`.pdbase` archive granularity:** separate `base/heads.pdbase` per I.6.

Phase 2 implementation gated on Mike's responses to I.1 through I.7.

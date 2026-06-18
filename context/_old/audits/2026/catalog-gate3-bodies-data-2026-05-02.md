# Catalog Gate 3 Bodies DATA Migration -- Phase 1 Audit

> **Date**: 2026-05-02
> **Session**: catalog-gate3-bodies-0501
> **Predecessors**: heads `a2ad421e` (2026-05-01); weapons F1-F13 `S484+S591` (2026-04-30).
> **Scope**: migrate body-relevant fields of `struct headorbody` for the BODY slots of `g_HeadsAndBodies[152]` from the static array to a Manager-owned pool backed by `base/bodies.pdbase`. Mirrors the validated heads template (`port/include/catalog_mgr_heads.h` + `port/src/catalog_mgr_heads.c` + `base/heads.pdbase` + `devtools/extract_heads_pdbase.py` + `tests/test_catalog_mgr_heads_api.cpp`). Heads decisions I.1-I.7 carry over unless body-specific concerns surface (Section J).
> **Stop conditions**: catalog row schema change incompatible with mod loading; integrated-head invariant compromised; B-275 hand-model registration broken; body modeldef cache loses thread-safety.

---

## A. Layer A inventory (where body data lives today)

### A.1 Source-of-truth data

| Symbol | File:line | Notes |
|---|---|---|
| `struct headorbody g_HeadsAndBodies[]` | [src/game/modeldata/robot.c:64](src/game/modeldata/robot.c:64) | 152 entries; each row has both head-eligible and body-eligible field interpretations. BODY slots have `unk00_01 == 0` for normal bodies, `unk00_01 == 1` for integrated-head bodies (Skedar, Dr Caroll, EyeSpy). HEAD slots have `unk00_01 == 1`. |
| `extern struct headorbody g_HeadsAndBodies[152]` | [src/include/data.h:390](src/include/data.h:390) | Public extern. |
| `struct headorbody { ismale:1, unk00_01:1, canvaryheight:1, type:3, height:8, filenum, scale, animscale, modeldef*, handfilenum }` | [src/include/types.h:3109-3120](src/include/types.h:3109) | 10 positional fields. Bodies use ALL of them (heads ignore `canvaryheight` and `handfilenum`). |
| `struct headorbody g_HeadsAndBodies[152]` server stub | [port/src/server_stubs.c:419](port/src/server_stubs.c:419) | Zero-initialised. The dedicated server has no character model data. |

### A.2 Body-side field accessors (Layer A read sites)

All currently in [port/src/assetcatalog_api.c](port/src/assetcatalog_api.c):

| Function | Line | Reads |
|---|---|---|
| `catalogGetBodyFilenumByIndex` | 1087 | `g_HeadsAndBodies[bodynum].filenum` (via catalog `source_filenum` indirection) |
| `catalogGetBodyScaleByIndex` | 1127 | `g_HeadsAndBodies[bodynum].scale` (via catalog `model_scale`) |
| `catalogGetBodyHandle` | 1227 | `g_HeadsAndBodies[bodynum].filenum` (via catalog `source.primary`) |
| `catalogGetBodyIsMale(bodynum)` | 1318 | `g_HeadsAndBodies[bodynum].ismale` |
| `catalogGetBodyType(bodynum)` | 1324 | `.type` |
| `catalogGetBodyHeight(bodynum)` | 1330 | `.height` |
| `catalogGetBodyAnimScale(bodynum)` | 1336 | `.animscale` |
| `catalogGetBodyCanVaryHeight(bodynum)` | 1342 | `.canvaryheight` (BODY-ONLY field) |
| `catalogGetBodyIsComplete(bodynum)` | 1348 | `.unk00_01` (integrated-head invariant; S593g body.c warning gate dependency) |
| `catalogGetBodyHandFilenum(bodynum)` | 1354 | `.handfilenum` (BODY-ONLY field; B-275 hand model registration consumer) |
| `catalogGetBodyModeldef(bodynum)` | 1435 | `.modeldef` lazy-load + cache (PD_SERVER excluded) |
| `catalogResetBodyModeldef(bodynum)` | 1461 | `.modeldef = NULL` |
| `catalogResetAllModeldefs()` | 1476 | walks `g_HeadsAndBodies[i].modeldef = NULL` for all i (heads section already routes through manager; bodies legacy walk remains) |
| `_Checked` variants `catalogGetBody{Scale,AnimScale,HandFilenum,Type,Height}Checked` | 1576-1638 | All read `.filenum` for the loud-fail sentinel and the requested field directly. |

### A.3 Body data registration (catalog row source_filenum + rig_class binding)

| Site | File:line | Reads |
|---|---|---|
| Base body registration loop | [port/src/assetcatalog_base.c:486-549](port/src/assetcatalog_base.c:486) | `g_MpBodies[idx].bodynum -> .filenum` for `source_filenum`, `.type` for `rig_class`. |
| SP body fallback registration | [port/src/assetcatalog_base.c:777-830](port/src/assetcatalog_base.c:777) | Walks `g_HeadsAndBodies[i]` skipping `.filenum == 0` sentinel and BODY_TESTCHR; entries with `.unk00_01 == 0` become `base:sp_body_<i>`. Reads `.filenum`, `.type`. |
| First-person hand model registration (B-275) | [port/src/assetcatalog_base_extended.c:737](port/src/assetcatalog_base_extended.c:737) | `g_HeadsAndBodies[i].handfilenum`; rejects `.filenum == 0 || handfilenum <= 0`. Registers each distinct hand filenum as `ASSET_MODEL` with RomProvider handle. **B-275 fix dependency.** |
| modelcatalog validation walk | [port/src/modelcatalog.c:381-460](port/src/modelcatalog.c:381) | Iterates `g_HeadsAndBodies[]` to validate model files at startup. Uses `.filenum` as sentinel and reads the whole struct. |

### A.4 Tier-2 body field consumers (through accessors)

`catalogGetBodyIsComplete` (integrated-head guard):
- [port/fast3d/pdgui_charpreview.c:230](port/fast3d/pdgui_charpreview.c:230) -- skip head load when integrated.
- [port/fast3d/pdgui_menu_agentcreate.cpp:305](port/fast3d/pdgui_menu_agentcreate.cpp:305) -- agent-create UI guard (B-296).
- [port/fast3d/pdgui_menu_botsetup.cpp:613](port/fast3d/pdgui_menu_botsetup.cpp:613) -- simulant bot UI guard (B-296).
- [port/fast3d/pdgui_menu_playerconfig.cpp:663](port/fast3d/pdgui_menu_playerconfig.cpp:663) -- player config UI guard (B-296).
- [port/fast3d/pdgui_menu_room.cpp:2733, 2760](port/fast3d/pdgui_menu_room.cpp:2733) -- Room "Change Character" modal guard (B-296).
- [src/game/body.c:255, 340, 364, 417, 544](src/game/body.c:255) -- body alloc head-bind logic + S593g warning gate.
- [src/game/menu.c:2142](src/game/menu.c:2142) -- legacy menu render character preview.
- [src/game/mplayer/setup.c:2469](src/game/mplayer/setup.c:2469) -- carousel head-disabled label.

`catalogGetBodyIsMale`:
- [src/game/body.c:456, 664](src/game/body.c:456) -- body alloc gender-specific assets (gloves / hands).
- [src/game/botmgr.c:167](src/game/botmgr.c:167) -- bot voice line gender selection.
- [src/game/chraction.c:5941, 6333](src/game/chraction.c:5941) -- chr animation gender selection.
- [src/game/mplayer/mplayer.c:2974](src/game/mplayer/mplayer.c:2974) -- `mpDefaultHeadForBody` random-gender fallback resolves through body's `ismale`.

`catalogGetBodyType`:
- [src/game/body.c:840, 844, 896, 915, 923](src/game/body.c:840) -- HEADBODYTYPE compatibility checks for head pairing.

`catalogGetBodyHeight`:
- [src/game/bot.c:1745](src/game/bot.c:1745) -- bot move speed scaling: `speed = catalogGetBodyHeight(chr->bodynum) * (1.0f / 159.0f)`.

`catalogGetBodyCanVaryHeight`:
- [src/game/body.c:298, 320](src/game/body.c:298) -- body alloc height variance branches; line 320 is Skedar-specific.

`catalogGetBodyHandFilenum`:
- [port/src/swarm_test.c:486](port/src/swarm_test.c:486) -- cited in comment; consumer is the Step F1 hand-model load chain that resolves through `bgun*` once swarm bots get first-person view.

`catalogGetBodyModeldef`:
- [src/game/body.c:167, 214](src/game/body.c:167) -- `bodyLoad`, `body0f02ce8c` lazy-load.
- [src/game/player.c:2751](src/game/player.c:2751) -- player respawn body model (SA-5f).
- [port/fast3d/pdgui_skin_uv.cpp:183](port/fast3d/pdgui_skin_uv.cpp:183) -- skin editor body model preview.
- [port/src/net/netmanifest.c:1978](port/src/net/netmanifest.c:1978) -- manifest body modeldef materialisation (PD_SERVER-excluded path).

`catalogResetAllModeldefs`:
- Body / stage transition reset paths in `bodiesReset`, `lvReset`, etc.

### A.5 Direct g_HeadsAndBodies BODY-side reads outside the catalog API

The post-heads-migration audit grep over the source tree returns ZERO direct `g_HeadsAndBodies[bodynum].<field>` reads outside [port/src/assetcatalog_api.c](port/src/assetcatalog_api.c) and the registration sites (Section A.3). Body data already routes through the catalog API; the bodies migration replaces the API's internal direct read with a manager call, identical to F2 on the heads side.

---

## B. Manager schema

### B.1 `body_data_t` (typed payload)

Mirrors all body-relevant fields of `struct headorbody`. Drops nothing -- bodies use the full field set including `canvaryheight` and `handfilenum`. Adds:

- `headnum` -- the back-reference into `g_HeadsAndBodies[]` (== runtime_index).
- `catalog_id[64]` -- the `base:body_*` / `base:sp_body_*` ID.
- `modeldef *` -- manager-owned lazy cache (replaces `g_HeadsAndBodies[bodynum].modeldef`).

```c
typedef struct body_data {
    /* Identity */
    s16  bodynum;             /* g_HeadsAndBodies[] index back-ref */
    char catalog_id[64];      /* "base:dark_combat" / "base:sp_body_67" */

    /* Body-relevant fields, bitfields unpacked */
    u8  ismale;               /* 0 = female, 1 = male */
    u8  unk00_01;             /* 1 = integrated-head body (Skedar/Dr Caroll/EyeSpy); 0 = normal body */
    u8  canvaryheight;        /* 1 = body height varies per chr (Skedar) */
    u8  type;                 /* HEADBODYTYPE_* */
    u16 height;               /* body height; bot.c speed scaling + B-275 first-person view height */
    u16 filenum;              /* CBODY_* ROM file ID */
    f32 scale;                /* body scale; chr->model->scale source */
    f32 animscale;            /* per-body animation rate scaling */
    u16 handfilenum;          /* first-person hand model file ID; B-275 dependency */

    /* Manager-owned lazy modeldef cache. */
    struct modeldef *modeldef;
} body_data_t;
```

Approx 32 bytes per body; 152 bodies = ~5 KB total pool overhead. Trivial.

### B.2 `body_data_t` vs `head_data_t` field divergence

| Field | head_data_t | body_data_t | Why |
|---|---|---|---|
| ismale | yes | yes | Both consult |
| unk00_01 | yes (1 = standalone head) | yes (1 = integrated-head body) | Same field, two semantics; body semantic is the integrated-head invariant |
| type | yes (HEADBODYTYPE_*) | yes (HEADBODYTYPE_*) | Both consult for rig class |
| height | yes | yes | Both consult |
| filenum | yes (CHEAD_*) | yes (CBODY_*) | Both consult |
| scale | yes | yes | Both consult |
| animscale | yes | yes | Both consult |
| modeldef* | yes (manager-owned) | yes (manager-owned) | Lazy cache lives on the manager pool slot |
| canvaryheight | NOT PRESENT | yes | BODY-ONLY: heads have no per-chr height variance |
| handfilenum | NOT PRESENT | yes | BODY-ONLY: heads have no first-person-hand pairing |

Heads dropped `canvaryheight` and `handfilenum` per heads decision I.1. Bodies must preserve both.

---

## C. `bodies.pdbase` schema

Mirrors `base/heads.pdbase` envelope:

```jsonc
{
  "pdbase_version": 1,
  "type": "bodies",
  "bodies": [
    {
      "id": "base:dark_combat",
      "bodynum": 92,
      "ismale": 0,
      "unk00_01": 0,
      "canvaryheight": 0,
      "type": "HEADBODYTYPE_FEMALE",
      "height": 13,
      "filenum": "FILE_CBODYDARK_COMBAT",
      "scale": 1.0,
      "animscale": 1.0,
      "handfilenum": "FILE_GHANDDARK_COMBAT"
    },
    ...
  ]
}
```

Field shape mirrors heads exactly with two additions (`canvaryheight`, `handfilenum`). Symbolic enum names per Path B (heads decision; carries over). Body slots with `filenum == 0` (the trailing sentinel) and `BODY_TESTCHR` (dev placeholder) are skipped; matches the SP body registration filter at `assetcatalog_base.c:780-786`.

---

## D. Manager API surface

### D.1 Public accessors (declared in `port/include/catalog_mgr_bodies.h`)

```c
const body_data_t *catalogManagerGetBodyByIndex(s32 bodynum);
const body_data_t *catalogManagerGetBodyById(const char *catalog_id);
s32 catalogManagerBodyCount(void);
const body_data_t *catalogManagerGetBodyAt(s32 iter_index);

struct modeldef *catalogManagerGetBodyModeldef(s32 bodynum);
s32 catalogManagerBodyIsModeldefLoaded(s32 bodynum);
void catalogManagerResetBodyModeldef(s32 bodynum);
void catalogManagerResetAllBodyModeldefs(void);

void catalogManagerBodyInit(void);
void catalogManagerRegisterBody(const char *id, const body_data_t *data);
void catalogManagerUnregisterBody(const char *id);
void catalogManagerBodyShutdown(void);
```

Symmetric with `catalog_mgr_heads.h` minus the random-gender pickers (which are heads-side; bodies don't pick random heads).

### D.2 Pure validators (declared in `port/include/catalog_mgr_bodies_pure.h`)

```c
#define CATALOG_MGR_BODY_COUNT_PURE 152

s32 catalogMgrBodyIsInRangePure(s32 bodynum);
s32 catalogMgrBodyIsIntegratedHeadPure(s32 unk00_01);
```

Pure layer mirrors heads pure layer. `IsInRangePure` is `[0, CATALOG_MGR_BODY_COUNT_PURE)` -- bodies have no `RANDOM_GENDER` sentinel (that's heads-only). `IsIntegratedHeadPure` returns 1 if `unk00_01 == 1`, 0 otherwise. The integrated-head invariant predicate lives in the pure layer so tests can pin it without dragging globals.

### D.3 Logging channels

- `CATALOG.MGR.BODY.MISS:` -- out-of-range index or unregistered id (rate-limited).
- `CATALOG.MGR.BODY.OVERRIDE:` -- mod overlay replaces base entry.
- `CATALOG.MGR.BODY.MUTATE:` -- explicit mutator called (none expected for bodies; reserved).
- `CATALOG.MGR.BODY.LOAD:` -- startup completion log.

Loader channels `LOADER.PDBASE.BODY.{SCAN_FAIL,RESOLVE_FAIL,FIELD_UNKNOWN,OK}` parallel the heads side.

---

## E. Loader integration

Mirrors `loader_pdbase.c` heads-section integration:

1. **Scan**: `loaderPdbaseScan` extends to load `bodies.pdbase` after `heads.pdbase`. Same `parseTopLevel` dispatch on `"bodies"` key.
2. **Parse**: new `parseBody(jstream_t *s)` parallel to `parseHead`. Reads the 10 fields into a stack-local `body_data_t`, validates `bodynum`, writes to `s_BodiesPool[bodynum]`, increments `s_BodiesRegistered`.
3. **Resolve**: `s_resolveHeadbodyType` reused; bodies share the HEADBODYTYPE_* enum.
4. **Build**: new `loaderPdbaseBuildBodyManager` parallel to `loaderPdbaseBuildHeadManager`. Sets `s_BodiesLoaderActive = 1`.
5. **Active check**: new `loaderPdbaseBodiesActive()` and `loaderPdbaseGetBody(idx)` accessors.
6. **Manager bridge**: `catalog_mgr_bodies.c::s_get` checks `loaderPdbaseBodiesActive()` and copies the loader-owned record into the manager pool slot (preserving the manager's modeldef cache pointer), exactly like the heads bridge in `catalog_mgr_heads.c::s_get`.

`port/src/main.c` extends the existing `loaderPdbaseScan("base", &pdb_result)` block:

```c
if (pdb_result.bodies_registered > 0) {
    loaderPdbaseBuildBodyManager();
}
```

---

## F. Migration sequencing (F1..F13)

Following the heads template:

| F | Scope |
|---|---|
| F1 | catalog_mgr_bodies.{h,c} + _pure.{h,c}; pool + parity-period bridge over g_HeadsAndBodies. main.c calls `catalogManagerBodyInit()` after `catalogManagerHeadInit()`. tests/test_catalog_mgr_bodies_api.cpp pure pin. CMakeLists hook for both pd targets and pd-tests. |
| F2 | assetcatalog_api.c: `catalogGetBodyIsMale/Type/Height/AnimScale/CanVaryHeight/IsComplete/HandFilenum` route through `catalogManagerGetBodyByIndex(bodynum)`. Heads counterparts already migrated; bodies F2 closes the head/body symmetry. |
| F3 | assetcatalog_api.c::catalogGetBodyModeldef -> catalogManagerGetBodyModeldef; catalogResetBodyModeldef -> catalogManagerResetBodyModeldef. The lazy cache slot moves from `g_HeadsAndBodies[].modeldef` to `s_Bodies[].modeldef`. |
| F4 | assetcatalog_api.c::catalogResetAllModeldefs drops the `for (i; g_HeadsAndBodies[i].filenum != 0; i++) g_HeadsAndBodies[i].modeldef = NULL;` legacy walk and gains `catalogManagerResetAllBodyModeldefs()` after the heads call. The legacy walk was already orphaned for HEAD slots after heads F4; bodies F4 retires it for BODY slots too. |
| F5 | body.c walkthrough: confirm zero direct g_HeadsAndBodies reads remain. The S593g warning gate at body.c:417 already routes through `catalogGetBodyIsComplete` which (after F2) routes through the manager. PRESERVE the gate; no source change required. |
| F6 | (No bodies analogue to heads F6 retirement of g_MpMaleHeads / g_MpFemaleHeads; bodies have no parallel static pool.) |
| F7 | port/include/assetcatalog.h: add `pdbase_path[128]`, `pdbase_offset`, `pdbase_size` to `ext.body` (mirrors the heads F7 ext.head fields). Registration code keeps the fields zero until F11 bind them. |
| F8 | (Reserved for unforeseen bodies-specific cleanup. Likely empty.) |
| F9 | loader_pdbase: add `s_BodiesPool[CATALOG_MGR_BODY_COUNT]` + `s_BodiesLoaderActive` + `s_BodiesRegistered`. New accessors `loaderPdbaseBodiesActive()` / `loaderPdbaseGetBody(idx)` / `loaderPdbaseGetBodiesRegistered()` / `loaderPdbaseBuildBodyManager()`. Public header in loader_pdbase.h. Scaffold returns NULL / 0 until F12. |
| F10 | (Reserved.) |
| F11 | base/bodies.pdbase: ~85 body records (~63 named base:body_* MP-eligible + ~22 SP fallback base:sp_body_*). Generated by devtools/extract_bodies_pdbase.py mirroring extract_heads_pdbase.py with body-side filtering (`unk00_01 == 0` for normal bodies + `unk00_01 == 1` body slots that aren't HEAD slots, distinguished by their `filenum` matching CBODY_* not CHEAD_*). |
| F12 | loader_pdbase.c: `parseBody` implementation + `s_resolveHeadbodyType` reused. `parseTopLevel` adds the `"bodies"` key dispatch. `loaderPdbaseScan` reads `base/bodies.pdbase` after `base/heads.pdbase` (single-call extension). `loaderPdbaseBuildBodyManager` sets active flag. main.c hook. catalog_mgr_bodies.c::s_get checks `loaderPdbaseBodiesActive()` and copies the loader record. |
| F13 | Field grep-guard test: pin that selectors / consumer files contain zero `g_HeadsAndBodies[bodynum].<field>` direct reads. Mirrors heads F13 test in `tests/test_catalog_mgr_heads_api.cpp`. |

---

## G. Tests

`tests/test_catalog_mgr_bodies_api.cpp`:

1. **Bounds-check pin**: `CATALOG_MGR_BODY_COUNT_PURE == 152`.
2. **In-range / out-of-range / negative**: standard predicate.
3. **Integrated-head predicate**: `catalogMgrBodyIsIntegratedHeadPure(0) == 0`, `(1) == 1`, `(2) == 0` (treat any non-1 as not-integrated; defensive).
4. **F2 routing pin**: assetcatalog_api.c contains `catalogManagerGetBodyByIndex(bodynum)`; does NOT contain `g_HeadsAndBodies[bodynum].ismale` / `.type` / `.height` / `.animscale` / `.canvaryheight` / `.unk00_01` / `.handfilenum`.
5. **F3 + F4 cache pin**: assetcatalog_api.c contains `catalogManagerGetBodyModeldef`, `catalogManagerResetBodyModeldef`, `catalogManagerResetAllBodyModeldefs`. Does NOT contain `g_HeadsAndBodies[bodynum].modeldef` direct reads, and the legacy walk loop `g_HeadsAndBodies[i].modeldef = NULL` is gone.
6. **F11 archive pin**: base/bodies.pdbase exists, has expected envelope, contains `"id": "base:dark_combat"`, `"id": "base:carrington"`, `"id": "base:sp_body_"`.
7. **F13 grep guard**: same `bad_patterns[]` table as heads F13, with body field names; walks the same selector / consumer file list.

---

## H. Cross-cuts

### H.1 Integrated-head invariant (S593g)

The body.c warning gate at line 416-422 reads:

```c
if (headnum >= 0 && headnum != HEAD_RANDOM_GENDER && !head_canon
        && !catalogGetBodyIsComplete(bodynum)) {
    sysLogPrintf(LOG_WARNING,
        "CHR.DIAG: bodyAllocateModel head_canon=NULL for headnum=%d "
        "-- catalog not registered, head model will be missing",
        headnum);
}
```

After F2, `catalogGetBodyIsComplete` reads `s_Bodies[bodynum].unk00_01`. The bodies migration must keep this answer correct -- specifically, body slots whose `unk00_01 == 1` (Skedar at bodynum 92, Dr Caroll, EyeSpy) must continue to suppress the warning. Test pin in F1 manager API spec covers the predicate; runtime validation is the swarm-test ladder which Mike runs as a regression baseline.

### H.2 B-275 hand model registration

`assetcatalog_base_extended.c:737` walks `g_HeadsAndBodies[i].handfilenum` to register first-person hand models. Two options:

- **Option A**: leave the registration loop reading the legacy table directly. The legacy table stays populated (data lives both there AND in the manager pool during the parity period; F13 retires the legacy table entirely).
- **Option B**: migrate the registration loop to call `catalogManagerGetBodyAt(i)->handfilenum`. Requires the manager to be initialised before `assetCatalogRegisterBodyHandModelFiles()` runs.

`assetCatalogRegisterBodyHandModelFiles` runs inside `assetCatalogRegisterBaseGame()` (call chain `main.c::main -> assetCatalogRegisterBaseGame -> ... -> assetCatalogRegisterBodyHandModelFiles`). `catalogManagerBodyInit` is called AFTER `assetCatalogRegisterBaseGame()` in main.c, so Option B requires a re-order or a lazy init in the manager.

**Decision**: Option A. The legacy table is the F1-F12 parity-period source; migrating the registration loop is unnecessary work and creates an init-order dependency. F13 will retire the legacy walk in `assetcatalog_base_extended.c::assetCatalogRegisterBodyHandModelFiles` along with the rest of the legacy table (the heads migration deferred this for the same reason).

### H.3 modelcatalog.c walk

[port/src/modelcatalog.c:381-460](port/src/modelcatalog.c:381) walks `g_HeadsAndBodies[]` for validation/thumbnail purposes (`catalogInit`, `findMphead*`, etc.). Same Option-A reasoning: the legacy table stays as the validation seed. F13 retires the walk by routing through the manager.

### H.4 Server build

`g_HeadsAndBodies[152]` is a server-stub zero-init (port/src/server_stubs.c:419). The manager pool init populates s_Bodies[] from this zero-init array; all body field accessors return zeros on the server, exactly like heads. The dedicated server has no body model data to load; the modeldef cache + lazy-load paths are PD_SERVER-excluded (mirrors heads F1).

### H.5 Mod loading

Mod-supplied bodies enter via `assetCatalogScanComponents()` and register into the catalog row layer. Today they do NOT have a `body_data_t` payload -- only an `ext.body` row. F12 doesn't change this; mods continue to surface through the catalog row layer + their `.pdmod` component-INI loader, which is orthogonal to `bodies.pdbase`. Per heads I.6 (carries over): mods don't ship `.pdbase` archives; they ship `.pdmod` archives that reference catalog IDs.

### H.6 Wire / save format

No protocol bump. Bodies on the wire use catalog ID strings (per the heads / bodies / arenas selector-pool migrations of 2026-04-26). The bodies migration is internal (manager + .pdbase); it does NOT change identity surface.

### H.7 Modeldef cache thread-safety

Identical to heads. The catalog manager runs on the main thread at startup (init), and accessors are called on the main thread thereafter (modeldef lazy-load fires from chr setup paths which are main-thread). No locking needed.

---

## I. Decisions (mirror heads I.1-I.7)

| # | Heads decision (carries over to bodies unless noted) | Bodies divergence |
|---|---|---|
| I.1 | `body_data_t` mirrors body-relevant subset of `struct headorbody`. Bitfields unpacked. | Bodies preserve `canvaryheight` and `handfilenum` (heads dropped them). |
| I.2 | Manager pool sized to full 152 range so `bodynum` indexes directly. | Same. |
| I.3 | `ext.body` gains `pdbase_path[128]` + `pdbase_offset` + `pdbase_size`. | Same. |
| I.4 | F4 splits `catalogResetAllModeldefs` into manager-call + legacy-walk. F13 removes the legacy walk after the bodies-side cache is also manager-owned. | Bodies F4 retires the legacy walk for body slots; heads F4 already retired it for head slots. After bodies F4, the legacy walk loop is gone entirely. |
| I.5 | Pure layer split (`*_pure.c` + `*_pure.h`) so pd-tests stays globals-free. | Same. |
| I.6 | One archive per asset class (`base/heads.pdbase`, `base/bodies.pdbase`). Mods ship `.pdmod` not `.pdbase`. | Same. |
| I.7 | Logging channel taxonomy `CATALOG.MGR.<TYPE>.{MISS,OVERRIDE,MUTATE,LOAD}` + `LOADER.PDBASE.<TYPE>.{SCAN_FAIL,RESOLVE_FAIL,FIELD_UNKNOWN,OK}`. | Same. |

---

## J. Bodies-specific concerns surfaced

**Concern 1: F4 legacy walk removal must be exhaustive.** Heads F4 only cleared the legacy walk for HEAD slots (the manager-call delegated, then the legacy walk continued). Bodies F4 must drop the legacy walk entirely because both head AND body cache slots will be manager-owned. The legacy walk currently iterates `g_HeadsAndBodies[i].filenum != 0` which covers both head and body slots; the heads-side loop is a no-op (slot already cleared by `catalogManagerResetAllHeadModeldefs`); after bodies F4, the body slots are cleared by `catalogManagerResetAllBodyModeldefs` and the legacy walk is structurally redundant. Remove the loop entirely.

**Concern 2: B-275 hand model registration reads handfilenum during base-game registration, BEFORE the body manager is initialised.** Per H.2, leave the read on the legacy table during the parity period. Document in Section H so a future reader knows why this isn't migrated in this session.

**Concern 3: modelcatalog.c walks the legacy table.** Same reasoning as H.2; defer to F13.

No concerns require Mike's call. Proceeding with mirrored decisions.

---

## K. Stop conditions checked

- ASSET_BODY catalog row schema change is additive (`pdbase_path`, `pdbase_offset`, `pdbase_size`); no break to mod loading.
- Integrated-head invariant preserved by routing `catalogGetBodyIsComplete` through the manager which reads the same `unk00_01` field.
- B-275 hand registration unchanged (Option A H.2).
- Body modeldef cache stays main-thread (H.7).

Phase 2 begins immediately.

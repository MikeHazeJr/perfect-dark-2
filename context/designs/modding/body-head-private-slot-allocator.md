# Body/Head Private Runtime-Slot Allocator (Gate 2)

> c3844 Gate 2 closure. Status: DESIGNED, ready for a one-pass implementation.
> Scoped 2026-06-09. The blast radius is verified-contained; the only
> unverifiable-until-B-801 part is the live render of a custom body/head.

## Problem (verified)

A fully NEW user-created `.pdbody`/`.pdhead` (one with no legacy `bodynum`/
`headnum`) cannot get a runtime slot and cannot assemble a model. B-904/905/906
made the chain loud-fail safely, but there is NO catalog-owned private body/head
slot allocator analogous to weapons.

- `g_HeadsAndBodies[]` and the manager pools are sized `CATALOG_MGR_BODY_COUNT`
  = `CATALOG_MGR_HEAD_COUNT` = 152. Base content already reaches ~150/152, so
  there is zero legacy headroom.
- Walkers (`loader_walker_body.c`/`head.c` `s_register`) force `bodynum/headnum
  = -1` when the manifest has no valid slot (B-904), so `parseBody`/`parseHead`
  drop the payload and `body0f02ce8c` returns NULL (B-905).
- Weapons solved this: `WEAPON_CUSTOM_START 0x56` / `MPWEAPON_CUSTOM_START 0x29`
  (constants.h) + `port/src/assetcatalog_weapon_slots.c`
  (`assetCatalogResolveWeaponPrivateSlots`, `s_allocateCustomWeaponSlot`,
  `assetCatalogResetCustomWeaponSlots`, loud `CATALOG.WEAPON.CUSTOM_SLOT_FAIL`).

## Feasibility (verified)

- Reverse map: `catalogBodyIdByBodynum` -> `catalogIdByRuntime(ASSET_BODY, n)`
  (assetcatalog_api.c:584) bounds-checks `n < RT_CACHE_SIZE` (=**1024**, ample)
  and FALLS BACK to a pool scan matching `e->runtime_index == n`. So a custom
  body with `runtime_index = <private slot>` reverse-resolves automatically with
  NO cache change, as long as the slot is < 1024.
- `catalogManagerBodyCount`/`HeadCount`/`GetBodyAt`/`GetHeadAt` have NO external
  consumers (grep clean), so growing array bounds does not break enumeration.
- Manager `s_get` already pulls from the loader pool when
  `loaderPoolBodiesActive()` and `loaderPoolGetBody(slot)` returns a record, so a
  custom slot populated by the walker is surfaced without extra manager wiring.

## Design: Option B (separate TOTAL, per-site loop decisions)

Do NOT just grow `CATALOG_MGR_BODY_COUNT`. The heads manager's
`s_pickRandomByGender` (catalog_mgr_heads.c ~336) builds
`candidates[CATALOG_MGR_HEAD_COUNT]` and would pollute the random-gender pool
with empty custom slots. Keep base-population loops at the BASE count; grow only
array storage + bounds to a TOTAL.

### Constants (`src/include/constants.h`, near the WEAPON_CUSTOM block)

```c
#define BODY_CUSTOM_COUNT 0x20            /* 32 private custom body slots */
#define BODY_CUSTOM_START CATALOG_MGR_BODY_COUNT   /* 152 */
#define BODY_CUSTOM_END   (BODY_CUSTOM_START + BODY_CUSTOM_COUNT)
#define HEAD_CUSTOM_COUNT 0x20
#define HEAD_CUSTOM_START CATALOG_MGR_HEAD_COUNT   /* 152 */
#define HEAD_CUSTOM_END   (HEAD_CUSTOM_START + HEAD_CUSTOM_COUNT)
```
(Or define the TOTALs in the catalog_mgr headers to avoid a constants.h ->
catalog_mgr include direction problem; keep the numbers in ONE place and derive.)

### Sizing / bounds (TOTAL) vs population (BASE)

| Site | Use |
|------|-----|
| `catalog_mgr_bodies.c` `s_Bodies[]` decl | size = `BODY_CUSTOM_END` |
| `catalog_mgr_heads.c` `s_Heads[]` decl | size = `HEAD_CUSTOM_END` |
| `loader_pool.c` `s_BodiesPool[]`/`s_HeadsPool[]` decl | size = custom END |
| `catalogMgrBodyIsInRangePure` / `catalogMgrHeadIsInRangePure` | bound = TOTAL |
| `catalogManagerGetBodyByIndex`/`GetByIndex` (head) bound | TOTAL (render path) |
| `s_populateFromAuthored` bound | TOTAL (custom slots zero-init via NULL lookup) |
| init/zero loops (bodies 123/127, heads 124/128) | TOTAL (zero custom slots) |
| modeldef-release loops (bodies 290, heads ~310) | TOTAL (release custom) |
| pool parse bounds (loader_pool.c 1943, 2002) | TOTAL |
| pool get bounds (loader_pool.c 2328, 2349) | TOTAL |
| `catalogManagerBodyCount`/`HeadCount` | KEEP base 152 (count semantics) |
| `GetBodyAt`/`GetHeadAt` iteration bound | KEEP base 152 (enumeration) |
| `s_pickRandomByGender` candidate loop (heads ~336) | KEEP base 152 (no pollution) |

The pure headers (`catalog_mgr_bodies_pure.h`/`heads_pure.h`) keep
`CATALOG_MGR_BODY_COUNT_PURE = 152` (base) and ADD
`CATALOG_MGR_BODY_TOTAL_PURE`/`..._HEAD_..` for the in-range bound. Update
`test_catalog_mgr_bodies_api.cpp`/`heads` to pin both (base 152 + total) and the
new in-range acceptance of `[152, TOTAL)`.

### Allocator: `port/src/assetcatalog_body_head_slots.c` (+ `.h`)

Mirror `assetcatalog_weapon_slots.c`:

```c
static char s_CustomBodyCatalogIds[BODY_CUSTOM_COUNT][CATALOG_ID_LEN];
static char s_CustomHeadCatalogIds[HEAD_CUSTOM_COUNT][CATALOG_ID_LEN];

void assetCatalogResetCustomBodyHeadSlots(void);   /* memset both, call on rebuild */
s32  assetCatalogResolveBodyPrivateSlot(const char *catalog_id);  /* -> [152,END) or -1 */
s32  assetCatalogResolveHeadPrivateSlot(const char *catalog_id);
```
Dedup by catalog id (same id -> same slot); first free slot otherwise; loud
`CATALOG.BODY.CUSTOM_SLOT_FAIL` / `CATALOG.HEAD.CUSTOM_SLOT_FAIL` on exhaustion;
return -1. Reset hook is called from the same place
`assetCatalogResetCustomWeaponSlots` is called (catalog rebuild) -- find that
call site and add the body/head reset beside it.

### Walker wiring (`loader_walker_body.c` / `loader_walker_head.c` `s_register`)

When `bodynum`/`headnum` is missing/out-of-range (currently forced to -1):
```c
if (bodynum < 0) {
    s32 slot = assetCatalogResolveBodyPrivateSlot(id);
    if (slot >= 0) { bodynum = slot; }   /* else stays -1, existing loud-fail */
}
```
Then keep the existing `assetCatalogRegisterBody(id, (s16)bodynum, ...)` and set
`e->runtime_index = bodynum` (so the reverse-map scan resolves it). Crucially,
pass the private slot into `loaderPoolParseBodyJson` so `parseBody` writes
`s_BodiesPool[slot]` from the public mesh source (today the manifest's bodynum
field drives the pool slot; the parse must use the allocated slot, not the
manifest's missing bodynum). Verify `loaderPoolParseBodyJson` keys the pool slot
off the same value the walker assigned.

## Tests (all static/adapter, no GPU)

1. Allocator unit (`tests/test_*` new or in test_catalog_provider_static): a
   synthetic id -> slot in `[152, END)`; same id -> same slot; exhaust -> loud
   fail + -1; reset clears.
2. Bounds: `catalogMgrBodyIsInRangePure` accepts `[0, TOTAL)`, rejects TOTAL;
   base-count pin stays 152.
3. Native-source guard extension (`tools/asset_native_source_guard.py`): assert
   no body/head private slot integer is ever serialized to wire/save/manifest/UI
   -- extend the existing weapon guard. Private slots are migration debt only.
4. Existing `[catalog][provider][static]` / `[modding][pdxxx][c3844][source]
   [static]` must stay green (additive change).

## Verify

`build-session -Session bh -Target all` + `-Target tests`;
`run-pd-tests -Selector "[catalog]"`; native-source guard; conformance unchanged.
Live custom-body render is the only B-801-blocked step -- prove it via a custom
`.pdbody` import once live smoke reopens (the scenario-scene-probe pattern does
not cover character assembly; a body CPU probe could be added).

## Risk

Pure data-layout / per-site-bound change on the character render path; base
indices [0,152) stay byte-for-byte unchanged so all base content is unaffected.
The private range is migration debt and must never cross a public boundary.

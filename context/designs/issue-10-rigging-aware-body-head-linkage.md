# Issue 10 design scope: rigging-aware body <-> head linkage

Status: SCOPED, NOT IMPLEMENTED.  Mike's design direction from the 2026-04-24 playtest batch.  Captured here so the next session has a starting point.

## Problem statement

Today's valid-head filter (commit `45bd3bdd`) gates on `category == "sp"` -- SP-only heads are excluded from MP body pick lists.  This stops the "stewardess has Joanna's head" crash-like symptom (commit `45bd3bdd`), but it's a symptom-stopper, not a root fix:

- Mike wants SP heads and bodies *available* in MP, not filtered out.
- Some heads bolt onto specific neck sockets on specific bodies; mismatched pairs produce the "no neck" visual Mike reported.  The correct filter is physical rig compatibility, not a metadata category.
- The current category filter conflates "SP-only" with "not MP-usable."  A cleaner model is: all heads + bodies are MP-eligible; the body <-> head pair is valid only if their rigging classes match.

## Design direction

Add an explicit `rig_class` field to every head and body catalog entry.  Examples:

| rig_class                      | Applies to                                                |
|--------------------------------|-----------------------------------------------------------|
| `human_male_neck_standard`     | Most DEFAULT-type male bodies (Connery, Mr Blonde, ...)   |
| `human_female_neck_standard`   | FEMALE + FEMALEGUARD bodies                               |
| `maian_tall_neck`              | MAIAN bodies (Elvis, Elvis_Gogs, Maian_S)                 |
| `skedar_broad_neck`            | (TBD -- no skedar neck yet? scedar is body-only)          |
| `drcarroll_floating`           | Dr Carroll standalone body (no separate head bolts in)    |
| `cass_neck`                    | Cassandra-specific rig                                    |
| `mrblonde_neck`                | Mr Blonde specific rig                                    |

Body <-> head query then becomes:

```c
const char *const *catalogGetBodyValidHeadIds(const char *body_id, int *out_count) {
    // Replace the current category gate + HEADBODYTYPE compat check with:
    // 1. Resolve body -> body.rig_class.
    // 2. Iterate ASSET_HEAD; include any head whose rig_class matches.
    // 3. Return the set.
}
```

No category filtering.  SP heads and MP heads are equal citizens.  The filter is purely physical rig compatibility.

## Data-authoring task

Every base body (63 entries) and every base head (76 + SP heads, ~100 total) needs a `rig_class` assigned.  Audit path:

1. Read `src/game/modeldata/robot.c` to see existing HEADBODYTYPE tags (DEFAULT, FEMALE, MAIAN, CASS, MRBLONDE, FEMALEGUARD).  These are the closest existing analog to a rig class but they are coarser than what Mike wants.
2. For each HEADBODYTYPE bucket, confirm whether all members share a neck socket or whether there are sub-buckets (e.g. are all DEFAULT heads interchangeable on all DEFAULT bodies?).  Test by loading known combinations and inspecting for the "missing neck" symptom Mike reported.
3. Populate the new `rig_class` field per entry.  Likely starts as a 1:1 mapping from HEADBODYTYPE_* then subdivides where rig drift shows.
4. Catalog schema bump: add `rig_class` to `ext.body` and `ext.head`.  Registration functions `assetCatalogRegisterBody` / `assetCatalogRegisterHead` gain an argument or are extended via a setter.
5. Update `s_BaseBodies` / `s_BaseHeads` tables in `port/src/assetcatalog_base.c` with the new field.

## Retirement

Once the rig_class linkage is live and validated:

- Remove the `category == "sp"` filter from `collectValidHead` (commit `45bd3bdd`).
- Remove the mp_index / HEADBODYTYPE fallbacks from `catalogGetBodyValidHeadIds`.
- "Missing necks" class bug is retired in one stroke.

## Follow-up work triggered by Issue 10

- `catalogGetBodyValidHeadIds` rewrite + callers.
- Playtest matrix: every body x every head compatible pair visited, missing-neck symptom confirmed absent.
- Audit any other catalog code that reads HEADBODYTYPE for compatibility (`modelcatalog.c::catalogIsHeadBodyCompatible` would migrate).

## Estimated effort

- Data entry: 4-6 hours per full audit of 160 entries.
- Catalog schema + code refactor: 1-2 hours.
- Playtest validation: hours of actual matrix testing, or build a debug model-grid screen that renders every pair.

Not a single-evening task.  Suitable for a scoped morning session when the rest of the playtest batch is green.

## Dependencies

- Issue 1's category filter (`45bd3bdd`) stays in place until Issue 10 lands, so the "stewardess / Joanna" symptom doesn't regress.
- Issue 5's FREEFLY observer gating (`1925f8a5`) is orthogonal; Issue 10 doesn't touch the Dr Carroll-in-Grid flow.
- If a future Dr Carroll observer chr type scaffolds (the "separate player controller" work Mike mentioned), it can either
  - reuse the existing player chr with rig_class=drcarroll_floating (data change only), or
  - live as a distinct chr type outside the rig_class scheme.  Decide at scaffold time.

## Reference: Mike's spec (verbatim, 2026-04-24 batch 3)

> "I want to have SP heads and bodies as well, just linked properly. Regarding the necks not appearing possibly as a result of a rigging thing, can we dive into that and more easily link the heads to their proper bodies? It seems like that would be a good avenue to take."

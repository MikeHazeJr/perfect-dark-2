# Custom-Model Runtime-Slot Allocator (B-911, c3848)

> Make a custom `.pdweapon`'s embedded `.pdmesh` (referenced by a catalog-ID
> `model_ref` in its projectile graph) actually render. Under the c3844 100%
> source/runtime parity umbrella. Mirrors the B-909 body/head allocator. The
> render path is `g_ModelStates`-indexed and B-801 live-unverifiable.

## Problem (root cause, verified by workflows wp36kfcp7 + wf3hlqwrf)

`catalogResolveModel(model_ref)` returns `out.modelnum = entry->runtime_index`
(`assetcatalog_api.c:164`), a fixed `g_ModelStates[NUM_MODELS]` index. A custom
embedded mesh has `runtime_index = -1` and there was no catalog-owned custom-model
slot allocator (weapons have `assetcatalog_weapon_slots.c`; bodies/heads got
`assetcatalog_body_head_slots.c` in B-909; models had none). Additionally, no
family ingests embedded `.pdmesh`/`.pdmaterial`/`.pdtexture` by nested catalog_id
(`weapon_graph_archive.c::typeForNestedArchiveName` recognized only
`.pdprojectile`/`.pdentity`). Base weapons dodge it via intra-archive
`model_archive` + pre-seeded base `ASSET_MODEL` rows; custom weapons cannot.

## Decision (Mike, 2026-06-10): build it the B-909 way

Catalog-owned custom-model slot allocator, fully catalog-ID-faithful: grow
`g_ModelStates` additively + register the embedded mesh as `ASSET_MODEL` with a
private custom slot + resolve `model_ref` -> slot. The private slot is migration
debt; it never crosses wire/save/manifest/UI (modelnum already crosses the wire
only as a catalog-ID-derived session ref).

## Slice 1 - Foundation (DONE, commit `9063004c`, BUILD-PENDING)

- `constants.h`: `MODEL_CUSTOM_COUNT 0x20`, `MODEL_CUSTOM_START NUM_MODELS`,
  `MODEL_CUSTOM_END (NUM_MODELS + MODEL_CUSTOM_COUNT)` (JPN-ternary safe).
- New `assetcatalog_model_slots.{c,h}`: dedup-by-id allocator, loud
  `CATALOG.MODEL.CUSTOM_SLOT_FAIL`, reset. Globals-free, in the pd-tests block.
- `g_ModelStates` grown to `MODEL_CUSTOM_END` (`data.h:338`, `general.c:404`,
  `server_stubs.c:100`); base init rows unchanged, 32 trailing slots zero-init.
- Runtime sites that index by a custom slot grow to TOTAL: `setup.c:1735`
  stage-init NULL-clear (required: stale modeldef pointer across a stage change
  into a recycled pool = UAF) + `prop.c:1678` bound. `romextract_*` +
  base-registration loops stay at `NUM_MODELS`. Dead Gex/GF64 arrays untouched.
- Reset wired at the 3 `assetcatalog.c` sites. `tests/test_model_slots.cpp`.

The foundation alone is additive headroom + a dead-but-tested allocator (no caller
yet -> zero behavior change for base content).

## Slice 2 - Ingest consumer (IN PROGRESS, APIs being mapped by workflow w6cid0dyo)

The needle mesh is DOUBLY nested:
`needler.pdweapon -> .../primary.pdprojectile -> .../needle.pdmesh -> model.gltf`,
and `model_ref` resolves at projectile-register time (`weapon_graph_runtime.c:2811`),
so the mesh must be registered BEFORE that. Plan:

1. `typeForNestedArchiveName`: add `.pdmesh -> ASSET_MODEL` (and `.pdmaterial`,
   `.pdtexture` if the mesh needs them to render).
2. In `weaponGraphRuntimeRegisterWeaponArchiveDependencies` (the dependency walk),
   add a MESH-FIRST pass: for each `.pdprojectile` payload, peel its embedded
   `.pdmesh` in memory (open the already-extracted projectile bytes as an archive,
   extract the `.pdmesh` member), read its `catalog_id`, register it as a
   MOD-category (NOT base/bundled) `ASSET_MODEL`, set
   `runtime_index = assetCatalogResolveModelPrivateSlot(catalog_id)`, and give it a
   renderable source handle. Do this BEFORE the existing projectile/entity register
   loop, with the same `registered[]`/`fail:` rollback symmetry.
3. Source handle / MATERIALIZATION (the one new design fork): the model loader
   (`modeldefLoadExternalCatalogSource` -> `fsFileLoad`) + the source-only guard
   (`setupModelHandlePassesSourceOnlyCheck` / `modeldefRefuseRomSource`) demand a
   public FileProvider-resolvable path, and `modvfs` `::` is single-level so a
   handle to the triply-nested `model.gltf` will not resolve. Leading approach:
   extract the embedded `.pdmesh` bytes to a public writable tier on disk during
   ingest, then `catalogSetPrimaryFile(e, "<path>::model.gltf")`. The workflow is
   confirming the exact writable tier + fs-write API + whether an in-memory /
   register-from-bytes provider path exists that avoids the disk write.

After this, `catalogResolveModel("mod_needler:needle")` returns the custom slot,
`has_projectile_modelnum = 1`, and `setupLoadModeldef(slot)` compiles + renders it.

## Slice 3 - CPU end-to-end test

Clone the walker block in `tests/test_mod_external_archive_static.cpp` (~4330-4389):
a fixture `.pdweapon` whose projectile embeds a `.pdmesh` declaring a catalog_id;
drive `weaponGraphRuntimeRegisterWeaponArchive`; assert the projectile runtime has
`has_projectile_modelnum == 1` and `projectile_modelnum` in
`[MODEL_CUSTOM_START, MODEL_CUSTOM_END)`.

## Slice 4 - Live render proof

B-801-gated (needs a live boot). Out of scope until the live-proof gate opens.

## Related

- `context/bugs.md` B-911 (root cause + fix locus), B-912 (projectile gameplay
  runtime unconsumed -- separate, the secondary's explode-on-contact needs it).
- `context/designs/modding/needler-weapon-mod.md` (the proof-of-need).
- Mirror reference: `port/src/assetcatalog_body_head_slots.c` (B-909).

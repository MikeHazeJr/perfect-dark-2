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

## Slice 2 - Ingest consumer (IMPLEMENTED 2026-06-10, build-pending)

The needle mesh is DOUBLY nested:
`needler.pdweapon -> .../primary.pdprojectile -> .../needle.pdmesh -> model.gltf`,
and `model_ref` resolves at projectile-register time, so the mesh must be
registered BEFORE that. As built (API map: workflow w6cid0dyo):

1. **Materialization fork DISSOLVED -- no disk extraction needed.** The earlier
   "modvfs `::` is single-level" concern applies only to archives resolved through
   a modvfs MOUNT (zipped `.pdmod`s). A loose on-disk `.pdweapon` resolves through
   `fs.c::fsLoadNestedArchiveEntry` -> `fsExtractNestedArchiveChain` (`fs.c:129-240`,
   verified by direct read), which RECURSES on every `::` -- arbitrary-depth
   nesting. The shipping `.pdbody` chain (`<body>.pdbody::mesh.pdmesh::model.obj`,
   `loader_walker_body.c:163`) is the 2-level precedent; B-911 binds the same kind
   of chain one level deeper:
   `<weapon path>::<projectile entry>::<mesh entry>::<geometry>`.
   The source-only guard passes (FileProvider handle; dev-mods/ path is not under
   `data/.../files|segments` and not `.bin`), and `.gltf` routes through
   `modAssetCompilerIsExternalSource` -> `modeldefLoadExternalCatalogSource` ->
   `fsFileLoad(chain)`.
2. **Pure scan (pd-tests-linkable)**: `weaponGraphArchiveScanEmbeddedMeshesBytes`
   (`weapon_graph_archive.c`) enumerates an in-memory nested payload's `.pdmesh`
   members (`modArchiveMemForEachEntry` + `modArchiveExtractMemAlloc` -- the proven
   bytes->archive primitives), reads each mesh's declared `catalog_id` +
   `model_file` from `mesh.ini` (`[mesh]`-gated local parser; ids NEVER derived),
   applies the parent-namespace gate (loud-skip), dedups by id. Returns >= 0.
3. **Catalog wiring (thin)**: `s_registerEmbeddedMeshDeps` in
   `weapon_graph_runtime.c`, called in the dependency walk right after each
   payload's bytes are extracted and BEFORE its IR is compiled/registered:
   `assetCatalogRegister(id, ASSET_MODEL)` (fresh entries default to the mod
   profile: bundled=0; category inherited from the parent weapon's row -- NOT
   `loaderWalkerMarkBaseArchiveEntry`, which would mismark it base/bundled),
   `e->runtime_index = assetCatalogResolveModelPrivateSlot(id)`, and
   `catalogSetPrimaryFile(e, <multi-level chain>)`. All failures loud-skip
   (`WEAPONGRAPH.MESH.INGEST:` channel); the weapon still registers minus its
   custom model (pre-B-911 behavior). Success logs id + slot + source.

After this, `catalogResolveModel("mod_needler:needle")` returns the custom slot,
`has_projectile_modelnum = 1`, and `setupLoadModeldef(slot)` compiles + renders it.

## Slice 3 - CPU tests (IMPLEMENTED 2026-06-10, run-pending)

pd-tests STUBS the catalog (`tests/stubs.c` `assetCatalogResolve` -> NULL is
load-bearing for netmanifest tests; base model refs in tests resolve via the
hardcoded alias table in `heldResolveProjectileModelRef`, not the catalog). So a
full-chain `has_projectile_modelnum`-in-custom-range assertion is structurally
impossible in pd-tests. The testable seam is the pure scan:
`tests/test_mod_external_archive_static.cpp` `[modding][pdxxx][model_slots][c3848]`
pins discovery (entry/id/geometry), the `model.obj` geometry default, the
namespace gate, the no-declared-id skip, dedup-by-id, and non-archive-bytes
robustness. The allocator itself is pinned by `tests/test_model_slots.cpp`
(`[catalog][model][slots][c3848]`, real allocator linked). New stubs:
`assetCatalogRegister` -> NULL + `catalogSetPrimaryFile` no-op keep the ingest
inert-but-exercised in the existing weapon-graph walk tests. The full resolve
chain is proven by review + the client build + the live render (Slice 4).

## Slice 4 - Live render proof

B-801-gated (needs a live boot). Out of scope until the live-proof gate opens.

## Related

- `context/bugs.md` B-911 (root cause + fix locus), B-912 (projectile gameplay
  runtime unconsumed -- separate, the secondary's explode-on-contact needs it).
- `context/designs/modding/needler-weapon-mod.md` (the proof-of-need).
- Mirror reference: `port/src/assetcatalog_body_head_slots.c` (B-909).

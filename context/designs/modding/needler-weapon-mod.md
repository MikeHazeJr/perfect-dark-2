# Needler Weapon Mod (c3847)

> Custom `.pdmod` weapon: looks like the Halo Needler, replicates its behavior.
> PRIMARY = tracking needles. SECONDARY = non-tracking needles that explode on
> contact with small PINK explosions. Authored against the live
> `.pdweapon`/`.pdprojectile`/`.pdentity` graph schema, bound to a catalog-owned
> custom weapon slot. Source lives in `dev-mods/needler/` (copied into the build
> by the c3846 dev-mod pipeline, so it survives clean builds).

## Status (2026-06-10)

Authoring in progress via the `needler-author` ultracode workflow, which adapts
the proven example builder `tools/build_typed_pdxxx_examples.py` (tri_weapon
emitter) into `tools/build_needler_mod.py`. CPU-verifiable via
`tools/asset_archive_conformance.py`. Live fire/visual proof is B-801-gated.

## Verified grounding (from the goal-phase-design workflow)

- The weapon-graph asset system is fully landed (C-3814). Canonical layout:
  `context/designs/modding/weapon-archive-clean-format.md`. A `.pdweapon` has
  `weapon.ini`, `behavior/{primary,secondary}.graph.json` (+ settings /
  variables / shared-context), `bindings/`, embedded typed deps under
  `dependencies/assets/...`, and `_meta/manifest.json`.
- Graph node `kind` strings: `port/src/weapon_graph_runtime.c:86-98`
  (`spawn.fired_projectile`, `projectile.motion`, `projectile.homing`,
  `projectile.impact`, `projectile.transition_to_entity`,
  `entity.armed_explosive`, ...).
- Projectile runtime record `weapon_graph_projectile_runtime_t`
  (`port/include/weapon_graph_runtime.h:280-368`) carries `has_homing` +
  `homing_steering_gain/damping` and `has_impact` + `impact_explosion_ref` /
  `impact_consume_on_hit` / `impact_stick_on_hit`.
- Custom-slot binding is automatic at scan:
  `assetcatalog_weapon_slots.c::assetCatalogResolveWeaponPrivateSlots` assigns
  runtime slot `WEAPON_CUSTOM_START+i` and MP slot `MPWEAPON_CUSTOM_START+i`.

## Design decisions (Mike's open questions, adopted defaults)

- **Q1 (homing gate) -> YES, widen it (FOLLOW-UP, not in the .pdmod).** The OG
  homing steering in `propobj.c:7159-7163` is gated on
  `weaponobj->weaponnum == WEAPON_HOMINGROCKET`, so a custom-slot homing
  projectile does not steer. `bondgun.c:1856-1858` already sets
  `FUNCFLAG_HOMINGROCKET` from `projectile->has_homing`. The fix is to widen the
  gate to honor `FUNCFLAG_HOMINGROCKET` (additive: OG rocket has both the
  weaponnum and the flag, so it is unchanged; custom homing now steers). This is
  behavior-changing core weapon runtime + cannot be live-verified under B-801 +
  needs the spawn/target-acquisition path traced so a custom homing projectile
  gets `targetprop`. Deferred to a dedicated runtime slice; the goal ("behavior
  replicated") sanctions it. **The Needler authoring is correct either way; only
  active primary TRACKING depends on this adapter.**
- **Q2 (pink) -> custom `.pdeffect` + pink spark `.pdtexture`.** `g_ExplosionTypes`
  (`explosions.c:44-85`) has no per-type color; the rendered tint is hard-coded
  white, so a scalar recolor is impossible. The visible pink comes from a custom
  small-class `.pdeffect` + a pink impact-spark texture; the impact
  `explosion_ref` still points at a small OG explosion class for blast/damage
  parity.

## Decisions from the goal-phase-design workflow (adopted)

- **Co-op (A):** ship reconnect-reliable now (ranks 8-11); hold fresh drop-in as
  a separate opt-in follow-up (it relaxes the mid-game reject that also protects
  Combat Sim late-joins + SEC-3). Process-restart reconnect stays unrecoverable
  under the no-disk-cookie constraint.
- **Co-op roster (B):** reuse the existing CHRS resync flag for v1 (no wire bump).
- **Versioning (C):** add a semver `version` to each family's `_meta/manifest.json`
  as optional-for-readers / required-for-writers (lands with conformance +
  netdistrib + the `manifest_pure.c` pin together).

## Archive contents (namespace `mod_needler`)

`needler.pdweapon` (embedded, self-contained):
- `weapon.ini`; `behavior/primary.graph.json` (homing), `behavior/secondary.graph.json`
  (non-tracking + contact explode); settings / variables / shared-context;
  `bindings/{material-slots,grip-sockets,presentation}.json`; `_meta/manifest.json`.
- `dependencies/assets/projectiles/homing.pdprojectile` (motion powered +
  `projectile.homing`), `.../burst.pdprojectile` (motion powered, NO homing,
  `projectile.impact` -> `mod_needler:pink_burst_effect`).
- `dependencies/assets/models/needle.pdmesh` (thin spike), `.../materials/*.pdmaterial`
  (pink crystalline), `.../textures/{body,pink_spark}.pdtexture`,
  `.../effects/pink_burst_effect.pdeffect` (small class).

## Verify

`python tools/build_needler_mod.py` then
`python tools/asset_archive_conformance.py --root dev-mods/needler`. Mesh quantize
is covered by conformance (integer-native check). Live fire + pink visuals +
tracking are B-801-gated (and tracking also needs the Q1 adapter).

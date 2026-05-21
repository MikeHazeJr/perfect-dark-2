# Weapon Behavior Graph Assets

Status: active design; schema, base-behavior audit, and module-parameter slices landed 2026-05-21 under Kanban `c3814`.

## Goal

Move weapon behavior out of C-shaped weapon records and into editable, self-contained asset archives that the upcoming weapon creation tool can parse, show as a graph, validate, and export.

This is not a new gameplay system. It is the existing base-game behavior expressed as authored data, then compiled into a deterministic runtime form.

## Hard Contract

- `.pdweapon` is the only weapon archive extension for this system. `.pdwpn` is fully deprecated, was never released, and must be removed from current code paths rather than accepted as input, alias, migration source, or compatibility path.
- `.pdprojectile` and `.pdentity` are first-class catalog asset types.
- `.pdentity` describes deployed or stuck behavior archetypes. It does not replace `ASSET_PROP` or the runtime prop system.
- A `.pdweapon` may embed its projectile/entity dependencies so the weapon is self-contained.
- Embedded dependencies still register as catalog assets with stable derived IDs.
- Duplicate embedded payload bytes may share cache/storage by SHA-256, but catalog identity remains name based.
- Authoring uses graph JSON. Runtime uses compiled IR generated from that graph.
- Base-game assets regenerate through the boot/self-heal path.
- Authored archives must remain zip-openable and must not contain authored `.bin` payloads.

## Current Runtime Shape

The current weapon data source is still `port/src/weapondata_authored.c`. The pre-release `.pdwpn` live paths were removed in the first runtime cutover slice; base extraction now emits `.pdweapon` archives only. Existing function records cover:

- `INVENTORYFUNCTYPE_SHOOT_SINGLE`
- `INVENTORYFUNCTYPE_SHOOT_AUTOMATIC`
- `INVENTORYFUNCTYPE_SHOOT_PROJECTILE`
- `INVENTORYFUNCTYPE_THROW`
- `INVENTORYFUNCTYPE_MELEE`
- `INVENTORYFUNCTYPE_SPECIAL`
- `INVENTORYFUNCTYPE_DEVICE`
- `INVENTORYFUNCTYPE_NONE`

Projectile and deployed-object behavior is currently split across weapon function data, `bondgun.c`, `propobj.c`, model manager preload paths, and object-specific runtime code. The new asset split makes that ownership explicit.

Coverage audit: [base-weapon-behavior-coverage.md](base-weapon-behavior-coverage.md). Current parameter matrix: [base-weapon-parameter-matrix.md](base-weapon-parameter-matrix.md). Module parameter spec: [weapon-graph-module-parameters.md](weapon-graph-module-parameters.md).

## Asset Types

### `.pdweapon`

Owns inventory behavior, held models, UI presentation, ammo, primary/secondary modes, animation references, graph authoring, and references to nested physical assets.

Required root files:

```text
weapon.ini
behavior.graph.json
```

Optional folders:

```text
models/
textures/
animations/
audio/
ui/
projectiles/
entities/
```

Canonical example:

```text
base_slayer.pdweapon
  weapon.ini
  behavior.graph.json
  models/first_person.gltf
  models/world.gltf
  animations/reload.pdanim
  audio/launch.wav
  projectiles/rocket.pdprojectile
  projectiles/flybywire_rocket.pdprojectile
```

### `.pdprojectile`

Owns the physical object launched or thrown by a weapon before it becomes an explosion, pickup, stuck object, or deployed entity.

Required root files:

```text
projectile.ini
```

Optional root files:

```text
behavior.graph.json
```

Common data:

- model and scale
- launch speed
- powered vs ballistic motion
- gravity, bounce, slide, sticky, and reflect behavior
- lifetime and arm delay
- impact rules
- damage and explosion references
- guidance mode such as none, homing, fly-by-wire, wall-hugger
- optional transition to `.pdentity` on stick, arm, or deploy

Examples:

- Slayer rocket
- Slayer fly-by-wire rocket
- Rocket Launcher rocket
- SuperDragon grenade round
- Devastator wall-hugger round
- Grenade or N-Bomb in flight
- Thrown mine before it sticks and arms
- Thrown Laptop Gun before it sticks and deploys

### `.pdentity`

Owns stateful world behavior after an object is deployed, armed, or stuck somewhere.

Required root files:

```text
entity.ini
behavior.graph.json
```

Common data:

- runtime archetype such as `armed_mine`, `remote_mine`, `autogun`, `sensor`, or `pickup`
- model and collision profile
- team/owner tracking
- interactability and pickup rules
- timers, activation state, and damage state
- targeting, scan cone, fire behavior, and ammo for turret-like entities
- detonation, disable, recover, and cleanup behavior

Examples:

- armed proximity mine
- armed timed mine
- armed remote mine
- deployed Laptop Gun autogun
- Dragon secondary self-destruct/proxy behavior

## Nested Catalog IDs

Nested assets are real catalog assets. Their default ID is derived from the parent catalog ID and local nested slug:

```text
<namespace>:<parent_slug>__projectile_<local_slug>
<namespace>:<parent_slug>__entity_<local_slug>
```

Examples:

```text
base:slayer__projectile_rocket
base:slayer__projectile_flybywire_rocket
base:laptopgun__projectile_thrown_laptop
base:laptopgun__entity_deployed_autogun
```

An embedded asset may declare an explicit `id` only if it remains in the same namespace as the parent or the archive declares a deliberate external catalog dependency. Mod tools should default to derived IDs to keep assets self-contained.

## Payload Sharing

Identity and payload storage are separate:

- Catalog identity is always the catalog ID string.
- Payload equality is SHA-256 over canonical archive content.
- Canonical archive content means sorted normalized entry paths plus exact uncompressed bytes.
- Zip timestamps, compression level, central-directory ordering, and other container metadata do not affect the digest.
- If two embedded assets have identical canonical payload digests, the cache may store one compiled payload and point both catalog entries at it.
- If the names differ, the catalog entries remain distinct even when payloads are shared.

## Graph Authoring Format

`behavior.graph.json` is tool-owned but readable. It is a typed graph with no arbitrary script text.

Top-level shape:

```json
{
  "schema": "pd.weapon_graph.v1",
  "asset_id": "base:slayer",
  "graph_id": "primary",
  "nodes": [],
  "edges": [],
  "exports": []
}
```

Nodes have stable IDs, a typed `kind`, and declarative parameters:

```json
{
  "id": "fire_primary",
  "kind": "event.trigger_pressed",
  "params": {
    "mode": "primary"
  }
}
```

Edges connect output pins to input pins:

```json
{
  "from": "fire_primary.exec",
  "to": "spawn_rocket.exec"
}
```

Time values must preserve authored intent and exact base-game timing. Use typed units:

```json
{ "value": 25, "unit": "centiseconds" }
{ "value": 15, "unit": "ticks60" }
{ "value": 9600, "unit": "ticks240" }
{ "value": 600, "unit": "rpm" }
```

The editor may show friendlier units, but it must keep the stored unit explicit so exact base-game behavior can round-trip.

## Node Categories

The v1 node set should cover existing behavior with editable modules:

- `event.*`: trigger pressed, trigger held, trigger released, reload, mode change, projectile impact, projectile tick, entity armed, entity tick, entity damaged, entity disabled.
- `gate.*`: ammo available, cooldown ready, charge threshold, zoom state, target lock, surface match, team filter, owner alive.
- `ammo.*`: consume, reserve transfer, clip refill, inventory read, entity ammo reserve.
- `fire.*`: hitscan shot, shotgun pattern, burst, automatic loop, beam tick, charge release.
- `spawn.*`: projectile, thrown projectile, deployed entity, impact effect, audio, muzzle flash, casing.
- `projectile.*`: set motion, apply guidance, bounce, slide, stick, arm, detonate, transition to entity.
- `entity.*`: acquire target, track target, fire, detonate, pickup, disable, cleanup.
- `presentation.*`: play animation, set model part visibility, drive ammo display, set reticle, set overlay, set zoom, camera effect.
- `device.*`: cloak, scanner, target locator, threat detector, remote detonator, combat boost.
- `math.*`: constants, curves, random, compare, clamp, vector operations.

Special base-game behaviors should become named modules with editable parameters, not opaque C callbacks. If exact parity needs a temporary adapter during implementation, the node must be named as a temporary adapter and carry enough parameters to remove it later.

## Runtime IR Boundary

The compiled IR is deterministic generated data. It is not authored by modders and should not be packed as source.

Compiler responsibilities:

- validate graph schema and pins
- resolve catalog references
- normalize units into runtime ticks/RPM/scalars
- topologically order flow where possible
- reject cycles unless the cycle is an explicit loop node such as automatic fire or beam tick
- emit stable opcodes and parameter blocks
- record dependency digests for cache invalidation

Runtime responsibilities:

- execute compiled opcodes only
- never parse editor-only graph affordances in hot weapon code
- fail closed on missing projectiles/entities
- log catalog misses by asset ID
- keep deterministic behavior for networked play

## Base-Game Behavior Coverage Target

The schema must cover these base behavior families before runtime cutover starts:

- single shot and shotgun-style multi-shot
- automatic and burst fire
- charge-and-release fire
- projectile launch with normal, homing, fly-by-wire, and wall-hugger guidance
- thrown grenades, N-Bombs, knives, mines, and thrown Laptop Gun
- sticky, bouncing, sliding, timed, and armed physical objects
- deployed entity behavior, including Laptop Gun autogun and armed mines
- remote detonation
- melee attacks
- zoom levels, target locator, threat detector, cloak, scanners, and combat boost
- reticles, overlays, camera effects, muzzle effects, sounds, and animation hooks
- ammo-driven model part visibility and in-weapon displays

The first audit pass is in [base-weapon-behavior-coverage.md](base-weapon-behavior-coverage.md). It covers the current 86-weapon function inventory, behavior families, physical payload candidates, and runtime mapping for Slayer rockets, grenades, proxy mines, mines, Dragon proxy behavior, thrown Laptop Gun, and deployed Laptop Gun autogun behavior.

The weapon-by-weapon current parameter table is in [base-weapon-parameter-matrix.md](base-weapon-parameter-matrix.md). The named module parameter spec is in [weapon-graph-module-parameters.md](weapon-graph-module-parameters.md). The runtime cutover split is in [weapon-graph-runtime-cutover-plan.md](weapon-graph-runtime-cutover-plan.md). Remaining before runtime implementation: graph archive readers/writers, generated nested payload inventory, final nested payload IDs, graph compiler, and runtime adapters.

## Laptop Gun Rule

The Laptop Gun is explicitly a two-stage asset:

1. `.pdweapon` primary mode covers normal held firing.
2. `.pdweapon` secondary mode spawns `base:laptopgun__projectile_thrown_laptop`.
3. The thrown projectile owns flight, collision, and sticky behavior.
4. On valid stick, it transitions to `base:laptopgun__entity_deployed_autogun`.
5. The entity owns team targeting, ammo transfer, beam state, barrel movement, interactability, pickup/recover, and cleanup.

Current runtime detail: `bgunCreateThrownProjectile2` calls `laptopDeploy()` before applying projectile flight, so an `OBJTYPE_AUTOGUN` exists during the throw. The graph contract should still model the behavior as a thrown projectile carrier that transitions to a deployed autogun entity on valid stick; the runtime adapter can preserve early instantiation if needed.

## Cutover Plan

1. Write and pin this design.
2. Audit every base weapon function and map it to v1 modules.
3. Remove fully deprecated `.pdwpn` code paths and teach the base extractor to emit `.pdweapon` archives only. Done in `c3814-s10`.
4. Add catalog enum/manifest/scanner recognition for `.pdweapon`, `.pdprojectile`, and `.pdentity`. `.pdweapon` landed in `c3814-s10`; `.pdprojectile` and `.pdentity` landed in `c3814-s11`.
5. Implement graph archive readers/writers, nested payload inventory, derived IDs, and canonical payload SHA-256 dedupe. Done in `c3814-s12`.
6. Emit nested `.pdprojectile` and `.pdentity` archives for base weapons that need them. Active next in `c3814-s13`.
7. Add graph validator and deterministic graph-to-IR compiler.
8. Add runtime adapter that feeds existing weapon structs from compiled IR.
9. Convert weapon behavior callsites module by module until runtime behavior is driven by compiled `.pdweapon` graph IR.

## Validation

Minimum gates before runtime cutover:

- JSON schema validation for `behavior.graph.json`.
- Archive inventory tests for `.pdweapon`, `.pdprojectile`, and `.pdentity`. First helper-level coverage landed in `c3814-s12`.
- Derived catalog ID collision tests. Helper-level collision coverage landed in `c3814-s12`; base payload IDs still land in `c3814-s13`.
- Canonical payload SHA-256 stability tests. Helper-level file/embedded archive coverage landed in `c3814-s12`.
- Base weapon audit table checked into context or tests. Done for the first pass in [base-weapon-parameter-matrix.md](base-weapon-parameter-matrix.md); final schema naming still needs `c3814-s8`.
- Module parameter spec checked into context or tests. Done in [weapon-graph-module-parameters.md](weapon-graph-module-parameters.md).
- Static guard that `.pdwpn` is not emitted, scanned, packed, accepted, migrated, or aliased.
- Runtime parity tests once the compiler exists.

## Not Implemented Yet

- `.pdweapon` scanner, packer, walker, emitter, example, and static-test paths are live.
- `.pdprojectile` and `.pdentity` catalog enum, scanner, packer, manifest, distribution, mod UI, and descriptor-template paths are live.
- Graph archive helper APIs are live for descriptor/text reads, root validation, derived nested IDs, duplicate-ID collision checks, canonical SHA-256 over sorted uncompressed archive entries, nested payload inventory scanning/formatting, and embedded in-memory archive hashing.
- Base `.pdweapon` archives now carry `nested_payloads.json`; it is empty until generated physical payloads land.
- No graph-authored `.pdweapon` base archives beyond the temporary legacy-manifest adapter yet, and no generated `.pdprojectile` or `.pdentity` base archives yet.
- Runtime implementation tasks are split in [weapon-graph-runtime-cutover-plan.md](weapon-graph-runtime-cutover-plan.md).
- No graph validator or compiler yet.
- No runtime IR execution yet.
- No final nested payload ID list yet.

This slice defines the contract so the next implementation pass can make code changes without inventing policy mid-stream.

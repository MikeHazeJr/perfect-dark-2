# Scenario Authoring And Mission Graph Migration

Status: implemented and validated 2026-05-25. Kanban owner: `c3841` (Done).

## Goal

Opening a `.pdscenario` or `.pdmission` archive should make sense immediately. Public files should be standard, named, and editable. Raw preprocessed word dumps are not an acceptable authored format.

## Scenario Source Contract

Preferred `.pdscenario` public layout:

```text
scenario.ini
scene.glb
collision.glb
pads.tsv
volumes.tsv
spawns.tsv
navigation.ini
level.graph.json
_meta/
  manifest.json
  inventory.json
  generated-collision.json
  generated-navmesh.json
  validation.json
  hashes.tsv
```

`scene.glb` or `scene.gltf` is the primary level source. OBJ/MTL/TGA remains a transition/export fallback, but GLB/glTF is the target because common tools can open it directly while preserving hierarchy, materials, UVs, and texture bindings.

The target user experience is stronger than "has an export." A modder should be able to open the scenario scene in Blender or 3DS Max and see the textured level immediately: correct mesh hierarchy, UVs, material assignments, and embedded or archive-local texture references. `scene.glb` is preferred for examples and generated base archives because it can carry buffers and textures in one standard file. `scene.gltf` is acceptable when it uses relative paths to files inside the same archive.

The same scene file is the game-facing source. Scenario loading should ingest `scene.glb`/`scene.gltf` through the catalog/provider pipeline and then derive renderer batches, room/portal metadata, collision fallback, and nav generation inputs from it. Any faster engine-native products are cache, not authored payload; deleting the cache must not make the archive unloadable.

`collision.glb` or `collision.obj` is optional. If present, it is the collision authority after validation. If absent, the game generates collision deterministically from `scene.glb` using rules in `scenario.ini`, such as node tags, material tags, mesh name prefixes, and default walkable/collidable policies.

`rooms.obj` should become a generated compatibility/debug artifact, not the primary user-edited source. It may remain in `_meta/` or generated cache while migration code needs it, but the authoring contract should not require editing two similar meshes.

Implementation note, 2026-05-26: generated `.pdscenario` archives now emit root `scene.glb` as the public level source and catalog primary file. Public `rooms.obj`, `tiles.tsv`, `scenario.mtl`, `visual/scene.obj`, `visual/scene.mtl`, `visual/materials.tsv`, and visual texture folders are stale outputs and fail strict archive conformance. `scenario.ini` declares `scene_file = scene.glb` and `runtime_source_file = scene.glb`; runtime metadata prefers `scene_file`, accepts optional `collision_file`, and builds collision from the override or scene source through `modAssetCompilerBuildColmesh()`.

## Navigation

Bot navigation should be generated from the scenario source, not manually carried as opaque legacy waypoint dumps.

Inputs:

- Scene/collision geometry.
- Pads, spawn records, cover tags, trigger volumes, and traversal volumes.
- Bot-profile capabilities such as canJump, canWallRun, canCeilingRun, canDrop, and canUseDoors.
- Scenario or gamemode constraints.

Generated outputs:

- Walkable surface graph.
- Jump links.
- Drop links.
- Door/lift/interactive traversal links.
- Wall and ceiling traversal graph for capable bot profiles.
- Validation report with unreachable spawn/pickup/objective anchors.

Generated nav data belongs in `_meta/` or private runtime cache unless we intentionally expose a readable override file later. The generation must be deterministic so host/client, rebuilds, and tests agree.

Implementation note, 2026-05-25: `navigation.ini` and `_meta/generated-navmesh.json` are emitted with deterministic source inputs and capability classes for walk, jump, drop, wall, and ceiling traversal.

## Setup And Mission Behavior

The raw `setup.tsv`, `mpsetup.tsv`, and `visual_segments.tsv` public outputs have been removed because they expose legacy table/model numbers instead of catalog-ID source. The replacement is decoded content:

- Props/entities: type, catalog asset, transform, health, flags, collision profile, interaction hooks.
- Doors/lifts/moving platforms: endpoints, timings, links, sounds, access rules.
- Pickups/weapons/ammo/shields: item refs, spawn pads/volumes, respawn rules.
- Characters: character refs, team/squadron, spawn, perception, default behavior graph/profile.
- Mission objectives: named criteria, stage flags, required objects, rooms, holograph, fail/complete rules.
- Briefing/text refs and localization keys.
- Tags and object relationships.
- Scenario-level trigger volumes and global settings.

Reusable level-local behavior lives in `level.graph.json`. Campaign story flow, objective progression, cutscenes, checkpoints, unlocks, and mission phase logic belong in `.pdmission` graph assets that reference one or more `.pdscenario` archives.

Implementation note, 2026-05-25: raw setup/mpsetup/visual word dumps are no longer public outputs. The extractor emits `objects.tsv` and `objectives.tsv` with named records and catalog-ID asset refs. `level.graph.json` links scene, collision, navigation, and decoded setup/objective tables. `.pdmission` descriptors, scanner/distribution/runtime bindings, and examples now prefer `mission.graph.json`.

## Runtime Parity

The migration must follow the weapon graph model: audit the original setup/AI/objective behavior first, define graph modules from the existing behavior, feed runtime from graph records, and keep original behavior parity until replacements are proven.

Acceptance:

- No authored `.bin` payloads.
- No raw public setup word dumps.
- Scenario opens textured in Blender and 3DS Max from the public `scene.glb`/`scene.gltf` source.
- Scenario runtime loads from that same scene source plus optional collision override.
- Collision generation fallback is deterministic and validated.
- Bot nav generation covers PC movement extensions.
- Mission/setup behavior graph coverage is mapped for every base mission before old setup execution paths are retired.

## Where To Look

- Archive family contract: [asset-archive-clean-formats.md](asset-archive-clean-formats.md).
- Weapon graph precedent: [weapon-graph-runtime-cutover-plan.md](weapon-graph-runtime-cutover-plan.md).
- Collision pillar: [../../pillars/physics-collision.md](../../pillars/physics-collision.md).

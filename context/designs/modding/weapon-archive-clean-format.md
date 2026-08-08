# Clean .pdweapon Archive Format

Status: target authoring format created 2026-05-22. Kanban owner: `c3832`.

This document records the clean weapon archive shape before we rebuild extraction and examples around it. It supersedes the earlier root-level `behavior.graph.json`, root `manifest.json`, and root `nested_payloads.json` weapon archive layout as the target authoring format. Current runtime code may keep transition readers while the migration lands.

## Goals

- A `.pdweapon` is a zip-openable, human-readable typed asset archive.
- Base-game and mod weapons use the same content model. Base weapons may carry extra provenance for known hi/lo source relationships, but mod authors are not required to provide high/low variants.
- Catalog identity is still catalog-owned. Base content has known catalog IDs; authored mods may get IDs dynamically, but all references stored in descriptors and manifests are human-readable catalog ID strings.
- `.pdmod` is the distribution envelope only. The typed assets inside remain the catalog units.
- A weapon archive must be self-contained for editing, cloning, sharing, validation, and loading. If it depends on a custom projectile, entity, skin, sound, model, or animation, that dependency is embedded or packaged in the same `.pdmod` dependency closure.

## Canonical Layout

```text
weapon.ini
behavior/
  primary.graph.json
  secondary.graph.json
  settings.json
  variables.json
  shared-context.json
bindings/
  presentation.json
dependencies/
  assets/
    models/
      weapon.pdmesh
    materials/
      default.pdmaterial
    textures/
      body.pdtexture
      grip.pdtexture
    animations/
      idle.pdanim
      fire.pdanim
      reload.pdanim
    audio/
      fire.pdsfx
      reload.pdsfx
    projectiles/
      primary.pdprojectile
    entities/
      deployed.pdentity
    ui/
      icon.pdui
      reticle.pdui
_meta/
  manifest.json
  inventory.json
  provenance.json
  validation.json
  hashes.json
```

Folders are omitted when unused. `dependencies/assets/` embeds cross-family assets as intact typed archives. The weapon root owns weapon identity, behavior, and bindings; it does not flatten mesh, texture, material, animation, audio, projectile, entity, or UI internals into loose root folders in release-format output. Transition readers may accept older `models/`, `materials/`, `textures/`, `animations/`, `sounds/`, `projectiles/`, `entities/`, and `ui/` folders until extraction is rebuilt, but new emitters and examples should use typed dependencies.

## Required Files

`weapon.ini` is the root descriptor. It carries the weapon display name, optional authored catalog ID, mod namespace hints, default model path, material assignment path, behavior entry paths, ammo/display defaults, and base fallback declarations.

The weapon model is normally an embedded `.pdmesh` dependency. That `.pdmesh` owns geometry, hierarchy, sockets, LODs, UVs, skinning, and optional collision proxy; its own material and texture payloads remain typed dependencies. GLB/GLTF/OBJ source files may appear inside the `.pdmesh` archive, not as loose weapon-owned files.

`behavior/primary.graph.json` and `behavior/secondary.graph.json` are the authoring surfaces for weapon modes. Shared state lives in `behavior/shared-context.json`, and constants/tuning live in `behavior/settings.json` plus `behavior/variables.json`. The compiler may combine these into one runtime IR, but the authored archive exposes the modes separately.

`_meta/manifest.json` is machine-owned dependency metadata. It declares required and optional dependency roles, embedded archive paths under `dependencies/assets/`, content digests, version requirements, and base-game fallbacks. Keeping it under `_meta/` prevents root clutter while preserving the manifest contract.

## Models And Hands

Authored mods provide one weapon model by default. If an author supplies additional first-person/world variants, those are optional and declared in `weapon.ini` or `_meta/manifest.json`.

Base-game extraction may know a hi/lo source relationship. That relationship is provenance, not a burden on mod authors. The clean output should name the authored intent, such as `models/weapon.glb` and optional `models/world.glb`, while source slots and original hi/lo labels stay under `_meta/provenance.json`.

Hands are character-owned, not weapon-owned. Weapon placement uses the established
`posx`, `posy`, `posz`, and `muzzlez` fields plus the nested model hierarchy.
Weapon-level grip-socket declarations are not part of v1 because no production
hand-animation attachment backend exists. Archives declaring
`grip_sockets_file` or `bindings/grip-sockets.json` are rejected rather than
silently ignored. Character hands, hand skins, and character-specific hand
material choices come from the character/body asset family.

## Materials And Skins

The nested `.pdmesh` owns its authored material elements and typed material and
texture dependencies. Weapon-level `material_slots_file` and
`bindings/material-slots.json` are not part of v1 because the held/world model
compiler has no second material-override consumer. Such declarations are
rejected. A future reusable weapon-skin contract must version the archive and
connect one named-slot authority to both held and world rendering before it is
advertised.

## Behavior

The behavior folder owns mode graphs plus weapon-level settings:

- Primary behavior graph.
- Secondary behavior graph.
- Shared context used across modes, projectiles, deployed entities, owner/team state, detonator links, targeting policy, hacking policy, and damage credit.
- Weapon variables and tunables, including ammo behavior, fire cadence, recoil, recovery, spread, noise, aim settings, reticles, overlays, zoom, model visibility, and screen-state hooks.

The graphs remain declarative. They do not contain arbitrary script text. Runtime uses deterministic compiled IR generated from these files.

## Dependencies And Fallbacks

Custom projectiles and deployed behavior are separate typed assets:

- `.pdprojectile` for launched, thrown, guided, bouncing, sticky, or timed physical behavior.
- `.pdentity` for armed, deployed, stuck, turret, pickup, or recoverable world behavior.

The weapon manifest declares dependencies by role:

```json
{
  "role": "primary_projectile",
  "id": "mod_example:projectile_rocket",
  "archive": "dependencies/assets/projectiles/primary.pdprojectile",
  "fallback": "base:projectile_rocket"
}
```

Fallbacks are base-game catalog IDs. They are used only when an optional dependency is missing, rejected, or intentionally absent. Required custom dependencies must be embedded in the `.pdweapon` or included beside it in the exported `.pdmod`.

## .pdmod Packaging And Import

Exporting a mod gathers the weapon plus its dependency closure into one `.pdmod` transport archive. Dependency assets stay organized as typed files inside the package rather than being flattened into one opaque weapon blob.

Importing validates every typed asset before install. The client installs only missing or newer compatible assets, registers them by catalog ID, and leaves already-present matching dependencies alone. Rejected optional dependencies fall back to the base catalog IDs declared by the weapon manifest.

## Stale Layout Removal

The migration must remove new emissions and examples of the older weapon layout:

- Root `behavior.graph.json`.
- Root `manifest.json`.
- Root `nested_payloads.json`.
- `models/`, `materials/`, `textures/`, `animations/`, `sounds/`, `projectiles/`, `entities/`, and `ui/` as canonical loose cross-family payload folders.
- `audio/` as the canonical sound folder.
- Weapon-owned `hand.gltf` or hand model descriptors.
- `bindings/material-slots.json`, `bindings/grip-sockets.json`,
  `material_slots_file`, and `grip_sockets_file` in v1 archives.
- Reference-only dependency manifests where custom dependencies are not embedded or packaged.
- Generated archive entries named from raw numeric slots rather than human-readable catalog IDs.

Historical notes may mention the old layout, but active docs, examples, tests, UI labels, and emitters should point at this clean format.

## Acceptance

- `examples/modding/typed-pdxxx-basic` includes a `.pdweapon` that follows this layout and embeds cross-family payloads under `dependencies/assets/` as typed archives.
- Base extractor output follows the clean layout, with legacy numeric and source-slot data confined to `_meta/provenance.json`.
- Validators accept this layout and reject unresolved internal references, authored `.bin`, `.pdwpn`, and root machine-metadata clutter in newly emitted archives.
- The Modding Hub Weapons tool saves this layout.
- `.pdmod` export/import installs dependency assets as typed catalog units and does not duplicate already-present matching dependencies.

## Where To Look

- Kanban: `c3832` for this weapon format, `c3824` for the cross-asset migration plan.
- Runtime graph work: `context/designs/modding/weapon-behavior-graph-assets.md`.
- Runtime cutover: `context/designs/modding/weapon-graph-runtime-cutover-plan.md`.

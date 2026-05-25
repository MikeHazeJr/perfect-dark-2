# Asset Archive Modularization Verification - 2026-05-24

## Scope

Mike asked whether the planned `*.pdxxx` archive families are structurally sufficient to modularize the game: each asset must contain what it needs to load, function, and reference dependent assets without becoming a descriptor-only shell.

This audit checks the approved plan against live runtime surfaces:

- `context/designs/modding/asset-archive-clean-formats.md`
- `context/designs/modding/weapon-archive-clean-format.md`
- `port/include/assetcatalog.h`
- `port/src/asset_archive_policy.c`
- `port/src/assetcatalog_scanner.c`
- `port/src/net/netdistrib.c`
- `port/include/net/netmanifest.h`
- `port/src/assetcatalog_load.c`

## Verification Criteria

A planned family is ready for extraction implementation only when all of these are true:

1. It has a root descriptor and public authored payload sufficient to instantiate the asset.
2. Cross-family dependencies are embedded as intact typed archives under `dependencies/assets/` or declared as explicit base fallbacks.
3. Its catalog type is real, unambiguous, and matches the archive policy mapping.
4. Scanners, packers, distribution, and validators recognize the descriptor and extension.
5. The catalog lifecycle can load the payload or intentionally activate metadata for a runtime subsystem adapter.
6. Match manifests can require and distribute the asset when it is reachable from online play.

## Result

The planned archive structures are conceptually sound after the post-decision matrix update, but they are not yet implementation-verified. The extraction card must close the gaps below before we can say every family is modularized end to end.

## Must Fix Before Or During `c3838`

### Catalog Type Mismatches

Approved mappings are ahead of the live enum:

- `.pdmaterial` is planned as `ASSET_MATERIAL`, but live `asset_archive_policy` maps it to `ASSET_NONE`.
- `.pdscenario` is planned as `ASSET_SCENARIO`, but live policy/scanner/distribution map `scenario.ini` to `ASSET_GAMEMODE`.
- `.pdfont` is planned as `ASSET_FONT`, but live policy/scanner/distribution map `font.ini` to `ASSET_UI`.
- `.pdtheme` has no live `ASSET_THEME`.

Extraction implementation must either add these catalog types or explicitly revise the plan. Current approved decisions require adding the types.

### Missing Archive Policy Coverage

Live `asset_archive_policy` does not yet recognize these approved typed extensions:

- `.pdskin`
- `.pdeffect`
- `.pdprop`
- `.pdvehicle`
- `.pdmission`
- `.pdgamemode`
- `.pdbotprofile`
- `.pdhud`
- `.pdtheme`

Without policy rows, release validation, descriptor discovery, typed archive scanning, and packer gates cannot enforce the planned structures.

### Scanner, Packer, And Distribution Gaps

The current scanner and network-distribution descriptor maps only partially cover the approved families. Several existing families also map to older types:

- `scenario.ini` still registers as `ASSET_GAMEMODE`.
- `font.ini` still registers as `ASSET_UI`.
- `material.ini`, `effect.ini`, `vehicle.ini`, `mission.ini`, `gamemode.ini`, `botprofile.ini`, and `theme.ini` are not fully recognized across scanner/distribution/packer surfaces.
- `.pdmesh` is policy-visible, but descriptor-name recognition is not consistently present across external-layout scanner/distribution maps because the current path treats it mainly as a typed archive.

The extraction session should add one shared descriptor/type table where possible so policy, scanner, packer, distribution, and examples do not drift.

### Manifest Coverage

`netmanifest.h` currently has manifest types through body/head/stage/weapon/component/model/anim/texture/lang/audio/projectile/entity. That is not enough for a fully modular online match when dependencies can include character assemblers, arenas, scenarios, UI, HUD, skins, props, vehicles, missions, gamemodes, bot profiles, effects, materials, fonts, and themes.

Implementation can solve this either by adding explicit `MANIFEST_TYPE_*` entries for all match-reachable families or by introducing a typed generic manifest entry that carries the catalog asset type. The important requirement is that every dependency reachable from a match manifest is represented by catalog ID, type, digest, and closure status.

### Runtime Utilization Adapters

Catalog lifecycle currently activates many non-model families as metadata payloads. That is a valid load state, but not enough by itself to prove the asset is usable. These families require explicit adapters from catalog metadata to runtime systems:

- `.pdskin`: apply slot/material/texture overrides.
- `.pdeffect`: instantiate effect graphs/timelines on scene, prop, weapon, or screen targets.
- `.pdvehicle`: bind handling, seats, cameras, hardpoints, hull/collision, damage states.
- `.pdmission`: drive briefing, objectives, phase flow, cutscenes, unlocks, checkpoints.
- `.pdgamemode`: drive scoring, teams, objectives, timers, loadouts, respawn behavior.
- `.pdbotprofile`: apply perception, movement abilities, loadout preferences, behavior tags.
- `.pdhud`: compose widgets/data bindings from `.pdui`, `.pdfont`, `.pdlang`, audio refs.
- `.pdtheme`: compose chrome/style tokens and procedural or animated texture behavior.
- `.pdmaterial` and `.pdfont`: need first-class typed catalog lifecycle if the approved `ASSET_MATERIAL` and `ASSET_FONT` decisions stand.
- `.pdscenario`: needs first-class scenario lifecycle separate from `ASSET_GAMEMODE`.

### Dependency Manifest Schema

The plan requires dependency closure, but implementation still needs a concrete shared `_meta/manifest.json` dependency schema. Minimum fields:

- `role`
- `type`
- `id`
- `archive`
- `required`
- `version` or `compatibility`
- `sha256` when available
- `fallback.id`
- `fallback.reason`

Validators must reject unresolved dependencies unless an embedded archive or explicit fallback satisfies the role.

### Weapon Detail Correction

`weapon-archive-clean-format.md` previously showed loose `models/`, `sounds/`, `textures/`, etc. folders as the canonical weapon layout. That contradicted the approved dependency-archive rule. The document now makes the canonical release layout use `dependencies/assets/` with intact typed archives for mesh/material/texture/animation/audio/projectile/entity/UI dependencies.

## Family Structure Verdict

The authoring boundaries are acceptable for all approved game-content families if `c3838` enforces the gates above. The families with the most implementation risk are:

- `.pdscenario`, because it must become `ASSET_SCENARIO` and separate map/world content from mission and gamemode wrappers.
- `.pdmaterial` and `.pdfont`, because approved first-class catalog types are missing.
- `.pdtheme`, because both catalog type and runtime/theme adapter are missing.
- `.pdvehicle`, `.pdmission`, `.pdgamemode`, `.pdbotprofile`, `.pdhud`, `.pdeffect`, and `.pdskin`, because current catalog metadata is not enough to prove runtime utilization.

The extraction implementation should not mark a family done because an archive exists. A family is done only when it passes descriptor, dependency-closure, catalog-type, scanner/packer/distribution, lifecycle, and runtime-adapter tests.

## Next Action

Update `c3838` to make these verification gates explicit, then begin implementation at `c3838-s1` by centralizing the family descriptor/type table and dependency manifest schema before rebuilding per-family emitters.

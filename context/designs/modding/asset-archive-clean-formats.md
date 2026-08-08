# Clean Asset Archive Family Formats

Status: frozen target contracts created 2026-05-24; post-decision load/use sweep updated 2026-05-24; strict generated-archive conformance gate added 2026-05-26; definitive optional-slot justification enforced 2026-05-27; runtime ROM fallback failure rule added 2026-05-27. Kanban owners: `c3824` for the frozen family contracts, `c3842` for native-source and strict conformance hardening, and `c3844` for runtime ROM fallback removal.

This document records the clean self-contained archive layouts for every current typed or otherwise referenceable Asset Pipeline family before the extraction, examples, validators, and release gates are rebuilt around them. It includes the later approved first-class families from the Asset Decisions tab. The broad implementation sweep starts from this file rather than inventing new family contracts piecemeal.

## Shared Contract

Every typed `*.pdxxx` archive is a zip-openable authoring unit. Changing the extension to `.zip` should reveal the descriptor and readable source files needed to edit, clone, share, validate, and load that single asset.

The public authoring files are also the game-facing source of truth. The client should ingest those files through the catalog/provider asset pipeline and derive any renderer, GPU, collision, animation, audio-codec, behavior-runtime, or other engine-ready products from them as source-hashed cache. The pipeline may pay a small import/cache cost to avoid duplicate authored representations; what it must not do is require users or extractors to maintain one editable file and a separate opaque runtime file for the same asset.

Runtime ROM fallback is an asset-chain failure. ROM may seed extraction/verification, but after that runtime loads must resolve through extracted typed archives, FileProvider/catalog source, or deterministic source-derived cache. A base-game dependency declaration is not permission to read bytes from ROM at runtime; if the on-disk/catalog chain is missing, the loader should fail loudly and the owning family should be fixed.

The archive has two zones:

- Public authoring zone: the root descriptor and readable files or purpose-named folders that modders naturally edit.
- `_meta/`: machine-owned manifest, inventory, provenance, validation reports, source handles, compatibility notes, and hash ledgers.

New emitters write machine data under `_meta/`. Transition readers may accept legacy root `manifest.json` and root SHA sidecars, but release/package validation must fail newly emitted or installed stale outputs that are non-zip typed files, `.pdwpn`, descriptor-less, authored `.bin` backed, or unresolved internally.

All asset references use catalog ID strings. Numeric or legacy-symbol asset references such as `model_id = 42`, `modelnum`, `filenum`, `weapon_id`, `sound_id`, `texnum`, `MODEL_*`, or `FILE_*` are invalid public authoring fields. A reference field is metadata, not dependency closure, unless the referenced authored payload also resolves inside the archive or inside an embedded typed dependency archive. Higher-level assets embed dependency assets as intact typed archives under `dependencies/assets/<type>/<id>.<typed-asset>` unless the family-specific notes below define a narrower legacy transition path. Validators fail unresolved references unless the archive embeds the dependency or declares an approved base dependency with catalog ID, compatibility/version/hash where available, reason, and on-disk/catalog provider source.

Each root descriptor must be sufficient to instantiate the asset without consulting legacy numeric tables: schema version, catalog ID or namespace hint, display name, role/kind, authored source paths, dependency roles, compatibility tags, fallback declarations, and loader/importer/exporter revision markers where relevant. Raw editable source files are allowed when owned by that family; cross-family resources stay as typed dependency archives.

Tools must be able to export an archive into an accessible folder of modder-editable files and import that folder back into the same valid typed archive. Import reconstructs descriptors, `_meta/`, hashes, and embedded typed dependencies rather than preserving loose cross-family sidecars.

Authored `.bin` payloads, raw preprocessed dumps, and parallel hand-maintained runtime payloads are rejected. Generated runtime cache remains private, readable when useful, source-hashed, rebuildable, and unshipped. `.pdmod` remains transport only.

Validation is strict by family schema, not "at least one editable file." `tools/asset_archive_conformance.py` opens every typed archive, checks the exact required/allowed public entries, rejects stale public files and numeric/legacy public asset references, verifies `_meta/manifest.json`, and recursively validates embedded typed dependency archives. Clean examples and fresh generated base archives must pass this gate before the migration is considered conformant.

Allowed entries are definitive schema slots, not a tolerance bucket. A public file may be optional only when the contract records its semantic role, owner subsystem or tool, loader/importer behavior, deterministic absence behavior, and final-vs-transition status. Examples: `collision.glb` is a scenario collision override whose absence means deterministic collision generation from `scene.glb`; arena `preview.png` is selection UI presentation data whose absence means generated/default UI preview; embedded `dependencies/assets/.../*.pdxxx` entries are typed dependency closure, not loose sidecars. Any public file, dependency path, or `_meta/` entry without a documented role must be removed from the schema and rejected by validation. This was completed under `c3842-s7` through `c3842-s9`.

## Definitive Optional Slot Matrix

The executable source of truth is `tools/asset_archive_conformance.py`: `OPTIONAL_PUBLIC_SLOT_CONTRACT` records each optional public exact path or glob with semantic role, owner, loader/importer behavior, absence behavior, and final/transition status; `META_SLOT_CONTRACT` plus the common meta contract do the same for `_meta/`. This table mirrors the family-level intent so users can understand the archives without reading the validator.

| Family | Optional public slots | Why they exist / absence behavior |
|--------|-----------------------|-----------------------------------|
| `.pdweapon` | `bindings/*.json`, `dependencies/assets/{models,materials,textures,animations,audio,projectiles,entities,ui}/*.pdxxx` | Bind graph requests to typed dependency archives. Missing bindings mean graph/default presentation values; missing dependencies are valid only when the graph does not use that class or declares an approved base dependency that resolves through catalog/on-disk provider source. |
| `.pdprojectile` | `dependencies/assets/{models,materials,textures,audio,effects,entities}/*.pdxxx` | Projectile graph visual/audio/effect/entity closure. Absence means the projectile graph does not use that dependency class or uses a declared base dependency that resolves through catalog/on-disk provider source. |
| `.pdentity` | `composition.json`, `dependencies/assets/{props,models,materials,textures,audio,effects}/*.pdxxx` | Optional world composition and typed dependency closure. Without composition the entity is behavior-only. |
| `.pdmaterial` | `dependencies/assets/texture/*.pdtexture`, `dependencies/assets/textures/*.pdtexture`, `dependencies/assets/effects/*.pdeffect` | Materials may bind reusable textures/effects. Absence means inline constant material values or no effect layer. |
| `.pdtexture` | one of `texture.png`, `texture.tga`, `texture.jpg`, `texture.jpeg` | Standard editable image source alternatives. At least one must be present. |
| `.pdcharacter` | `portrait.png`, `body.pdbody`, `head.pdhead`, `dependencies/assets/{body,bodies,head,heads,skins,voice,animations,ui}/*.pdxxx` | Top-level character assembler dependencies and portrait. Body/head are required through one of the accepted slots; other dependencies default to shared/base presentation. |
| `.pdhead` | `dependencies/assets/{materials,textures,animations}/*.pdxxx` | Head mesh material, texture, and animation dependency closure. Absence means the mesh/shared animation source carries the default. |
| `.pdbody` | `hand.pdmesh`, `dependencies/assets/{materials,textures,animations}/*.pdxxx` | Optional first-person hand mesh plus typed visual/animation closure. Without `hand.pdmesh`, `mesh.pdmesh` is the fallback hand/body source. |
| `.pdarena` | `preview.png`, `thumbnail.png`, `dependencies/assets/scenarios/*.pdscenario` | Selection UI art and playable scenario closure. Only explicit Random selector arenas may omit a scenario dependency. |
| `.pdscenario` | `scene.glb` or `scene.gltf`, `collision.glb`, `collision.obj`, `mission.graph.json` | One DCC-openable scene source is required. Decoded navigation tables are required public source files; empty tables mean deterministic navigation is generated from the scene, pads, and volumes. Collision overrides replace deterministic scene-derived collision. |
| `.pdmesh` | `model.gltf`, `model.glb`, `model.obj`, `model.mtl`, `model.nodes.json`, `model.parts.json`, `model.faces.json`, `model.render.json`, `export_version.txt`, `dependencies/assets/{materials,textures}/*.pdxxx` | One mesh source is required. OBJ may use `model.mtl`; extracted native modeldefs use semantic JSON for hierarchy, part lookup, face matrix bindings, and render command order. Exporter provenance is diagnostic. Public mesh TSV sidecars are not valid final source. Textures/materials are typed dependencies or embedded/declared by the model source, never loose mesh sidecars. |
| `.pdanim` | `animation.gltf`, `animation.glb`, `commands.json` | Animation source alternatives. Character/body animation uses semantic GLTF/GLB channel source; native zero-frame placeholders use explicit no-op GLTF, not zero-count sample accessors; weapon/inventory animation uses semantic command source with catalog-ID animation/audio refs. At least one source shape is required. |
| `.pdsfx` | `sample.ogg`, `sample.flac` | Optional alternate standard audio source alongside authoritative `sample.wav`. Absence uses WAV. |
| `.pdvoice` | `sample.wav`, `sample.mp3`, `subtitle.json`, `locales/*.wav`, `locales/*.ogg`, `locales/*.mp3` | Spoken-line source and locale variants. Absence uses no subtitle and the default `sample.wav` or `sample.mp3`. |
| `.pdsong` | `sequence.mid`, `sequence.json`, `track.wav`, `track.ogg`, `track.mp3`, `cues.json`, `sections.json` | Music source alternatives and cue/section metadata. A sequence or track source is required; cues/sections are absent when not authored. |
| `.pdui` | `texture.png`, `texture.tga`, `textures/*.png`, `textures/*.tga`, `layout.json`, `nineslice.ini` | UI owns its own visual source. Multi-slot textures and semantic layout/nine-slice metadata are only present when needed. |
| `.pdfont` | `font.ttf`, `font.otf`, `glyphs.pgm`, `font.metrics.json` | Vector or bitmap font source alternatives. Bitmap metrics and kerning live in semantic JSON when bitmap source is used. |
| `.pdlang` | none | Localization has a definitive two-file shape: `lang.ini` plus `strings.json`. |
| `.pdskin` | `texture.png`, `texture.tga`, `swatches.json`, `dependencies/assets/{material,materials,texture,textures}/*.pdxxx` | Skin appearance source can be inline texture/swatch data or typed material/texture dependencies. Absence means the compatible asset default is used. |
| `.pdeffect` | `effect.graph.json`, `timeline.json`, `dependencies/assets/{materials,textures,audio}/*.pdxxx` | Effect source alternatives and typed visual/audio closure. One graph or timeline is required. |
| `.pdprop` | `model.gltf`, `model.glb`, `model.obj`, `mesh.pdmesh`, `behavior.graph.json`, `dependencies/assets/{models,materials,textures,effects}/*.pdxxx` | Prop model source/dependency alternatives plus optional behavior and effects. A model source or model dependency is required. |
| `.pdvehicle` | `model.gltf`, `model.glb`, `model.obj`, `mesh.pdmesh`, `behavior.graph.json`, `dependencies/assets/{models,materials,textures,audio,effects,weapons}/*.pdxxx` | Vehicle model, behavior, audio/effect, and mounted weapon closure. A model source or model dependency is required. |
| `.pdmission` | `dependencies/assets/scenario/*.pdscenario`, `dependencies/assets/scenarios/*.pdscenario`, `dependencies/assets/audio/*.pdsfx`, `dependencies/assets/voice/*.pdvoice` | Campaign mission binds scenario, audio, and voice dependencies. A scenario dependency is required through one accepted path. |
| `.pdgamemode` | `dependencies/assets/hud/*.pdhud`, `dependencies/assets/audio/*.pdsfx` | Rule packs can carry private HUD/audio dependencies. Absence uses default HUD/audio. |
| `.pdbotprofile` | none | Bot profile has a definitive descriptor plus `profile.json` source. |
| `.pdhud` | `texture.png`, `texture.tga`, `dependencies/assets/{ui,fonts,lang,audio}/*.pdxxx` | HUD can own a simple texture source or compose typed UI/font/lang/audio dependencies. Absence uses defaults or required UI dependency. |
| `.pdtheme` | `dependencies/assets/{ui,font,fonts,audio,music,effects}/*.pdxxx` | Themes compose UI, font, audio, music, and effect assets. Missing classes mean no override for that class. |

Definitive `_meta/` slots are common across families: `_meta/manifest.json`, `_meta/inventory.json`, `_meta/provenance.json`, `_meta/validation.json`, `_meta/source-handles.json`, `_meta/hashes.json`, plus `_meta/<public-entry>.sha256` only when the matching public source entry exists. Scenario may also carry `_meta/generated-collision.json` and `_meta/generated-navmesh.json`; weapon may carry `_meta/nested-payloads.json` as private dependency-closure inventory. No other `_meta/` entry is valid.

Mesh and scenario geometry also have a current integer-native runtime boundary. Authoring files may use normal GLTF/GLB/OBJ floating-point coordinates so modders can work in Blender, 3DS Max, and similar tools, but extracted base archives must preserve native integer/fixed-point vertex, UV/tile, room/portal, part, matrix, and render-order semantics in editable source metadata. Imports of custom geometry must quantize into documented integer native units before runtime cache/modeldef use, and validators should reject or warn on values that overflow, collapse, or otherwise lose parity during that transform. This is a geometry/runtime representation limitation only; asset references remain catalog ID strings.

## Frozen Family Contracts

| Family | Required root descriptor | Public authored payload | Ownership boundary |
|--------|--------------------------|--------------------------|--------------------|
| `.pdweapon` | `weapon.ini` | `behavior/`, `bindings/`, optional visuals/audio/UI refs, embedded typed dependencies | Weapon identity, slots, fire modes, and behavior bindings. Projectile/entity/model/material internals stay separate typed dependencies. |
| `.pdprojectile` | `projectile.ini` | flight/collision/damage/lifecycle data, optional behavior graph, dependency refs | Reusable launched/thrown projectile behavior. Visual, audio, effect, and entity handoff payloads stay typed dependencies. |
| `.pdentity` | `entity.ini` | `bindings.json`, optional `behavior.graph.json`, optional `composition.json` | Runtime world archetype state, lifecycle, interaction, and composition. It does not replace prop, vehicle, map, model, material, texture, or effect families. |
| `.pdmaterial` | `material.ini` | material params, render flags, slot compatibility, texture/effect bindings | Reusable render/surface material. Texture payloads remain typed dependencies. |
| `.pdtexture` | `texture.ini` | editable image source, alpha/palette/mip/import settings | Atomic reusable texture. GPU/runtime cache is rebuildable private data, not authoring payload. |
| `.pdcharacter` | `character.ini` | roster/faction/unlock tags, portraits, default body/head/skin/voice/anim/UI bindings | Top-level character assembler. Body/head/skin/voice/animation/UI assets remain typed dependencies. |
| `.pdhead` | `head.ini` | head socket/expression/material-slot metadata and presentation bindings | Focused reusable head. Mesh, material, texture, and animation payloads remain typed dependencies. |
| `.pdbody` | `body.ini` | skeleton/rig contract, body proportions, sockets, first-person hand bindings, material slots | Focused reusable body. Body and hand roles stay explicit so dedupe does not collapse responsibilities. |
| `.pdarena` | `arena.ini` | preview/thumbnail, match defaults, spawn playlist metadata, scenario reference | Multiplayer-facing arena selection wrapper around an embedded `.pdscenario`. Explicit Random selector meta-arenas are the only no-scenario variant and must declare that role in the descriptor. |
| `.pdscenario` | `scenario.ini` | DCC-openable textured `scene.glb` or `scene.gltf`, optional `collision.glb`/`collision.obj`, pads/volumes, spawn profiles, generated navigation inputs, graph-linked level settings/triggers | Actual level/map content. Arena, mission, and gamemode wrappers select or configure it; the game consumes the same scene source through the asset pipeline. |
| `.pdmesh` | `mesh.ini` | geometry, hierarchy, skinning/rig binding, sockets, LODs, optional collision proxy | Reusable geometry/model data. Materials and textures remain typed dependencies. |
| `.pdanim` | `animation.ini` | timeline/channel data, events/notifies, retargeting/target rig metadata, semantic weapon commands | Reusable animation clip or sequence. Body, weapon, mesh, and audio refs remain catalog IDs with embedded dependencies when needed; configured weapon-command SFX aliases are archive-backed `.pdsfx` source, not runtime-only aliases. |
| `.pdsfx` | `sound.ini` | editable source audio, loop points, attenuation and mixer tags, import settings | Atomic sound effect. Codec/runtime cache is private and rebuildable. |
| `.pdvoice` | `voice.ini` | speaker/context/category metadata, locale audio variants, subtitle binding, fallback rules | Spoken-line metadata and audio variants. `.pdlang` owns text strings. |
| `.pdsong` | `music.ini` | `sequence.mid`, semantic `sequence.json`, `track.wav`, `track.ogg`, or `track.mp3`, loop/cue/adaptive metadata | Reusable music asset. Generated playback cache is private and rebuildable. |
| `.pdui` | `ui.ini` | `texture.png` or `texture.tga` at root or under `textures/`, optional readable nine-slice/layout metadata | Atomic UI visual or chrome asset. Screens and themes compose `.pdui` refs rather than hiding unrelated UI payloads in one archive. |
| `.pdfont` | `font.ini` | authored `font.ttf`/`font.otf`, or decoded ROM bitmap font files `glyphs.pgm` and `font.metrics.json` | Reusable font face/range asset. Charset/range/import notes live in descriptor and `_meta/`, not raw runtime blobs. |
| `.pdlang` | `lang.ini` | `strings.json` as editable indexed text source | Text/localization owner. Voice assets bind to string keys and audio locale fallbacks; `.pdlang` does not own spoken audio. |
| `.pdskin` | `skin.ini` | material slot overrides, texture/material bindings, palette/color data, preview swatches | Appearance variant for compatible bodies, heads, weapons, props, or vehicles. Geometry stays in mesh/body/head/prop/vehicle assets. |
| `.pdeffect` | `effect.ini` | effect graph or timeline, emitter/decal/beam/post-process data, attachment rules | Reusable visual or feedback effect. Materials, textures, audio, lights, camera shake, and target assets stay typed dependencies. |
| `.pdprop` | `prop.ini` | placement metadata, sockets, collision profile, simple interaction/damage/physics flags | Physical world object payload. Behavior-heavy active/deployed objects use `.pdentity` and can depend on `.pdprop`. |
| `.pdvehicle` | `vehicle.ini` | handling, hull/collision, seats, cameras, hardpoints, damage states, animation/audio bindings | Drivable or interactable vehicle asset. Mesh, material, texture, audio, effect, weapon, and occupant refs stay typed dependencies. |
| `.pdmission` | `mission.ini` | briefing, objectives, phase flow, cutscene list, scenario refs, unlock/checkpoint rules | Campaign wrapper around one or more `.pdscenario` assets. It does not own map geometry or general multiplayer rules. |
| `.pdgamemode` | `gamemode.ini` | scoring, teams, timers, loadouts, objective rules, spawn modifiers, compatible scenario tags | Combat Simulator or custom rules wrapper. Scenario/arena/map content remains separate. |
| `.pdbotprofile` | `botprofile.ini` | skill/personality, perception, movement abilities, loadout prefs, behavior tags | Reusable AI profile. Character, voice, weapon, team, and special ability refs remain catalog dependencies. |
| `.pdhud` | `hud.ini` | layout/widgets, anchors, data bindings, reticle/radar/status rules | Reusable gameplay HUD composition. UI visuals, fonts, language, SFX, and theme refs stay typed dependencies. |
| `.pdtheme` | `theme.ini` | style tokens, chrome bindings, procedural or animated texture rules, accessibility tags | Menu/UI visual theme. It composes `.pdui`, `.pdfont`, `.pdsfx`, `.pdsong`, and optional `.pdeffect` assets. |

`.pdtool` is approved as a deferred high-priority follow-up, not part of the current game-content extraction sweep. If tools are packaged later, they need a separate secure tool/plugin package model rather than this normal runtime asset contract.

## Load And Utilization Closure

The extraction implementation should treat this as the minimum "can load and can be used" checklist. A family passes only when its descriptor, public payload, `_meta/` data, and embedded typed dependencies can feed the catalog loader, the relevant runtime subsystem, and mod tools without external lookup except approved base dependencies that resolve through catalog/on-disk provider source. Public payloads should be the native client input; generated products are cache derived by the asset pipeline, not extra source files authors must edit or keep synchronized.

| Family | Must be present to load | Must be present to utilize |
|--------|-------------------------|----------------------------|
| `.pdweapon` | `weapon.ini`, behavior entry files, dependency manifests, model/audio/projectile/entity refs | Held and AI fire-mode bindings, ammo/display defaults, nested-model materials/hierarchy, established held placement, sounds, animations, UI refs, projectiles/entities. Weapon-level material/grip override files are retired from v1 until a real renderer/attachment backend exists. |
| `.pdprojectile` | `projectile.ini`, motion, collision, damage, lifecycle, optional behavior graph | Owner/damage credit, impact rules, visual/audio/effect dependencies, entity transition or spawned payload refs. |
| `.pdentity` | `entity.ini`, bindings/composition, optional behavior graph | Lifecycle state, interaction rules, ownership/team context, prop/mesh/effect/audio dependencies. |
| `.pdmaterial` | `material.ini`, render/surface params, slot and texture/effect refs | Variants, compatibility tags, classic fields, optional PBR-ready fields; PBR material payloads remain standalone material assets. |
| `.pdtexture` | `texture.ini`, editable image source, alpha/palette/mip/import settings | Rebuildable GPU texture/cache data, usage tags, color space and compression intent. |
| `.pdcharacter` | `character.ini`, identity/roster tags, body/head/skin/voice/anim/UI refs | Unlock/roster presentation, default loadout/appearance assembly, dependency closure for all bound parts. |
| `.pdhead` | `head.ini`, socket/expression/material-slot metadata | Mesh/material/texture/animation deps, expression bindings, body compatibility tags. |
| `.pdbody` | `body.ini`, skeleton/rig, proportions, sockets, first-person hand bindings | Body and hand mesh/material deps, animation target metadata, attachment compatibility. |
| `.pdarena` | `arena.ini`, embedded `.pdscenario` for real arenas, preview/default refs | Multiplayer selection metadata, match defaults, spawn playlists, gametype/team overrides. Random selector meta-arenas resolve to a real arena at match start and do not carry level content. |
| `.pdscenario` | `scenario.ini`, textured DCC-openable scene source, optional collision override, pads/volumes, decoded setup tables/graphs, navigation generation inputs | Native scenario load from the same scene source, spawn profiles, mission/gamemode hooks, lighting/material deps, objective/phase override anchors, graph-linked triggers/global settings. |

## Scenario Authoring Correction

2026-05-25 direction: the prior `rooms.obj` plus `visual/scene.obj` split is transitional. The final `.pdscenario` contract should make immediate sense when opened by a modder.

Canonical target:

```text
scenario.ini
scene.glb
collision.glb          # optional override; collision.obj is allowed during transition
pads.json
volumes.json
spawns.json
navigation.ini
navigation/waypoints.json
navigation/waygroups.json
navigation/covers.json
navigation/paths.json
objects.json
setup.fields.json
objectives.json
mission.graph.json     # optional, or owned by .pdmission when campaign-specific
level.graph.json       # triggers, global settings, scenario-local behavior hooks
_meta/
  manifest.json
  inventory.json
  provenance.json
  validation.json
  generated-collision.json
  generated-navmesh.json
  hashes.json
```

`scene.glb` or `scene.gltf` is the primary level mesh, visual authoring source, and native scenario load source. It must carry mesh hierarchy, materials, UVs, and archive-local or embedded textures so Blender and 3DS Max open the level already textured without project-specific plugins. `scene.glb` is preferred for generated examples because it keeps buffers and textures in one file; `scene.gltf` is allowed when all referenced buffers/textures stay inside the archive through relative paths. `OBJ+MTL+TGA` may remain a transition/export fallback, but it is not the final scenario source contract.

Runtime should read `scene.glb`/`scene.gltf` through the catalog/provider asset pipeline, then derive private renderer, room/portal, collision fallback, and navigation cache from source hashes. Those generated products are speed aids and diagnostics, not the authored files that make the archive loadable.

2026-05-28 implementation note: generated base `scene.glb` files carry `_PD_ROOM` vertex metadata so the runtime can preserve room ownership while deriving the legacy-compatible tile cache directly from the public scene source. Author-created scenes may supply equivalent room tags through `_PD_ROOM` or a future documented scene-node/tag mapping; raw public `tiles.tsv` or `rooms.obj` remains invalid as an authored requirement.

`collision.glb` or `collision.obj` is optional. If present, the game uses it as the collision authority after validation. If absent, collision is generated deterministically from `scene.glb` using explicit import rules recorded in `scenario.ini` and `_meta/generated-collision.json`. Collision generation must preserve room/portal/floor metadata through named sidecars or scene node tags rather than requiring a second user-edited mesh by default.

Bot navigation is generated, not authored as opaque legacy waypoint dumps. Every scenario carries decoded public `navigation/waypoints.json`, `navigation/waygroups.json`, and `navigation/covers.json` rows so OG waypoint, group, and cover behavior can round-trip exactly through user-editable source files; these are semantic source rows, not raw preprocessed words or TSV-shaped binary dumps. Empty navigation rows mean the generation input is the scene/collision mesh plus pads, volumes, gameplay tags, public `navigation/paths.json`, and bot-profile capability rules. The generated nav data must support normal walkable surfaces, jump links, drop links, wall traversal, and ceiling traversal for bot profiles that opt into those movement abilities.

Raw preprocessed setup dumps are not acceptable public payloads. The extractor no longer writes public `setup.tsv`, `mpsetup.tsv`, or `visual_segments.tsv`; Scenario object rows now live in semantic `objects.json` with catalog IDs for asset refs, `setup.fields.json` is the named per-command setup field source that removes the prior summary-only gap without exposing raw words, and `ai/ailists.json` is the semantic AI command/list source with operand arrays instead of a flattened public command TSV. B-780 through B-789 moved objective, spawn, volume, pad, navigation path/table, portal, object, setup-field, and AI-list rows to semantic JSON. `level.graph.json` links scene/collision/navigation/setup/AI tables. Campaign-specific flow lives in `.pdmission` through `mission.graph.json`; reusable level-local triggers and global settings live in `.pdscenario`.
| `.pdmesh` | `mesh.ini`, geometry, UVs, hierarchy, sockets, LODs, skinning, collision proxy | Material/texture typed deps, rig binding, model and collision loader metadata. |
| `.pdanim` | `animation.ini`, timeline/channels/events/notifies, target metadata | Retargeting, weapon/body binding, optional legacy opcode listings, event refs. |
| `.pdsfx` | `sound.ini`, editable source audio | Loop points, attenuation, mixer/category tags, import/codec intent. |
| `.pdvoice` | `voice.ini`, speaker/context/category, locale audio variants | Subtitle/localization key bindings, fallback rules, priority/playback tags. |
| `.pdsong` | `music.ini`, sequence or track sources | Loop points, sections/cues, tempo/transition tags, optional adaptive layers. |
| `.pdui` | `ui.ini`, texture or texture slots, layout/nine-slice metadata | Scale/theme/chrome/atlas role, GL upload guard metadata, UI composition refs. |
| `.pdfont` | `font.ini`, vector font or decoded glyph/metrics/kerning files | Charset/range, fallback chain, baseline metrics, UI/lang bindings. |
| `.pdlang` | `lang.ini`, UTF-8 `strings.json` | Locale/fallback data, stable string keys, context notes for voice/subtitle binding. |
| `.pdskin` | `skin.ini`, slot overrides, material/texture deps, compatibility tags | Appearance application to compatible bodies, heads, weapons, props, or vehicles; preview/swatch data. |
| `.pdeffect` | `effect.ini`, effect graph/timeline, emitter/decal/beam/post-process data | Attachment/target rules, lifetime/priority, material/texture/audio/light/camera deps. |
| `.pdprop` | `prop.ini`, physical object metadata, mesh/collision/material refs | Placement, sockets, simple interaction, damage/break/physics/pickup hooks. |
| `.pdvehicle` | `vehicle.ini`, hull/collision/seats/cameras/hardpoints/handling | Driving/interact behavior, damage states, animation/audio/effect/weapon/occupant deps. |
| `.pdmission` | `mission.ini`, scenario refs, briefing/objective/phase data | Cutscenes, scripts, unlocks, checkpoints, mission-phase spawn overrides. |
| `.pdgamemode` | `gamemode.ini`, scoring/team/timer/objective/loadout rules | Spawn modifiers, HUD/audio refs, compatible scenario tags, round/respawn behavior. |
| `.pdbotprofile` | `botprofile.ini`, skill/personality/behavior tags | Perception, movement abilities such as wall running or jumping, loadout, character/voice refs. |
| `.pdhud` | `hud.ini`, widget layout and data bindings | Reticle/radar/ammo/status composition, `.pdui`/font/lang/SFX deps. |
| `.pdtheme` | `theme.ini`, style tokens and theme asset refs | Chrome composition, procedural/animated texture behavior, accessibility and scope tags. |

The extraction session should not emit descriptor-only stubs just to cover a family. Emit a typed archive only when the public zone contains enough authored data to load and use that asset, or when the descriptor explicitly declares an approved base dependency that provides the missing runtime payload through catalog/on-disk provider source.

### `.pdeffect` base profile graph v2

Base extraction emits three real profile-library archives instead of generic
tint/glow/shimmer/darken/screen/particle stand-ins:

- `base:effect_explosion_profiles` contains all 26 `g_ExplosionTypes` rows.
- `base:effect_spark_profiles` contains all 27 `g_SparkTypes` rows.
- `base:effect_smoke_profiles` contains all 23 `g_SmokeTypes` rows.

`effect.ini` fixes `schema = pd.effect_graph.v2`, `profile_kind`, and
`effect_file = effect.graph.json`. The graph separates `stored` fields copied
from the native table, `source_derived` constants/formulas proven by the native
consumer, and `callsite_owned` policy. Base target, attachment, priority,
enable, owner, and scorch-enable are callsite decisions and must not be
synthesized as table values. Explosion audio uses catalog IDs, never numeric
sound identity. Explosion, spark, and smoke are the native particle systems;
the base game has no independent beam or screen profile table, so v2 declares
those tables absent instead of emitting fake assets. T-ASSETS-017 through
T-ASSETS-020 own shared parsing, execution, dependencies, and fail-closed
activation of this public source.

## Final Batch Details

### `.pdui`

Canonical layout:

```text
ui.ini
texture.png
layout.json
nineslice.ini
_meta/
  manifest.json
  provenance.json
  validation.json
  gl-upload-guards.json
  hashes.json
```

`texture.tga` may replace `texture.png`, and multi-part UI assets may use `textures/<slot>.png` plus semantic `layout.json` or `nineslice.ini`. The descriptor names the UI role, texture slots, intended scale behavior, nine-slice insets, atlas regions, theme/chrome compatibility tags, and catalog refs. GL upload caps and source texture provenance stay under `_meta/`. Public `layout.tsv` is not a valid final UI source.

`.pdui` is not a whole menu screen contract. It is a reusable UI visual asset such as chrome, icon, reticle, portrait, atlas, or texture slice. Larger theme or screen bundles compose `.pdui` assets through their own future wrappers or `.pdmod` transport.

### `.pdfont`

Canonical layout for authored vector fonts:

```text
font.ini
font.ttf
_meta/
  manifest.json
  provenance.json
  validation.json
  hashes.json
```

Canonical layout for decoded ROM bitmap fonts:

```text
font.ini
glyphs.pgm
font.metrics.json
_meta/
  manifest.json
  provenance.json
  validation.json
  source-format.json
  hashes.json
```

`font.ini` owns the display name, catalog ID, style/weight, baseline/ascent/descent, default size, charset/range, fallback chain, and whether the payload is vector or bitmap. Bitmap glyph imagery remains a visible bitmap source, while `font.metrics.json` owns named glyph metrics, atlas coordinates, and kerning as editable semantic source. Raw `data.bin`, public charset TSVs, and public metric TSVs are not authoring fallbacks.

### `.pdlang`

Canonical layout:

```text
lang.ini
strings.json
_meta/
  manifest.json
  provenance.json
  validation.json
  locale.json
  hashes.json
```

`lang.ini` owns locale, namespace, fallback locale, string table role, and compatibility tags. `strings.json` is UTF-8 and is the editable indexed source of text. Entries use semantic fields such as `index`, `key`, `text`, `context`, and `notes`; validators may allow extra fields if they preserve round-trip editing. `.pdvoice` assets reference `.pdlang` keys for subtitles and localization, but audio remains in `.pdvoice`.

## Implementation Sweep Acceptance

The rebuild after this freeze must update emitters, examples, validators, scanners, Modding Hub save/export paths, and release/package gates to match these contracts.

Acceptance for the sweep:

- Each approved game-content family through `.pdtheme` has a root descriptor matching the table above. `.pdtool` stays deferred to a later secure tool/plugin package contract.
- New machine-owned metadata writes under `_meta/`.
- All descriptor, source-format, material, graph, nested-descriptor, and payload references resolve inside the archive or inside an embedded typed archive.
- Export exposes files in an accessible modder-editable folder, and import reconstructs a valid typed archive with descriptors, `_meta/`, hashes, and embedded typed dependencies.
- The game client utilizes each family from those same public source files through the catalog/provider pipeline, with generated cache allowed only as a source-hashed implementation detail.
- Base dependency declarations are explicit records with catalog ID, compatibility/version/hash where available, reason, and on-disk/catalog provider source; no unresolved reference passes silently and no runtime ROM fallback is implied.
- New emitters stop writing authored `.bin` payloads, `.pdwpn`, descriptor-less archives, and root machine-metadata clutter.
- Transition readers can load legacy root metadata while validators reject stale installed outputs for release/package builds.
- Examples show the clean two-zone shape for every current family, including `.pdcharacter`, `.pdprojectile`, `.pdentity`, `.pdskin`, `.pdeffect`, `.pdprop`, `.pdvehicle`, `.pdmission`, `.pdgamemode`, `.pdbotprofile`, `.pdhud`, `.pdtheme`, `.pdui`, `.pdfont`, and `.pdlang`.

## Where To Look

- Kanban: `c3824` for the umbrella archive migration, `c3842` for the global native editable-source correction, `c3841` for the scenario/mission cleanup lane, `c3832` for the detailed `.pdweapon` layout, `c3834` for per-family utility flows, and `c3835` for the shared node editor foundation.
- Weapon detail: [weapon-archive-clean-format.md](weapon-archive-clean-format.md).
- Shipped external-format baseline: [external-format-pdmod-pipeline.md](external-format-pdmod-pipeline.md).

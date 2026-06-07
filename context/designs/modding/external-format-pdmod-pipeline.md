# External-Format Content + `.pdmod` Transport Pipeline

Status: done, shipped 2026-05-20.

## Goal

Make typed mod content files usable by external modders without requiring private JSON blobs or raw engine binary payloads. `*.pdxxx` remains the preferred content-unit family for organization and authoring clarity, while `.pdmod` remains the ZIP bundle/transport envelope with root `mod.json` for sharing and online-required content delivery. The payloads inside those content units and bundles should be standard editable files plus semantic `.ini`, `.json`, or graph metadata.

## Hard Contract

- `mod.json` is still required at archive root for mod discovery, sharing, registry-backed publication, network validation, and shell metadata.
- Authored mod content and `.pdmod` transport payloads must not contain `.bin` files. Engine-native binary data may exist only as an internal cache generated from external source files.
- Typed `*.pdxxx` files are the preferred content-unit format for modder organization. The cleanup target is their internals: they should expose readable standard files and semantic INI/JSON/graph metadata instead of opaque JSON/bin payloads or public TSV tables. `.pdmod` is the higher-level package/transport used for sharing, Public Mods, and online-required content delivery.
- Weapon behavior moves to the `.pdweapon` graph asset schema in [weapon-behavior-graph-assets.md](weapon-behavior-graph-assets.md). `.pdwpn` is fully deprecated, was never released, and must be removed rather than accepted as input, alias, migration source, or compatibility path.
- Folder mods and `.pdmod` archive mods must use the same component registration behavior.
- Archive paths must keep the existing trust rules: reserved inbox folders are browse-only and never auto-mounted.

## Canonical Layout

Typed `.pdxxx` content files are the preferred authored content units. Each
typed file is itself a zip-openable asset archive: the archive root contains the
readable descriptor (`head.ini`, `arena.ini`, `animation.ini`, etc.) plus the
standard source files needed by that asset. A `.pdmod` archive wraps those typed
asset archives for transport and keeps `mod.json` at archive root.

```text
mod.json
weapons/<id>.pdweapon
characters/<id>.pdcharacter
heads/<id>.pdhead
bodies/<id>.pdbody
arenas/<id>.pdarena
scenarios/<id>.pdscenario
meshes/<id>.pdmesh
audio/sfx/<id>.pdsfx
audio/voice/<id>.pdvoice
audio/music/<id>.pdsong
ui/<id>.pdui
fonts/<id>.pdfont
lang/<id>.pdlang
animations/<id>.pdanim
```

Accepted alternates are deliberately narrow: `texture.tga` for UI textures, `track.mp3` or `track.wav` for music, `font.otf` for fonts, and `geometry.gltf` when a model needs hierarchy or skinning. No `.bin` alternate is valid for authored content.

The earlier canonical-folder INI layout (`characters/heads/<id>/head.ini`,
`maps/<id>/arena.ini`, `animations/<kind>/<id>/animation.ini`, etc.) remains
accepted as a compatibility authoring layout, but it is no longer the preferred
content-unit shape.

For character assets, `characters/<id>.pdcharacter` is the top-level authored
archive with root `character.ini`. Existing `heads/<id>.pdhead` and
`bodies/<id>.pdbody` archives remain lower-level dependency/runtime
compatibility packages while the runtime still models heads and bodies
separately.

## Standard File Families

- UI textures: `ui/<id>/ui.ini` points at `texture.png` or `texture.tga`. The runtime loads those bytes through `fsFileLoad`, so mounted archive entries and loose files use the same path.
- Fonts: `fonts/<id>/font.ini` points at `font.ttf` or `font.otf` for authored UI fonts. Current base `.pdfont` extraction exposes the ROM bitmap font as `glyphs.pgm` plus TSV metric bridges instead of `data.bin`; under the no-public-TSV rule those metric bridges are migration debt and should become semantic font metric source or generated private cache.
- Language banks: `lang/<id>/lang.ini` should point at semantic UTF-8 localization source rather than ripped public TSV rows. Current `strings.tsv` extraction is a migration bridge from the ROM language offset table and does not expose `data.bin`, but it is not the final accessible `.pdlang` contract.
- Sound effects and voice: `audio/sfx/<id>.pdsfx` and `audio/voice/<id>.pdvoice` carry `sound.ini` or `voice.ini` plus decoded mono PCM16 `sample.wav`. Base extraction keeps the original ROM codec in `source_format` metadata but does not expose `sample.bin` as authored content.
- Music: `audio/music/<id>.pdsong` carries `music.ini`, standard `sequence.mid`, and currently a `sequence.tsv` event bridge. Base extraction inflates the N64 RareZip compressed-MIDI stream and converts it rather than exposing a raw `data.bin` authored payload; the final source should keep MIDI plus semantic cue/event metadata instead of public TSV.
- Models, maps, characters, and animations: typed `.pdcharacter`/`.pdhead`/`.pdbody`/`.pdmesh`/`.pdarena`/`.pdscenario`/`.pdanim` descriptors point at `model.gltf`, `animation.gltf`, `geometry.obj`, `rooms.obj`, or nested typed dependencies as the authored source. `.pdcharacter` owns the skeletal character compound at the top level; `.pdhead` and `.pdbody` remain compatibility/dependency layers while the runtime split exists. The runtime cache adapter validates those standard source files through the same VFS-capable path as loose folders and archives. OBJ map/scenario sources now normalize to readable mesh JSON and activate as engine-owned collision mesh payloads. Model/head/body/weapon/prop GLTF/GLB/OBJ sources normalize to readable model JSON and activate as generated in-memory `modeldef` payloads; the old modeldef loader must never be handed a raw GLTF/OBJ file. Static/empty GLTF/GLB animation sources and skeletal translation/rotation/scale channels normalize to readable animation JSON and activate as engine `animtableentry` clip payloads; unsupported weights/interpolation fail clearly instead of falling back to raw source or `.bin`. The packed `.pdmod` fixture path now uses the production archive writer/reader plus VFS mount and reads typed content units plus sidecars without extracting them; templates allow optional `catalog_id`/`id` overrides for repeated canonical leaf names.
- Base model, scenario, and character-animation extraction now avoids authored `.bin`: `.pdmesh` archives expose standard Wavefront `model.obj` plus `model.mtl` generated from promoted `PD_MODELDEF` display lists; `.pdscenario` archives expose source-readable scene, collision, pad, setup, navigation, and graph payloads rather than raw word dumps; character `.pdanim` archives expose `animation.gltf` semantic transform channels rather than public byte tables. TSV byte tables such as `header.tsv` / `frames.tsv` are not an acceptable public animation source because they are effectively ripped native bytes, not modder-usable authored data.

## Weapon Behavior

The long-term weapon archive is `.pdweapon` with root `weapon.ini` plus graph source files under `behavior/` (`primary.graph.json` and `secondary.graph.json` for base weapons, or a single `behavior.graph.json` for simple authored mods). Those graphs map modder-facing events to the existing game weapon behavior modules and compile to deterministic runtime IR. The first target expression is:

```text
when trigger pulled -> shoot <custom projectile> every <centiseconds>
```

The behavior format should cover the behaviors already present in the game: single shot, burst and full-auto looping fire, sustained hold-fire beams, charge-and-release shots, secondary modes, melee attacks, zoom levels, reticles, overlays, zoom-camera effects, ammo-display surfaces, and model-part state driven by ammo or reload state. It should not hardcode these as one-off exceptions. Halo-style examples are valid requirements for the modularized format: a Needler-like weapon can display remaining physical needles, animate empty slots, and refill only as many needles as inventory allows; an assault-rifle-like weapon can render the exact remaining bullet count on an in-weapon screen.

The runtime implementation path should reuse the existing weapon behavior machinery, progressively factoring it into named behavior modules that `.pdweapon` can reference. Mod tools can then author or edit the project-owned graph without requiring raw C structs, opaque engine blobs, or a separate scripting system.

## INI Style

Every `.ini` file uses sectioned settings with comments generated by the packer/template helper. Required values are uncommented with defaults. Optional or currently unused values are present but commented out.

Example:

```ini
[asset]
id = base:farsight
name = Farsight XR-20
schema = weapon.v1

[models]
first_person = models/farsight_fp.gltf
world = models/farsight_world.gltf
; pickup_icon = ui/farsight_icon.png

[behavior]
slot = rifle
; secondary_mode = target_locator
```

## Runtime Cache Rule

The game may compile standard source files into runtime data under a private cache directory. Cache keys must include the mod id, archive digest or entry CRCs where available, compiler version, asset id, full source hash, source path, and descriptor version. Cache files are readable generated implementation details and must not be packed back into `.pdmod`.

If a cache is missing, stale, or invalid, the runtime recompiles from the source files. If compilation fails, the mod is invalidated with a clear validation error rather than silently falling back to a `.bin`.

Current s6 state: done. `modasset_compiler` writes private `.pdmc` descriptors under `$S/mod-cache/<mod>/<asset>/` for validated GLTF/GLB/OBJ sources. OBJ map/scenario sources also write readable normalized `.pdmesh.json` cache files and activate as `struct colmesh` collision meshes; stage load merges matching catalog colmeshes into `g_WorldMesh`. Static model sources for model/head/body/weapon/prop entries write readable `.pdmodel.json` caches and activate as generated in-memory `modeldef` payloads. Static/empty GLTF/GLB weapon and character animation sources plus skeletal translation/rotation/scale channels write readable `.pdanimation.json` caches, activate as catalog-owned `ASSET_PAYLOAD_ANIMATION_CLIP` payloads, and install at the same `animtableentry` plus header/frame byte-stream boundary used by base animation data. `.gltf` sidecar binary buffer URIs are rejected because authored `.bin` payloads are outside the external-format contract.

Current s7 state: done. `modpackPdmodFromFolder()` validates typed `.pdxxx` asset archives plus the compatibility canonical layout before writing, rejects authored `.bin`, generates missing commented descriptor and supporting INI templates for the compatibility layout, checks referenced source files and unsafe paths, and surfaces detailed errors to Modding Hub through `modpackPdmodLastError()`. The in-memory writer rejects `.bin` entries too.

Current s8 state: done. Mike's Build log validated boot, legacy `.pd*` runtime registration, `.pdui` auto-emit/reload, and zero-bin installed `.pdmod` discovery. Runtime smoke gates now validate the preferred authored shape both ways: enabled typed `.pdxxx` content as a loose folder mod and the same content wrapped in real `.pdmod` transport, loading GLTF head, OBJ arena, and GLTF skeletal animation sources into modeldef, colmesh, and animation clip payloads through readable generated caches. Public Mods transfer/install closure is implemented: received `.pdmod` downloads validate root `mod.json`, reject authored `.bin`, install into `mods/installed`, refresh `g_ModRegistry`, and compare archive/folder mods by root `mod.json` digest. Verified with focused `[social][public_mods][static]`, focused `[modding][pdmod][static][c3809]`, `pdxxx_content_folder_smoke`, `pdxxx_content_transport_smoke`, `public_mods_pdmod_install_smoke`, and queued `modpipe` all build.

Startup policy: intro/menu presentation should come first. Enabled-mod validation/conversion may run on worker threads once it is safe, but GPU upload and engine object installation remain main/render-thread work.

## Implementation Order

1. Add shared INI parsing for disk and archive memory buffers.
2. Add an external component scanner that accepts both folders and archive entries.
3. Register first-class external descriptors in the catalog with source paths pointing at VFS-readable files.
4. Add packer validation and template generation so malformed archives are rejected before they are shared.
5. Add compiler/importer adapters per asset family, starting with the formats the runtime already partially supports: audio, UI textures, fonts, language text, then models/maps/animations.

## Validation

Each implementation slice must include:

- A folder fixture and a `.pdmod` archive fixture.
- A legacy `.pd*` compatibility fixture when touching an existing universal kind.
- Static or runtime tests proving `.bin` payloads are rejected from authored typed content and `.pdmod` transport archives.
- Context and Kanban updates before the subtask is marked done.

## Where to Look

- Live pillar: `context/pillars/modding.md`
- Current task tracker: `tools/kanban/state.json` card `c3809`
- Archive/VFS code: `port/src/modarchive.c`, `port/src/modvfs.c`, `port/src/modmgr.c`

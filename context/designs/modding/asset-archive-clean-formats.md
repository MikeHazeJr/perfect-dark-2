# Clean Asset Archive Family Formats

Status: frozen target contracts created 2026-05-24. Kanban owner: `c3824`.

This document records the clean self-contained archive layouts for every current typed or otherwise referenceable Asset Pipeline family before the extraction, examples, validators, and release gates are rebuilt around them. The broad implementation sweep starts from this file rather than inventing new family contracts piecemeal.

## Shared Contract

Every typed `*.pdxxx` archive is a zip-openable authoring unit. Changing the extension to `.zip` should reveal the descriptor and readable source files needed to edit, clone, share, validate, and load that single asset.

The archive has two zones:

- Public authoring zone: the root descriptor and readable files or purpose-named folders that modders naturally edit.
- `_meta/`: machine-owned manifest, inventory, provenance, validation reports, source handles, compatibility notes, and hash ledgers.

New emitters write machine data under `_meta/`. Transition readers may accept legacy root `manifest.json` and root SHA sidecars, but release/package validation must fail newly emitted or installed stale outputs that are non-zip typed files, `.pdwpn`, descriptor-less, authored `.bin` backed, or unresolved internally.

All asset references use catalog ID strings. A reference field is metadata, not dependency closure, unless the referenced authored payload also resolves inside the archive or inside an embedded typed dependency archive. Higher-level assets embed dependency assets as intact typed archives under `dependencies/assets/<type>/<id>.<typed-asset>` unless the family-specific notes below define a narrower legacy transition path.

Authored `.bin` payloads are rejected. Generated runtime cache remains private, readable when useful, rebuildable, and unshipped. `.pdmod` remains transport only.

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
| `.pdarena` | `arena.ini` | preview/thumbnail, match defaults, spawn playlist metadata, scenario reference | Multiplayer-facing arena selection wrapper around an embedded `.pdscenario`. |
| `.pdscenario` | `scenario.ini` | rooms, portals, pads, setup/object placements, collision/navigation, lighting/visual scene data, spawn profiles | Actual level/map content. Arena, mission, and gamemode wrappers select or configure it. |
| `.pdmesh` | `mesh.ini` | geometry, hierarchy, skinning/rig binding, sockets, LODs, optional collision proxy | Reusable geometry/model data. Materials and textures remain typed dependencies. |
| `.pdanim` | `animation.ini` | timeline/channel data, events/notifies, retargeting/target rig metadata, optional legacy opcodes | Reusable animation clip or sequence. Body, weapon, mesh, and audio refs remain catalog IDs with embedded dependencies when needed. |
| `.pdsfx` | `sound.ini` | editable source audio, loop points, attenuation and mixer tags, import settings | Atomic sound effect. Codec/runtime cache is private and rebuildable. |
| `.pdvoice` | `voice.ini` | speaker/context/category metadata, locale audio variants, subtitle binding, fallback rules | Spoken-line metadata and audio variants. `.pdlang` owns text strings. |
| `.pdsong` | `music.ini` | `sequence.mid`, `sequence.tsv`, `track.wav`, `track.ogg`, or `track.mp3`, loop/cue/adaptive metadata | Reusable music asset. Generated playback cache is private and rebuildable. |
| `.pdui` | `ui.ini` | `texture.png` or `texture.tga` at root or under `textures/`, optional readable nine-slice/layout metadata | Atomic UI visual or chrome asset. Screens and themes compose `.pdui` refs rather than hiding unrelated UI payloads in one archive. |
| `.pdfont` | `font.ini` | authored `font.ttf`/`font.otf`, or decoded ROM bitmap font files `glyphs.pgm`, `metrics.tsv`, and `kerning.tsv` | Reusable font face/range asset. Charset/range/import notes live in descriptor and `_meta/`, not raw runtime blobs. |
| `.pdlang` | `lang.ini` | `strings.tsv` as UTF-8 text | Text/localization owner. Voice assets bind to string keys and audio locale fallbacks; `.pdlang` does not own spoken audio. |

## Final Batch Details

### `.pdui`

Canonical layout:

```text
ui.ini
texture.png
layout.tsv
nineslice.ini
_meta/
  manifest.json
  provenance.json
  validation.json
  gl-upload-guards.json
  hashes.tsv
```

`texture.tga` may replace `texture.png`, and multi-part UI assets may use `textures/<slot>.png` plus readable `layout.tsv` or `nineslice.ini`. The descriptor names the UI role, texture slots, intended scale behavior, nine-slice insets, atlas regions, theme/chrome compatibility tags, and catalog refs. GL upload caps and source texture provenance stay under `_meta/`.

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
  charset.tsv
  hashes.tsv
```

Canonical layout for decoded ROM bitmap fonts:

```text
font.ini
glyphs.pgm
metrics.tsv
kerning.tsv
_meta/
  manifest.json
  provenance.json
  validation.json
  source-format.json
  hashes.tsv
```

`font.ini` owns the display name, catalog ID, style/weight, baseline/ascent/descent, default size, charset/range, fallback chain, and whether the payload is vector or bitmap. Bitmap glyph imagery and metrics remain visible authoring files. Raw `data.bin` is not an authoring fallback.

### `.pdlang`

Canonical layout:

```text
lang.ini
strings.tsv
_meta/
  manifest.json
  provenance.json
  validation.json
  locale.json
  hashes.tsv
```

`lang.ini` owns locale, namespace, fallback locale, string table role, and compatibility tags. `strings.tsv` is UTF-8 and is the editable source of text. Recommended columns are `key`, `text`, `context`, and `notes`; validators may allow extra columns if they preserve round-trip editing. `.pdvoice` assets reference `.pdlang` keys for subtitles and localization, but audio remains in `.pdvoice`.

## Implementation Sweep Acceptance

The rebuild after this freeze must update emitters, examples, validators, scanners, Modding Hub save/export paths, and release/package gates to match these contracts.

Acceptance for the sweep:

- Each current family has a root descriptor matching the table above.
- New machine-owned metadata writes under `_meta/`.
- All descriptor, source-format, material, graph, nested-descriptor, and payload references resolve inside the archive or inside an embedded typed archive.
- New emitters stop writing authored `.bin` payloads, `.pdwpn`, descriptor-less archives, and root machine-metadata clutter.
- Transition readers can load legacy root metadata while validators reject stale installed outputs for release/package builds.
- Examples show the clean two-zone shape for every current family, including `.pdcharacter`, `.pdprojectile`, `.pdentity`, `.pdui`, `.pdfont`, and `.pdlang`.

## Where To Look

- Kanban: `c3824` for the umbrella archive migration, `c3832` for the detailed `.pdweapon` layout, `c3834` for per-family utility flows, and `c3835` for the shared node editor foundation.
- Weapon detail: [weapon-archive-clean-format.md](weapon-archive-clean-format.md).
- Shipped external-format baseline: [external-format-pdmod-pipeline.md](external-format-pdmod-pipeline.md).

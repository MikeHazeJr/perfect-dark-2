# Standard source authoring

Typed `.pdxxx` assets are ZIP archives containing an editable descriptor, public
source files, and any embedded typed dependencies. Start from a matching archive
in [typed-pdxxx-basic](../../examples/modding/typed-pdxxx-basic), preserve its
family layout, and give your asset a unique catalog ID such as
`my_pack:model_triangle`. The game loads public source through the catalog and
file provider. Generated engine products are disposable, source-hashed cache.

This guide describes the current importer contract. The
[canonical source/runtime audit](../../context/audits/2026/asset-source-runtime-contract-2026-09-05.md)
records incomplete semantics and verification. A format appearing below does
not mean every feature exported by an authoring application is implemented.

Native graph action preparation now reads exact `weapon_animation` catalog IDs
from public `commands.json` sources. Includes form an acyclic graph; missing or
wrong-type dependencies reject the whole candidate. Prepared actions retain their
command arrays, clips and sounds through source edits and catalog retirement.
Editing a command, referenced glTF buffer or sound file produces a new source
identity; it does not mutate an existing action. This dependency adapter is
installed-client verified. Complete equipped graph gameplay is still in progress.

## Choose a source

Texture inputs read named `surface_type` and `sound_surface_type` values from
`texture.ini`, along with `tile_column_offset`, `tile_row_offset`,
`mask_s_reduction`, and `mask_t_reduction`. Base extraction supplies these fields;
ordinary native texture lookup and retained model inputs consume them. Image and
descriptor edits create a new retained generation; existing model consumers keep
their selected pixels and properties until release. Ordinary lookup refreshes at
catalog reload. Invalid values reject the candidate.

Ordinary image and material lookup share the same catalog selection. Menu or
stage settings that suppress base-texture replacements select the public base
source; explicitly selected custom textures remain usable. Removing other
catalog entries does not hide later texture, animation, language, or stage rows.

| Asset | Public source to edit |
|---|---|
| Mesh, `.pdmesh` | `mesh.ini` and `model.obj`/`model.mtl`, or `model.gltf`/`model.glb`. Preserve extracted `model.nodes.json`, `model.parts.json`, `model.faces.json`, and `model.render.json` when present; these carry native model semantics. |
| Animation, `.pdanim` | `animation.ini` and `animation.gltf`/`animation.glb`, or weapon `commands.json`. |
| Body/head, `.pdbody`/`.pdhead` | `body.ini`/`head.ini`, the selected `mesh_archive`, and an optional body `hand_archive`. These dependencies are editable `.pdmesh` archives. |
| Texture, `.pdtexture` | `texture.ini` and `texture.png`, `.tga`, `.jpg`, `.jpeg`, or `.bmp`; preserve `palette.json` for an extracted indexed texture. |
| Sound, `.pdsfx` | `sound.ini` and the WAV, MP3, or Vorbis `.ogg` member selected by `file_path`; extracted samples normally use `sample.wav`. |
| Voice, `.pdvoice` | `voice.ini`, its selected WAV, MP3, or Vorbis `.ogg` member, and optional `subtitle.json`. |
| Music, `.pdsong` | `music.ini` plus `sequence.mid` **and** `sequence.json`, or `track.wav`, `track.ogg`, or `track.mp3`. |
| UI, `.pdui` | `ui.ini`, PNG/TGA images, and optional `layout.json`/`nineslice.ini`. |
| Font/language | `.pdfont`: `font.ini`, `glyphs.pgm` and `font.metrics.json` for game text; `font.ttf`/`font.otf` serve the ImGui font path. `.pdlang`: `lang.ini` and UTF-8 `strings.json`. |
| Scenario/mission | Keep the existing descriptor, scene/collision sources, and named setup, navigation, AI, and objective JSON together. Scenario `level.graph.json` and mission `mission.graph.json` have distinct roles. |
| Behavior/composition | Edit the family's INI/JSON and declared graph files; reference models, audio, effects, and other dependencies by catalog ID. |

The [archive schemas](../../tools/asset_archive_conformance.py) define exact
filenames and dependency slots. Edit public descriptors and their selected files.
Audio controls and body/head scalars come from those descriptors; stale private
copies must not override an edit. `_meta/` contains generated provenance, including
the private native identity used when replacing an existing base asset. Do not
hand-maintain parallel authored/runtime values. Do not replace public sources
with runtime dumps, TSV word tables, or a parallel `runtime.graph.json`.

## Texture materials and stage overrides

Keep `properties_version = 1` in the `[texture]` section. Missing material fields
then mean `default` for surfaces and zero for tile fields. An older extracted
descriptor is upgraded by adding its missing original fields and version marker;
existing values, comments, images, and other source members are preserved. Once
versioned, deleting an optional field deliberately restores its default and a
later extraction pass does not add it back. Explicit forced extraction still
replaces sources.

Surface names are `default`, `stone`, `wood`, `metal`, `glass`, `shallow_water`,
`snow`, `dirt`, `mud`, `tile`, `metal_object`, `character`, `glass_translucent`,
`none`, and `deep_water`. Tile fields accept integers from 0 through 15. Mask
reductions must also fit the image dimensions, and tile offsets must fit the
native tile command. The current image importer supports dimensions up to
255 by 255; full palette and mip-level parity remains unfinished.

To override materials for a level, add these root properties to its existing
`level.graph.json`, keeping the other graph fields:

```json
{
  "texture_properties_version": 1,
  "texture_properties": {
    "mode": "multiplayer",
    "entries": [
      {
        "texture": "my_pack:floor",
        "surface_type": "wood",
        "sound_surface_type": "wood"
      }
    ]
  }
}
```

Use an enabled texture's complete catalog ID. `mode` accepts `all`, `solo`, or
`multiplayer` and defaults to `all`. Each row replaces only its stated fields;
the others come from the texture descriptor. Duplicate IDs, invalid properties,
and unresolved active references reject the complete candidate. The previous
active overrides survive a rejected candidate, and level reset clears them.
Deleting `texture_properties` while keeping its version marker removes all
overrides. Extracted base levels preserve their original multiplayer-only rules.

Standalone and embedded scenarios each own their graph. Automatic upgrades
preserve each copy's other graph data and update generated hashes atomically.
An archive with unsupported or filtered members is rejected for rewriting so
those members cannot silently disappear.

## Weapon animation commands

For a `weapon_animation` `.pdanim`, keep `commands.json` as the public source.
The descriptor's catalog ID identifies the animation; the JSON `name` is a label.
Keep `source_format` set to `weapon_animation_commands`, use a `commands` array,
and end it with exactly one `{ "command": "end" }`. If `command_count` is present,
it must match the array length. Scalar fields must be whole numbers within their
native command ranges; malformed or narrowing values reject the entire source.
`wait_for_trigger_release` accepts the extracted `trigger: "z"` selector; its
optional `slot` defaults to zero. Other trigger names are rejected.

Use complete catalog IDs for `include_animation`, `random_animation`,
`play_character_animation`, and `play_sound` references. For example,
`first_pack:reload` and `second_pack:reload` remain different dependencies even
when both JSON documents use the name `Reload`. Includes and random branches must
form an acyclic command graph. A missing or wrong-kind clip/sound dependency cannot
silently become a native fallback. These command sequences and the executable
weapon behavior graph are separate layers; command admission does not establish
full executable graph gameplay support.


Folder and `.pdmod` imports register sibling metadata before compiling command
sources. You can place a command before its referenced command, glTF clip, or
sound in directory/ZIP order. The importer compiles the submitted command graph
in dependency order, including replacements that reverse an old dependency.
A rejected sibling or cycle restores the prior catalog rows, provider paths,
private slot reservations and native commands for that import. Archive source
paths remain bound to their containing archive.

Keep one selected source per catalog ID within an import. Repeated discovery of
the same file is harmless; conflicting files for one ID reject the batch. A file
changed during admission is rejected so compilation cannot mix two edits. These
transactions protect admission; stable runtime ownership for an already equipped
executable behavior graph remains a separate integration requirement.

## glTF buffers and cache identity

Use glTF 2.0 with one declared buffer and nonempty, bounded `bufferViews`.
Every view refers to buffer `0`; accessors must fit their view. Multiple buffers
are rejected. Supported storage choices are:

- **Local `.gltf` buffer:** a URI such as `buffers/triangle.bin`, resolved
  relative to the glTF member inside the same archive. Its actual byte count
  must equal `buffers[0].byteLength`.
- **Embedded `.gltf` buffer:** exactly
  `data:application/octet-stream;base64,` or
  `data:application/gltf-buffer;base64,` followed by complete canonical base64.
  The decoded byte count must equal `byteLength`.
- **`.glb`:** a complete version-2 container with its JSON chunk first and its
  BIN chunk second when present. The buffer has no `uri`; BIN may contain at
  most three bytes of alignment padding beyond the declared byte count.

Use forward slashes and simple portable filenames. URI spaces may be encoded
as `%20`; decoded names must be valid UTF-8. Absolute paths, network URLs,
drive letters, `.`/`..` segments, encoded separators, and cross-archive paths
are rejected. A `.bin` file is allowed only when a valid glTF document declares
that exact local buffer. This exception does not admit arbitrary binary dumps.

Both JSON and the resolved buffer bytes enter the text glTF cache fingerprint.
Editing only the buffer therefore changes cache identity; the compiler uses
the same loaded buffer snapshot for hashing and compilation. GLB source bytes
include their embedded buffer. Missing or invalid source cannot be rescued by
an old cache. Bufferless native animation documents still receive strict
document validation; an absent declaration cannot hide dangling buffer views.

## Minimal triangle source

This is a geometry example, not a complete `.pdmesh` archive. Use it as
`model.gltf` in a matching archive and select it with `geometry_file` in
`mesh.ini` through the authoring flow.

```json
{
  "asset": {"version": "2.0"},
  "buffers": [{"uri": "buffers/triangle.bin", "byteLength": 36}],
  "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962}],
  "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3,
                 "type": "VEC3", "min": [0, 0, 0], "max": [64, 64, 0]}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "mode": 4}]}],
  "nodes": [{"mesh": 0}],
  "scenes": [{"nodes": [0]}],
  "scene": 0
}
```

`buffers/triangle.bin` contains three little-endian float32 XYZ triples:
`(0,0,0)` at byte 0, `(64,0,0)` at byte 12, and `(0,64,0)` at byte 24.
For example, run this Python in your unpacked authoring folder to create it:

```python
from pathlib import Path
import struct
Path("buffers").mkdir(exist_ok=True)
Path("buffers/triangle.bin").write_bytes(struct.pack("<9f", 0, 0, 0, 64, 0, 0, 0, 64, 0))
```

The general mesh importer currently consumes triangle positions, optional
indices/private room tags, and constant base color factors. Both collision and
rendering follow the selected scene, parent transforms, TRS or matrix nodes,
mesh instances, and reflected winding. If no default scene is selected, the
first scene is used; a scene-less node hierarchy uses its roots. Geometry-only
files without nodes or scenes retain one identity instance per mesh.

UVs, normals, vertex colors, skins, and material texture bindings are not yet
preserved by the general generated-model importer. A textured or rigged DCC
export therefore still needs consumer-specific appearance and behavior checks;
the extracted Scenario renderer has its own material source contract.

## Language source edits

`strings.json` is UTF-8 JSON with `pd_kind: "language_strings"`, schema version
1, and a `strings` array. Each row has an integer `index` and `text`. Cover every
index from zero through the last slot, in any row order. Use `text: null` for a
null slot and `text: ""` for an existing empty string. An empty bank uses
`strings: []` and `string_count = 0`; descriptor and manifest counts must match.

The current game-bank codec supports Latin1 characters. Literal Unicode text
and equivalent JSON escapes, such as `é` and `\u00e9`, produce identical native
bytes; unsupported characters reject with an explicit codec error. This does
not add regional language selection or missing glyphs to the game text renderer.

## Body, head, and sound edits

Keep the catalog ID when replacing an existing asset. Use a new, namespaced ID
for a separate asset, and update every dependency that should select it. Two
different public archives claiming the same model ID must have the same public
source content; an accidental duplicate with different geometry is rejected.

Body/head `mesh_catalog_id` identifies the model in `mesh_archive`. A body may
also declare `hand_catalog_id` and `hand_archive`. The selected mesh's public
`geometry_file` determines which OBJ/glTF/GLB member is consumed. Body/head
height, scale, animation scale, type, rig compatibility, and unlock requirement
belong in the body/head descriptor. A loaded model must be released before
replacing its source; an in-place edit must not reuse the old resident geometry.
Hand loading retains the selected catalog ID because several native models can
share one original file number. A hand's ID selects its source for both weapon
attachments and unarmed fists; private file-number bridges remain unique.
Direct `.pdmesh` replacements use the public geometry selection in catalog
loading, previews, and queued hand/weapon loading. For example,
`geometry_file=custom.obj` selects that file even if `model.obj` also exists.
A missing or conflicting selection is rejected; private metadata cannot choose
another member. Nested typed mesh archives use the same resolution.
Model skeleton and `model_scale` come from public `mesh.ini`; private extraction
metadata cannot override them. Loose OBJ/glTF/GLB sources read adjacent model
metadata and sidecars as well as typed archive members. The compiler also accepts
captured source reads and texture inputs: installed checks verify that later disk
edits cannot change an already captured model's geometry, scale, or bound pixels.
Complete retained model ownership in the equipped graph path is still in progress.
These source-consumer routes have installed runtime checks. Complete character
selection, ordinary gameplay presentation, and controller use remain separate
validation work, as recorded in the audit.

For a sound or voice replacement, change `file_path` to the relative member
containing your audio. WAV decoding supports integer PCM8/16/24/32 and float32;
MP3 supports MPEG1/2/2.5 Layer III; `.ogg` means Vorbis. The runtime converts
these sources to its mixer format. Duration follows decoded frames rather than
an assumed compressed bitrate. Descriptive voice text and context belong in
`voice.ini`.

Preserve extracted native sound controls while replacing a clip. Historical
`key_min`, `key_max`, `velocity_min`, and `velocity_max` are packed control bytes,
not ordered MIDI ranges. Their admission is implemented, but native chained
sounds, start delays, and timed restarts still need complete file-source playback
support. Correct base sample identity is also being checked against native bank
indexing. Passing archive conformance does not close those playback gaps.

## Animation edits

Use one clip per animation source. Standard channels support translation,
rotation, and scale with `LINEAR` or `STEP` interpolation; cubic spline and
morph-weight channels are unsupported. Preserve the extracted node/part
mapping and the descriptor's clip identity.

Root `extras` can contain native animation tails, for example:

```json
{
  "pd_repeat_ranges": [{"repeat_to_frame": 2, "repeat_from_frame": 8}],
  "pd_cut_skip_frames": [3, 4]
}
```

Tail frame values must be JSON integers from `0` through `32767`. Repeat rows
require exactly the two named fields; malformed rows, duplicate fields, and
unrepresentable values reject the import. Arrays are no longer silently
truncated at 512 entries; the rebuilt native header must still fit its format.
Choose frame values meaningful for your clip and keep their intended order.

Clips containing `pd_special_parts` rebuild native part data from those
captured fields. Editing ordinary glTF channels does not currently override
that path. Preserve these fields when maintaining extracted clips; full DCC
round-trip editing of native special/root-motion/camera semantics remains open.

## Graph edits and author checks

Weapons use `behavior/primary.graph.json`, `behavior/secondary.graph.json`,
and the accompanying settings, variables, and shared-context JSON. Projectile
and entity archives use their own `behavior.graph.json`. Start with a matching
shipped module and retain its schema, parameter types, and valid enum names.
Model, sound, projectile, entity, and effect references must be catalog IDs
such as `my_pack:sfx_reload`, with the required dependency included. Numeric
ROM IDs and guessed fallback aliases are not authored identities. Use `null`
only where the parameter permits absence.

Settings and variable values must be complete, finite numbers in the destination
field's range. Keep timing units with their values. Prop health must fit the
native tenths-of-health field: zero through 3276.7, including graph actions.

Current weapon adapters do not execute arbitrary authored edge topology,
gates, events, or shared-state flow. Some adapters project selected nodes into
native function records. D-006 selected executable modular graphs: authored
events, connections, gates and state will drive reusable native modules. The
new compiler/executor and lifetime kernel passes focused tests, but is not yet
attached to equipped gameplay. Passing reference validation does not establish
graph gameplay control. Keep using the supported shipped schema until that
attachment is available; changing only a document's schema version cannot
enable executable topology.

Character templates use `head_policy` in public `character.ini`: `fixed` names
both `body_asset`/`bodyfile` and `head_asset`/`headfile`; `random_gender` names the
body and leaves the head absent or empty; `integrated` does the same only for a
body whose source declares its own complete head. Missing policy preserves only
legacy fixed-head declarations. Contradictory aliases and duplicate keys are
rejected. Keep child archives embedded at the declared paths. Source policy and
dependency activation are under T048 validation; selector and multiplayer
integration are still pending.

For file-backed music, a `.pdsong` uses `music.ini` with `[music]`, its catalog ID,
`audio_category = music`, and `file_path = track.ogg` (or an ordinary WAV/MP3
source). The Ogg source must contain Vorbis audio; an Opus stream is a different
codec. The current T049 implementation uses real Vorbis decoding and bounded
PCM accumulation; installed public-song/catalog/ZIP and synchronized-playlist
checks pass. This does not establish audible output or network transport. Descriptive voice
fields belong in public `voice.ini`; do not maintain duplicate private actor,
transcript, language or context values.

After saving and repacking, validate the folder containing your archives from
the repository root:

```powershell
python tools/asset_archive_conformance.py --root "C:\MyMods\MyPack"
```

This includes embedded typed dependencies by default. Check rejected paths,
missing catalog references, source byte counts, and stale metadata before
launching. Then load the asset in its actual game consumer and make one obvious
source change to confirm it reaches gameplay; inspect appearance separately
for visual assets. A ZIP opening, DCC preview, or conformance pass alone does
not prove runtime utilization. Repository contributors also run
`python tools/asset_native_source_guard.py` under the project's coordinated
verification procedure.


Animation generation lifetime (2026-09-08): native graph adapters can acquire an
owned generation of a selected glTF/GLB clip. The generation hash covers the
actual parsed document, resolved buffer bytes and selected frame count. Editing
only an external glTF buffer creates a distinct generation. Existing retained
generations preserve their compiled bytes and private playback slots through
catalog removal/reset; final release invalidates header/frame caches before
slot reuse. These are rebuildable private products, never authored files.
The ordinary decoder path is installed-client verified; complete equipped graph
dependency preparation and publication remain in progress.


Sound generation lifetime (2026-09-08): engine graph adapters can retain the exact
selected WAV, MP3 or Ogg bytes as decoded PCM with a source/parameter hash and
private leaf sound slot. No second file lookup occurs during snapshot decoding.
New voices copy retained PCM and preserve loop/envelope/source-rate settings, so
playing voices remain valid after catalog removal or final generation release.
The ordinary native sound router and mixer are installed-client verified with
dummy output; complete equipped graph command preparation remains in progress.

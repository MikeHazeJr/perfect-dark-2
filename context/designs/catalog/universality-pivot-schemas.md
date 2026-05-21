# Catalog Universality Pivot Schemas (Step 0 Lock-Down)

> Step 0 of the catalog universality pivot. This doc locks down the per-asset compound schemas that subsequent migration steps will produce, parse, and ship. Each subsequent step (1, 2, 3a, 3, 4, 5) emits or consumes content that conforms to the schemas defined here.
>
> **Date:** 2026-05-02. **Companion:** [audits/catalog-universality-pivot-plan-2026-05-02.md](../../audits/catalog-universality-pivot-plan-2026-05-02.md). **Decision authority:** Mike's 2026-05-02 directives on Q-1 through Q-5 (audit Section 5.4).
>
> **Methodology gates** per [procedures.md](../../procedures.md): possibility framing, file:line evidence for current-state references, no em-dashes, no code shipped this session.
>
> **Status:** binding reference. Subsequent migration steps cite this doc by name. If a schema needs to change after a step ships, this doc updates first and the field bump is explicit (`pd_schema_version` increments).

---

## 1. Universal envelope

Every `.pd*` file (whether plain JSON or compound ZIP) carries the same envelope at the top of its primary JSON document. The envelope identifies kind + schema version so the universal loader can route to the right per-class parser without filename inspection.

### 1.1 Top-level fields

```json
{
  "pd_kind": "<kind>",
  "pd_schema_version": 1,
  "id": "<namespace>:<readable_name>",
  ...
}
```

Required fields:

- `pd_kind` (string). One of the 13 lockdown values (Section 1.2). Identifies which per-class parser handles the file.
- `pd_schema_version` (integer). Currently 1. Bumps explicitly when a schema makes a non-additive change.
- `id` (string). Catalog ID in `"namespace:readable_name"` form per [constraints.md](../../constraints.md) "Catalog ID strings at all interface boundaries". `namespace` is `base` for ROM-derived content, `mods/<modname>` for mod content. `readable_name` is human-friendly.

Optional fields (per kind):

- `display_name` (string). UI-facing name. Defaults to `id`'s readable portion.
- `description` (string). UI tooltip text or modder note.
- `tags` (string array). Discoverable categorization in tools.
- `origin` (string). Provenance hint when an in-client tool clones an asset (per audit M-11 extrapolation).

### 1.2 The 13 lockdown kinds

| `pd_kind` | Extension | Container | Replaces |
|---|---|---|---|
| `weapon` | `.pdwpn` | JSON | One row of `weapons.pdbase` `weapons[]` |
| `head` | `.pdhead` | JSON | One row of `heads.pdbase` `heads[]` |
| `body` | `.pdbody` | JSON | One row of `bodies.pdbase` `bodies[]` |
| `arena` | `.pdarena` | JSON | One row of `arenas.pdbase` `arenas[]` |
| `mesh` | `.pdmesh` | ZIP | One `data/<romid>/files/<name>.bin` plus metadata sidecar |
| `animation` | `.pdanim` | JSON | One row of `weapons.pdbase` `animations[]` (weapon anims) OR one entry inside `data/<romid>/segs/animations.bin` (character anims) |
| `sfx` | `.pdsfx` | ZIP | One entry inside `data/<romid>/segs/sfxtbl.bin` |
| `voice` | `.pdvoice` | ZIP | A subset of SFX bank entries identified as voice |
| `song` | `.pdsong` | ZIP | One entry inside `data/<romid>/segs/seqtbl.bin` |
| `scenario` | `.pdscenario` | ZIP (UNIFIED per Q-1) | A row of `g_Stages[]` plus its bg / pads / setup / mpsetup / tile files |
| `ui` | `.pdui` | ZIP | Loose `data/ui/textures/*.tga + .png + .9slice.json` per [pdgui_theme.cpp:2424](../../../port/fast3d/pdgui_theme.cpp:2424) |
| `font` | `.pdfont` | ZIP | One face inside `data/<romid>/segs/font<face>.bin` |
| `lang` | `.pdlang` | ZIP | One `data/<romid>/files/L<stage>Z.bin` or `data/<romid>/segs/mpstrings*.bin` |

Container types:

- **JSON:** the file is a single JSON document at the path. Plain text. Modders can edit in any editor.
- **ZIP:** the file is a ZIP archive containing `manifest.json` (the JSON document holding the envelope + metadata) plus the authored source payloads for that asset, such as OBJ/MTL geometry, WAV audio, MIDI music, TSV metadata, or generated private cache inputs. Reuses [port/src/modarchive.c](../../../port/src/modarchive.c) writer infrastructure.

The `pd_kind` value is authoritative; the file extension is informational. A `.pdwpn` file MUST carry `"pd_kind": "weapon"`. Mismatches LOUDFAIL at parse time (`LOUDFAIL.LOAD.SCHEMA.KIND_MISMATCH`).

### 1.3 Cross-references

Asset references between `.pd*` files use catalog ID strings, not filenames or filesystem paths. A weapon's `hi_model` field carries `"base:falcon2_hi"`, not `"data/ntsc-final/meshes/base_falcon2_hi.pdmesh"`. The catalog resolves the ID at load time, which makes the asset relocatable (a modder shipping a custom mesh under a new ID gets picked up automatically) and also makes the `.pd*` files portable across `data/` and `mods/` trees.

Symbolic enum references (e.g. animation opcode arguments like `"ANIM_GUN_FALCON2_RELOAD"`) stay as enum-strings because the underlying engine indexes them by integer through the enum table at [port/src/loader_pdbase_enums.c](../../../port/src/loader_pdbase_enums.c). The loader resolves the enum string to its integer at parse time.

---

## 2. Per-kind schemas

Each subsection covers one `pd_kind` with: required fields, optional fields, an example, and migration notes from the current `.pdbase` shape.

### 2.1 `pd_kind: weapon` (`.pdwpn`)

Authored asset archive. One file per weapon. The archive root carries `weapon.ini` and a weapon behavior document, plus all authored model, animation, audio, texture, reticle, overlay, and display assets needed to edit or clone the weapon. Legacy JSON `.pdwpn` files remain a migration input, but the target modder-facing shape is a zip-openable archive consistent with the c3812 typed-asset contract.

Required fields:

- `pd_kind`, `pd_schema_version`, `id` (envelope).
- `weapon_id` (integer). The legacy `WEAPON_*` enum value. Used by the loader to populate the right slot in `s_Weapons[CATALOG_MGR_WEAPON_COUNT]`. Range 0..85.
- `symbol` (string). Original C symbol name from `invitems.c` (e.g. `"invitem_falcon2"`). Provenance only; modders can drop or set arbitrary value for new weapons.
- `hi_model` (catalog ID string). Reference to the `.pdmesh` for the weapon's high-detail model.
- `lo_model` (catalog ID string). Reference to the LOD `.pdmesh`.
- `flags` (integer). Bitfield of `WEAPONFLAG_*` constants.
- `name`, `shortname` (string). Lang-table identifiers (`L_GUN_*`).
- `manufacturer`, `description` (string). Lang-table identifiers.

Optional fields (today populated for most weapons):

- `equip_animation`, `unequip_animation`, `pritosec_animation`, `sectopri_animation` (catalog ID string or null). References to `.pdanim` files.
- `functions` (array of function objects). Each function has `_struct` discriminator (`weaponfunc_shootsingle` / `weaponfunc_shootauto` / `weaponfunc_shootprojectile` / `weaponfunc_throw` / `weaponfunc_melee` / `weaponfunc_special` / `weaponfunc_device`) plus per-struct fields. Inlined.
- `ammos` (array of ammo objects, may include `null` slots).
- `aimsettings` (object inline).
- `muzzlez`, `posx`, `posy`, `posz`, `sway` (numbers). Render-time positioning.
- `gunviscmds` (array of two-element arrays). Visibility command pairs.
- `partvisibility` (array of two-element arrays).
- `bot_pref` (object inline). Bot AI preferences (target ammo per priority, distance configs, etc.).

**Audio reference type-tolerance (Q-2 architectural invariant).** Any field referencing audio (`shootsound` inside `functions`, future `equip_sound`, etc.) accepts EITHER an `.pdsfx` catalog ID OR a `.pdvoice` catalog ID. The audio playback layer routes by kind at the consumer. This means a modder making a "weapon fire sound = voice line" mod sets `"shootsound": "modname:my_voice_line"` and it just works.

**Behavior modularization (2026-05-20 c3812 clarification).** Weapon behavior already exists in game code and should be modularized into the `.pdwpn` authored format rather than reinvented. The project-owned behavior document should express trigger events, fire cadence in centiseconds, projectile references, looping fire, hold-fire beams, charge-and-release, secondary modes, melee attacks, zoom levels, reticle/overlay/zoom-camera effects, and model/HUD state tied to ammo and reload state. Examples that must fit the same format include Needler-style physical ammo needles with reload slot animation and assault-rifle-style numeric ammo screens on the weapon model.

Example (Falcon 2):

```json
{
  "pd_kind": "weapon",
  "pd_schema_version": 1,
  "id": "base:falcon2",
  "weapon_id": 2,
  "symbol": "invitem_falcon2",
  "hi_model": "base:falcon2_hi",
  "lo_model": "base:falcon2_lo",
  "equip_animation": "base:invanim_falcon2_equip",
  "unequip_animation": "base:invanim_falcon2_unequip",
  "pritosec_animation": null,
  "sectopri_animation": null,
  "functions": [
    {
      "_struct": "weaponfunc_shootsingle",
      "name": "L_GUN_085",
      "ammoindex": 0,
      "noisesettings": {
        "minradius": 0, "maxradius": 14, "incradius": 2,
        "decbasespeed": 1, "decremspeed": 6
      },
      "fire_animation": "base:invanim_falcon2_shoot",
      "flags": 0,
      "recoilsettings": {
        "xrange": 0.6, "yrange": 0.6, "zrange": 0.6,
        "unk0c": 0.2, "unk10": 1
      },
      "recoverytime60": 16, "damage": 1, "spread": 1,
      "shootsound": "base:sfx_804d",
      "penetration": 1
    },
    {
      "_struct": "weaponfunc_melee",
      "name": "L_GUN_094",
      "fire_animation": "base:invanim_falcon2_pistolwhip",
      "flags": 4301312,
      "damage": 0.9, "range": 60
    }
  ],
  "ammos": [
    {
      "type": 1, "casingeject": 0, "clipsize": 8,
      "reload_animation": "base:invanim_falcon2_reload",
      "flags": 0
    },
    null
  ],
  "aimsettings": {
    "zoomfov": 0, "guntransup": 3, "guntransdown": 8, "guntransside": 15,
    "aimdamppal": 0.9721, "aimdamp": 0.9767,
    "tracktype": 1, "flags": 2
  },
  "muzzlez": 2,
  "posx": 9, "posy": -15.7, "posz": -23.8, "sway": 1,
  "gunviscmds": [
    ["sethidden", 42], ["sethidden", 43], ["sethidden", 44],
    ["sethidden", 45], ["sethidden", 46], ["sethidden", 47],
    ["sethidden", 53], ["end"]
  ],
  "partvisibility": [
    [90, 0], [42, 0], [43, 0], [44, 0], [45, 0], [46, 0], [47, 0], [255]
  ],
  "shortname": "L_GUN_007",
  "name": "L_GUN_007",
  "manufacturer": "L_GUN_150",
  "description": "L_GUN_156",
  "flags": 702076,
  "bot_pref": { "unk00": 56, "unk01": 60, "unk02": 84, "unk03": 88,
                "haspriammogoal": 1, "hassecammogoal": 1,
                "pridistconfig": 1, "secdistconfig": 0 }
}
```

Migration notes from `weapons.pdbase`:

- `_symbol` debug fields on inline sub-records are dropped (Falcon 2 example removes `"_symbol": "invfunc_falcon2_singleshot"` etc.). They were round-trip helpers during F11; not needed in production.
- Cross-references (`hi_model`, `lo_model`, anim refs, `shootsound`) become catalog IDs instead of `FILE_*` / `ANIM_*` / `SFX_*` symbolic enum strings. The enum table at [loader_pdbase_enums.c](../../../port/src/loader_pdbase_enums.c) is no longer needed for resolving these specific refs (catalog ID layer mediates).
- `ANIM_GUN_*` opcodes inside `.pdanim` arguments stay as enum strings because the legacy `invexec.c` interpreter indexes them by integer through the enum table.

### 2.2 `pd_kind: head` (`.pdhead`)

JSON document. One file per head. Direct lift from [base/heads.pdbase](../../../base/heads.pdbase).

Required:

- Envelope.
- `headnum` (integer). Slot in `s_HeadsPool[CATALOG_MGR_HEAD_COUNT]`.
- `ismale` (integer 0/1).
- `type` (enum string). `HEADBODYTYPE_DEFAULT` / `HEADBODYTYPE_FEMALE` / etc.
- `mesh` (catalog ID string). Reference to `.pdmesh` for this head's geometry. (Replaces `filenum` symbolic enum.)

Optional:

- `unk00_01` (integer). Legacy field; semantics under investigation.
- `height` (integer). Rendering height.
- `scale`, `animscale` (number).

Example:

```json
{
  "pd_kind": "head",
  "pd_schema_version": 1,
  "id": "base:head_dark_combat",
  "headnum": 4,
  "ismale": 0,
  "unk00_01": 1,
  "type": "HEADBODYTYPE_FEMALE",
  "height": 13,
  "mesh": "base:head_dark_combat_mesh",
  "scale": 1.0,
  "animscale": 1.0
}
```

Migration notes:

- `filenum` symbolic enum (e.g. `"FILE_CHEADDARK_COMBAT"`) becomes `mesh` catalog ID (e.g. `"base:head_dark_combat_mesh"`).
- SP-only fallback heads keep their existing `base:sp_head_<num>` IDs.

### 2.3 `pd_kind: body` (`.pdbody`)

JSON document. One file per body. Lift from [base/bodies.pdbase](../../../base/bodies.pdbase).

Required:

- Envelope.
- `bodynum` (integer). Slot in `s_BodiesPool[CATALOG_MGR_BODY_COUNT]`.
- `ismale` (integer 0/1).
- `type` (enum string).
- `mesh` (catalog ID string). Reference to `.pdmesh` for body geometry.

Optional:

- `canvaryheight` (integer).
- `unk00_01`, `height` (integer).
- `scale`, `animscale` (number).
- `hand` (catalog ID string). Reference to `.pdmesh` for hand model. (Replaces `handfilenum` symbolic enum.)

Example:

```json
{
  "pd_kind": "body",
  "pd_schema_version": 1,
  "id": "base:djbond",
  "bodynum": 0,
  "ismale": 1,
  "unk00_01": 0,
  "canvaryheight": 0,
  "type": "HEADBODYTYPE_DEFAULT",
  "height": 167,
  "mesh": "base:djbond_mesh",
  "scale": 1.0,
  "animscale": 1.0446009635925,
  "hand": "base:hand_ddsecurity"
}
```

### 2.4 `pd_kind: arena` (`.pdarena`)

JSON document. One file per arena. Lift from [base/arenas.pdbase](../../../base/arenas.pdbase).

Required:

- Envelope.
- `arena_index` (integer). Slot in `s_ArenasPool[CATALOG_MGR_ARENA_COUNT]`.
- `slug` (string). Short identifier (`mp_skedar`, `mp_villa`, etc.).
- `category` (string). UI category (`Dark`, `MP Maps`, etc.).
- `scenario` (catalog ID string). Reference to the `.pdscenario` for this arena's playable stage. (Replaces `stagenum` integer at the schema layer; the integer stagenum is derived at load time from the resolved scenario.)

Optional:

- `requirefeature` (integer). Bitfield of feature requirements.
- `name_langid` (integer). Lang-table string ID for the display name.
- `load_mode` (enum string). `ARENA_LOADMODE_PLAYABLE`, etc.

Example:

```json
{
  "pd_kind": "arena",
  "pd_schema_version": 1,
  "id": "base:arena_mp_skedar",
  "arena_index": 0,
  "slug": "mp_skedar",
  "category": "Dark",
  "scenario": "base:scenario_mp_skedar",
  "requirefeature": 0,
  "name_langid": 20599,
  "load_mode": "ARENA_LOADMODE_PLAYABLE"
}
```

Note: today the arena record carries `stagenum` integer directly. Under the universality model, `stagenum` derivation goes through the catalog (the arena's `scenario` ID resolves to a `.pdscenario` whose envelope carries the stagenum). This satisfies the [constraints.md](../../constraints.md) "Catalog-first pattern for stage identity" rule which mandates `stage_id` as authoritative and `stagenum` as derived at consumption.

### 2.5 `pd_kind: mesh` (`.pdmesh`, ZIP compound)

ZIP archive. One file per model. Replaces the old `data/<romid>/files/<name>.bin` raw byte slabs with standard geometry output.

Container layout:

```
manifest.json            <- envelope + metadata (this file's pd_kind, id, etc.)
model.ini                <- editable descriptor
model.obj                <- Wavefront OBJ converted from model display lists
model.mtl                <- material stub/references for OBJ tooling
model.obj.sha256         <- digest for self-heal (existing pattern)
model.mtl.sha256
```

`manifest.json` required fields:

- Envelope.
- `source_format` (string). Provenance of the base extractor input. Current base extraction uses `"PD_MODELDEF"`.
- `format` (string). Standard geometry format. Current base extraction uses `"OBJ"`.
- `geometry` (string). Filename inside the ZIP holding the standard geometry payload. Current base extraction uses `"model.obj"`.
- `material` (string). Filename inside the ZIP holding the material payload. Current base extraction uses `"model.mtl"`.

`manifest.json` optional fields:

- `source_filenum_symbol` (string). Provenance: original `FILE_*` symbol from invitems.c / robot.c. Helps modders trace lineage.
- `materials` (array). Texture material slot definitions. Reserved for future schema; today empty.
- `attach_points` (array). Forward-looking accessory attach points per audit FW-1. Today empty.
- `skeleton` (object). Forward-looking skeletal data. Today empty.
- `collision` (object). Collision capsule / bbox. Today derived from model at load time.

Example `manifest.json`:

```json
{
  "pd_kind": "mesh",
  "pd_schema_version": 1,
  "id": "base:falcon2_hi",
  "source_filenum_symbol": "FILE_GFALCON2",
  "source_format": "PD_MODELDEF",
  "format": "OBJ",
  "geometry": "model.obj",
  "material": "model.mtl",
  "triangle_count": 128,
  "display_list_count": 4
}
```

### 2.6 `pd_kind: animation` (`.pdanim`)

JSON document. One file per animation. Two categories distinguished by the `category` field per Q-3:

- `category: "weapon_animation"`. Gunscript opcode array (today inline in `weapons.pdbase` `animations[]`).
- `category: "character_animation"`. Frame-data animation (today inside `data/<romid>/segs/animations.bin`).

Required:

- Envelope.
- `category` (enum string). `"weapon_animation"` or `"character_animation"`.

For `weapon_animation`:

- `opcodes` (array of arrays). Each inner array is `[mnemonic, arg, arg, ...]`. Mnemonics include `playanimation`, `waittime`, `random`, `include`, `end`. Symbolic enum strings (`ANIM_GUN_*`) stay as strings; the loader resolves through the enum table.

For `character_animation`:

- `frames` (string). Filename inside an adjacent or embedded byte payload holding the raw frame data. Possibility: character anims are JSON-described with a sibling `.bin` payload (then `.pdanim` becomes a compound ZIP for this category). Possibility: character anims embed frame bytes as base64 in JSON (simpler, fatter file).

  **Lock-down:** character animations are ZIP compounds for this category (mirrors `.pdmesh` pattern). The plain-JSON form is reserved for `weapon_animation`. Schema doc decision; alternative is open if Step 3a discovers a friction point.

Example (weapon animation):

```json
{
  "pd_kind": "animation",
  "pd_schema_version": 1,
  "id": "base:invanim_falcon2_reload",
  "category": "weapon_animation",
  "opcodes": [
    ["playanimation", "ANIM_GUN_FALCON2_RELOAD", 0, 10000],
    ["waittime", 7, 2],
    ["end"]
  ]
}
```

Example (character animation, ZIP compound `manifest.json`):

```json
{
  "pd_kind": "animation",
  "pd_schema_version": 1,
  "id": "base:anim_chr_run",
  "category": "character_animation",
  "frames": "frames.bin",
  "frame_count": 24,
  "frame_rate_hz": 30,
  "loops": true
}
```

The `frames.bin` inside the ZIP holds the same bytes as today's slot in `data/<romid>/segs/animations.bin`.

### 2.7 `pd_kind: sfx` (`.pdsfx`, ZIP compound)

ZIP archive. One file per sound effect. Replaces the unsplit SFX bank in today's `data/<romid>/segs/sfxctl.bin` + `sfxtbl.bin`.

Container layout:

```
manifest.json
sample.bin               <- raw audio sample bytes (ALADPCM, etc.)
sample.bin.sha256
```

`manifest.json` required fields:

- Envelope.
- `format` (enum string). `"ALADPCM"`, `"PCM16"`, `"OGG_REPLACEMENT"`, etc.
- `sample_rate_hz` (integer). 22050 typical for SFX.
- `data` (string). Filename inside ZIP. Conventionally `"sample.bin"`.

Optional:

- `loop_start_samples` (integer). Loop point.
- `loop_end_samples` (integer).
- `tags` (array of strings). Modder-discoverable categorization (`"weapon"`, `"explosion"`, etc.).

### 2.8 `pd_kind: voice` (`.pdvoice`, ZIP compound)

ZIP archive. One file per voice line. Same byte payload pattern as `.pdsfx` but with voice-specific metadata.

`manifest.json` required fields:

- Envelope.
- `format` (enum string). Same set as `.pdsfx`.
- `sample_rate_hz` (integer).
- `data` (string). Filename inside ZIP.
- `actor` (string). Speaker identifier (`"carrington"`, `"jonathan"`, etc.).

Optional:

- `transcript` (string). Subtitle text.
- `language` (string). ISO 639-1 code (`"en"`, `"jp"`).
- `context` (string). Mission / scene the line belongs to.

**Q-2 type tolerance invariant.** A weapon's `shootsound` field accepts a `.pdvoice` catalog ID just as readily as a `.pdsfx` one. This rule applies at every audio-consumption site in the engine: weapon function audio, prop ambient audio, footstep audio, hit audio, etc. The audio playback layer reads `pd_kind` at resolve time and routes accordingly. Document at every audio-ref schema field: "accepts any audio kind."

### 2.9 `pd_kind: song` (`.pdsong`, ZIP compound)

ZIP archive. One file per music track.

`manifest.json` required fields:

- Envelope.
- `format` (enum string). `"ALSEQ"` (N64 sequence), `"OGG_REPLACEMENT"`, etc.
- `data` (string). Filename inside ZIP.

Optional:

- `tempo_bpm` (number).
- `loops` (boolean).
- `intro_data` (string). Filename for an optional intro segment that plays once before the loop.

### 2.10 `pd_kind: scenario` (`.pdscenario`, UNIFIED ZIP per Q-1)

ZIP archive. One file per stage / mission / firing range / MP arena's playable level. UNIFIED per Mike's Q-1 directive: a stage is one file.

Container layout:

```
manifest.json            <- envelope + scenario metadata + props + AI
geometry.bin             <- bg geometry segment (was bg_<stage>.seg)
geometry.bin.sha256
tiles.bin                <- walkable tile data (was bg_<stage>_tilesZ)
tiles.bin.sha256
pads.bin                 <- pad collision data (was bg_<stage>_padsZ)
pads.bin.sha256
setup.bin                <- mission setup script (was U<stage>Z)
setup.bin.sha256
mpsetup.bin              <- MP-specific setup overlay (was MP setup file)
mpsetup.bin.sha256
```

`manifest.json` required fields:

- Envelope.
- `kind` (enum string). `"solo"` / `"mp"` / `"firingrange"` / `"coop"`.
- `stagenum` (integer). Logical stagenum (e.g. 0x5e). Used at `mainChangeToStage` consumption per [constraints.md](../../constraints.md) Catalog-first stage identity rule.
- `geometry`, `tiles`, `pads`, `setup` (strings). Filenames inside ZIP holding the byte payloads. Conventionally as named above.

Optional:

- `mpsetup` (string). Filename for MP setup overlay; absent for solo missions.
- `display_name` (string).
- `music` (catalog ID string). Reference to default `.pdsong`.
- `ambient_sfx` (array of catalog ID strings). Ambient `.pdsfx` references.
- `lang` (catalog ID string). Reference to `.pdlang` for stage-specific dialog.
- `props` (array). Per-prop spawn declarations with `asset_id` (catalog ID), `transform`, etc. Reserved for future modder-readable scenario edit; today empty (props are encoded in the binary `setup.bin` payload).
- `weapon_spawns`, `pickup_spawns` (arrays). Per-location spawn declarations per [audits/rom-extraction-audit-2026-04-30.md](../../audits/rom-extraction-audit-2026-04-30.md) Section 3.2. Reserved.
- `ai`, `events` (arrays). Reserved.

Example `manifest.json` (Skedar MP):

```json
{
  "pd_kind": "scenario",
  "pd_schema_version": 1,
  "id": "base:scenario_mp_skedar",
  "kind": "mp",
  "stagenum": 50,
  "display_name": "Skedar Ruins",
  "geometry": "geometry.bin",
  "tiles": "tiles.bin",
  "pads": "pads.bin",
  "setup": "setup.bin",
  "mpsetup": "mpsetup.bin",
  "music": "base:song_skedar_ruins",
  "lang": "base:lang_skedar"
}
```

**Q-1 unified rationale.** Modding mostly happens in-client per Mike. A modder loads one `.pdscenario` and the in-client tool handles geometry / tiles / setup / mpsetup / pads atomically. Splitting them into siblings would force the modder to manage five files for one logical stage. The unified ZIP also makes network distribution simpler (one digest, one transfer) and matches modder mental model of "this is my level."

### 2.11 `pd_kind: ui` (`.pdui`, ZIP compound)

ZIP archive. UI texture bundle plus theme metadata. Replaces today's loose `data/ui/textures/*.tga + .png + .9slice.json` files written by [pdgui_theme.cpp:2424](../../../port/fast3d/pdgui_theme.cpp:2424).

Container layout:

```
manifest.json            <- envelope + theme metadata
textures/<name>.tga      <- decoded RGBA32 textures (top-down 32-bit)
textures/<name>.png      <- PNG mirror for modder editing
textures/<name>.9slice   <- per-texture nineslice INI (4 numbers)
themes/<theme>.json      <- theme palette / scanlines / tint
```

`manifest.json` required fields:

- Envelope.
- `theme_count` (integer).
- `texture_count` (integer).

Per [audits/ui-asset-pipeline-investigation-2026-04-30.md](../../audits/ui-asset-pipeline-investigation-2026-04-30.md) for the deeper schema sketch including theme JSON structure.

### 2.12 `pd_kind: font` (`.pdfont`, ZIP compound)

ZIP archive. One file per font face.

`manifest.json` required:

- Envelope.
- `face` (string). `"handelgothic"`, `"bankgothic"`, etc.
- `sizes` (array of integers). Available point sizes.
- `data` (string). Filename inside ZIP.

### 2.13 `pd_kind: lang` (`.pdlang`, ZIP compound)

ZIP archive. Language string bank.

`manifest.json` required:

- Envelope.
- `locale` (string). ISO 639-1 (`"en"`, `"jp"`).
- `category` (enum string). `"stage"` / `"mp_ui"` / `"system"`.
- `data` (string). Filename inside ZIP.

---

## 3. Cross-cutting decisions

### 3.1 Audio reference type-tolerance (Q-2 invariant)

Any field in any schema that names an audio asset (today: `shootsound` inside `.pdwpn` `functions`; future: ambient_sfx, footstep_sfx, prop hit audio, weapon equip sound, etc.) accepts a catalog ID resolving to ANY audio kind: `.pdsfx`, `.pdvoice`, or `.pdsong`.

The audio playback layer reads `pd_kind` from the resolved catalog row at play time and routes to the right decoder. A weapon's `shootsound: "modname:my_voice_line"` works because:

1. The catalog resolves `"modname:my_voice_line"` to a row with `pd_kind: "voice"`.
2. The audio playback layer reads the row's `pd_kind`, sees voice.
3. Routes to the voice decoder (same ALADPCM decoder; voice is structurally an SFX with metadata).

This rule lets modders write "my weapon plays a one-liner instead of a gunshot" without engine changes.

**Implementation note for Step 4 loader:** when registering audio catalog rows, the row's `runtime_index` must encode kind so consumers can route without an extra lookup. Possibility: extend `ASSET_AUDIO` row's `ext.audio` struct with a `kind` field. Possibility: register voice as a distinct `ASSET_VOICE` enum value. Step 4 picks one before the loader walks `audio/sfx/`, `audio/voice/`, `audio/music/` directories.

**Schema documentation convention:** every audio-ref field in this doc carries the comment "accepts any audio kind" so modders see it in the per-field schema sheet.

### 3.2 Unnamed ROM file slots (Q-4 policy)

ROM file slots without internal names. Today extracted as `data/<romid>/files/G_<XXXX>.bin` per [romextract.c:97](../../../port/src/romextract.c:97). Under the universality model, each unnamed slot needs a disposition.

Three buckets, decided per-slot during Step 1 implementation:

**Bucket 1: zero references found.** The slot has no consumer in the runtime (no `g_FileInfo[<XXXX>]` access, no `fileLoadToNew(<XXXX>)`, no `assetLoadRom*(<XXXX>)`). Treated as junk. Action: do not extract, do not emit a `.pd*` file.

Identification: grep audit for the slot index across `src/`, `port/src/`, `port/include/`. Log per-slot disposition in an audit appendix produced as part of Step 1's deliverable.

**Bucket 2: has references, purpose obscure.** The slot has consumers but the consumer code does not make the asset class clear. Action: investigate the consumer code, infer the kind from how the bytes are used (loaded by `texLoadFromConfig` -> texture; loaded by `modeldefLoad` -> mesh; etc.). Name the catalog ID accordingly: `base:tex_unknown_<XXXX>`, `base:mesh_unknown_<XXXX>`, etc.

**Bucket 3: has references with clear purpose.** The slot has consumers that make the asset class obvious AND a sensible name can be inferred from context (variable names, comments, surrounding code). Name properly: `base:tex_carrington_logo`, `base:mesh_skedar_idol`, etc.

**Audit appendix.** Step 1's deliverable includes `context/audits/catalog-universality-unnamed-slot-dispositions-YYYY-MM-DD.md` listing every unnamed slot with bucket, references found, and final catalog ID (or "junked" status).

### 3.3 Parity check pattern (Q-5)

During Steps 1 through 4, both the legacy `.pdbase` archives and the new per-asset `.pd*` files exist on disk. The parity check runs at boot (or, where possible, at test time only to avoid runtime cost) and asserts every field in every loaded record matches between the two sources.

Implementation:

- After both loaders run (legacy `.pdbase` parser AND new universal `.pd*` parser), an additional pass walks each pool and compares record-by-record, field-by-field.
- Mismatches log `LOADER.UNIVERSAL.PARITY_FAIL: <pool>[<idx>].<field> legacy=<L> new=<N>`.
- Same channel pattern as F12 of the weapons gate (`LOADER.PDBASE.WEAPON.PARITY_FAIL` per [pillars/catalog.md](../../pillars/catalog.md)).
- Mike validates after each migration step with the parity check enabled.

Retirement at Step 5:

- The parity check, the legacy `.pdbase` parsers, and the `.pdbase` archives themselves all delete in the same commit.
- Test pin (per F13 weapons pattern): a grep-guard test asserts no live code references `.pdbase` filenames or the parity check function names.

### 3.4 Storage layout (locked)

Per audit Section 2.2 Hypothesis A:

```
data/
  <romid>/                         <- "ntsc-final", future "pal-final", "jpn-final"
    weapons/      *.pdwpn
    heads/        *.pdhead
    bodies/       *.pdbody
    arenas/       *.pdarena
    meshes/       *.pdmesh
    animations/   *.pdanim         <- both weapon and character animations
    audio/
      sfx/        *.pdsfx
      music/      *.pdsong
      voice/      *.pdvoice
    scenarios/    *.pdscenario     <- one ZIP per stage (UNIFIED per Q-1)
    ui/           *.pdui
    fonts/        *.pdfont
    lang/         *.pdlang
    manifest.pdmanifest            <- top-level: every file + SHA-256
  _quarantine/
    <romid>/
      <unixtime>_<basename>        <- corrupted asset evicted before re-extract
mods/
  <modname>/                       <- folder mod
    weapons/      *.pdwpn
    ...
  <modname>.pdmod                  <- archive mod (ZIP); same internal structure
```

Catalog ID to filename mapping: `<namespace>:<readable_name>` becomes `<namespace>_<readable_name>.pd<ext>`. Colon to underscore. Namespace cannot contain underscores by convention; safe round-trip.

### 3.5 Parser strategy (locked)

Per audit Section 2.4 Strategy A: one parser per `.pd*` extension. Each parser owns its kind's schema. Implementation lives in `port/src/loader_universal_<kind>.c` files (renamed from today's `loader_pdbase.c`). The universal walker dispatches by `pd_kind` at the top of each file.

Strategy B (one universal schema-driven parser) is reserved for a polish pass after all kinds are migrated; not in scope for the core pivot.

### 3.6 Boot order (locked)

Per audit Section 1.5 + Step 4 considerations:

```
romdataInit()                                  <- ROM file loaded into memory
romExtractAllFiles() + Verify                  <- Pass A
romExtractAllPdwpn() + Verify                  <- Step 1 emits .pdwpn
romExtractAllPdhead() + ... + Verify           <- Step 2 emits .pdhead/.pdbody/.pdarena
romExtractAllPdanim() + Verify                 <- Step 1 weapon anims + Step 3a chr anims
romExtractAllPdaudio() + ... + Verify          <- Step 3 byte-payload classes
romExtractAllPdscenario() + Verify             <- Step 3 scenarios (unified ZIP)
romExtractAllPdui() + ... + Verify             <- Step 3 ui/fonts/lang
romExtractEmitBootIntegrityReport()            <- Pass D
romdataReleaseRom()                            <- Pass C: ROM mapping freed
gameInit()
modmgrInit()
catalogInit()
stageTableInit()
assetCatalogInit()
catalogUniversalScan(romid)                    <- Step 4: walks data/<romid>/ and mods/
catalogManagerHeadInit() + ... etc             <- managers populate from catalog rows
```

Step 4 collapses the historical `assetCatalogRegisterBaseGame()` plus `loaderPdbaseScan("base", ...)` into the single `catalogUniversalScan()` call.

### 3.7 Catalog row registration order (locked)

Catalog rows are registered AS the universal scanner discovers each `.pd*` file. The legacy "register from in-binary tables first, then load from .pdbase" pattern goes away. Implication: the legacy `g_HeadsAndBodies[]`, `g_MpArenas[]`, `g_Stages[]` static arrays become unused at registration time after Step 4 (they may stay alongside as compatibility shims for any consumer that still reads them by index, but registration drives off the loaded `.pd*` files).

### 3.8 Compound (ZIP) writer infrastructure

ZIP-based `.pd*` kinds (`.pdmesh`, `.pdsfx`, `.pdvoice`, `.pdsong`, `.pdscenario`, `.pdui`, `.pdfont`, `.pdlang`) reuse [port/src/modarchive.c](../../../port/src/modarchive.c) writer subset. Specifically `modArchiveBegin / AddFileMem / AddFileDisk / Finish` per [pillars/modding.md](../../pillars/modding.md). Same atomic write pattern (write to `.tmp`, rename on success).

Possibility: the `modarchive` API is structured around mod-authoring use cases and may need a slight extension for extractor use (e.g. embedding sidecar `.sha256` files inside the ZIP rather than alongside it). Step 1 implementation considers this; if friction, a thin sibling `port/src/pdcompound.c` is acceptable.

### 3.9 SHA-256 sidecar policy

Two layers:

1. **Inside compound `.pd*` ZIPs:** every binary blob (e.g. `geometry.bin` inside a `.pdmesh`) has its `.sha256` sidecar inside the same ZIP. The self-heal loop verifies blob integrity at load time without unzipping the whole archive.
2. **Top-level `manifest.pdmanifest`:** lists every `.pd*` file under `data/<romid>/` with its outer-file SHA-256. Equivalent to today's per-file `.sha256` sidecars under `data/<romid>/files/` but at the `.pd*` granularity instead of the raw `.bin` granularity.

The Pass D self-heal loop (`romExtractVerifyAll`, `romExtractToastDrain`, `romExtractEmitBootIntegrityReport` per [romextract.h](../../../port/include/romextract.h)) ports to operate on `.pd*` files instead of raw `.bin` files at Step 4. Same logic; updated paths.

### 3.10 LOUDFAIL channels

Per [audits/rom-extraction-audit-2026-04-30.md](../../audits/rom-extraction-audit-2026-04-30.md) Section 3.8. Channels for the universality pivot:

- `LOUDFAIL.EXTRACT.PDWPN.<reason>`, `LOUDFAIL.EXTRACT.PDHEAD.<reason>`, etc. Per-kind extraction failures.
- `LOUDFAIL.LOAD.SCHEMA.KIND_MISMATCH`. File extension does not match `pd_kind` field.
- `LOUDFAIL.LOAD.SCHEMA.VERSION_UNSUPPORTED`. File's `pd_schema_version` is newer than the loader supports.
- `LOUDFAIL.LOAD.UNIVERSAL.PARSE_FAIL`. JSON malformed or required field missing.
- `LOUDFAIL.LOAD.UNIVERSAL.RESOLVE_FAIL`. Cross-reference (catalog ID) does not resolve.
- `LOUDFAIL.LOAD.UNIVERSAL.PARITY_FAIL`. Field mismatch between legacy `.pdbase` source and new `.pd*` source during Step 1-4 parity period.
- `LOUDFAIL.HEAL.UNIVERSAL.<reason>`. Self-heal events on `.pd*` files.

---

## 4. Schema lock-in policy

### 4.1 What "locked" means

Schemas in this doc are LOCKED for the duration of the migration. A subsequent step that needs to add a field follows the lock-in escape hatches:

1. **Optional field addition.** A new optional field that older readers can ignore (and old writers can omit) does NOT require a schema version bump. Land the new field, document in this doc.
2. **Required field addition or behavior change.** Bumps `pd_schema_version` from 1 to 2. This doc updates with the new version's full schema. Old loaders see version 2, log `LOUDFAIL.LOAD.SCHEMA.VERSION_UNSUPPORTED`, refuse to load. Migration of existing `.pd*` files happens via the extractor regenerating them.
3. **Field rename or type change.** Same as field addition: bumps version.

### 4.2 Versioning across mods

Mods author against a specific `pd_schema_version`. The loader accepts mods at the current version OR any prior supported version. Mods at a future version log `LOUDFAIL.LOAD.SCHEMA.VERSION_UNSUPPORTED` and are skipped. This means a mod authored against v1 keeps working when the project bumps to v2 (loader supports both), but a mod authored against v3 cannot load on a v2-only runtime.

Currently all schemas in this doc are at version 1.

### 4.3 Open evolution paths (not in scope for v1)

These are forward-looking; track but do not implement in v1:

- **`.pdmesh` skeleton field** for forward-looking accessory attach points and behavior layer (audit FW-1, FW-2).
- **`.pdscenario` `props`, `weapon_spawns`, `pickup_spawns` arrays** for in-tool scenario editing (audit M-8).
- **`.pdui` theme schema** beyond the basic palette / scanlines / tint set; full parametric theming.
- **`.pdfont` glyph metrics** for proper kerning and per-glyph fallback.
- **Localization-aware compound `.pdlang`** with multi-locale bundles in one file.

Each of these is a v2 schema bump candidate. Document the proposed v2 fields in audit appendices as the evolution paths surface; do not pre-commit to v2 in v1.

---

## 5. Implementation hooks

This section gives concrete starting points for each subsequent migration step.

### 5.1 Step 1 (weapons emitter) entry points

- New file `port/src/romextract_pdwpn.c` defining `romExtractPdwpnEmit(weapon_index, out_dir)`.
- New file `port/src/romextract_curation_weapons.c` holding `static const struct weapon_curation_t s_BaseWeapons[86]` derived from current [base/weapons.pdbase](../../../base/weapons.pdbase).
- Throwaway converter `devtools/pdbase_to_c_curation.py` produces the curation C file from the existing JSON. One-time tool; deletes at Step 5.
- Wire `romExtractAllPdwpn()` into [main.c:296](../../../port/src/main.c:296) area after `romExtractAllFiles + Verify`.

### 5.2 Step 2 (heads + bodies + arenas) entry points

- New files `port/src/romextract_pdhead.c`, `_pdbody.c`, `_pdarena.c` and matching curation tables.
- Wire each `romExtractAll*` into the boot sequence in numbered order.

### 5.3 Step 3a (character animations) entry points

- New file `port/src/romextract_pdanim_chr.c`.
- New curation table `port/src/romextract_curation_chr_anims.c` holding per-character-anim metadata (frame rate, loop flag, transition rules).
- Loader extension: the `.pdanim` parser from Step 1 must accept the `category` field per Section 2.6.
- Wire `romExtractAllPdanimChr()` after Step 2 emitters.

### 5.4 Step 3 (byte-payload classes) entry points

- Per-class extractors: `romextract_pdsfx.c`, `_pdvoice.c`, `_pdsong.c`, `_pdscenario.c`, `_pdui.c`, `_pdfont.c`, `_pdlang.c`.
- The hardest class is `.pdscenario` (UNIFIED ZIP per Q-1). Implementation walks `g_Stages[]`, for each stage reads bg / pads / setup / mpsetup / tile bytes from existing extraction output, packages into one ZIP per stage.
- The retirement of [pdgui_theme.cpp:2424](../../../port/fast3d/pdgui_theme.cpp:2424) `pdguiThemeExtractRomTextures` happens here as the `.pdui` extractor takes over.

### 5.5 Step 4 (loader directory walker) entry points

- Rewrite [loaderPdbaseScan()](../../../port/src/loader_pdbase.c:1869) into a directory walker. Rename file to `port/src/loader_universal.c`.
- Drop the four hardcoded archive name lookups; replace with directory iteration over `data/<romid>/<class>/` and `mods/<modname>/<class>/`.
- Boot order shift (audit Section 5.1 boot order regressions risk).
- Catalog row registration moves AFTER the universal scan; coordinate with [main.c:357-388](../../../port/src/main.c:357).

### 5.6 Step 5 (retirement) entry points

- Delete `base/weapons.pdbase`, `base/heads.pdbase`, `base/bodies.pdbase`, `base/arenas.pdbase`.
- Delete `devtools/extract_*_pdbase.py` (4 scripts).
- Delete `devtools/pdbase_to_c_curation.py` (the throwaway converter from Step 1).
- Delete the parity check function from `loader_universal.c`.
- Delete the legacy `.pdbase` parsing code from `loader_universal.c` (formerly `loader_pdbase.c`).
- Update CMake so the release pipeline does not copy `.pdbase` files.
- Update [pillars/catalog.md](../../pillars/catalog.md) "What is done" + "Where to look" sections.
- Add grep-guard test `tests/test_no_pdbase_in_repo.cpp` per audit Step 5 deliverable.

---

## 6. Reference and provenance

### 6.1 Source of decisions

- **Q-1 unified scenario:** Mike, 2026-05-02. "Modding will mostly occur in-client, and so users will load one and it will load it smoothly."
- **Q-2 separate kinds + type-tolerant refs:** Mike, 2026-05-02. Architectural note on cross-type audio use ("a modder might make a mod where when a player fires a weapon, the sound is a voice line instead of a gunshot").
- **Q-3 character anims required:** Mike, 2026-05-02. "DO NOT DEFER beyond the scope of the catalog work. Catalog is not complete unless it is COMPLETE. IT IS FOUNDATIONAL TO EVERYTHING."
- **Q-4 unnamed slots policy:** Mike, 2026-05-02. "If there are no references, they are junk, right? If they are simply unnamed, try to determine their use or what they are and name them properly."
- **Q-5 parity period:** Mike, 2026-05-02. "Let's do like we did before, include parity until we test and validate our work is fully functional, then remove pdbase."

### 6.2 Where to look

- For the migration plan: [audits/catalog-universality-pivot-plan-2026-05-02.md](../../audits/catalog-universality-pivot-plan-2026-05-02.md).
- For the underlying ROM extraction model: [audits/rom-extraction-audit-2026-04-30.md](../../audits/rom-extraction-audit-2026-04-30.md).
- For `.pdui` schema details: [audits/ui-asset-pipeline-investigation-2026-04-30.md](../../audits/ui-asset-pipeline-investigation-2026-04-30.md).
- For current `.pdbase` shape (until Step 5 retires): [base/weapons.pdbase](../../../base/weapons.pdbase), [base/heads.pdbase](../../../base/heads.pdbase), [base/bodies.pdbase](../../../base/bodies.pdbase), [base/arenas.pdbase](../../../base/arenas.pdbase).
- For current loader: [port/src/loader_pdbase.c](../../../port/src/loader_pdbase.c), [port/include/loader_pdbase.h](../../../port/include/loader_pdbase.h).
- For current ROM extractor (Pass A through D shipped 2026-05-02): [port/src/romextract.c](../../../port/src/romextract.c), [port/include/romextract.h](../../../port/include/romextract.h).
- For modding pipeline (mod compatibility surface): [pillars/modding.md](../../pillars/modding.md).
- For catalog identity rules at boundaries: [constraints.md](../../constraints.md).

---

## 7. Sentinel

This doc ends here. If a future reader sees content past this line, the document was modified after the original draft.

DOC_END_2026-05-02_UNIVERSALITY_PIVOT_SCHEMAS_V1

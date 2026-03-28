# Mod Conversion Guide — Monolithic to Component Layout

> **Version**: 1.0
> **Date**: 2026-03-23
> **Related**: [ADR-002](../context/ADR-002-component-filesystem-decomposition.md), [Component Architecture](../context/component-mod-architecture.md)

This guide documents how the 5 bundled monolithic mods are converted to the
component-based filesystem layout, and how future modders can convert legacy
mods to the new format.

---

## 1. How the Legacy Mod System Works

Understanding the legacy system is essential before converting anything.

### 1.1 Monolithic Directory Structure

Each legacy mod lives in `mods/mod_{name}/` with this layout:

```
mod_{name}/
├── modconfig.txt       ← stage table patches (optional — may be empty or absent)
├── files/              ← replacement game files
│   ├── bgdata/         ← stage geometry, tiles, pads
│   ├── Ump_setup*Z     ← multiplayer setup files
│   ├── C*Z             ← character body/head models
│   ├── G*Z             ← gun/weapon models
│   ├── P*Z             ← prop models
│   └── L*              ← language/menu text files
└── textures/           ← texture replacements (keyed by hex ID)
    ├── 0049.bin
    ├── 004A.bin
    └── ...
```

### 1.2 Two Loading Mechanisms

Legacy mods use two distinct mechanisms, and understanding the difference is
the single most important thing in this guide.

**Mechanism A: File Replacement (implicit, most content)**

`modmgrResolvePath(relPath)` intercepts every file load in the game. It
iterates enabled mods in registry order and checks if the mod's directory
contains a file matching the requested relative path. First hit wins.

Example: when the game loads `bgdata/bg_arec.seg` for stage 0x17 (Ravine),
if `mod_gex/files/bgdata/bg_arec.seg` exists, the mod's version is loaded
instead. The mod never declares anything about stage 0x17 — it just provides
the replacement file and the resolver does the rest.

This is how the vast majority of mod content works. A mod that provides 30
bgdata files is replacing the geometry for 30 base game stages, but its
`modconfig.txt` may declare zero stages.

**Mechanism B: Stage Table Patching (explicit, rare)**

`modConfigLoad()` parses `modconfig.txt` and directly mutates `g_Stages[]`.
A `stage 0xNN { ... }` block can override any field of a stage table entry:
bgfileid, tilefileid, padsfileid, setupfileid, mpsetupfileid, allocation
values, music tracks, and weather configuration.

This is only needed when a mod wants to **redirect** which files a stage
points to — i.e., make stage 0x24 load `bg_mp20.seg` instead of the default
`bg_pam.seg`. Pure file replacement can't do this because the filename is
different.

### 1.3 Shared Model Files

Every bundled mod includes these 8 "coexistence" model files:

| File | Purpose |
|------|---------|
| `Ccarroll2Z` | Dr. Carroll body/head (renamed from CtestchrZ in v6) |
| `Cskedar2Z` | Skedar body/head (renamed from Pttb_boxZ in v6) |
| `Ghand_carollZ` | Dr. Carroll hand model |
| `Ghand_skedarZ` | Skedar hand model |
| `CheadgreyZ` | Joanna Dark JP version head |
| `Gm16Z` | AR53/AR33 weapon model |
| `Gmp5kZ` | DMC/D5K weapon model |
| `GskorpionZ` | KLO1313/KLOBB weapon model |

These exist in every mod for compatibility with Perfect Dark Plus. They are
**shared resources** — identical files duplicated across all 5 mods. In the
component layout, these become a single shared component referenced via
`depends_on`.

### 1.4 Texture Replacement

Textures live in `textures/{hex_id}.bin`. The hex ID corresponds to a texture
slot in the game's texture table. When the game loads texture 0x0049, if
`mod_gex/textures/0049.bin` exists, the mod's version is used.

This is file replacement applied to textures rather than bgdata. The same
mechanism, different directory.

---

## 2. File-to-Stage Mapping Reference

This is the critical reference for Option 3 decomposition. Each bgdata file
set corresponds to a base game stage. When a mod provides a replacement
bgdata file, it is implicitly targeting that stage.

### 2.1 Base Game Stage Table (relevant entries)

Extracted from `src/game/stagetable.c`. Format: `stagenum → STAGE_NAME → bgfile`

> **TODO (D3R-3/S30):** The hex values in the Stage Slot Usage tables below are
> ARRAY INDICES from stagetable.c, not logical stage IDs from constants.h.
> The GEX stage_patch annotations also conflate which slots the mod actually
> patches (EXTRA slots, not base game slots). These tables need a rewrite
> using correct STAGE_* IDs. The bgdata-to-stagenum mapping in §2.2 has
> already been corrected.

**Solo/Coop stages:**

| Stage | Name | BG File | Used By |
|-------|------|---------|---------|
| 0x00 | MAIANSOS | bg_lue | allinone (pads, setup) |
| 0x05 | ESCAPE | bg_lue | allinone (pads via bg_tra) |
| 0x07 | RETAKING | bg_dish | GEX overrides to bg_run (Train) |
| 0x08 | CRASHSITE | bg_azt | allinone (pads, setup) |
| 0x09 | CHICAGO | bg_pete | allinone (pads, setup) |
| 0x0a | G5BUILDING | bg_depo | allinone (pads, setup) |
| 0x0d | PELAGIC | bg_dam | allinone (pads, setup) |
| 0x0e | EXTRACTION | bg_ame | allinone (pads, setup) |
| 0x0f | TEST_RUN | bg_run | — |
| 0x10 | STAGE_24 | bg_sevx | GEX overrides to bg_tra (Bunker) |
| 0x13 | AIRBASE | bg_cave | allinone (pads, setup) |
| 0x14 | STAGE_28 | bg_cat | GEX overrides to bg_silo (Archives 1F) |
| 0x18 | VILLA | bg_eld | allinone overrides allocation (Suburb) |
| 0x19 | DEFENSE | bg_dish | allinone (pads, setup) |
| 0x1b | INFILTRATION | bg_lue | allinone (pads, setup) |
| 0x1c | DEFECTION | bg_ame | allinone (pads, setup) |
| 0x1d | AIRFORCEONE | bg_rit | allinone (pads, setup) |
| 0x1f | INVESTIGATION | bg_ear | allinone (pads, setup) |
| 0x20 | ATTACKSHIP | bg_lee | allinone (pads, setup) |
| 0x24 | DEEPSEA | bg_pam | Kakariko overrides to bg_mp20 |

**Multiplayer stages:**

| Stage | Name | BG File | Used By |
|-------|------|---------|---------|
| 0x03 | MP_RAVINE | bg_arec | GEX, GF64, Kakariko (file replacement) |
| 0x04 | TEST_ARCH | bg_arch | allinone (file replacement) |
| 0x0b | MP_COMPLEX | bg_ref | GEX, GF64, Kakariko (file replacement) |
| 0x0c | MP_G5BUILDING | bg_cryp | GEX (file replacement) |
| 0x11 | MP_TEMPLE | bg_jun | GEX (file replacement) |
| 0x15 | MP_PIPES | bg_crad | GEX, GF64 (file replacement) |
| 0x16 | SKEDARRUINS | bg_sho | allinone (pads, setup) |
| 0x1e | MP_SKEDAR | bg_oat | GEX (file replacement) |
| 0x2a | TEST_MP2 | bg_mp2 | allinone, GEX (file replacement) |
| 0x2b | MP_AREA52 | bg_mp3 | GEX (file replacement) |
| 0x2c | MP_WAREHOUSE | bg_mp4 | GEX (file replacement) |
| 0x2d | MP_CARPARK | bg_mp5 | GEX (file replacement) |
| 0x2e | TEST_MP6 | bg_mp6 | allinone, GEX (file replacement) |
| 0x2f | TEST_MP7 | bg_mp7 | Dark Noon (file replacement) |
| 0x31 | MP_RUINS | bg_mp9 | GEX (file replacement) |
| 0x32 | MP_SEWERS | bg_mp10 | GEX (file replacement) |
| 0x33 | MP_FELICITY | bg_mp11 | GEX, GF64 (file replacement) |
| 0x34 | MP_FORTRESS | bg_mp12 | GEX (file replacement) |
| 0x35 | MP_VILLA | bg_mp13 | GEX, Kakariko (file replacement) |
| 0x36 | TEST_MP14 | bg_mp14 | GEX (file replacement) |
| 0x38 | TEST_MP16 | bg_mp16 | GEX (file replacement) |
| 0x39 | TEST_MP17 | bg_mp17 | GEX (file replacement) |
| 0x3a | TEST_MP18 | bg_mp18 | GEX (file replacement) |
| 0x3b | TEST_MP19 | bg_mp19 | GEX (file replacement) |
| 0x3c | TEST_MP20 | bg_mp20 | GEX (file replacement) |

**Extended stages (added for mods):**

| Stage | Name | BG File | Purpose |
|-------|------|---------|---------|
| 0x3d–0x4d | EXTRA1–17 | various | GoldenEye X dedicated slots |
| 0x4e–0x4f | EXTRA18–19 | various | Kakariko dedicated slots |
| 0x50–0x53 | EXTRA20–23 | various | Goldfinger 64 dedicated slots |
| 0x54 | EXTRA24 | bg_mp13 | Suburb (allinone) |
| 0x55 | EXTRA25 | bg_stat | Training Day (allinone) |
| 0x56 | EXTRA26 | bg_mp13 | Additional |

### 2.2 BG File to Stage Lookup

To find which stage a bgdata file belongs to, match the `bg_*` stem:

| BG Stem | Base Stage | Hex |
|---------|------------|-----|
| bg_ame | DEFECTION | 0x30 |
| bg_arec | MP_RAVINE | 0x17 |
| bg_arch | TEST_ARCH | 0x18 |
| bg_ark | EXTRACTION (pads) | 0x22 |
| bg_azt | CRASHSITE | 0x1c |
| bg_cat | STAGE_28 | 0x28 |
| bg_cave | AIRBASE | 0x27 |
| bg_crad | MP_PIPES | 0x29 |
| bg_cryp | MP_G5BUILDING | 0x20 |
| bg_dam | PELAGIC | 0x21 |
| bg_depo | G5BUILDING | 0x1e |
| bg_dest | TEST_DEST | 0x1a |
| bg_dish | CITRAINING | 0x26 |
| bg_ear | INVESTIGATION | 0x33 |
| bg_eld | VILLA | 0x2c |
| bg_jun | MP_TEMPLE | 0x25 |
| bg_lam | TEST_LAM | 0x50 |
| bg_lee | ATTACKSHIP | 0x34 |
| bg_lue | INFILTRATION | 0x2f |
| bg_mp1 | MP_BASE | 0x39 |
| bg_mp2 | TEST_MP2 | 0x3a |
| bg_mp3 | MP_AREA52 | 0x3b |
| bg_mp4 | MP_WAREHOUSE | 0x3c |
| bg_mp5 | MP_CARPARK | 0x3d |
| bg_mp6 | TEST_MP6 | 0x3e |
| bg_mp7 | TEST_MP7 | 0x3f |
| bg_mp8 | TEST_MP8 | 0x40 |
| bg_mp9 | MP_RUINS | 0x41 |
| bg_mp10 | MP_SEWERS | 0x42 |
| bg_mp11 | MP_FELICITY | 0x43 |
| bg_mp12 | MP_FORTRESS | 0x44 |
| bg_mp13 | MP_VILLA | 0x45 |
| bg_mp14 | TEST_MP14 | 0x46 |
| bg_mp15 | MP_GRID | 0x47 |
| bg_mp16 | TEST_MP16 | 0x48 |
| bg_mp17 | TEST_MP17 | 0x49 |
| bg_mp18 | TEST_MP18 | 0x4a |
| bg_mp19 | TEST_MP19 | 0x4b |
| bg_mp20 | TEST_MP20 | 0x4c |
| bg_oat | MP_SKEDAR | 0x32 |
| bg_pam | DEEPSEA | 0x38 |
| bg_pete | CHICAGO | 0x1d |
| bg_ref | MP_COMPLEX | 0x1f |
| bg_rit | AIRFORCEONE | 0x31 |
| bg_run | TEST_RUN | 0x23 |
| bg_sho | SKEDARRUINS | 0x2a |
| bg_silo | TEST_SILO | 0x14 |
| bg_stat | WAR | 0x16 |
| bg_tra | ESCAPE (pads) | 0x19 |

---

## 3. Understanding the Two Asset Types

When decomposing a monolithic mod, assets fall into two categories:

### 3.1 File-Replacement Maps

These provide replacement bgdata files (and possibly setup files, textures)
for existing base game stages. They have **no explicit stage declaration**
in `modconfig.txt`. Their stagenum is determined by which bgdata files they
replace.

Example: GoldenEye X provides `bg_arec.seg` (replaces 0x17 MP_RAVINE data),
`bg_ref.seg` (replaces 0x1f MP_COMPLEX data), etc. When the game loads
stage 0x03, it finds the GEX version of bg_arec files via
`modmgrResolvePath()`.

In the component `.ini`, these maps declare:

```ini
[map]
name = Temple
stagenum = 0x3d
resolution = file_replace
replaces_bgdata = bg_arec
```

The `resolution` field tells the scanner/loader how this map works:
- `file_replace` — provides replacement files for a base game stage
- `stage_patch` — has explicit modconfig.txt-style stage table overrides
- `dedicated` — uses one of the EXTRA stage slots (0x3d+)

Most maps will have `resolution = dedicated` pointing to their EXTRA slot,
with the file replacement happening implicitly because the bgdata filenames
match.

### 3.2 Stage-Patching Maps

These have explicit `stage 0xNN { ... }` blocks in `modconfig.txt` that
change which files a stage loads. They modify the `g_Stages[]` table at
load time.

Only 6 such declarations exist across all bundled mods:

| Mod | Stage | What It Does |
|-----|-------|-------------|
| allinone | 0x18 (VILLA) | Override allocation for Suburb |
| gex | 0x10 (STAGE_24) | Redirect bg → bg_tra (Bunker) |
| gex | 0x49 (EXTRA13) | Redirect bg → bg_mp5 (Facility BZ) |
| gex | 0x07 (RETAKING) | Redirect bg → bg_run (Train) |
| gex | 0x14 (STAGE_28) | Redirect bg+pads → bg_silo (Archives 1F) |
| kakariko | 0x24 (DEEPSEA) | Redirect bg+tiles+pads+setup+mpsetup → bg_mp20, weather config |

In the component `.ini`, these declare `resolution = stage_patch` and carry
the override fields:

```ini
[map]
name = Kakariko Village
stagenum = 0x24
resolution = stage_patch
bgfile = bgdata/bg_mp20.seg
tilesfile = bgdata/bg_mp20_tilesZ
padsfile = bgdata/bg_mp20_padsZ
setupfile = Usetupmp20Z
mpsetupfile = Ump_setupmp20Z
allocation = -ml0 -me0 -mgfx200 -mvtx200 -ma400
weather_exclude_rooms = 0x05 0x06 0x07 ...
```

### 3.3 Shared Resources (Characters, Models, Textures)

Non-map assets that exist across multiple mods. In the component layout,
each becomes a shared component with its own folder. Maps and other
components reference them via `depends_on`.

---

## 4. Field Mapping Reference

### 4.1 modconfig.txt → map.ini

| modconfig.txt field | map.ini field | Notes |
|---------------------|---------------|-------|
| `stage 0xNN` | `stagenum = 0xNN` | Hex stage number |
| `bgfile "name"` | `bgfile = bgdata/name` | Full relative path |
| `tilesfile "name"` | `tilesfile = bgdata/name` | Full relative path |
| `padsfile "name"` | `padsfile = bgdata/name` | Full relative path |
| `setupfile "name"` | `setupfile = name` | File name or number |
| `mpsetupfile "name"` | `mpsetupfile = name` | File name or number |
| `allocation "flags"` | `allocation = flags` | Memory allocation string |
| `music { ... }` | `music_primary = N` | Decomposed into individual fields |
| | `music_ambient = N` | |
| | `music_xtrack = N` | |
| `weather { ... }` | `weather_exclude_rooms = ...` | Room list as hex values |
| | `weather_flags = ...` | Flag names |
| | `weather_wind = angle,speed` | Decomposed wind values |
| (comment: author) | `author = Name` | From # comments or mod.json |
| (dirname) | `category = modname` | Derived from mod_ prefix |
| (not present) | `name = Display Name` | Human-readable, from comments/docs |
| (not present) | `description = ...` | From comments or external docs |
| (not present) | `bundled = true` | For shipped mods only |
| (not present) | `enabled = true` | Default state |
| (not present) | `depends_on = ...` | Texture pack references |
| (not present) | `resolution = ...` | file_replace, stage_patch, or dedicated |
| (not present) | `replaces_bgdata = bg_*` | For file_replace maps only |

### 4.2 Character Files → character.ini

Character models are identified by their file naming convention:

| File Pattern | INI Field | Example |
|-------------|-----------|---------|
| `C{name}Z` | `bodyfile = files/C{name}Z` | `Ccarroll2Z` |
| `Chead{name}Z` | `headfile = files/Chead{name}Z` | `CheadgreyZ` |
| `Ghand_{name}Z` | `handfile = files/Ghand_{name}Z` | `Ghand_carollZ` |

### 4.3 Weapon/Prop Models

| File Pattern | Type | Example |
|-------------|------|---------|
| `G{name}Z` | Weapon model | `Gm16Z` (AR53) |
| `P{name}Z` | Prop model | `PbarrelZ` |

### 4.4 Setup Files

| File Pattern | Purpose | Example |
|-------------|---------|---------|
| `Ump_setup{name}Z` | MP setup for a stage | `Ump_setupmp20Z` |
| `Usetup{name}Z` | Solo setup for a stage | `Usetupmp20Z` |
| `L{name}E/J/P` | Language text (English/Japanese/PAL) | `LmpmenuE` |

### 4.5 Texture Files

Textures use hex IDs: `{NNNN}.bin` where NNNN is the texture table index.
In the component layout, textures are grouped into texture pack components.

---

## 5. Conversion Process — Step by Step

### Step 1: Inventory the Mod

List all files in the mod directory and categorize them:

```bash
# List bgdata files (maps)
ls mod_{name}/files/bgdata/

# List model files (characters, weapons, props)
ls mod_{name}/files/[CGP]*Z

# List setup files
ls mod_{name}/files/[UL]*

# List textures
ls mod_{name}/textures/

# Read modconfig.txt for explicit stage declarations
cat mod_{name}/modconfig.txt
```

### Step 2: Identify Components

From the inventory, determine what independent components exist:

1. **Maps**: Each set of bgdata files (bg_*.seg + bg_*_tilesZ + bg_*_padsZ)
   is one map component. Check the BG File to Stage Lookup (§2.2) to find
   the base stagenum.

2. **Characters**: Each C*Z / Chead*Z / Ghand*Z triplet (or pair) is one
   character component.

3. **Texture Packs**: Group textures that are shared across multiple maps.
   If all textures in the mod are used by all maps, it's one texture pack.

4. **Setup File Overrides**: Ump_setup*Z files that override multiplayer
   setups for existing stages. These may be part of a map component (if
   they correspond to a specific bgdata set) or standalone.

5. **Language/Menu Files**: L* files that modify game text. These are
   typically mod-wide and belong to a shared component.

6. **Shared Models**: The 8 coexistence models (§1.3) become one shared
   component referenced by all maps in the mod.

### Step 3: Determine Map Resolution Type

For each map component:

- Does `modconfig.txt` have a `stage 0xNN` block for this map?
  - **Yes, with file redirects** → `resolution = stage_patch`
  - **Yes, allocation only** → `resolution = dedicated` (allocation goes in .ini)
  - **No** → Check if the bgdata filename matches a base stage or an EXTRA slot
    - Matches an EXTRA slot (0x3d+) → `resolution = dedicated`
    - Matches only a base stage → `resolution = file_replace`

### Step 4: Create Component Folders

For each component, create the directory structure:

```
mods/{category}/{asset_id}/
├── {type}.ini
└── files/
    └── (moved data files)
```

Where:
- `{category}` = maps, characters, textures, etc.
- `{asset_id}` = lowercase, underscore-separated identifier
  (e.g., `gex_temple`, `gf64_bond`, `pdplus_textures`)
- `{type}` = map, character, skin, textures, etc.

### Step 5: Write .ini Manifests

Use the field mapping (§4) to populate each .ini:

```ini
[map]
name = Facility BZ
category = goldeneye_x
stagenum = 0x49
resolution = stage_patch
bgfile = bgdata/bg_mp5.seg
tilesfile = bgdata/bg_mp5_tilesZ
mode = mp
description = GoldenEye Facility basement level
author = Wreck
version = 1.0.0
bundled = true
enabled = true
depends_on = gex_textures, pdplus_shared_models
```

### Step 6: Handle Shared Resources

Move shared files to their own component folders:

- The 8 coexistence models → `characters/pdplus_shared_models/`
- Mod-wide texture pack → `textures/{mod}_textures/`
- Language files → `textures/{mod}_lang/` or a dedicated `lang/` category

Update `depends_on` in all map .ini files to reference these shared components.

### Step 7: Validate

For each component:
1. Verify all referenced files exist in `files/`
2. Verify stagenum is correct (cross-reference §2)
3. Verify `depends_on` targets exist
4. Verify no orphaned files remain in the old mod directory

---

## 6. Per-Mod Conversion Notes

### 6.1 mod_allinone (Perfect Dark Plus + All Solos in Multi)

**Components**: ~15 maps (solo stages in multi), shared models, textures,
language files.

**Key facts**:
- Contains setup file overrides (Ump_setup*Z) for nearly every solo stage
  to make them playable in multiplayer
- Most maps use file replacement — no stage declarations except 0x18 (Suburb
  allocation override)
- bgdata replacements cover: ame, arch, azt, cave, dam, depo, dest, ear,
  eld, lee, lue, mp2, mp6, pam, pete, rit, sho
- Stage 0x18 (VILLA/Suburb) has an explicit allocation override in modconfig
- Language files (LmpmenuE/J/P, LoptionsE/J/P) are mod-wide shared resources
- Prop models (Paivilladoor1Z, Paivilladoor4Z, PretinalockZ) are shared

**Conversion approach**: Each bgdata set becomes a map component. The setup
files go with their corresponding map (e.g., `Ump_setupameZ` goes with the
map that replaces `bg_ame`). Language files become a shared `lang` component.

### 6.2 mod_gex (GoldenEye X)

**Components**: ~25 maps, shared models, large texture pack (711 textures),
many prop models.

**Key facts**:
- Largest mod by file count (158 files + 711 textures)
- 4 explicit stage declarations in modconfig.txt:
  - 0x10: bg → bg_tra (Bunker)
  - 0x49: bg → bg_mp5 (Facility BZ)
  - 0x07: bg → bg_run (Train)
  - 0x14: bg+pads → bg_silo (Archives 1F)
- Uses EXTRA stage slots 0x3d–0x4d extensively
- Many prop models unique to GEX (door variants, furniture, etc.)
- Some bgdata files overlap with other mods (bg_arec, bg_ref, bg_crad)

**Conversion approach**: Each bgdata set is a map. The 4 stage-patching maps
need `resolution = stage_patch`. Props and character models specific to GEX
(Cdark_af1Z, CstewardZ) become their own character components. The large
texture pack stays as one `gex_textures` component.

### 6.3 mod_kakariko (Kakariko Village + Golden Nintendo Maps)

**Components**: ~4 maps, shared models, texture pack (234 textures), prop models.

**Key facts**:
- 1 explicit stage declaration: 0x24 (DEEPSEA → bg_mp20 + full override
  including weather with 40+ excluded rooms)
- bgdata for: arec, mp13, mp20, ref (4 stage sets)
- Uses EXTRA stage slots 0x4e–0x4f
- Setup files: Ump_setuparecZ, Ump_setupmp13Z, Ump_setupmp20Z, Ump_setuprefZ
- Props: Pborg_crateZ, Pci_cabinet/desk/chair, Pttb_boxZ
- The weather configuration for 0x24 is complex (exclude_rooms with clear + 40 room IDs)

**Conversion approach**: 4 maps. The 0x24 map needs `resolution = stage_patch`
with the full weather config preserved. Others use dedicated EXTRA slots.

### 6.4 mod_dark_noon

**Components**: 1 map, shared models, texture pack (39 textures), props.

**Key facts**:
- Zero stage declarations in modconfig.txt
- Only 1 bgdata set: bg_mp7 (replaces stage 0x3f TEST_MP7)
- Uses EXTRA slot or direct file replacement of 0x2f
- One setup file: Ump_setupmp7Z
- Props: Pa51_crate1Z, Pa51_exp1Z

**Conversion approach**: Simplest mod. One map component targeting 0x2f,
one texture pack, shared models.

### 6.5 mod_goldfinger_64

**Components**: ~4 maps, shared models, texture pack (104 textures), props.

**Key facts**:
- Zero stage declarations in modconfig.txt
- bgdata for: arec, crad, mp11, ref (4 stage sets)
- Uses EXTRA stage slots 0x50–0x53
- Setup files: Ump_setuparecZ, Ump_setupcradZ, Ump_setupmp11Z, Ump_setuprefZ
- Includes comment names: Mall (mp11), Steel Mill (crad), Tunnels (ref),
  Junkyard (arec)
- Props: PbarrelZ, Pmulti_ammo_crateZ, Paivilladoor1Z, Paivilladoor4Z

**Conversion approach**: 4 maps using dedicated EXTRA slots. Each bgdata set
becomes a map component named from the comments (gf64_mall, gf64_steel_mill,
gf64_tunnels, gf64_junkyard).

---

## 7. Gotchas and Edge Cases

### 7.1 Overlapping bgdata Files

Multiple mods may provide the same bgdata file (e.g., both GEX and GF64
provide `bg_arec.seg`). In the legacy system, mod load order determines
which version wins. In the component system, each mod's version lives in
its own component folder, and the game loads the one belonging to whichever
stage is currently active.

When decomposing: don't assume two mods sharing a filename provide identical
content. They may contain completely different level geometry that happens
to occupy the same stage slot.

### 7.2 Setup File Ambiguity

A setup file like `Ump_setuparecZ` may exist in multiple mods. In the
legacy system, the first enabled mod's version wins. In the component
system, the setup file belongs to the map component that uses it. If two
map components need the same setup file name but different content, they
each carry their own copy.

### 7.3 Texture Conflicts

When two mods replace the same texture ID, the legacy system picks the
first enabled mod. The component system resolves this per-component: the
texture pack that's a dependency of the currently loaded map takes
priority.

### 7.4 The allocation Field

Memory allocation strings like `-ml0 -me0 -mgfx200 -mvtx200 -ma400` are
advisory on PC (the N64 memory budget doesn't apply). They're preserved in
the .ini as `hint_memory` for documentation purposes and potential future
use, but the PC port ignores them.

### 7.5 File Number vs. Filename

`modconfig.txt` accepts both file numbers (ROM indices) and filenames for
`bgfile`, `tilesfile`, etc. The component .ini always uses filenames
(human-readable). The scanner resolves filenames to file numbers at
registration time using `romdataFileGetNumForName()`.

### 7.6 Weather Configuration Complexity

The Kakariko mod's weather config (40+ excluded rooms) is the most complex
case. In the .ini, this becomes:

```ini
weather_mode = exclude
weather_rooms = 0x05 0x06 0x07 0x08 0x0A 0x0B 0x0C 0x0D 0x37 0x5B ...
```

The `clear` keyword in the legacy format (which resets the room list before
adding new entries) is implicit in the component format — each component
specifies its complete room list.

---

## 8. Verification Checklist

After converting a mod, verify:

- [ ] Every bgdata file set has a corresponding map component
- [ ] Every map .ini has a correct stagenum (cross-ref §2.2)
- [ ] Stage-patching maps have all override fields from modconfig.txt
- [ ] Shared models are in a single shared component, not duplicated
- [ ] Texture packs contain all textures from the original mod
- [ ] `depends_on` references are valid (target components exist)
- [ ] `bundled = true` is set on all converted components
- [ ] No files remain in the old monolithic directory
- [ ] Setup files are paired with their correct map components
- [ ] Language/menu files are in a shared component

---

## 9. Future: Converting Third-Party Legacy Mods

Third-party mods that follow the same monolithic format can be converted
using the same process. The key steps are:

1. Inventory files and read modconfig.txt
2. Map bgdata files to base game stages using §2.2
3. Identify whether maps use file replacement or stage patching
4. Create component folders and .ini manifests
5. Set `bundled = false` (these are user-installed, not shipped)
6. Test that all stages load correctly through the component scanner

The INI Manager tool (future) will provide an in-game GUI for creating
and editing .ini files, making this process accessible to non-technical
modders.

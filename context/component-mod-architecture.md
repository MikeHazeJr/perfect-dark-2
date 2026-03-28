# Component Mod Architecture — D3 Revised Design

> **Created**: Session 27, 2026-03-23
> **Status**: Design phase — discussion complete, awaiting implementation
> **Supersedes**: `docs/MOD_LOADER_PLAN.md` (D3 original — monolithic mod approach)
> **Back to**: [index](README.md)

---

## 1. Vision

Replace the monolithic mod system (5 static mod directories, `modconfig.txt`, `g_ModNum`) with a **component-based architecture** where every asset — map, character, skin, bot variant, weapon, prop, texture pack — is an independent, self-describing unit with its own folder and `.ini` manifest.

Mods are not containers. Mods are **category labels** that group related components. Toggling a category in the Mod Manager disables all components tagged with that category. Components can be created in-game (bot customizer, future level editor) or with external tools, and the result is the same `.ini` format either way.

### Core Principles

1. **Name-based resolution, never numeric** — Every asset has a string ID. Nothing is referenced by ROM address, table index, or array offset. The Asset Catalog resolves names to runtime data. This is a **project constraint** (see constraints.md).
2. **Self-describing components** — Each asset folder contains everything the loader needs: an `.ini` with metadata, plus the asset's data files. No external registry required.
3. **Soft dependencies** — Components can reference other components (a skin references a character, a map depends on a texture pack). Missing references degrade gracefully — fallback to base game assets, or the component simply doesn't appear in menus.
4. **Category-first scanning** — The filesystem is organized by category (`maps/`, `characters/`, `skins/`, etc.). The scanner reads categories first, then enumerates components within needed categories. Unknown categories are tolerated and logged.
5. **One format, multiple interfaces** — The `.ini` is the source of truth. In-game tools (bot customizer, level editor, INI manager) and external tools all read/write the same format.
6. **Server-authoritative distribution** — In multiplayer, the server's component list is canonical. Missing components are offered for download (permanent or session-only). Players can decline and spectate from lobby.

---

## 2. Three-Layer Architecture

```
┌─────────────────────────────────────────────────────┐
│  Layer 3: Composition & Tools                        │
│  - Category grouping (mod manager toggle by category)│
│  - depends_on for shared resources (texture packs)   │
│  - Mod pack export/import (.pdpack)                  │
│  - INI Manager tool (in-game GUI editor)             │
│  - Bot Customizer (saves to bot_variants/)           │
│  - Future: Level Editor (saves to maps/)             │
├─────────────────────────────────────────────────────┤
│  Layer 2: Asset Catalog (Translation Layer)          │
│  - String-keyed hash table: name → runtime data      │
│  - Resolves "gf64_bond" → body index, file path,     │
│    scale, metadata                                    │
│  - Replaces ALL numeric lookups (bodies, heads,       │
│    stages, weapons, textures, sounds)                 │
│  - Network sync uses name hashes, not indices         │
│  - Two-phase: base assets registered first,           │
│    then variant assets (skins) resolve lazily         │
├─────────────────────────────────────────────────────┤
│  Layer 1: Component Filesystem                       │
│  - mods/{category}/{asset_id}/asset.ini + data files │
│  - Scanner builds catalog from filesystem             │
│  - Each component is independently loadable           │
│  - Categories are filesystem directories              │
└─────────────────────────────────────────────────────┘
```

---

## 3. Filesystem Structure

```
mods/
├── maps/
│   ├── gf64_temple/
│   │   ├── map.ini
│   │   ├── [stage data files]
│   │   └── [textures, music, etc.]
│   ├── gf64_dam/
│   │   ├── map.ini
│   │   └── [...]
│   ├── kakariko_village/
│   │   ├── map.ini
│   │   └── [...]
│   └── darknoon_saloon/
│       ├── map.ini
│       └── [...]
├── characters/
│   ├── gf64_bond/
│   │   ├── character.ini
│   │   ├── Cbond_bodyZ
│   │   └── Hbond_headZ
│   └── gf64_natalya/
│       ├── character.ini
│       └── [...]
├── skins/
│   ├── gf64_bond_gold_tuxedo/
│   │   ├── skin.ini
│   │   └── [texture/model override files]
│   └── [...]
├── bot_variants/
│   ├── aggressive_sniper/
│   │   └── bot.ini
│   └── [...]
├── weapons/
│   └── [...]
├── textures/
│   ├── gf64_textures/
│   │   ├── textures.ini
│   │   └── [texture files]
│   └── [...]
├── sfx/
│   └── [...]
├── music/
│   └── [...]
├── props/
│   └── [...]
├── vehicles/
│   └── [...]
├── missions/
│   └── [...]
├── ui/
│   └── [...]
├── tools/
│   └── [...]
└── .temp/
    └── [session-only downloads, same structure as above]
```

---

## 4. INI Format — Per Category Type

### 4.1 Maps

```ini
[map]
name = Temple
category = goldfinger64
stagenum = 0x3d
mode = mp                       ; mp, solo, coop, mp+coop
music = files/music_temple.bin
weather = none                  ; none, snow, rain
model_scale = 1.0
depends_on = gf64_textures, gf64_props
description = Classic Goldfinger 64 Temple map
author = Community
version = 1.0.0
```

### 4.2 Characters

```ini
[character]
name = James Bond
category = goldfinger64
bodyfile = files/Cbond_bodyZ
headfile = files/Hbond_headZ
model_scale = 1.0
description = Classic Bond character from GoldenEye
author = Community
version = 1.0.0
```

### 4.3 Skins (Soft Reference)

```ini
[skin]
name = Gold Tuxedo
category = goldfinger64
target = gf64_bond              ; soft reference to a character asset ID
bodyfile = files/Cbond_gold_bodyZ
; OR for texture-only skins:
; texture_override = files/bond_gold_tex.bin
model_scale = 1.0
description = Golden tuxedo variant for Bond
```

- `target` is resolved lazily against the current catalog
- If the referenced character doesn't exist (mod not installed), the skin silently doesn't appear
- Can target base game characters: `target = base:joanna_dark`

### 4.4 Bot Variants

```ini
[bot_variant]
name = Aggressive Sniper
category = custom
base_type = NormalSim
accuracy = 0.85
reaction_time = 0.3
aggression = 0.9
weapon_preference = SniperRifle, FarSightXR20
patrol_tendency = 0.2
description = High-accuracy bot that prefers ranged weapons
author = Mike
version = 1.0.0
```

- Created by the in-game Bot Customizer or by hand-editing the `.ini`
- `base_type` determines the AI behavior template; trait values override defaults
- Saved to `mods/bot_variants/{name}/bot.ini` automatically by the customizer

### 4.5 Texture Packs (Shared Dependency)

```ini
[textures]
name = Goldfinger 64 Textures
category = goldfinger64
description = Shared texture pack for all GF64 maps
file_count = 158
author = Community
version = 1.0.0
```

- Referenced by maps via `depends_on = gf64_textures`
- Multiple maps share one texture pack (no duplication)
- If a map's dependency is missing, it falls back to base game textures

### 4.6 Common Fields (All Types)

These fields are valid in any `.ini` section type:

| Field | Type | Default | Purpose |
|-------|------|---------|---------|
| `name` | string | folder name | Display name in menus |
| `category` | string | "uncategorized" | Grouping label for mod manager |
| `description` | string | "" | Tooltip / detail text |
| `author` | string | "Unknown" | Creator attribution |
| `version` | string | "1.0.0" | Semver for update detection |
| `model_scale` | float | 1.0 | Asset-level scale override (solves B-13 class) |
| `depends_on` | string list | "" | Comma-separated asset IDs (soft dependencies) |
| `enabled` | bool | true | Per-component enable/disable |

**Unknown fields are preserved.** The loader ignores fields it doesn't recognize but the INI manager tool displays them, and export/pack preserves them. Future versions that add new fields automatically work with existing `.ini` files that already include them.

---

## 5. Asset Catalog — The Translation Layer

### 5.1 Purpose

The Asset Catalog is a runtime hash table that maps string IDs to everything the game needs to load and reference an asset. It replaces **all** numeric lookups — bodies, heads, stages, weapons, textures, sounds, props, everything.

**Old code (numeric, fragile):**
```c
chrSetBody(chr, 0x3A);           // magic number, breaks when mods shift indices
stageLoad(g_Stages[85]);         // array index, OOB risk with mods
```

**New code (name-based, stable):**
```c
chrSetBody(chr, catalogResolve("gf64_bond"));     // returns current runtime index
stageLoad(catalogResolveStage("gf64_temple"));    // returns stage definition
```

### 5.2 Data Structure (Conceptual)

```c
typedef struct catalog_entry {
    char id[64];                    // "gf64_bond", "base:joanna_dark"
    u32 id_hash;                    // CRC32 of id (for fast lookup + network)
    char category[64];              // "goldfinger64", "base"
    asset_type_e type;              // ASSET_MAP, ASSET_CHARACTER, ASSET_SKIN, etc.
    char dirpath[FS_MAXPATH];       // absolute path to component folder
    f32 model_scale;                // from .ini
    bool enabled;                   // user toggle (per-component)
    bool temporary;                 // downloaded for this session only
    s32 runtime_index;              // current index in the relevant runtime array
    // Type-specific data stored in a union or extension pointer
} catalog_entry_t;
```

### 5.3 Registration Flow

1. **Scanner** walks `mods/{category}/` directories
2. For each component folder, parse the `.ini`
3. Create a `catalog_entry_t` and insert into the hash table
4. **Base game assets** are registered first at startup with `"base:"` prefix IDs
5. **Mod assets** are registered after, during mod scan
6. **Variant assets** (skins) are registered but their `target` soft references are resolved lazily — only when a menu or match setup actually queries "what skins exist for gf64_bond?"

### 5.4 Network Sync

- Server sends asset requirements as CRC32 hashes of string IDs
- Client resolves against local catalog
- Missing entries → client knows exactly which components to request
- No index-based confusion — string IDs are stable across different mod configurations

### 5.5 Base Game Cataloging

All base game assets get catalog entries with `"base:"` prefix:

```
base:joanna_dark       → body/head data for Joanna
base:laptop_gun        → weapon definition for Laptop Gun
base:carrington_inst   → stage definition for CI
base:dark_sim          → bot type definition for DarkSim
```

This means the **entire game** speaks the same language. `"base:joanna_dark"` and `"gf64_bond"` are resolved through the same function. No special cases for base vs. mod content.

**Scope note**: This is a comprehensive rewrite of asset resolution. The base game is not too large (63 bodies, 76 heads, 87 stages, ~30 weapons), making it a bounded task despite touching many files.

---

## 6. Category System

### 6.1 Baked-In Categories

These are compiled into the game client. Each has a known schema (expected `.ini` fields), a loader function, and a registration pipeline:

| Category | Directory | INI Section | Loader |
|----------|-----------|-------------|--------|
| Maps | `maps/` | `[map]` | Stage table registration |
| Characters | `characters/` | `[character]` | Body/head array registration |
| Skins | `skins/` | `[skin]` | Variant registration (soft ref) |
| Bot Variants | `bot_variants/` | `[bot_variant]` | Simulant type registration |
| Weapons | `weapons/` | `[weapon]` | Weapon definition registration |
| Textures | `textures/` | `[textures]` | Texture file registration |
| SFX | `sfx/` | `[sfx]` | Sound effect registration |
| Music | `music/` | `[music]` | Music track registration |
| Props | `props/` | `[prop]` | Prop definition registration |
| Vehicles | `vehicles/` | `[vehicle]` | Vehicle definition registration |
| Missions | `missions/` | `[mission]` | Mission definition registration |
| UI | `ui/` | `[ui]` | UI element registration |
| Tools | `tools/` | `[tool]` | Tool definition registration |

### 6.2 Extensibility

Unknown category directories are tolerated. The scanner logs them at INFO level and skips them. Future versions can add new category handlers without breaking existing installations. Modders can include custom category folders; the game ignores them, but the INI manager tool can still browse and edit their `.ini` files.

### 6.3 Category Grouping in Mod Manager

The Mod Manager UI presents two views:

**By Category**: Tree view organized by asset type. `Maps > [gf64_temple, gf64_dam, kakariko_village, ...]`. Useful for fine-grained control.

**By Mod (Category Label)**: Tree view organized by the `category` field. `Goldfinger 64 > [gf64_temple, gf64_dam, gf64_bond, gf64_natalya, gf64_textures, ...]`. Useful for toggling everything from a mod at once. Toggle the parent → toggles all children.

---

## 7. Mod Manager Features

### 7.1 Core Functions

- **Browse** all installed components (by category or by mod group)
- **Enable/disable** individual components or entire mod groups
- **Apply Changes** → hot-toggle via soft reload (rebuild catalog, return to title)
- **Validate** → scan for broken soft references, missing dependencies, duplicate IDs
- **Component details** → tooltip/panel with description, author, version, dependency list

### 7.2 INI Manager Tool

In-game ImGui screen for creating and editing component `.ini` files:

- **Browse/Edit mode**: Select any installed component, see its `.ini` as a form with labeled fields, dropdowns for enums, file pickers for paths, soft-reference browser for `target` fields
- **Create mode**: Pick a category, get a blank form with all relevant fields (from the baked-in schema), fill in values, generates folder structure and `.ini`
- **Validate mode**: Scan all components, report issues (missing files, broken references, duplicates)
- **Schema-driven**: The tool's form fields are generated from the category schema definitions in code. When a new field is added to a category, the tool automatically shows it.

### 7.3 Bot Customizer

Located in Combat Simulator match setup. Creates bot variants as mod components:

- **Normal settings**: Simulant type, difficulty, team (existing PD options)
- **Advanced options**: Full trait editor — sliders for accuracy, reaction time, aggression, weapon preferences, patrol behavior, etc.
- **Save**: Writes `mods/bot_variants/{name}/bot.ini` → immediately available in bot selection
- **Same format** as hand-edited bot variant `.ini` files
- **Editable** via the INI Manager tool after creation

### 7.4 Mod Pack Export/Import

- **Export**: Select components (by individual pick or category group) → bundle into a `.pdpack` file (zip archive containing folder structure + manifest)
- **Import**: Extract `.pdpack` → components go to their correct category folders → catalog picks them up on next scan
- **Manifest** inside the `.pdpack` lists all included components with their IDs, categories, and versions

---

## 8. Network Distribution

### 8.1 Connection Flow (Revised from D3.6)

```
Client connects → sends catalog hash (combined hash of all enabled components)
  │
  Server compares:
  ├─ Match → proceed to match
  └─ Mismatch → Server sends required component list (string IDs + hashes + sizes)
       │
       Client compares against local catalog:
       ├─ All present → proceed (hash was stale)
       └─ Missing components → UI prompt:
            │
            ├─ [Download]              → permanent install to mods/{category}/
            ├─ [Download This Session] → install to mods/.temp/{category}/
            └─ [Cancel]               → stay in lobby, spectate
```

### 8.2 Server-Side Pack Compilation

The server doesn't send whole mod archives. It compiles a **delta pack** containing only the components the client is missing:

1. Compare client's catalog hash against server's required components
2. Identify missing component IDs
3. Bundle those components (folder contents) into a compressed stream
4. Stream via dedicated ENet channel (chunked, LZ4-compressed)

### 8.3 Client-Side Extraction

Received components are extracted directly into the correct category folder:
- Permanent: `mods/{category}/{component_id}/`
- Session-only: `mods/.temp/{category}/{component_id}/`

The catalog is rebuilt after extraction. No restart needed (hot-toggle path).

### 8.4 Late Download / Post-Decline

Players who declined can change their mind at any time:
- Persistent UI bar in lobby: "This match requires [X]. [Download Now] [Download This Session Only]"
- Players joining mid-match see the same prompt
- Download happens in background; completion enables hot-join or ready-for-next-round

### 8.5 Combat Log for Lobby Spectators

Players waiting in lobby receive a live kill feed from the active match:

**Event payload** (server → spectating clients):
- `attacker_name` (string, pre-resolved by server)
- `victim_name` (string, pre-resolved — or multiple for multi-kills)
- `weapon_display_name` (string, pre-resolved — critical because spectator may not have the weapon mod)
- `flags` (headshot, explosive, proximity, multi-kill, etc.)

**Display examples:**
- "MeatSim1 killed MeatSim3 with a headshot using the Dragon"
- "MeatSim4 detonated MeatSim1 and MeatSim2 with a proximity mine"
- "DarkSim picked up the FarSight XR-20"

Server sends display names, not asset IDs, because the spectating client may have declined the mods and lacks catalog entries for mod weapons/items.

### 8.6 Transfer Limits (carried from D3.6)

- Max component size: 50 MB (configurable)
- Max total transfer per session: 200 MB
- Dedicated ENet channel for transfers (separate from gameplay)
- Progress bar in lobby UI

---

## 9. Temporary Mod Crash Recovery

### 9.1 Recovery Flow

```
Launch after crash with temp mods in .temp/:
  │
  First crash → "Temporary mods from previous session. [Keep] [Keep but Disable] [Discard]"
  │
  ├─ Keep → load normally, increment crash counter per temp mod
  ├─ Keep but Disable → files stay on disk, enabled=false in catalog, logged
  └─ Discard → delete .temp/ contents

Second crash (same temp mods) → same prompt, "Keep but Disable" highlighted/recommended
  │
  Most-recently-loaded temp mod flagged as suspect in prompt

Third crash → auto-disable all temp mods, launch clean, notification:
  "Temporary mods were disabled after repeated crashes. Check Mod Manager for details."
```

### 9.2 Crash Counter Persistence

A small file `mods/.temp/.crash_state` tracks:
```ini
[crash_recovery]
crash_count = 2
last_crash_timestamp = 2026-03-23T14:30:00
suspect_component = gf64_temple
```

Reset when the game runs successfully (clean exit or plays for >60 seconds without crash).

### 9.3 "Keep but Disable"

- Files remain in `mods/.temp/` (no re-download needed)
- Flagged `enabled = false` in catalog
- Mod manager log: "Disabled gf64_temple (temp) after crash — suspected incompatibility"
- User can re-enable manually from Mod Manager

---

## 10. Asset Loading Rewrite Scope

### 10.1 What Changes (Layers 1 & 2)

**Layer 1 — File Resolution (fs.c refactor)**:
- Replace `g_ModNum` / per-mod directory globals with catalog-driven resolution
- `fsFullPath()` iterates enabled components in the relevant category
- Load priority: component-specific files → shared dependencies → base game

**Layer 2 — Table Registration (catalog replaces modconfig.txt)**:
- `modconfig.txt` stage patching → map `.ini` driven registration
- Static `g_MpBodies[]` / `g_MpHeads[]` / `g_MpArenas[]` → catalog lookups
- `g_Stages[]` patching → dynamic stage table built from catalog

### 10.2 What Stays (Layer 3 — Runtime Loading)

The actual model/texture/audio loaders mostly work as-is. Once handed a file path, they load the data. Key exception: rendering must respect `model_scale` from the catalog (generalizes the B-13 fix).

### 10.3 Migration Path

1. **Build catalog infrastructure** — hash table, string-keyed lookup, registration API
2. **Register base game assets** — 63 bodies, 76 heads, 87 stages, ~30 weapons, etc.
3. **Decompose existing mods** — convert 5 bundled mods to component filesystem
4. **Wire scanner to catalog** — scan `mods/{category}/`, parse `.ini`, register
5. **Migrate callsites** — replace numeric lookups with catalog lookups (incremental, by subsystem)
6. **Remove legacy** — strip `g_ModNum`, `modconfig.txt` parsing, static array patching

---

## 11. Existing Mod Conversion

### 11.1 Process

For each of the 5 bundled mods (allinone, gex, kakariko, darknoon, goldfinger64):

1. Parse `modconfig.txt` to identify all declared assets (stages, bodies, heads, arenas)
2. Create folder structure under `mods/{category}/{component_id}/`
3. Move relevant data files into each component folder
4. Generate `.ini` with metadata (name, category, stagenum, model_scale, etc.)
5. Verify: component loads correctly through new scanner, same behavior as before

### 11.2 Conversion Reference (for repeating the process)

Document the exact steps, field mappings (modconfig.txt field → `.ini` field), and any manual decisions made. Store as `docs/MOD_CONVERSION_GUIDE.md` — useful both internally and for modders converting legacy mods.

### 11.3 Bundled Flag

Converted mods from the original game ship with a `bundled = true` flag in their `.ini` files. This prevents accidental deletion and allows the mod manager to distinguish "came with the game" from "user-installed."

---

## 12. Implementation Priority

### Phase Order

```
1. Component Filesystem        — decompose existing mods, establish folder/INI structure
2. Asset Catalog               — name-based resolution, hash table, registration API
3. Base Game Cataloging        — register all base assets with "base:" prefix IDs
4. Scanner + Loader            — scan categories, parse INIs, build catalog
5. Callsite Migration          — replace numeric lookups with catalog lookups (incremental)
6. Mod Manager UI              — browse, toggle, validate, apply changes
7. INI Manager Tool            — in-game editor for component INIs
8. Bot Customizer              — trait editor → saves as bot_variant component
9. Network Distribution        — delta packs, session-only downloads, lobby spectator feed
10. Mod Pack Export/Import     — .pdpack creation and extraction
11. Legacy Cleanup             — remove g_ModNum, modconfig.txt, static array patching
```

### Dependencies

- Phases 1–4 are foundation (must be sequential)
- Phase 5 is incremental and can interleave with 6–8
- Phases 6, 7, 8 can partially parallelize (different UI screens)
- Phase 9 depends on catalog being functional (Phases 2–4)
- Phase 10 depends on Phase 9 (distribution format)
- Phase 11 is cleanup after everything else works

---

## 13. Relationship to Existing Work

### What This Replaces

- `docs/MOD_LOADER_PLAN.md` (D3 original) — monolithic mod approach. The filesystem scanning, network sync protocol, and ImGui integration concepts carry forward, but the data model is fundamentally different (component-based vs. monolithic).

### What This Builds On

- **B-12 Participant System** — dynamic player/bot pool, already coded (Phase 1). Bot variants from the customizer integrate with this.
- **Stage Decoupling** (S23) — safety guards for mod stages. The dynamic stage table (Phase 2) aligns with catalog-driven stage registration.
- **ImGui Integration** (S22+) — all mod manager / INI manager / bot customizer UI uses the existing pdgui/dynmenu system.
- **Networking** (Phases 1–10, C1–C12) — the component distribution system extends the existing ENet reliable channel architecture.

### What This Enables (Future)

- **In-game Level Editor** — saves maps as components, same `.ini` format, editable in external PC tools
- **Workshop/Community Hub** — component-based mods are trivially shareable (single folder = one asset)
- **Arbitrary mod extensibility** — new category types can be added without restructuring existing content

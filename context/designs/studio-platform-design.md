# Studio Platform Design — v0.5.0 "Studio"

> **ADR-class design document.** Defines the architecture for transforming PD2 from a moddable port into a creation platform with runtime asset import, data-driven weapons, composable projectiles, in-client editors, and a map editor.
>
> Revision: 1.0 | Date: 2026-04-09 | Author: Mike Hays + Claude

---

## Table of Contents

1. [Design Principles](#1-design-principles)
2. [Layer 1 — Asset Import](#2-layer-1--asset-import)
3. [Layer 2 — Weapon Definition](#3-layer-2--weapon-definition)
4. [Layer 3 — Data-Driven Projectiles](#4-layer-3--data-driven-projectiles)
5. [Layer 4 — In-Client Editor UI](#5-layer-4--in-client-editor-ui)
6. [Layer 5 — Map Editor](#6-layer-5--map-editor)
7. [Cross-Cutting — ADS, Auto-Aim, Mod Integration](#7-cross-cutting--ads-auto-aim-mod-integration)
8. [Implementation Phases](#8-implementation-phases)
9. [Architecture Decision Records](#9-architecture-decision-records)

---

## 1. Design Principles

1. **Everything is a mod.** Base game content and user-created content use the same pipeline. No special paths for built-in assets.
2. **Catalog is the backbone.** All Studio-created assets register in the Asset Catalog with `"namespace:name"` IDs. The catalog constraint (`context/constraints.md`) is non-negotiable.
3. **JSON over C.** Weapon behaviors, projectile configs, and map metadata are JSON. Modders never touch C code.
4. **Hot-reload everywhere.** Change a texture, see it in-game immediately. Change a weapon JSON, fire it next tick.
5. **Network-transparent.** Any Studio asset distributes via the existing `netdistrib.c` PDCA pipeline. No new wire protocol needed.
6. **Existing systems, not parallel systems.** Studio extends `assetcatalog.h`, `modmgr.h`, `actionmap.h`, `netdistrib.c` — it does not replace them.

---

## 2. Layer 1 — Asset Import

### 2.1 Purpose

Runtime import of external meshes (OBJ, glTF), textures (PNG, TGA), and audio (WAV, OGG) into engine-native formats, with automatic catalog registration and hot-reload.

### 2.2 Structs

```c
/* port/include/studio/asset_import.h */

typedef enum {
    IMPORT_MESH_OBJ  = 0,
    IMPORT_MESH_GLTF = 1,
} import_mesh_format_e;

typedef enum {
    IMPORT_TEX_PNG = 0,
    IMPORT_TEX_TGA = 1,
} import_tex_format_e;

typedef enum {
    IMPORT_AUDIO_WAV = 0,
    IMPORT_AUDIO_OGG = 1,
} import_audio_format_e;

/* Result of a mesh import operation */
typedef struct {
    s32   success;            /* bool */
    s32   num_vertices;
    s32   num_triangles;
    s32   num_materials;
    char  catalog_id[64];    /* assigned catalog ID "modname:meshname" */
    char  error[256];        /* error message if !success */
} import_mesh_result_t;

/* Result of a texture import operation */
typedef struct {
    s32   success;
    s32   width;
    s32   height;
    s32   channels;          /* 3=RGB, 4=RGBA */
    char  catalog_id[64];
    char  error[256];
} import_tex_result_t;

/* Result of an audio import operation */
typedef struct {
    s32   success;
    s32   sample_rate;
    s32   channels;
    s32   duration_ms;
    char  catalog_id[64];
    char  error[256];
} import_audio_result_t;

/* Import options */
typedef struct {
    char  mod_namespace[64]; /* e.g. "mymod" — prepended to catalog ID */
    f32   mesh_scale;        /* default 1.0 */
    s32   tex_generate_mips; /* bool: generate mipmaps */
    s32   audio_normalize;   /* bool: normalize audio levels */
} import_options_t;
```

### 2.3 API

```c
/* port/include/studio/asset_import.h */

/* Initialize import subsystem (call once at startup) */
void importInit(void);
void importShutdown(void);

/* Mesh import: file → engine vertex/index buffers → catalog registration */
import_mesh_result_t importMesh(const char *filepath,
                                import_mesh_format_e format,
                                const import_options_t *opts);

/* Texture import: file → GL texture → catalog registration */
import_tex_result_t importTexture(const char *filepath,
                                   import_tex_format_e format,
                                   const import_options_t *opts);

/* Audio import: file → PCM/Vorbis buffer → catalog registration */
import_audio_result_t importAudio(const char *filepath,
                                   import_audio_format_e format,
                                   const import_options_t *opts);

/* Hot-reload: re-import a previously imported asset by catalog ID.
 * Returns true if the asset was found and successfully reloaded. */
s32 importHotReload(const char *catalog_id);

/* Watch a directory for changes. Calls importHotReload automatically
 * when source files are modified. Event-driven via OS file watcher. */
s32 importWatchDirectory(const char *dirpath, const char *mod_namespace);
void importUnwatchDirectory(const char *dirpath);
```

### 2.4 Mesh Import Pipeline

```
OBJ/glTF file
  → parse (tinyobjloader / cgltf, both C, header-only)
  → triangulate + compute normals
  → convert to engine Vtx[] + Col[] arrays
  → allocate via gfxAllocateVertices() / gfxAllocateColours()
  → store as asset_entry_t with ASSET_MODEL type
  → catalog register: "namespace:model_name"
  → runtime_index assigned by catalog
```

**GBI integration:** Imported meshes are rendered via standard `gSPVertex` + `gSPTri2` display list commands. The import pipeline produces Vtx arrays in the same format the N64 GBI translator expects. No new rendering path needed.

### 2.5 Texture Import Pipeline

```
PNG/TGA file
  → decode (stb_image, already in port/)
  → convert to RGBA8888
  → optional mipmap generation (stb_image_resize2)
  → upload to GL texture via gfx_rapi->upload_texture()
  → store as asset_entry_t with ASSET_TEXTURE type
  → catalog register: "namespace:tex_name"
```

### 2.6 Audio Import Pipeline

```
WAV/OGG file
  → decode (dr_wav for WAV, stb_vorbis for OGG — both header-only C)
  → resample to engine rate (22050 Hz) if needed
  → normalize (optional)
  → store as PCM buffer in asset_entry_t.loaded_data
  → catalog register: "namespace:sfx_name" with ASSET_AUDIO type
  → sound_id mapped at registration
```

### 2.7 Hot-Reload Architecture

File watcher (event-driven, not polling) monitors the mod's source directory. On file change:
1. Hash the new file (SHA-256)
2. Compare to stored hash in `asset_entry_t`
3. If different: re-run the import pipeline for that file type
4. Bump the catalog generation counter
5. Flush relevant runtime caches (texture cache for textures, vertex buffers for meshes)
6. Log the reload event

Platform: `ReadDirectoryChangesW` on Windows (single threaded, polled once per frame in `importTick()`).

### 2.8 Catalog Integration

Every imported asset gets a catalog entry with:
- `id`: `"namespace:asset_name"` (derived from filename + mod namespace)
- `category`: mod namespace
- `dirpath`: source file directory
- `bundled`: false
- `load_state`: ASSET_STATE_LOADED (data is immediately resident)
- `loaded_data`: pointer to converted engine data
- `data_size_bytes`: size of converted data

Imported assets participate in the manifest pipeline (`netmanifest.h`) and distribution pipeline (`netdistrib.c`) identically to existing mod components.

---

## 3. Layer 2 — Weapon Definition

### 3.1 Purpose

JSON-driven weapon creation. Modders define fire rate, damage, projectile type, ADS behavior, auto-aim magnetism, and link custom model/texture/audio — all without touching C code.

### 3.2 JSON Schema

```json
{
  "id": "mymod:plasma_rifle",
  "name": "Plasma Rifle",
  "model_id": "mymod:model_plasma_rifle",
  "fire_texture_id": "mymod:tex_plasma_muzzle",
  "icon_texture_id": "mymod:tex_plasma_icon",

  "audio": {
    "fire": "mymod:sfx_plasma_fire",
    "reload": "mymod:sfx_plasma_reload",
    "empty": "mymod:sfx_plasma_empty",
    "equip": "mymod:sfx_plasma_equip"
  },

  "stats": {
    "damage": 12.0,
    "fire_rate_rps": 8.0,
    "reload_time_sec": 2.1,
    "magazine_size": 30,
    "max_ammo": 120,
    "range": 2000.0,
    "spread_hip_deg": 3.5,
    "spread_ads_deg": 0.8
  },

  "projectile": "mymod:proj_plasma_bolt",

  "ads": {
    "zoom_fov": 45.0,
    "transition_sec": 0.2,
    "move_speed_mult": 0.6,
    "steady_sway_deg": 0.5
  },

  "auto_aim": {
    "magnetism_deg": 4.0,
    "sticky_time_sec": 0.15,
    "falloff_start": 500.0,
    "falloff_end": 1500.0
  },

  "flags": {
    "dual_wieldable": true,
    "has_secondary_fire": false,
    "automatic": true
  }
}
```

### 3.3 Structs

```c
/* port/include/studio/weapon_def.h */

typedef struct {
    /* Identity */
    char id[64];                /* catalog ID "namespace:weapon_name" */
    char name[64];

    /* Asset references (catalog IDs) */
    char model_id[64];
    char fire_texture_id[64];
    char icon_texture_id[64];
    char audio_fire[64];
    char audio_reload[64];
    char audio_empty[64];
    char audio_equip[64];

    /* Combat stats */
    f32  damage;
    f32  fire_rate_rps;         /* rounds per second */
    f32  reload_time_sec;
    s32  magazine_size;
    s32  max_ammo;
    f32  range;
    f32  spread_hip_deg;
    f32  spread_ads_deg;

    /* Projectile reference */
    char projectile_id[64];     /* catalog ID of projectile_def_t */

    /* ADS */
    f32  ads_zoom_fov;
    f32  ads_transition_sec;
    f32  ads_move_speed_mult;
    f32  ads_steady_sway_deg;

    /* Auto-aim */
    f32  autoaim_magnetism_deg;
    f32  autoaim_sticky_time_sec;
    f32  autoaim_falloff_start;
    f32  autoaim_falloff_end;

    /* Flags */
    s32  dual_wieldable;        /* bool */
    s32  has_secondary_fire;    /* bool */
    s32  automatic;             /* bool */
} weapon_def_t;

#define WEAPON_DEF_MAX 128
```

### 3.4 API

```c
/* port/include/studio/weapon_def.h */

/* Parse a weapon JSON file and register in catalog */
s32 weaponDefLoad(const char *json_path, const char *mod_namespace);

/* Resolve a weapon_def_t by catalog ID (returns NULL if not found) */
const weapon_def_t *weaponDefGet(const char *catalog_id);

/* Get all loaded custom weapon defs (for editor UI iteration) */
s32 weaponDefGetAll(const weapon_def_t **out_array, s32 max);

/* Hot-reload a weapon def from its JSON source */
s32 weaponDefReload(const char *catalog_id);
```

### 3.5 Engine Integration

The weapon def system sits between JSON config and the existing weapon system (`src/game/bondgun.c`, `src/game/weapons.c`). At match start:

1. `matchStart()` resolves weapon catalog IDs to `weapon_def_t` structs
2. For each custom weapon: populate a `weaponobj` entry from `weapon_def_t` stats
3. Model/texture/audio resolved from catalog IDs at this point
4. The existing combat code (`bondgun.c`) operates on `weaponobj` fields as before

Custom weapons appear in the weapon slot menu, the lobby weapon picker, and distribute over the network via catalog ID strings (existing `CLC_LOBBY_START` weapon slot fields).

---

## 4. Layer 3 — Data-Driven Projectiles

### 4.1 Purpose

Extensible, composable projectile behaviors defined in JSON. No C code needed for new projectile types. Behaviors are combinable: tracking + accumulate = Needler-style.

### 4.2 Behavior Primitives

| Behavior | Description | Key Parameters |
|----------|-------------|----------------|
| `straight` | Linear trajectory, constant speed | `speed`, `gravity` |
| `tracking` | Homes toward nearest enemy | `turn_rate_deg_sec`, `acquire_range`, `lock_delay_sec` |
| `arc` | Parabolic arc (grenades) | `launch_angle_deg`, `gravity_mult` |
| `spread` | Fires N pellets in a cone (shotgun) | `pellet_count`, `cone_deg` |
| `beam` | Instant hitscan ray | `max_range`, `penetration_count` |
| `bounce` | Ricochets off surfaces | `max_bounces`, `energy_loss` |
| `cluster` | Splits into sub-projectiles on impact | `child_id`, `child_count`, `child_spread_deg` |
| `accumulate` | Projectiles stick to target, detonate on threshold | `stick_count`, `detonate_damage_mult` |
| `remote` | Player-detonated (remote mines) | `arm_time_sec`, `detonate_radius` |

### 4.3 JSON Schema

```json
{
  "id": "mymod:proj_plasma_bolt",
  "model_id": "mymod:model_plasma_bolt",
  "trail_effect_id": "mymod:effect_plasma_trail",
  "impact_effect_id": "mymod:effect_plasma_impact",
  "audio_travel": "mymod:sfx_plasma_travel",
  "audio_impact": "mymod:sfx_plasma_hit",

  "physics": {
    "speed": 1800.0,
    "gravity": 0.0,
    "lifetime_sec": 3.0,
    "radius": 4.0
  },

  "behaviors": [
    { "type": "straight" },
    { "type": "tracking", "turn_rate_deg_sec": 90.0, "acquire_range": 800.0, "lock_delay_sec": 0.3 }
  ],

  "impact": {
    "damage": 12.0,
    "splash_radius": 0.0,
    "splash_falloff": "linear",
    "knockback": 50.0
  }
}
```

### 4.4 Structs

```c
/* port/include/studio/projectile_def.h */

typedef enum {
    PROJBHV_STRAIGHT = 0,
    PROJBHV_TRACKING,
    PROJBHV_ARC,
    PROJBHV_SPREAD,
    PROJBHV_BEAM,
    PROJBHV_BOUNCE,
    PROJBHV_CLUSTER,
    PROJBHV_ACCUMULATE,
    PROJBHV_REMOTE,
    PROJBHV_COUNT
} proj_behavior_type_e;

typedef struct {
    proj_behavior_type_e type;
    union {
        struct { f32 speed; f32 gravity; } straight;
        struct { f32 turn_rate; f32 acquire_range; f32 lock_delay; } tracking;
        struct { f32 launch_angle; f32 gravity_mult; } arc;
        struct { s32 pellet_count; f32 cone_deg; } spread;
        struct { f32 max_range; s32 penetration; } beam;
        struct { s32 max_bounces; f32 energy_loss; } bounce;
        struct { char child_id[64]; s32 child_count; f32 child_spread; } cluster;
        struct { s32 stick_count; f32 detonate_mult; } accumulate;
        struct { f32 arm_time; f32 detonate_radius; } remote;
    } params;
} proj_behavior_t;

#define PROJ_MAX_BEHAVIORS 4

typedef struct {
    char id[64];
    char model_id[64];
    char trail_effect_id[64];
    char impact_effect_id[64];
    char audio_travel[64];
    char audio_impact[64];

    f32  speed;
    f32  gravity;
    f32  lifetime_sec;
    f32  radius;

    proj_behavior_t behaviors[PROJ_MAX_BEHAVIORS];
    s32  num_behaviors;

    f32  impact_damage;
    f32  splash_radius;
    f32  splash_falloff;     /* 0=none, 1=linear, 2=quadratic */
    f32  knockback;
} projectile_def_t;

#define PROJECTILE_DEF_MAX 256
```

### 4.5 Runtime Execution

Each tick, the projectile system iterates active custom projectiles:

```c
void projectileDefTick(struct projectile *proj, projectile_def_t *def) {
    for (s32 i = 0; i < def->num_behaviors; i++) {
        proj_behavior_t *bhv = &def->behaviors[i];
        switch (bhv->type) {
            case PROJBHV_STRAIGHT: projBhvStraight(proj, bhv); break;
            case PROJBHV_TRACKING: projBhvTracking(proj, bhv); break;
            /* ... */
        }
    }
}
```

Behaviors compose sequentially: `straight` sets the base velocity, `tracking` adjusts the heading, `accumulate` handles stick logic. Order matters — put the most fundamental behavior first.

### 4.6 Catalog Integration

Projectile defs register as `ASSET_PROP` type (subtype: projectile) in the catalog. Weapons reference them by catalog ID. The projectile def is resolved at fire time, not at match start, enabling hot-reload.

---

## 5. Layer 4 — In-Client Editor UI

### 5.1 Purpose

ImGui-based creation tools embedded in the game client. Asset browser, weapon editor with live preview, model/texture/audio viewers, mod packager.

### 5.2 Editor Windows

All editors are ImGui windows (`port/fast3d/pdgui_studio_*.cpp`), following the existing `pdgui_menu_*.cpp` pattern. They use the `InputContext` system for mouse capture.

| Window | File | Description |
|--------|------|-------------|
| **Asset Browser** | `pdgui_studio_browser.cpp` | Tree view of catalog by type/namespace. Search, filter, drag-drop to editors. |
| **Weapon Editor** | `pdgui_studio_weapon.cpp` | Edit `weapon_def_t` fields. Stat sliders, catalog ID pickers for model/audio. Live fire test button. |
| **Projectile Editor** | `pdgui_studio_projectile.cpp` | Compose behaviors with visual pipeline view. Preview trajectory arc. |
| **Model Viewer** | `pdgui_studio_modelview.cpp` | 3D viewport rendering a single model. Orbit camera, wireframe toggle, bone overlay. |
| **Texture Viewer** | `pdgui_studio_texview.cpp` | 2D viewer with zoom, pan, channel isolation (R/G/B/A), mip level selector. |
| **Audio Player** | `pdgui_studio_audio.cpp` | Waveform display, play/pause/seek, volume, looping toggle. |
| **Mod Packager** | `pdgui_studio_packager.cpp` | Select assets, generate `mod.json`, compute SHA-256, create PDCA archive for sharing. |

### 5.3 Editor Framework

```cpp
/* port/include/studio/studio_editor.h */

/* Base interface for all studio editor windows */
struct StudioEditor {
    const char *title;           /* window title */
    s32 is_open;                 /* bool: visible */
    InputContext *input_ctx;     /* owned input context */

    void (*render)(struct StudioEditor *self);   /* ImGui render */
    void (*on_open)(struct StudioEditor *self);  /* push input ctx */
    void (*on_close)(struct StudioEditor *self); /* pop input ctx */
};

/* Registry */
void studioInit(void);
void studioShutdown(void);
void studioRenderAll(void);     /* called from pdgui_backend frame */
void studioToggleEditor(const char *name);
```

### 5.4 Live Preview

The weapon editor includes a "Test Fire" button that:
1. Saves the current weapon_def_t to a temp JSON
2. Calls `weaponDefReload()` to hot-reload
3. Spawns the weapon in the player's hand
4. Player can fire it immediately

The model viewer renders using the same GBI display list pipeline as the game, ensuring WYSIWYG fidelity.

---

## 6. Layer 5 — Map Editor

### 6.1 Purpose

In-client 3D map editor. Place imported meshes, generate collision, define spawn points, create navmesh for bots, save as a mod, playtest instantly, distribute to other players.

### 6.2 Architecture

The map editor operates on a **Paradox stage** — a blank template stage with an empty room, a floor plane, and basic lighting. The editor adds geometry by placing imported meshes (from Layer 1) as static props.

### 6.3 Map Document

```c
/* port/include/studio/map_editor.h */

typedef struct {
    char mesh_id[64];           /* catalog ID of the mesh asset */
    struct coord pos;           /* world position */
    struct coord rot;           /* euler rotation (degrees) */
    f32 scale;                  /* uniform scale */
    s32 collision_enabled;      /* bool: generates collision geometry */
    s32 visible;                /* bool: renders in game */
} map_placement_t;

typedef struct {
    struct coord pos;
    f32 yaw;                    /* facing direction (degrees) */
} map_spawn_point_t;

typedef struct {
    struct coord min;           /* AABB min */
    struct coord max;           /* AABB max */
} map_bounds_t;

#define MAP_MAX_PLACEMENTS    4096
#define MAP_MAX_SPAWN_POINTS  64
#define MAP_MAX_NAVNODES      2048

typedef struct {
    char map_id[64];                         /* catalog ID */
    char name[64];                           /* display name */
    char author[64];

    map_placement_t placements[MAP_MAX_PLACEMENTS];
    s32 num_placements;

    map_spawn_point_t spawns[MAP_MAX_SPAWN_POINTS];
    s32 num_spawns;

    map_bounds_t bounds;                     /* playable area */

    /* Navmesh: baked node graph for bot AI pathfinding */
    struct {
        struct coord pos;
        s32 neighbors[8];                    /* indices into navnodes[], -1 = none */
        s32 num_neighbors;
    } navnodes[MAP_MAX_NAVNODES];
    s32 num_navnodes;

    /* Environment */
    u8 sky_r, sky_g, sky_b;
    s32 clouds_enabled;                      /* bool */
    f32 fog_near, fog_far;
} map_document_t;
```

### 6.4 API

```c
/* port/include/studio/map_editor.h */

/* Create a new empty map */
map_document_t *mapEditorNew(const char *name, const char *mod_namespace);

/* Load from JSON */
map_document_t *mapEditorLoad(const char *json_path);

/* Save to JSON + generate mod structure */
s32 mapEditorSave(map_document_t *doc, const char *output_dir);

/* Placement operations */
s32 mapEditorAddPlacement(map_document_t *doc, const char *mesh_id,
                           struct coord pos, struct coord rot, f32 scale);
void mapEditorRemovePlacement(map_document_t *doc, s32 index);
void mapEditorMovePlacement(map_document_t *doc, s32 index, struct coord new_pos);

/* Spawn points */
s32 mapEditorAddSpawn(map_document_t *doc, struct coord pos, f32 yaw);

/* Collision generation: triangulate all collision-enabled placements
 * into the engine's collision format (cdTestVolume compatible) */
s32 mapEditorBuildCollision(map_document_t *doc);

/* Navmesh generation: flood-fill walkable surfaces from spawn points,
 * create node graph for bot pathfinding */
s32 mapEditorBuildNavmesh(map_document_t *doc);

/* Instant playtest: save, register as temp mod, start match on this map */
s32 mapEditorPlaytest(map_document_t *doc);
```

### 6.5 3D Viewport

The map editor viewport is an ImGui window with an embedded OpenGL framebuffer:
- Orbit/fly camera (WASD + mouse drag)
- Grid floor with configurable spacing
- Gizmo overlays for translate/rotate/scale (3 modes, T/R/S hotkeys)
- Selection highlighting (outline shader)
- Spawn point markers (colored spheres)
- Navmesh visualization (wireframe overlay)

### 6.6 Collision Generation

For each `collision_enabled` placement:
1. Retrieve the mesh's triangle data from the catalog
2. Transform triangles by placement pos/rot/scale
3. Build an AABB tree for broad-phase
4. Convert to the engine's `cdTestVolume` collision format
5. Write to the map's collision data block

The engine collision system (`src/lib/collision/`) operates on pre-baked collision geometry. The map editor pre-bakes at save time, not at runtime.

### 6.7 Navmesh Generation

Approach: **grid-based reachability flood fill**.

1. Create a 3D grid (cell size ~64 units) covering map bounds
2. For each cell, raycast downward to find the floor
3. Mark cells as walkable if floor exists and height clearance > 180 units (player height)
4. Flood-fill from spawn points to find connected walkable cells
5. Reduce to a node graph: merge adjacent walkable cells into convex regions
6. Connect nodes with visibility checks (raycast between node centers)
7. Store as `navnodes[]` in the map document

Bot AI uses this via a simple A* pathfinding replacement for the existing pad-based system (`src/game/pad.c`). Custom maps register their navmesh as the pad graph for the stage.

### 6.8 Network Distribution

A Studio-created map is a standard mod component:
- `mod.json` manifest with map metadata
- Mesh files (OBJ/glTF)
- Texture files (PNG/TGA)
- `map.json` (the map document)
- Collision data (pre-baked binary)
- Navmesh data (pre-baked binary)

Distribution uses the existing `netdistrib.c` PDCA pipeline. The host's manifest includes the map's catalog ID; clients that don't have it receive it automatically.

---

## 7. Cross-Cutting — ADS, Auto-Aim, Mod Integration

### 7.1 ADS System (Per-Weapon)

ADS (Aim Down Sights) is configured per weapon via `weapon_def_t.ads_*` fields. Integration with the existing action map:

```
ACTION_FIRE_SECONDARY (right mouse / R_TRIG)
  → if weapon has ADS: enter ADS mode
  → camera FOV lerps to ads_zoom_fov over ads_transition_sec
  → movement speed multiplied by ads_move_speed_mult
  → spread reduced to spread_ads_deg
  → sway applied at ads_steady_sway_deg amplitude
```

The existing `g_Vars.currentplayer->insightaimmode` flag is reused. ADS parameters are read from `weapon_def_t` instead of being hardcoded per weapon enum.

### 7.2 Auto-Aim System (Per-Weapon)

Auto-aim magnetism is configured per weapon via `weapon_def_t.autoaim_*` fields:

- **Magnetism cone**: `autoaim_magnetism_deg` defines the angular cone within which the crosshair is pulled toward targets
- **Sticky time**: `autoaim_sticky_time_sec` — how long the assist stays locked after the target leaves the cone
- **Distance falloff**: full magnetism at `falloff_start`, zero at `falloff_end`, linear interpolation between

Integration point: `autoaimTick()` in `src/game/lv.c` already runs every frame. The current hardcoded auto-aim parameters are replaced with reads from the active weapon's `weapon_def_t`.

### 7.3 Mod Integration

All Studio output is a mod. The packaging pipeline:

```
Studio asset creation
  → JSON config files (weapon_def.json, projectile_def.json, map.json)
  → Asset files (meshes, textures, audio)
  → mod.json manifest (generated by packager)
  → SHA-256 hashes computed per file
  → Optional: PDCA archive for single-file distribution
  → Register in catalog via modmgrReload()
  → Network distribution via netdistrib.c
```

### 7.4 Performance Targets

| Metric | Target | Rationale |
|--------|--------|-----------|
| Mesh import time (10K tri OBJ) | < 200ms | Must feel interactive in editor |
| Texture import time (2048x2048 PNG) | < 100ms | Hot-reload responsiveness |
| Weapon def load (JSON parse) | < 5ms | Negligible on match start |
| Collision bake (1000 placements) | < 2s | Acceptable for save-time operation |
| Navmesh generation (medium map) | < 5s | Background task with progress bar |
| Editor viewport | 60 FPS | Must match gameplay framerate |
| Custom weapon fire overhead | < 0.1ms/tick | Imperceptible vs base weapons |

---

## 8. Implementation Phases

| # | Phase | Goal | Key Files | LOC Est. | Sessions | Dependencies | Acceptance Criteria |
|---|-------|------|-----------|----------|----------|--------------|---------------------|
| S1 | **Mesh Import Core** | OBJ file → engine Vtx[] → catalog entry | `port/src/studio/import_mesh.c`, `port/include/studio/asset_import.h` | ~800 | 2 | None (cgltf/tinyobj vendored) | Load cube.obj, see it render in-game via catalog ID |
| S2 | **Texture Import** | PNG/TGA → GL texture → catalog | `port/src/studio/import_tex.c` | ~400 | 1 | stb_image (already vendored) | Import a PNG, see it applied to a model in-game |
| S3 | **Audio Import** | WAV/OGG → PCM buffer → catalog | `port/src/studio/import_audio.c` | ~500 | 1 | dr_wav + stb_vorbis (vendor) | Import WAV, play it as a weapon fire sound |
| S4 | **Hot-Reload Watcher** | File watcher + auto re-import | `port/src/studio/import_watch.c` | ~300 | 1 | S1-S3 | Modify OBJ on disk, see model update in-game without restart |
| S5 | **Weapon Def JSON** | JSON → weapon_def_t → weaponobj | `port/src/studio/weapon_def.c`, `port/include/studio/weapon_def.h` | ~600 | 2 | S1-S3 (for asset refs) | Create weapon.json, equip and fire it in match |
| S6 | **Projectile Def JSON** | JSON → projectile_def_t → tick behaviors | `port/src/studio/projectile_def.c` | ~800 | 2 | S5 | Weapon with tracking+straight projectile works in match |
| S7 | **ADS + Auto-Aim Integration** | Per-weapon ADS/auto-aim from weapon_def_t | `port/src/studio/weapon_aim.c`, modify `src/game/bondgun.c` | ~400 | 1 | S5 | Custom weapon has working ADS zoom and auto-aim magnetism |
| S8 | **Asset Browser UI** | ImGui catalog tree view + search | `port/fast3d/pdgui_studio_browser.cpp` | ~500 | 1 | S1-S3 | Browse all catalog entries, filter by type/namespace |
| S9 | **Model Viewer** | 3D viewport with orbit camera | `port/fast3d/pdgui_studio_modelview.cpp` | ~600 | 2 | S1, S8 | View any catalog model, rotate/zoom |
| S10 | **Weapon Editor UI** | Edit weapon JSON visually + live test | `port/fast3d/pdgui_studio_weapon.cpp` | ~700 | 2 | S5, S8, S9 | Edit damage slider, click Test Fire, see result |
| S11 | **Texture/Audio Viewers** | 2D texture viewer + audio player | `pdgui_studio_texview.cpp`, `pdgui_studio_audio.cpp` | ~400 | 1 | S2, S3, S8 | View textures with channel isolation, play audio with seek |
| S12 | **Mod Packager** | Generate mod.json + PDCA archive | `port/fast3d/pdgui_studio_packager.cpp` | ~500 | 1 | S8 | Package a custom weapon mod, install on another client |
| S13 | **Map Editor Core** | Paradox stage + mesh placement + save/load | `port/src/studio/map_editor.c`, `port/include/studio/map_editor.h` | ~1200 | 3 | S1, S9 | Place meshes in 3D, save map, load it back |
| S14 | **Collision Generation** | Triangulate placements → engine collision | `port/src/studio/map_collision.c` | ~800 | 2 | S13 | Walk on placed meshes, collide with walls |
| S15 | **Navmesh Generation** | Flood-fill → node graph for bot AI | `port/src/studio/map_navmesh.c` | ~1000 | 3 | S13, S14 | Bots navigate a custom map using generated navmesh |
| S16 | **Map Editor UI** | ImGui 3D viewport + gizmos + spawn placement | `port/fast3d/pdgui_studio_mapeditor.cpp` | ~1500 | 4 | S13, S9 | Full visual map editing with translate/rotate gizmos |
| S17 | **Instant Playtest** | Save → temp mod → launch match | `port/src/studio/map_playtest.c` | ~300 | 1 | S13, S14, S15 | Click Playtest, play the custom map in < 5 seconds |
| | **TOTAL** | | | ~10,300 | ~30 | | |

---

## 9. Architecture Decision Records

### ADR-S1: Mesh Format — OBJ + glTF

**Context:** Need a mesh import format that modders already know.

**Options:**
| Option | Pros | Cons |
|--------|------|------|
| OBJ only | Universal, trivial parser | No animation, no PBR materials, no scene graph |
| glTF only | Modern, animations, PBR, scene hierarchy | More complex parser, overkill for simple props |
| FBX | Industry standard | Proprietary SDK, massive binary dependency |
| Custom binary | Fastest load times | Zero ecosystem adoption, bad DX |

**Decision:** Support both OBJ (simple props, fast iteration) and glTF (animated models, complex scenes). Use tinyobjloader (C, header-only, ~1200 LOC) for OBJ and cgltf (C, header-only, ~4000 LOC) for glTF. Both are well-tested, MIT-licensed, and compile under MinGW/GCC.

**Consequences:** Two parsers to maintain. OBJ is the recommended default for map editor assets (static meshes). glTF is recommended for weapons and characters (may have animations in future).

---

### ADR-S2: Projectile Architecture — Composable Behavior Stack

**Context:** Need extensible projectile types without per-type C code.

**Options:**
| Option | Pros | Cons |
|--------|------|------|
| Hardcoded enum (like base game) | Simple, zero overhead | Every new projectile requires C code |
| Inheritance hierarchy | OOP patterns | C codebase, vtable overhead, rigid |
| Entity-Component System | Maximum flexibility | Massive overengineering for projectiles |
| **Behavior stack** | Composable, JSON-driven, simple dispatch | Fixed max behaviors (4), ordering matters |

**Decision:** Behavior stack. Each projectile_def_t carries up to 4 behaviors applied sequentially per tick. This covers all known PD weapon behaviors (Slayer rockets = tracking, Devastator grenades = arc + cluster, laptop gun = remote + straight) and allows novel combinations.

**Consequences:** Behavior ordering is a potential foot-gun (tracking before straight vs after). Document the recommended ordering in the JSON schema. Limit to 4 behaviors keeps the per-tick cost bounded.

---

### ADR-S3: Navmesh Approach — Grid Flood-Fill

**Context:** Custom maps need bot pathfinding. The base game uses a manually-authored pad system (`src/game/pad.c`) with hardcoded waypoints per stage.

**Options:**
| Option | Pros | Cons |
|--------|------|------|
| Manual waypoint placement | Precise control, matches base game | Tedious, error-prone, poor modder UX |
| Recast/Detour (industry standard) | Robust, handles complex geometry | 30K+ LOC C++ dependency, hard MinGW integration |
| **Grid flood-fill** | Simple, predictable, pure C | Less optimal paths than Recast, struggles with multi-level geometry |
| Visibility graph | Optimal paths | Expensive to compute, complex to implement |

**Decision:** Grid flood-fill with convex region merging. Simple, pure C, no external dependencies. Good enough for arena-style maps (single floor, open areas with walls). Multi-level maps (ramps, stairs) handled by extending the raycast step to detect floor height changes.

**Consequences:** Bot navigation on complex multi-story maps will be less smooth than Recast. This is acceptable for v0.5.0. If demand exists, Recast integration can be a v0.7.0+ upgrade.

---

### ADR-S4: Editor UI — Embedded ImGui vs External Tool

**Context:** Where do the creation tools live?

**Options:**
| Option | Pros | Cons |
|--------|------|------|
| External desktop app | Full IDE, no game performance impact | Separate codebase, no live preview, distribution headache |
| Web-based editor | Cross-platform, no install | No GPU access, can't preview in-engine |
| **Embedded ImGui** | Live preview, single binary, hot-reload, same input system | ImGui limitations (no rich text, limited layout), editor code in game binary |

**Decision:** Embedded ImGui. The game already has a mature ImGui overlay with input context management, theming, and scaling. Studio editors are additional ImGui windows that render during the game's frame. Live preview is trivial — the editor literally runs inside the game.

**Consequences:** Game binary size increases. Editor windows are compiled into all builds (gated behind a debug/studio flag at runtime, not at compile time). ImGui layout limitations are acceptable for tooling UX.

---

### ADR-S5: ADS Implementation — Weapon-Driven vs Global

**Context:** ADS zoom, sway, and speed reduction need to vary per weapon.

**Options:**
| Option | Pros | Cons |
|--------|------|------|
| Global ADS config | Simple, one code path | All weapons feel the same |
| Per-weapon-enum config in C | Per-weapon tuning | Requires C code for each weapon |
| **Per-weapon JSON config** | Data-driven, modder-accessible | Need to plumb weapon_def_t into existing ADS code paths |

**Decision:** Per-weapon JSON config via `weapon_def_t.ads_*` fields. The existing ADS code paths in `bondgun.c` and `bondmove.c` are modified to read from the active weapon's def instead of global constants.

**Consequences:** The existing hardcoded ADS values for base game weapons must be converted to `weapon_def_t` entries (either generated at startup or stored as JSON). Base game weapons become data-driven too.

---

*End of document.*

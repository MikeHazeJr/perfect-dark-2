# PD2 Studio — In-Client Creation Tools Platform

> **Version**: v0.5.0+ | **Author**: Design Document | **Date**: 2026-04-09
> **Dependencies**: Asset Catalog (M0), Mod System (D3R), ImGui Menus (P10), Network Distribution (D3R-9)

## Vision

PD2 Studio transforms Perfect Dark 2 from a game into a creation platform. Players import meshes, textures, and audio at runtime; define custom weapons and projectile behaviors entirely through JSON data; build playable multiplayer maps with collision and bot navigation; and package everything as mods distributable over the network. Every asset created in Studio is a first-class catalog citizen — registered with a mod-scoped ID, hot-reloadable, and automatically synchronized to match participants via netdistrib. No C code required.

---

## Architecture Overview

```
+------------------------------------------------------------------+
|                        PD2 Studio Platform                        |
+------------------------------------------------------------------+
|                                                                    |
|  +-----------------+   +------------------+   +----------------+  |
|  | LAYER 1         |   | LAYER 2          |   | LAYER 3        |  |
|  | Asset Import    |   | Weapon Defs      |   | Projectile Sys |  |
|  | Pipeline        |   | (JSON Schema)    |   | (Data-Driven)  |  |
|  |                 |   |                  |   |                |  |
|  | OBJ/glTF → GBI  |   | fire_rate,dmg,   |   | straight,arc,  |  |
|  | PNG/TGA → RGBA  |   | ammo,spread,     |   | tracking,beam, |  |
|  | WAV/OGG → ADPCM |   | projectile_type, |   | accumulate,    |  |
|  |                 |   | ads,autoaim      |   | bounce,spread  |  |
|  +---------+-------+   +--------+---------+   +-------+--------+  |
|            |                     |                     |           |
|            v                     v                     v           |
|  +------------------------------------------------------------------+
|  |                    ASSET CATALOG (SSOT)                           |
|  |  assetcatalog.h — FNV-1a hash, "namespace:name" IDs              |
|  |  Registration → Resolution → Iteration → Hot-Reload              |
|  +------------------------------------------------------------------+
|            |                     |                     |           |
|            v                     v                     v           |
|  +-----------------+   +------------------+   +----------------+  |
|  | LAYER 4         |   | LAYER 5          |   | CROSS-CUTTING  |  |
|  | Editor UI       |   | Map Editor       |   |                |  |
|  | (ImGui)         |   |                  |   | ADS/Auto-Aim   |  |
|  |                 |   | Mesh placement   |   | Mod packaging  |  |
|  | Asset Browser   |   | Collision gen    |   | Net distrib    |  |
|  | Weapon Editor   |   | Navmesh gen      |   | Performance    |  |
|  | Model Viewer    |   | Spawn points     |   | Hot-reload     |  |
|  | Mod Packager    |   | Play test        |   |                |  |
|  +-----------------+   +------------------+   +----------------+  |
|                                                                    |
+------------------------------------------------------------------+
         |                    |                      |
         v                    v                      v
  +-------------+    +--------------+    +-------------------+
  | modmgr.h    |    | netdistrib.c |    | actionmap.h       |
  | Mod system  |    | PDCA archive |    | Input contexts     |
  | Component   |    | SVC_DISTRIB  |    | Editor IMC         |
  | enable/dis  |    | SHA-256 auth |    | Gizmo bindings     |
  +-------------+    +--------------+    +-------------------+
```

---

## Layer 1: Asset Import Pipeline

### Goal

Enable runtime import of external assets (meshes, textures, audio) into the engine, converting them to internal formats and registering them in the Asset Catalog with mod-scoped IDs.

### New Asset Types

The catalog already defines the necessary `asset_type_e` values. Studio uses:

| Catalog Type | Studio Use | Import Format | Internal Format |
|-------------|-----------|---------------|-----------------|
| `ASSET_MODEL` | Imported 3D mesh | OBJ, glTF 2.0 | GBI vertex buffer + display list |
| `ASSET_TEXTURE` | Imported texture | PNG, TGA | RGBA32 (GPU texture) |
| `ASSET_AUDIO` | Imported audio | WAV, OGG | Raw PCM (SDL_mixer), optional ADPCM |
| `ASSET_WEAPON` | Custom weapon def | JSON | `studio_weapon_def_t` struct |

### Import Pipeline Architecture

```
External File
      |
      v
  +-------------------+
  | Format Decoder     |  OBJ parser, glTF parser, stb_image, stb_vorbis
  +-------------------+
      |
      v
  +-------------------+
  | Format Converter   |  Mesh → GBI vertices, Tex → RGBA32, Audio → PCM
  +-------------------+
      |
      v
  +-------------------+
  | Asset Serializer   |  Write to mod directory as .pdmesh / .pdtex / .pdaudio
  +-------------------+
      |
      v
  +-------------------+
  | Catalog Register   |  assetCatalogRegister() with mod namespace
  +-------------------+
```

### File: `port/include/studio/studio_import.h`

```c
#ifndef _IN_STUDIO_IMPORT_H
#define _IN_STUDIO_IMPORT_H

#include <PR/ultratypes.h>
#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================
 * Import Result
 * ==================================================================== */

typedef enum {
    IMPORT_OK = 0,
    IMPORT_ERR_FILE_NOT_FOUND,
    IMPORT_ERR_FORMAT_UNSUPPORTED,
    IMPORT_ERR_PARSE_FAILED,
    IMPORT_ERR_CONVERT_FAILED,
    IMPORT_ERR_TOO_LARGE,
    IMPORT_ERR_CATALOG_FULL,
    IMPORT_ERR_WRITE_FAILED
} studio_import_result_t;

/* ====================================================================
 * Mesh Import
 * ==================================================================== */

#define STUDIO_MAX_VERTICES     65536
#define STUDIO_MAX_TRIANGLES    65536
#define STUDIO_MAX_MESHES_PER_FILE 64

/* Intermediate mesh representation (format-agnostic) */
typedef struct {
    f32 *positions;     /* x,y,z per vertex */
    f32 *normals;       /* nx,ny,nz per vertex (NULL if absent) */
    f32 *texcoords;     /* u,v per vertex (NULL if absent) */
    u8  *colors;        /* r,g,b,a per vertex (NULL if absent) */
    u32 *indices;       /* triangle indices (3 per face) */
    s32  num_vertices;
    s32  num_indices;
    char name[64];
} studio_raw_mesh_t;

typedef struct {
    studio_raw_mesh_t meshes[STUDIO_MAX_MESHES_PER_FILE];
    s32 num_meshes;
    f32 bounds_min[3];
    f32 bounds_max[3];
} studio_mesh_import_t;

/* Import OBJ or glTF mesh file.
 * Populates out with intermediate mesh data.
 * Does NOT register in catalog — call studioMeshRegister() after. */
studio_import_result_t studioMeshImport(const char *filepath,
                                         studio_mesh_import_t *out);

/* ====================================================================
 * GBI Mesh Conversion
 * ==================================================================== */

/* Converted GBI mesh — ready for the fast3d renderer */
typedef struct {
    void *display_list;       /* GBI command buffer (Gfx*) */
    s32   display_list_size;  /* bytes */
    void *vertex_buffer;      /* Vtx* array */
    s32   num_vertices;
    s32   num_triangles;
    f32   bounds_min[3];
    f32   bounds_max[3];
} studio_gbi_mesh_t;

/* Convert intermediate mesh to GBI display list + vertex buffer.
 * Handles vertex splitting for N64 GBI constraints (32-vertex cache).
 * Allocates display_list and vertex_buffer — caller frees. */
studio_import_result_t studioMeshConvertToGBI(const studio_raw_mesh_t *raw,
                                               studio_gbi_mesh_t *out);

/* ====================================================================
 * Texture Import
 * ==================================================================== */

#define STUDIO_MAX_TEXTURE_DIM  2048

typedef struct {
    u8  *pixels;        /* RGBA32 pixel data */
    s32  width;
    s32  height;
    s32  channels;      /* always 4 after import (RGBA) */
} studio_texture_import_t;

/* Import PNG or TGA texture file.
 * Converts to RGBA32 regardless of source format.
 * Max dimension: 2048x2048. */
studio_import_result_t studioTextureImport(const char *filepath,
                                            studio_texture_import_t *out);

/* Free imported texture pixel data. */
void studioTextureFree(studio_texture_import_t *tex);

/* ====================================================================
 * Audio Import
 * ==================================================================== */

#define STUDIO_MAX_AUDIO_BYTES  (10 * 1024 * 1024)  /* 10 MB */

typedef struct {
    void *pcm_data;     /* raw PCM samples (s16, mono or stereo) */
    s32   pcm_size;     /* bytes */
    s32   sample_rate;  /* Hz (e.g. 22050, 44100) */
    s32   channels;     /* 1 = mono, 2 = stereo */
    s32   bits;         /* 16 */
    s32   duration_ms;
} studio_audio_import_t;

/* Import WAV or OGG audio file.
 * Decodes to raw PCM. Max 10 MB source file. */
studio_import_result_t studioAudioImport(const char *filepath,
                                          studio_audio_import_t *out);

/* Free imported audio PCM data. */
void studioAudioFree(studio_audio_import_t *audio);

/* ====================================================================
 * Catalog Registration
 * ==================================================================== */

/* Register an imported mesh in the catalog and serialize to mod directory.
 * mod_id: e.g. "mymod" — becomes catalog ID "mymod:asset_name"
 * asset_name: e.g. "weapon_needler_model"
 * Writes .pdmesh file to mods/<mod_id>/models/<asset_name>.pdmesh
 * Returns catalog entry or NULL on failure. */
asset_entry_t *studioRegisterMesh(const char *mod_id, const char *asset_name,
                                   const studio_gbi_mesh_t *mesh);

/* Register an imported texture in the catalog and serialize to mod directory.
 * Writes .pdtex file to mods/<mod_id>/textures/<asset_name>.pdtex */
asset_entry_t *studioRegisterTexture(const char *mod_id, const char *asset_name,
                                      const studio_texture_import_t *tex);

/* Register an imported audio clip in the catalog and serialize to mod directory.
 * Writes .pdaudio file to mods/<mod_id>/audio/<asset_name>.pdaudio */
asset_entry_t *studioRegisterAudio(const char *mod_id, const char *asset_name,
                                    const studio_audio_import_t *audio);

/* ====================================================================
 * Hot-Reload
 * ==================================================================== */

/* Re-import a previously imported asset, update catalog entry and GPU resources.
 * Returns IMPORT_OK on success. Existing references (weapons using this model)
 * automatically see the updated data on next frame. */
studio_import_result_t studioReimport(const char *catalog_id);

#ifdef __cplusplus
}
#endif

#endif /* _IN_STUDIO_IMPORT_H */
```

### Serialized File Formats

**`.pdmesh`** — Binary mesh format:
```
Header:
  u32 magic          0x4853454D ("MESH")
  u16 version        1
  u16 flags          (0x01 = has normals, 0x02 = has texcoords, 0x04 = has colors)
  u32 num_vertices
  u32 num_triangles
  f32 bounds_min[3]
  f32 bounds_max[3]
Data:
  Vtx[num_vertices]          GBI vertex format (16 bytes each)
  Gfx[display_list_size]     GBI command buffer
```

**`.pdtex`** — Binary texture format:
```
Header:
  u32 magic          0x58455450 ("PTEX")
  u16 version        1
  u16 format         0 = RGBA32
  u32 width
  u32 height
Data:
  u8[width * height * 4]     RGBA32 pixels
```

**`.pdaudio`** — Binary audio format:
```
Header:
  u32 magic          0x4F494441 ("ADIO")
  u16 version        1
  u16 channels       1 or 2
  u32 sample_rate
  u32 pcm_size
Data:
  s16[pcm_size / 2]          PCM samples
```

### Integration with Asset Catalog

The import pipeline integrates with the existing catalog through standard registration:

1. **Import**: `studioMeshImport()` parses OBJ/glTF into `studio_raw_mesh_t`
2. **Convert**: `studioMeshConvertToGBI()` produces GBI vertex/display list data
3. **Register**: `studioRegisterMesh("mymod", "needler_model", &gbi)` calls `assetCatalogRegister()` with:
   - `id = "mymod:needler_model"`
   - `type = ASSET_MODEL`
   - `category = "mymod"`
   - `dirpath` = path to serialized `.pdmesh`
   - `bundled = 0`, `enabled = 1`
4. **Serialize**: Writes `.pdmesh` to `mods/mymod/models/needler_model.pdmesh`
5. **Reload**: `studioReimport("mymod:needler_model")` re-reads source, re-converts, updates `loaded_data` and `data_size_bytes`, bumps catalog generation

### GBI Mesh Conversion Details

The N64 GBI vertex format uses 32-vertex register windows. The converter must:

1. **Split meshes** into batches of <=32 unique vertices per draw call
2. **Emit `gSPVertex()`** to load vertex batches into DMEM
3. **Emit `gSP2Triangles()`** for triangle pairs using loaded vertex indices
4. **Handle UVs**: Scale from [0,1] float to S10.5 fixed-point (`u * 32.0f * texwidth`)
5. **Handle normals**: Convert float normals to signed bytes for GBI lighting
6. **Generate bounding box** for frustum culling

The fast3d renderer (`gfx_pc.cpp`) already handles GBI display list execution — imported meshes use the exact same rendering path as ROM models.

### Third-Party Libraries

| Library | Purpose | License | Integration |
|---------|---------|---------|-------------|
| `tinyobj_loader_c` | OBJ parsing | MIT | Single header, vendored in `port/external/` |
| `cgltf` | glTF 2.0 parsing | MIT | Single header, vendored in `port/external/` |
| `stb_image` | PNG/TGA decode | Public domain | Already vendored (check `port/external/`) |
| `stb_vorbis` | OGG Vorbis decode | Public domain | Single header, vendored in `port/external/` |

All are single-file C libraries — no build system changes needed (CMake GLOB_RECURSE auto-discovers).

---

## Layer 2: Weapon Definition Schema

### Goal

Enable creation of fully custom weapons through JSON data files, with no C code modifications. Custom weapons integrate with the existing weapon system (`bondgun.c`) via a data-driven dispatch layer.

### Weapon Definition JSON Schema

```json
{
  "id": "mymod:needler",
  "name": "Needler",
  "description": "Fires tracking needles that accumulate and detonate",
  
  "model": "mymod:needler_model",
  "texture": "mymod:needler_tex",
  "hud_icon": "mymod:needler_icon",
  
  "primary_fire": {
    "projectile_type": "tracking",
    "fire_rate": 10.0,
    "damage": 5.0,
    "ammo_type": "needler_ammo",
    "magazine_size": 30,
    "reload_time": 2.0,
    "spread": 2.0,
    "range": 2000.0,
    "projectile": {
      "speed": 800.0,
      "gravity_scale": 0.0,
      "lifetime": 3.0,
      "tracking_strength": 0.8,
      "tracking_turn_rate": 180.0,
      "tracking_range": 500.0,
      "model": "mymod:needle_projectile",
      "trail": "spark_pink",
      "impact": "spark_small",
      "accumulate": {
        "enabled": true,
        "threshold": 7,
        "stick_to_target": true,
        "detonation_damage": 100.0,
        "detonation_radius": 200.0,
        "detonation_effect": "explosion_needler"
      }
    }
  },
  
  "secondary_fire": {
    "projectile_type": "straight",
    "fire_rate": 1.0,
    "damage": 0.0,
    "description": "Throw weapon — all stuck needles detonate",
    "behavior": "throw_and_detonate"
  },
  
  "ads": {
    "zoom_level": 1.0,
    "movement_speed_modifier": 1.0,
    "spread_reduction": 0.0
  },
  
  "autoaim": {
    "magnetism_strength": 0.3,
    "range": 400.0,
    "snap_speed": 5.0
  },
  
  "properties": {
    "dual_wieldable": false,
    "pickup_ammo": 15,
    "max_ammo": 120,
    "weight": 1.0
  },
  
  "audio": {
    "fire": "mymod:needler_fire",
    "reload": "mymod:needler_reload",
    "empty": "base:click_empty",
    "pickup": "base:weapon_pickup",
    "flight_loop": "mymod:needle_flight",
    "impact": "mymod:needle_impact",
    "accumulate_tick": "mymod:needle_stick",
    "detonation": "mymod:needler_explode"
  }
}
```

### File: `port/include/studio/studio_weapon.h`

```c
#ifndef _IN_STUDIO_WEAPON_H
#define _IN_STUDIO_WEAPON_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================
 * Constants
 * ==================================================================== */

#define STUDIO_WEAPON_MAX          64   /* max custom weapons loaded */
#define STUDIO_WEAPON_NAME_LEN     64
#define STUDIO_WEAPON_DESC_LEN    256
#define STUDIO_WEAPON_ID_LEN       64
#define STUDIO_AMMO_TYPE_LEN       32

/* Custom weapon IDs start above the base game weapon count.
 * Base game uses WEAPON_UNARMED(0) through WEAPON_NBOMB(~40).
 * Custom weapons occupy slots STUDIO_WEAPON_BASE .. STUDIO_WEAPON_BASE+63. */
#define STUDIO_WEAPON_BASE        128

/* ====================================================================
 * Projectile Type Enum
 * ==================================================================== */

typedef enum {
    PROJ_STRAIGHT = 0,   /* linear trajectory, no gravity */
    PROJ_ARC,            /* parabolic arc (gravity affected) */
    PROJ_TRACKING,       /* homing toward nearest valid target */
    PROJ_SPREAD,         /* shotgun-style multi-projectile */
    PROJ_BEAM,           /* instant hitscan (no projectile entity) */
    PROJ_TYPE_COUNT
} studio_proj_type_t;

/* ====================================================================
 * Projectile Behavior Flags (composable)
 * ==================================================================== */

#define PROJ_FLAG_BOUNCE       (1 << 0)   /* bounces off surfaces */
#define PROJ_FLAG_PENETRATE    (1 << 1)   /* passes through targets */
#define PROJ_FLAG_ACCUMULATE   (1 << 2)   /* sticks to target, counts */
#define PROJ_FLAG_EXPLODE      (1 << 3)   /* area damage on impact/death */
#define PROJ_FLAG_CLUSTER      (1 << 4)   /* spawns sub-projectiles */
#define PROJ_FLAG_TIMED        (1 << 5)   /* detonates after lifetime */

/* ====================================================================
 * Accumulation Definition
 * ==================================================================== */

typedef struct {
    s32 enabled;                /* bool */
    s32 threshold;              /* needles required to detonate */
    s32 stick_to_target;        /* bool: embed in target chr */
    f32 detonation_damage;
    f32 detonation_radius;
    char detonation_effect[STUDIO_WEAPON_ID_LEN]; /* catalog ID for VFX */
} studio_accumulate_def_t;

/* ====================================================================
 * Projectile Definition
 * ==================================================================== */

typedef struct {
    studio_proj_type_t type;
    u32  behavior_flags;         /* PROJ_FLAG_* composable bitmask */
    f32  speed;                  /* units per second */
    f32  damage;                 /* per-hit damage */
    f32  radius;                 /* collision radius */
    f32  lifetime;               /* seconds before despawn */
    f32  gravity_scale;          /* 0.0 = no gravity, 1.0 = normal */
    f32  tracking_strength;      /* 0.0-1.0, how aggressively it homes */
    f32  tracking_turn_rate;     /* degrees per second max turn */
    f32  tracking_range;         /* max range to acquire target */
    s32  bounce_count;           /* max bounces (if PROJ_FLAG_BOUNCE) */
    f32  penetrate_damage_falloff; /* multiplier per penetration */
    s32  spread_count;           /* projectiles per shot (if PROJ_SPREAD) */
    f32  spread_angle;           /* cone half-angle in degrees */
    
    /* Accumulation (e.g. Needler) */
    studio_accumulate_def_t accumulate;
    
    /* Asset references (catalog IDs) */
    char model[STUDIO_WEAPON_ID_LEN];      /* projectile model */
    char trail_effect[STUDIO_WEAPON_ID_LEN];
    char impact_effect[STUDIO_WEAPON_ID_LEN];
    char explosion_effect[STUDIO_WEAPON_ID_LEN];
} studio_projectile_def_t;

/* ====================================================================
 * Fire Mode Definition
 * ==================================================================== */

typedef struct {
    f32  fire_rate;             /* rounds per second */
    f32  damage;                /* damage per hit (hitscan) or per projectile */
    f32  spread;                /* degrees of random spread */
    f32  range;                 /* max effective range (units) */
    s32  magazine_size;         /* rounds per clip (0 = unlimited) */
    f32  reload_time;           /* seconds to reload */
    char ammo_type[STUDIO_AMMO_TYPE_LEN];  /* ammo pool name */
    studio_projectile_def_t projectile;
    char behavior[64];          /* special: "throw_and_detonate", etc. */
} studio_fire_mode_t;

/* ====================================================================
 * ADS (Aim Down Sights) Definition
 * ==================================================================== */

typedef struct {
    f32 zoom_level;             /* 1.0 = no zoom, 2.0 = 2x, etc. */
    f32 movement_speed_mod;     /* 0.5 = half speed while ADS */
    f32 spread_reduction;       /* multiplier: 0.0 = perfect accuracy */
} studio_ads_def_t;

/* ====================================================================
 * Auto-Aim Definition
 * ==================================================================== */

typedef struct {
    f32 magnetism_strength;     /* 0.0 = none, 1.0 = full snap */
    f32 range;                  /* effective auto-aim range */
    f32 snap_speed;             /* degrees per frame of aim assist */
} studio_autoaim_def_t;

/* ====================================================================
 * Weapon Audio References
 * ==================================================================== */

typedef struct {
    char fire[STUDIO_WEAPON_ID_LEN];
    char reload[STUDIO_WEAPON_ID_LEN];
    char empty_click[STUDIO_WEAPON_ID_LEN];
    char pickup[STUDIO_WEAPON_ID_LEN];
    char flight_loop[STUDIO_WEAPON_ID_LEN];
    char impact[STUDIO_WEAPON_ID_LEN];
    char accumulate_tick[STUDIO_WEAPON_ID_LEN];
    char detonation[STUDIO_WEAPON_ID_LEN];
} studio_weapon_audio_t;

/* ====================================================================
 * Complete Weapon Definition
 * ==================================================================== */

typedef struct {
    /* Identity */
    char id[STUDIO_WEAPON_ID_LEN];       /* catalog ID: "mymod:needler" */
    char name[STUDIO_WEAPON_NAME_LEN];
    char description[STUDIO_WEAPON_DESC_LEN];
    
    /* Fire modes */
    studio_fire_mode_t primary;
    studio_fire_mode_t secondary;
    s32 has_secondary;                    /* bool */
    
    /* Aim assist */
    studio_ads_def_t   ads;
    studio_autoaim_def_t autoaim;
    
    /* Audio */
    studio_weapon_audio_t audio;
    
    /* Model/texture references (catalog IDs) */
    char model[STUDIO_WEAPON_ID_LEN];
    char texture[STUDIO_WEAPON_ID_LEN];
    char hud_icon[STUDIO_WEAPON_ID_LEN];
    
    /* Properties */
    s32 dual_wieldable;
    s32 pickup_ammo;
    s32 max_ammo;
    f32 weight;                           /* affects movement speed */
    
    /* Runtime binding (assigned during registration) */
    s32 runtime_weapon_slot;              /* STUDIO_WEAPON_BASE + index */
    s32 loaded;                           /* bool: definition parsed OK */
} studio_weapon_def_t;

/* ====================================================================
 * Weapon Registry API
 * ==================================================================== */

/* Initialize the custom weapon registry. Call once at startup. */
void studioWeaponInit(void);

/* Load a weapon definition from JSON file.
 * Parses, validates, registers in catalog as ASSET_WEAPON.
 * Returns pointer to definition or NULL on parse error. */
studio_weapon_def_t *studioWeaponLoad(const char *json_path, const char *mod_id);

/* Save a weapon definition to JSON file. */
s32 studioWeaponSave(const studio_weapon_def_t *def, const char *json_path);

/* Get custom weapon definition by catalog ID. Returns NULL if not found. */
studio_weapon_def_t *studioWeaponGet(const char *catalog_id);

/* Get custom weapon definition by runtime slot index. */
studio_weapon_def_t *studioWeaponGetBySlot(s32 slot);

/* Is this weapon slot a Studio custom weapon? */
s32 studioWeaponIsCustom(s32 weaponnum);

/* Get count of loaded custom weapons. */
s32 studioWeaponGetCount(void);

/* Hot-reload: re-read weapon JSON and update runtime definition.
 * Returns 1 on success, 0 on parse error. */
s32 studioWeaponReload(const char *catalog_id);

#ifdef __cplusplus
}
#endif

#endif /* _IN_STUDIO_WEAPON_H */
```

### Integration with Existing Weapon System

The existing weapon system in `bondgun.c` uses hardcoded per-weapon functions via `struct weaponfunc` and `struct weaponfunc_shootprojectile`. Studio weapons integrate via a data-driven dispatch layer:

```
Existing flow:
  bgunTickIncAttack() → per-weapon C function → bgunCreateFiredProjectile()

Studio flow:
  bgunTickIncAttack() → studioWeaponIsCustom(weaponnum)?
    YES → studioWeaponFirePrimary(weaponnum) → studioProjectileSpawn()
    NO  → existing per-weapon C function (unchanged)
```

**Key integration points in `bondgun.c`:**

1. **`bgunTickIncAttack()`** — Add check: if `studioWeaponIsCustom(weaponnum)`, delegate to `studioWeaponFireTick()` which reads fire_rate, spread, etc. from the `studio_weapon_def_t`.

2. **`bgunGetAmmoCount()` / `bgunSetAmmoQtyForWeapon()`** — Extend ammo system with custom ammo types. Custom weapons store ammo in a parallel `s_StudioAmmo[STUDIO_WEAPON_MAX]` array indexed by `weaponnum - STUDIO_WEAPON_BASE`.

3. **`g_MpWeapons[]`** — Custom weapons do NOT occupy slots in `g_MpWeapons[]`. Instead, the weapon select UI queries both `g_MpWeapons[]` (base) and `studioWeaponGetCount()` (custom). The match config stores weapon references as catalog ID strings (per M0.1c constraint: `weapon_ids[6][64]`).

4. **Network sync** — Custom weapon definitions are included in the mod package. The server's catalog session ref maps custom weapon slots to catalog IDs. Clients receiving the mod via netdistrib get the weapon JSON and register it locally.

### Weapon Select Integration

Custom weapons appear in the Combat Sim weapon select dropdown alongside base weapons:

```c
/* In pdgui_menu_matchsetup.cpp weapon dropdown: */
for (s32 i = 0; i < base_weapon_count; i++) {
    /* existing base weapon entries */
}
/* Append Studio weapons */
for (s32 i = 0; i < studioWeaponGetCount(); i++) {
    studio_weapon_def_t *w = studioWeaponGetBySlot(STUDIO_WEAPON_BASE + i);
    if (w && w->loaded) {
        /* ImGui::Selectable(w->name, ...) */
    }
}
```

---

## Layer 3: Data-Driven Projectile System

### Goal

Replace hardcoded per-weapon projectile behaviors with a data-driven system where projectile type, physics, tracking, accumulation, and effects are all defined in the weapon JSON.

### Projectile Lifecycle

```
Spawn (studioProjectileSpawn)
   |
   v
Per-Frame Tick (studioProjectileTick)
   |
   +-- Move: apply velocity + gravity
   +-- Track: rotate toward target (if tracking type)
   +-- Collide: capsule sweep against world + chrs
   +-- Accumulate: if hit + accumulate enabled, stick to target
   +-- Expire: if lifetime exceeded, destroy or detonate
   |
   v
Impact / Destroy (studioProjectileImpact)
   +-- Apply damage
   +-- Check accumulation threshold → detonate all
   +-- Spawn impact VFX + audio
   +-- Remove projectile entity
```

### File: `port/include/studio/studio_projectile.h`

```c
#ifndef _IN_STUDIO_PROJECTILE_H
#define _IN_STUDIO_PROJECTILE_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================
 * Constants
 * ==================================================================== */

#define STUDIO_MAX_PROJECTILES  200   /* matches pool: NUMPROJECTILES=200 */
#define STUDIO_MAX_ACCUM_PER_CHR 32   /* max stuck projectiles per target */

/* ====================================================================
 * Active Projectile Instance
 * ==================================================================== */

typedef struct studio_projectile {
    s32  active;                 /* bool: slot in use */
    s32  owner_chrnum;           /* who fired it */
    s32  weapon_slot;            /* which weapon definition */
    s32  fire_mode;              /* 0 = primary, 1 = secondary */
    
    /* Physics */
    f32  pos[3];                 /* current position */
    f32  vel[3];                 /* current velocity vector */
    f32  speed;                  /* magnitude of vel (cached) */
    f32  age;                    /* seconds since spawn */
    
    /* From definition (cached at spawn for perf) */
    studio_proj_type_t type;
    u32  behavior_flags;
    f32  damage;
    f32  radius;
    f32  lifetime;
    f32  gravity_scale;
    f32  tracking_strength;
    f32  tracking_turn_rate;
    f32  tracking_range;
    s32  bounces_remaining;
    f32  penetrate_falloff;
    
    /* Tracking state */
    s32  target_chrnum;          /* -1 = no target */
    
    /* Accumulation (if stuck) */
    s32  stuck;                  /* bool: embedded in target */
    s32  stuck_chrnum;           /* chr this is stuck to */
    f32  stuck_offset[3];       /* local offset from chr pos */
    
    /* Visual */
    s32  prop_handle;            /* defaultobj handle for rendering */
} studio_projectile_t;

/* ====================================================================
 * Accumulation Tracking (per-chr)
 * ==================================================================== */

typedef struct {
    s32 count;                            /* number of stuck projectiles */
    s32 weapon_slot;                      /* which weapon's needles */
    s32 projectile_indices[STUDIO_MAX_ACCUM_PER_CHR]; /* pool indices */
} studio_accum_state_t;

/* ====================================================================
 * Projectile System API
 * ==================================================================== */

/* Initialize projectile pool. Call once at startup. */
void studioProjectileInit(void);

/* Spawn a projectile from a weapon fire event.
 * origin: world-space fire position
 * dir: normalized fire direction
 * owner: chrnum of firing chr
 * weapon_slot: runtime weapon slot (STUDIO_WEAPON_BASE + index)
 * fire_mode: 0 = primary, 1 = secondary
 * Returns projectile pool index or -1 if pool full. */
s32 studioProjectileSpawn(const f32 origin[3], const f32 dir[3],
                           s32 owner, s32 weapon_slot, s32 fire_mode);

/* Tick all active projectiles. Call once per game frame (60 Hz).
 * Handles movement, tracking, collision, accumulation, expiry. */
void studioProjectileTick(f32 dt);

/* Clear all active projectiles. Call on stage change / match end. */
void studioProjectileClearAll(void);

/* Get accumulation count on a chr for a specific weapon.
 * Returns number of stuck projectiles. */
s32 studioProjectileGetAccumCount(s32 chrnum, s32 weapon_slot);

/* Force-detonate all accumulated projectiles on a chr.
 * Called when threshold reached or weapon throw-detonation. */
void studioProjectileDetonateAccum(s32 chrnum, s32 weapon_slot);

/* Get active projectile count (for debug/perf display). */
s32 studioProjectileGetActiveCount(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_STUDIO_PROJECTILE_H */
```

### Projectile Tick Implementation

The per-frame tick handles each behavior type:

**Straight projectiles:**
```c
pos += vel * dt;
if (age > lifetime) destroy();
```

**Arc projectiles (gravity):**
```c
vel.y -= GRAVITY * gravity_scale * dt;
pos += vel * dt;
```

**Tracking projectiles (homing):**
```c
/* Find nearest valid target within tracking_range */
target = findNearestEnemy(pos, tracking_range, owner);
if (target) {
    f32 to_target[3];
    vec3Sub(to_target, target->pos, pos);
    vec3Normalize(to_target);
    /* Rotate velocity toward target, limited by turn rate */
    f32 max_turn = tracking_turn_rate * DEG_TO_RAD * dt;
    f32 current_dir[3]; vec3Normalize2(current_dir, vel);
    f32 angle = acosf(vec3Dot(current_dir, to_target));
    if (angle > max_turn) {
        /* Slerp by max_turn / angle fraction */
        f32 t = max_turn / angle * tracking_strength;
        vec3Lerp(vel, current_dir, to_target, t);
        vec3Normalize(vel);
        vec3Scale(vel, speed);
    }
}
pos += vel * dt;
```

**Spread projectiles (shotgun):**
Spawn `spread_count` straight projectiles with random angular offsets within `spread_angle` cone. Each is an independent projectile instance.

**Beam (hitscan):**
Instant raycast from origin along direction. No projectile entity spawned. Damage applied immediately. Trail effect rendered as a line.

### Accumulation System

Accumulation state is tracked per-chr in a lightweight array parallel to the chr pool:

```c
static studio_accum_state_t s_AccumState[MAX_MPCHRS]; /* 36 slots */
```

When a tracking projectile with `PROJ_FLAG_ACCUMULATE` hits a chr:
1. Projectile's `stuck = 1`, `stuck_chrnum = hit chr`
2. `s_AccumState[chrnum].count++`
3. Projectile visually attaches to chr (offset stored, updated each frame)
4. When `count >= threshold`, call `studioProjectileDetonateAccum()`:
   - Apply `detonation_damage` at chr position with `detonation_radius`
   - Spawn detonation VFX/audio
   - Remove all stuck projectiles for that weapon on that chr

### Collision Integration

Projectiles use the capsule sweep system for world collision:

```c
struct capsulecast cast = {
    .origin = proj->pos,
    .dir = normalized_vel,
    .length = speed * dt,
    .radius = proj->radius,
    .ymin = -proj->radius,
    .ymax = proj->radius
};
capsuleSweep(&cast);
if (cast.hittype != CAPSULE_HIT_NONE) {
    studioProjectileImpact(proj, &cast);
}
```

For chr collision, a separate sphere-vs-capsule test against each chr's collision cylinder (already computed by the game for standard weapons).

---

## Layer 4: In-Client Editor UI

### Goal

Provide ImGui-based creation tools accessible from the main menu, following the established `pdgui_menu_*.cpp` patterns.

### Menu Integration Architecture

Studio screens integrate with the existing ImGui menu stack:

```
Main Menu
  └── "Studio" button
        └── Studio Hub (pdgui_menu_studio.cpp)
              ├── Asset Browser
              ├── Weapon Editor
              ├── Map Editor
              ├── Model Viewer
              ├── Texture Viewer
              ├── Audio Player
              └── Mod Packager
```

Each Studio screen:
1. Pushes a dialog def onto the menu stack via `menuPushDialog()`
2. Activates `g_ImcStudioEditor` input mapping context (new IMC, priority 15)
3. On close: pops dialog, deactivates IMC

### New Input Mapping Context

```c
/* In actionmap.h — add to existing IMC singletons */
extern InputMappingContext g_ImcStudioEditor;   /* priority 15 — 3D viewport + gizmos */
```

Studio-specific actions (extend `InputAction` enum):

```c
/* ---- Studio editor ---- */
ACTION_STUDIO_SELECT,       /* left click — select object */
ACTION_STUDIO_TRANSLATE,    /* W — translate gizmo */
ACTION_STUDIO_ROTATE,       /* E — rotate gizmo */
ACTION_STUDIO_SCALE,        /* R — scale gizmo */
ACTION_STUDIO_DELETE,       /* Delete — remove selected */
ACTION_STUDIO_DUPLICATE,    /* Ctrl+D — duplicate selected */
ACTION_STUDIO_UNDO,         /* Ctrl+Z */
ACTION_STUDIO_REDO,         /* Ctrl+Y */
ACTION_STUDIO_ORBIT,        /* Middle mouse — orbit camera */
ACTION_STUDIO_PAN,          /* Shift+Middle — pan camera */
ACTION_STUDIO_ZOOM,         /* Scroll — zoom camera */
ACTION_STUDIO_FOCUS,        /* F — focus on selected */
ACTION_STUDIO_GRID_SNAP,    /* G — toggle grid snapping */
ACTION_STUDIO_PLAY_TEST,    /* F5 — launch play test */
```

### File: `port/fast3d/pdgui_menu_studio.cpp`

**Studio Hub** — main entry point with tab bar:

```cpp
/* Screen layout:
 * +---------------------------------------------+
 * | [Assets] [Weapons] [Maps] [Viewer] [Package] |
 * +---------------------------------------------+
 * |                                               |
 * |          Tab-specific content area            |
 * |                                               |
 * +---------------------------------------------+
 */

static void studioRender(void) {
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x * 0.9f,
                                     ImGui::GetIO().DisplaySize.y * 0.85f),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.05f,
                                    ImGui::GetIO().DisplaySize.y * 0.05f),
                            ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("PD2 Studio", &s_StudioOpen, ImGuiWindowFlags_MenuBar)) {
        if (ImGui::BeginTabBar("StudioTabs")) {
            if (ImGui::BeginTabItem("Asset Browser"))  { studioAssetBrowser();  ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Weapon Editor"))   { studioWeaponEditor();  ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Map Editor"))      { studioMapEditor();     ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Model Viewer"))    { studioModelViewer();   ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Mod Packager"))    { studioModPackager();   ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}
```

### Asset Browser

Displays all catalog assets with filtering by type, namespace, and search text:

```
+----------------------------------------------+
| Type: [All v]  Namespace: [All v]  [Search..] |
+----------------------------------------------+
| Icon | ID                    | Type    | Mod  |
|------|-----------------------|---------|------|
| [M]  | base:falcon2          | Weapon  | base |
| [M]  | mymod:needler         | Weapon  | mymod|
| [T]  | mymod:needler_tex     | Texture | mymod|
| ...                                          |
+----------------------------------------------+
| Preview:                                      |
| [3D model preview / texture / waveform]       |
+----------------------------------------------+
```

- **Type filter**: Dropdown with all `asset_type_e` values
- **Namespace filter**: Populated from catalog categories
- **Search**: Substring match on `id` field
- **Preview**: Renders to an offscreen FBO, displayed as ImGui::Image texture
- **Actions**: Import, Delete, Reimport, Copy ID

### Weapon Editor

Visual editor for weapon JSON with live preview:

```
+--------------------------------------------------+
| Weapon: [mymod:needler v]    [New] [Save] [Load] |
+--------------------------------------------------+
| Name: [Needler           ]                        |
| Model: [mymod:needler_model    ] [Browse]        |
| Texture: [mymod:needler_tex   ] [Browse]         |
+--------------------------------------------------+
| PRIMARY FIRE                                      |
|   Type: [Tracking v]                              |
|   Fire Rate: [====|====] 10.0 rps                 |
|   Damage:    [==|======]  5.0                     |
|   Spread:    [=|=======]  2.0 deg                 |
|   Range:     [======|==] 2000                     |
|   Magazine:  [30        ]                         |
|   Reload:    [2.0       ] sec                     |
|                                                    |
|   PROJECTILE                                       |
|     Speed:   [====|====] 800                       |
|     Tracking: [======|=] 0.8                       |
|     Turn Rate: [===|===] 180 deg/s                 |
|                                                    |
|   ACCUMULATION                                     |
|     [x] Enabled                                    |
|     Threshold: [7    ]                             |
|     [x] Stick to target                           |
|     Detonation Damage: [100.0]                     |
|     Detonation Radius: [200.0]                     |
+--------------------------------------------------+
| PREVIEW:                                          |
| [3D weapon model + particle trail visualization]  |
+--------------------------------------------------+
```

Implementation: sliders map directly to `studio_weapon_def_t` fields. On any change, the preview updates. Save writes JSON to the mod directory.

### Model Viewer

3D viewport with orbit camera:

```
+----------------------------------------------+
| Model: [mymod:needler_model v]  [Import OBJ] |
+----------------------------------------------+
|                                                |
|         [3D viewport with orbit camera]        |
|         - Left drag: orbit                     |
|         - Right drag: pan                      |
|         - Scroll: zoom                         |
|         - Grid floor visible                   |
|                                                |
+----------------------------------------------+
| Vertices: 1,234  |  Triangles: 2,048          |
| Bounds: (-10, -5, -10) to (10, 15, 10)        |
+----------------------------------------------+
```

### 3D Viewport Rendering (FBO Pipeline)

All 3D previews (model viewer, weapon preview, map editor) render to offscreen framebuffer objects displayed as ImGui textures:

```c
/* In studio_viewport.c */
typedef struct {
    u32 fbo;             /* OpenGL FBO */
    u32 color_tex;       /* color attachment (ImGui texture) */
    u32 depth_rbo;       /* depth renderbuffer */
    s32 width, height;
    f32 camera_pos[3];
    f32 camera_target[3];
    f32 camera_up[3];
    f32 fov;
    f32 near_clip, far_clip;
} studio_viewport_t;

void studioViewportInit(studio_viewport_t *vp, s32 w, s32 h);
void studioViewportResize(studio_viewport_t *vp, s32 w, s32 h);
void studioViewportBegin(studio_viewport_t *vp);   /* bind FBO */
void studioViewportEnd(studio_viewport_t *vp);     /* unbind FBO */
u32  studioViewportGetTexture(studio_viewport_t *vp); /* for ImGui::Image */

/* Orbit camera controls */
void studioViewportOrbit(studio_viewport_t *vp, f32 dx, f32 dy);
void studioViewportPan(studio_viewport_t *vp, f32 dx, f32 dy);
void studioViewportZoom(studio_viewport_t *vp, f32 delta);
void studioViewportFocus(studio_viewport_t *vp, const f32 target[3], f32 distance);
```

This is separate from the game camera (`g_Vars.currentplayer->cam_*`). The viewport renders using OpenGL directly (not through the GBI pipeline), with a simple forward renderer:

1. `studioViewportBegin()` — bind FBO, set viewport, clear
2. Set projection + view matrices from camera state
3. For each object: bind VAO, set model matrix, draw
4. `studioViewportEnd()` — unbind FBO
5. `ImGui::Image((ImTextureID)studioViewportGetTexture(vp), size)` — display

For GBI meshes (imported via Layer 1), the display lists are executed through the fast3d renderer into the FBO. This requires `gfx_pc_begin_dl()` / `gfx_pc_end_dl()` scoped to the FBO context.

### Mod Packager

Select assets from the catalog, define mod manifest, export as `.pdpack`:

```
+----------------------------------------------+
| Mod ID: [mymod            ]                   |
| Name:   [My Custom Mod    ]                   |
| Author: [Player           ]                   |
| Version: [1.0.0           ]                   |
+----------------------------------------------+
| INCLUDED ASSETS:                              |
| [x] mymod:needler          (Weapon)           |
| [x] mymod:needler_model    (Model)            |
| [x] mymod:needler_tex      (Texture)          |
| [x] mymod:needler_fire     (Audio)            |
| [ ] mymod:other_thing      (Model)            |
|                                                |
| Total size: 2.4 MB                            |
+----------------------------------------------+
| [Auto-detect dependencies]  [Export .pdpack]  |
+----------------------------------------------+
```

The `.pdpack` format is the existing PDCA archive format from `netdistrib.c` — same magic, same structure. This means mod packages created in Studio are directly compatible with network distribution.

---

## Layer 5: Map Editor

### Goal

Create playable multiplayer maps inside the client with mesh placement, auto-generated collision and navmesh, spawn points, and instant play-test capability.

### Map Scene Format

Maps created in Studio use a JSON scene description + binary mesh data:

**`map.json`** — Scene description:
```json
{
  "id": "mymod:arena_custom",
  "name": "Custom Arena",
  "version": 1,
  
  "template": "blank",
  
  "lighting": {
    "ambient": [0.3, 0.3, 0.4],
    "directional": {
      "direction": [-0.5, -1.0, -0.3],
      "color": [1.0, 0.95, 0.9],
      "intensity": 0.7
    }
  },
  
  "objects": [
    {
      "id": "floor_1",
      "mesh": "mymod:floor_tile",
      "position": [0, 0, 0],
      "rotation": [0, 0, 0],
      "scale": [10, 1, 10],
      "collision": true,
      "flags": ["floor"]
    },
    {
      "id": "wall_1",
      "mesh": "mymod:wall_segment",
      "position": [500, 0, 0],
      "rotation": [0, 90, 0],
      "scale": [1, 3, 5],
      "collision": true,
      "flags": ["wall"]
    }
  ],
  
  "spawns": {
    "players": [
      { "position": [0, 100, 0], "rotation": 0 },
      { "position": [500, 100, 500], "rotation": 180 },
      { "position": [-500, 100, 0], "rotation": 90 },
      { "position": [0, 100, -500], "rotation": 270 }
    ],
    "weapons": [
      { "position": [250, 50, 250], "weapon_id": "base:falcon2" },
      { "position": [-250, 50, -250], "weapon_id": "base:shotgun" }
    ],
    "ammo": [
      { "position": [100, 50, 100], "ammo_type": "base:ammo_pistol" }
    ]
  },
  
  "properties": {
    "player_count_min": 2,
    "player_count_max": 8,
    "bot_friendly": true,
    "music": "base:combat_theme_1"
  }
}
```

### Replacing ROM-Based Stage Loading

The existing stage system loads from ROM via `bgReset(stagenum)` → `bgBuildTables()` → room/portal binary data. Studio maps bypass this entirely:

```
ROM Stage Flow:
  lvReset(stagenum) → bgLoadFile() → unpack binary rooms/portals → bgBuildTables()

Studio Map Flow:
  lvReset(STUDIO_STAGENUM) → studioMapLoad(catalog_id)
    → parse map.json
    → load meshes from catalog
    → generate collision mesh (Layer 5.2)
    → generate navmesh (Layer 5.3)
    → register as single room (no portal system)
    → place spawn pads + weapon pickups
```

Studio maps use a reserved stagenum (`STUDIO_STAGENUM = 0xFF`) and a single-room architecture (no portal visibility culling — modern GPUs handle full-scene rendering).

### File: `port/include/studio/studio_map.h`

```c
#ifndef _IN_STUDIO_MAP_H
#define _IN_STUDIO_MAP_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================
 * Constants
 * ==================================================================== */

#define STUDIO_STAGENUM        0xFF    /* reserved stagenum for Studio maps */
#define STUDIO_MAP_MAX_OBJECTS 1024
#define STUDIO_MAP_MAX_SPAWNS  32
#define STUDIO_MAP_MAX_WEAPONS 64
#define STUDIO_MAP_ID_LEN      64
#define STUDIO_MAP_NAME_LEN    128

/* ====================================================================
 * Map Object (placed mesh)
 * ==================================================================== */

typedef struct {
    char id[64];                    /* unique object ID within map */
    char mesh_id[STUDIO_MAP_ID_LEN]; /* catalog ID of mesh asset */
    f32  position[3];
    f32  rotation[3];               /* euler angles (degrees) */
    f32  scale[3];
    s32  has_collision;             /* bool: include in collision mesh */
    u32  collision_flags;           /* GEOFLAG_FLOOR, GEOFLAG_WALL, etc. */
    s32  selected;                  /* bool: editor selection state */
} studio_map_object_t;

/* ====================================================================
 * Spawn Point
 * ==================================================================== */

typedef struct {
    f32  position[3];
    f32  rotation;                  /* yaw in degrees */
} studio_spawn_point_t;

/* ====================================================================
 * Weapon Pickup
 * ==================================================================== */

typedef struct {
    f32  position[3];
    char weapon_id[STUDIO_MAP_ID_LEN]; /* catalog ID */
} studio_weapon_spawn_t;

/* ====================================================================
 * Lighting
 * ==================================================================== */

typedef struct {
    f32 ambient_color[3];           /* RGB 0-1 */
    f32 dir_direction[3];           /* normalized direction */
    f32 dir_color[3];               /* RGB 0-1 */
    f32 dir_intensity;              /* 0-1 */
} studio_map_lighting_t;

/* ====================================================================
 * Complete Map State
 * ==================================================================== */

typedef struct {
    /* Identity */
    char id[STUDIO_MAP_ID_LEN];
    char name[STUDIO_MAP_NAME_LEN];
    char mod_id[STUDIO_MAP_ID_LEN];
    
    /* Objects */
    studio_map_object_t objects[STUDIO_MAP_MAX_OBJECTS];
    s32 num_objects;
    
    /* Spawns */
    studio_spawn_point_t player_spawns[STUDIO_MAP_MAX_SPAWNS];
    s32 num_player_spawns;
    
    studio_weapon_spawn_t weapon_spawns[STUDIO_MAP_MAX_WEAPONS];
    s32 num_weapon_spawns;
    
    /* Lighting */
    studio_map_lighting_t lighting;
    
    /* Properties */
    s32 player_count_min;
    s32 player_count_max;
    s32 bot_friendly;               /* bool: has navmesh */
    char music_id[STUDIO_MAP_ID_LEN];
    
    /* Generated data (runtime only, not serialized in JSON) */
    void *collision_mesh;           /* generated geotile data */
    s32   collision_mesh_size;
    void *navmesh;                  /* generated navigation mesh */
    s32   navmesh_size;
    s32   navmesh_generating;       /* bool: async generation in progress */
    f32   navmesh_progress;         /* 0.0 - 1.0 */
    
    /* Editor state (not serialized) */
    s32 dirty;                      /* bool: unsaved changes */
    s32 selected_object;            /* index or -1 */
    s32 gizmo_mode;                 /* 0=translate, 1=rotate, 2=scale */
} studio_map_t;

/* ====================================================================
 * Map Editor API
 * ==================================================================== */

/* Create a new empty map. */
void studioMapNew(studio_map_t *map, const char *mod_id, const char *name);

/* Load map from mod directory. Returns 1 on success. */
s32 studioMapLoad(studio_map_t *map, const char *catalog_id);

/* Save map to mod directory as map.json + binary assets. */
s32 studioMapSave(const studio_map_t *map);

/* Add a mesh object to the map. Returns object index or -1. */
s32 studioMapAddObject(studio_map_t *map, const char *mesh_id,
                        const f32 pos[3], const f32 rot[3], const f32 scale[3]);

/* Remove object by index. */
void studioMapRemoveObject(studio_map_t *map, s32 index);

/* Duplicate selected object. Returns new index or -1. */
s32 studioMapDuplicateObject(studio_map_t *map, s32 index);

/* Add player spawn point. Returns index or -1. */
s32 studioMapAddPlayerSpawn(studio_map_t *map, const f32 pos[3], f32 rotation);

/* Add weapon spawn. Returns index or -1. */
s32 studioMapAddWeaponSpawn(studio_map_t *map, const f32 pos[3],
                             const char *weapon_id);

/* ====================================================================
 * Collision Generation
 * ==================================================================== */

/* Generate collision mesh from all objects with has_collision = true.
 * Converts imported meshes to geotile format compatible with
 * the existing collision system (cdTestVolume, capsuleSweep).
 * Synchronous — may take 100-500ms for complex maps. */
s32 studioMapGenerateCollision(studio_map_t *map);

/* ====================================================================
 * Navmesh Generation
 * ==================================================================== */

/* Begin async navmesh generation.
 * Updates map->navmesh_progress (0.0 - 1.0) during generation.
 * Sets map->navmesh_generating = 0 and populates map->navmesh on completion.
 * Call studioMapNavmeshPoll() each frame to check progress. */
void studioMapGenerateNavmeshAsync(studio_map_t *map);

/* Poll navmesh generation progress. Returns 1 when complete. */
s32 studioMapNavmeshPoll(studio_map_t *map);

/* ====================================================================
 * Play Test
 * ==================================================================== */

/* Launch the current map for play testing with bots.
 * Generates collision + navmesh if dirty.
 * Registers map in catalog as temporary asset.
 * Starts a local match with g_MissionConfig.stage_id = map->id.
 * Returns 1 on success. */
s32 studioMapPlayTest(studio_map_t *map, s32 num_bots);

/* Return to editor from play test. Restores editor state. */
void studioMapEndPlayTest(void);

/* ====================================================================
 * 3D Gizmo System
 * ==================================================================== */

typedef enum {
    GIZMO_TRANSLATE = 0,
    GIZMO_ROTATE,
    GIZMO_SCALE
} studio_gizmo_mode_t;

/* Render gizmo for selected object. Call during viewport render pass.
 * Returns 1 if gizmo is being interacted with (consuming input). */
s32 studioGizmoRender(studio_viewport_t *vp, studio_map_t *map);

/* Process mouse input for gizmo interaction.
 * dx, dy: mouse delta in pixels.
 * Returns 1 if gizmo consumed the input. */
s32 studioGizmoProcessInput(studio_viewport_t *vp, studio_map_t *map,
                             f32 dx, f32 dy);

/* Object picking: cast ray from mouse position, return object index or -1. */
s32 studioMapPick(studio_viewport_t *vp, studio_map_t *map,
                   f32 mouse_x, f32 mouse_y);

#ifdef __cplusplus
}
#endif

#endif /* _IN_STUDIO_MAP_H */
```

### Collision Generation

The collision generator converts Studio map meshes to the engine's collision format:

1. **Collect triangles**: For each object with `has_collision = true`, transform mesh vertices by object's position/rotation/scale matrix
2. **Classify triangles**: Normal-based classification:
   - Normal.y > 0.7 → `GEOFLAG_FLOOR1` (walkable)
   - Normal.y < -0.7 → ceiling
   - Otherwise → `GEOFLAG_WALL`
3. **Generate geotiles**: Convert triangles to `struct geotilef` (float-precision tiles used by the lift/platform system, which already handles arbitrary geometry)
4. **Build spatial index**: Grid-based spatial hash (256-unit cells) for `cdCollectGeoForCyl()` lookups
5. **Register room**: Studio maps are single-room — set `g_BgRooms[0]` to contain all generated collision

This integrates directly with the existing `capsuleSweep()` and `cdTestVolume()` — no new collision code paths needed.

### Navmesh Generation

The navmesh generator creates bot navigation data:

1. **Voxelize walkable surfaces**: Rasterize floor triangles into a 2D grid (cell size ~50 units)
2. **Mark walkable cells**: Cells with floor geometry and sufficient headroom (>180 units to ceiling)
3. **Region growing**: Group contiguous walkable cells into convex regions
4. **Generate navigation graph**: Each region becomes a nav node; edges connect adjacent regions
5. **Store as pad data**: Convert nav nodes to `struct pad` format (the game's existing AI waypoint system)

The bot AI already pathfinds using pad-to-pad connections. Studio navmesh generates compatible pad data, so bots "just work" on custom maps.

**Async implementation**: Navmesh generation runs in a background thread (`_beginthreadex` on Windows). The main thread polls `navmesh_progress` each frame and displays a progress bar in the editor.

### Map Editor Viewport

The map editor uses a full-screen 3D viewport with the Studio viewport system (Layer 4):

```
+--------------------------------------------------+
| [File v] [Edit v] [View v]  [Snap: 50] [Play F5] |
+--------------------------------------------------+
| Object  |                                         |
| List    |     3D Viewport                         |
|         |                                         |
| > floor |     [orbit/pan/zoom camera]             |
|   wall1 |     [gizmo on selected object]          |
|   wall2 |     [grid overlay on XZ plane]          |
|   spawn1|     [spawn point markers]               |
|   spawn2|     [weapon pickup icons]               |
|         |                                         |
+---------+                                         |
| Props   |                                         |
| [+Mesh] |                                         |
| [+Spawn]|                                         |
| [+Wpn]  |                                         |
+---------+-----------------------------------------+
| Selected: wall1 | Pos: 500,0,0 | Rot: 0,90,0     |
+--------------------------------------------------+
```

### Network Transfer

Studio maps package as mods using the existing mod system:

```
mods/mymod/
  mod.json              (standard mod manifest)
  maps/
    arena_custom/
      map.json          (scene description)
      collision.bin     (generated collision)
      navmesh.bin       (generated navmesh)
  models/
    floor_tile.pdmesh
    wall_segment.pdmesh
  textures/
    floor_tex.pdtex
```

When a player joins a match using a Studio map, the standard `netdistrib.c` transfer pipeline sends the entire mod (PDCA archive, zlib compressed, SHA-256 verified). The receiving client extracts, registers in catalog, generates collision/navmesh locally, and loads the map.

---

## Cross-Cutting: ADS / Auto-Aim System

### Architecture

ADS and auto-aim are per-weapon properties defined in the weapon JSON (Layer 2) and applied through the action map system.

### ADS (Aim Down Sights)

```c
/* In port/src/studio/studio_ads.c */

/* Called per-frame when player holds ADS button */
void studioAdsUpdate(s32 player) {
    s32 weaponnum = bgunGetWeaponNum(player, HAND_RIGHT);
    if (!studioWeaponIsCustom(weaponnum)) return;
    
    studio_weapon_def_t *def = studioWeaponGetBySlot(weaponnum);
    if (!def) return;
    
    /* Apply zoom */
    if (def->ads.zoom_level > 1.0f) {
        /* Modify g_Vars.currentplayer->vv_fovzoom */
        f32 target_fov = g_Vars.currentplayer->fovy / def->ads.zoom_level;
        /* Smooth transition over 0.2 seconds */
    }
    
    /* Apply movement speed modifier */
    /* Multiply into movement speed factor in bondwalk.c */
    
    /* Apply spread reduction */
    /* Multiply into spread calculation in fire function */
}
```

### Auto-Aim (Magnetism)

Auto-aim modifies the aim axis values based on proximity to valid targets:

```c
/* Called after actionmapPollFrame(), before game logic reads aim values */
void studioAutoAimUpdate(s32 player) {
    s32 weaponnum = bgunGetWeaponNum(player, HAND_RIGHT);
    if (!studioWeaponIsCustom(weaponnum)) return;
    
    studio_weapon_def_t *def = studioWeaponGetBySlot(weaponnum);
    if (!def || def->autoaim.magnetism_strength <= 0.0f) return;
    
    /* Scale magnetism by input device */
    f32 mag = def->autoaim.magnetism_strength;
    if (actionmapGetLastDevice() == ACTIONMAP_DEVICE_KBM) {
        mag *= 0.3f;  /* MKB gets weaker auto-aim */
    }
    
    /* Find nearest enemy within autoaim range */
    f32 aim_x = actionValue(player, ACTION_AXIS_AIM_X);
    f32 aim_y = actionValue(player, ACTION_AXIS_AIM_Y);
    
    /* Project aim direction to find closest target screen position */
    struct chrdata *target = findNearestTargetInCone(player, def->autoaim.range);
    if (!target) return;
    
    /* Calculate correction vector toward target center mass */
    f32 target_screen_x, target_screen_y;
    worldToScreen(target->prop->pos, &target_screen_x, &target_screen_y);
    
    f32 correction_x = target_screen_x * mag * def->autoaim.snap_speed * dt;
    f32 correction_y = target_screen_y * mag * def->autoaim.snap_speed * dt;
    
    /* Apply correction to aim axis (modifies ACTION_AXIS_AIM_X/Y) */
    actionmapAddAimCorrection(player, correction_x, correction_y);
}
```

### Per-Weapon Configuration Examples

| Weapon Type | ADS Zoom | ADS Speed | Spread Reduction | Auto-Aim | Input Difference |
|-------------|----------|-----------|-----------------|----------|-----------------|
| Sniper | 4.0x | 0.3x | 0.1x | None | Same (precision weapon) |
| SMG | None | 1.0x | N/A | 0.4 | Controller: 0.4, MKB: 0.12 |
| Shotgun | None | 0.9x | N/A | 0.2 | Controller: 0.2, MKB: 0.06 |
| Needler | None | 1.0x | N/A | 0.3 | Controller: 0.3, MKB: 0.09 |
| Rocket | 1.5x | 0.5x | 0.5x | None | Same (splash weapon) |

---

## Cross-Cutting: Mod Integration

Everything created in Studio is a mod:

1. **Studio creates mod directory**: `mods/<mod_id>/` with `mod.json`
2. **Assets registered in catalog**: All with `category = mod_id`, `bundled = 0`
3. **Weapons**: `mods/<mod_id>/weapons/<name>.json` → parsed by `studioWeaponLoad()`
4. **Maps**: `mods/<mod_id>/maps/<name>/map.json` → parsed by `studioMapLoad()`
5. **Enable/Disable**: Standard `modmgrSetEnabled()` / `modmgrApplyChanges()`
6. **Network distribution**: `netdistrib.c` handles PDCA archive transfer
7. **Load order**: Mods override base game via catalog last-write-wins

### mod.json for Studio Content

```json
{
  "id": "mymod",
  "name": "My Custom Content",
  "version": "1.0.0",
  "author": "Player",
  "description": "Custom weapons and maps",
  "base_fallback": "base:dark_combat",
  "studio_version": 1,
  "content": {
    "weapons": ["weapons/needler.json"],
    "maps": ["maps/arena_custom/map.json"],
    "models": ["models/*.pdmesh"],
    "textures": ["textures/*.pdtex"],
    "audio": ["audio/*.pdaudio"]
  }
}
```

---

## Cross-Cutting: Performance

| Requirement | Target | Strategy |
|-------------|--------|----------|
| Imported mesh rendering | 60 fps with 100 objects | Standard GBI path; fast3d already handles this |
| Texture import max size | 2048x2048 RGBA32 | 16 MB per texture; GPU upload via `glTexImage2D` |
| Audio import max size | 10 MB source | Decoded to PCM in memory; ~40 MB for 10 MB OGG |
| Navmesh generation | Async, show progress | Background thread; ~2-10 seconds for typical map |
| Collision generation | Synchronous, <500ms | Simple triangle classification; no spatial partitioning needed for <10K tris |
| Editor viewport | 60 fps | Separate FBO render; no GBI overhead for editor grid/gizmos |
| Network transfer | Reasonable wait | zlib compression (existing); large maps may be 5-20 MB compressed |
| Projectile system | 200 concurrent | Pool-based; O(n) tick; capsule sweep per projectile |

### Memory Budget

| Asset Type | Per-Unit Memory | Max Count | Total Budget |
|-----------|----------------|-----------|-------------|
| Imported mesh | ~500 KB avg | 100 | 50 MB |
| Imported texture | up to 16 MB | 50 | ~200 MB (mipmapped: less) |
| Imported audio | ~5 MB avg | 30 | 150 MB |
| Weapon definitions | ~4 KB each | 64 | 256 KB |
| Projectile pool | ~128 B each | 200 | 25 KB |
| Map scene state | ~200 KB | 1 | 200 KB |
| Collision mesh | ~1 MB | 1 | 1 MB |
| Navmesh | ~500 KB | 1 | 500 KB |
| **Total** | | | **~400 MB peak** |

Modern PCs have 8-32 GB RAM. 400 MB is well within budget.

---

## Implementation Phases

| Phase | Name | Goal | Files to Create/Modify | Est. LOC | Est. Sessions | Dependencies | Acceptance Criteria |
|-------|------|------|----------------------|----------|---------------|-------------|-------------------|
| **S1** | Asset Import Core | Import OBJ meshes + PNG textures | `port/src/studio/studio_import.c`, `port/include/studio/studio_import.h`, vendor `tinyobj_loader_c.h`, `cgltf.h` | ~1,500 | 3 | None | OBJ file → GBI display list → renders in game; PNG → RGBA32 GPU texture |
| **S2** | Audio Import | Import WAV/OGG audio | `port/src/studio/studio_audio_import.c`, vendor `stb_vorbis.c` | ~600 | 1 | None | WAV/OGG → playable sound in engine |
| **S3** | Catalog Registration | Register imported assets + serialize .pd* formats | `port/src/studio/studio_register.c` | ~800 | 2 | S1, S2 | Imported assets appear in catalog with mod namespace; persist across restart |
| **S4** | Weapon Schema + Parser | JSON weapon def parser + weapon registry | `port/src/studio/studio_weapon.c`, `port/include/studio/studio_weapon.h` | ~1,200 | 3 | S3 | JSON weapon loads, appears in weapon select, fires with correct stats |
| **S5** | Projectile System | Data-driven projectile behaviors | `port/src/studio/studio_projectile.c`, `port/include/studio/studio_projectile.h`, modify `src/game/bondgun.c` | ~1,500 | 4 | S4 | Straight/arc/tracking/beam/spread projectiles work; accumulation works |
| **S6** | Studio UI Shell | ImGui Studio hub + Asset Browser + Model Viewer | `port/fast3d/pdgui_menu_studio.cpp`, `port/src/studio/studio_viewport.c` | ~1,200 | 3 | S3 | Studio accessible from main menu; browse catalog; 3D model viewer with orbit camera |
| **S7** | Weapon Editor UI | Visual weapon editor with live preview | extend `pdgui_menu_studio.cpp` | ~800 | 2 | S4, S6 | Edit weapon stats via sliders; save/load JSON; preview weapon model |
| **S8** | Map Editor Core | 3D map editor: mesh placement + gizmos | `port/src/studio/studio_map.c`, `port/include/studio/studio_map.h`, extend `pdgui_menu_studio.cpp` | ~2,000 | 5 | S6, S3 | Place meshes in 3D; translate/rotate/scale; save/load map.json |
| **S9** | Collision + Navmesh Gen | Auto-generate collision and navigation | `port/src/studio/studio_collision.c`, `port/src/studio/studio_navmesh.c` | ~1,500 | 4 | S8 | Generated collision works with capsuleSweep; bots navigate custom map |
| **S10** | Play Test Pipeline | Launch custom map from editor | modify `src/game/lv.c`, `src/game/bg.c` | ~500 | 2 | S8, S9 | Press F5 in editor → playing map with bots → return to editor |
| **S11** | ADS + Auto-Aim | Per-weapon aim assist | `port/src/studio/studio_ads.c`, modify `port/src/actionmap.cpp` | ~600 | 2 | S4 | ADS zoom works; auto-aim pulls toward targets; controller gets stronger assist |
| **S12** | Mod Packager | Export Studio content as .pdpack | extend `pdgui_menu_studio.cpp`, reuse `netdistrib.c` PDCA format | ~500 | 1 | S3, S6 | Select assets → export .pdpack → importable by other players |
| **S13** | Hot-Reload + Polish | Re-import without restart; UX polish | modify all studio files | ~400 | 2 | S1-S12 | Change source file → reimport → see update in editor instantly |
| **S14** | Network Integration | Studio mods distribute via netdistrib | modify `port/src/net/netdistrib.c`, `netmanifest.c` | ~300 | 1 | S12 | Host with custom weapon/map → client auto-downloads and plays |
| | **TOTAL** | | | **~13,400** | **~35** | | |

### Dependency Graph

```
S1 (Mesh Import) ──┐
                    ├── S3 (Catalog Reg) ──┬── S4 (Weapon Schema) ──┬── S5 (Projectile Sys) ── S11 (ADS)
S2 (Audio Import) ─┘                      │                        │
                                           │                        └── S7 (Weapon Editor UI)
                                           │
                                           ├── S6 (Studio UI Shell) ──┬── S7
                                           │                          ├── S8 (Map Editor) ── S9 (Collision/Nav) ── S10 (Play Test)
                                           │                          └── S12 (Mod Packager)
                                           │
                                           └── S12 ── S14 (Network)
                                           
S13 (Hot-Reload) depends on S1-S12
```

### Parallelizable Work

- **S1 + S2**: Mesh import and audio import can develop in parallel
- **S4 + S6**: Weapon schema and UI shell can develop in parallel (S3 shared dep)
- **S7 + S8**: Weapon editor UI and map editor core can develop in parallel (S6 shared dep)
- **S11**: ADS/Auto-Aim can develop any time after S4

---

## Architectural Decision Records

### ADR-S01: GBI Display Lists for Imported Meshes (vs. Direct OpenGL)

**Decision**: Convert imported meshes to N64 GBI display lists and render through the existing fast3d pipeline.

**Alternatives considered**:
- **Direct OpenGL VAO/VBO**: Simpler import path; bypasses GBI entirely
- **Hybrid**: GBI for base game, OpenGL for imported

**Trade-offs**:
| Factor | GBI Path | Direct OpenGL |
|--------|----------|--------------|
| Rendering consistency | Same pipeline as all game content | Separate render pass; potential Z-fighting, lighting mismatch |
| Implementation effort | Higher (must generate valid GBI) | Lower (standard mesh upload) |
| Shader compatibility | Inherits fast3d shaders | Needs separate shader |
| Material system | Uses existing N64 combiner modes | Needs new material definition |
| Network transfer size | Slightly larger (GBI overhead) | Slightly smaller |

**Rationale**: GBI path ensures imported meshes look identical to base game content — same lighting, same materials, same render order. The fast3d renderer is battle-tested. The conversion overhead is one-time at import.

### ADR-S02: JSON Weapon Definitions (vs. Binary / Lua / C Plugins)

**Decision**: JSON files for weapon definitions.

**Alternatives considered**:
- **Binary struct**: Faster to parse, smaller on disk
- **Lua scripting**: More expressive, supports custom logic
- **C plugin DLLs**: Maximum flexibility, direct engine access

**Trade-offs**:
| Factor | JSON | Lua | C Plugin |
|--------|------|-----|----------|
| Ease of creation | High (text editor) | Medium (needs Lua knowledge) | Low (needs C toolchain) |
| Expressiveness | Data only | Data + logic | Unlimited |
| Safety | Perfectly safe | Sandboxing needed | Unsafe (arbitrary code) |
| Parse speed | Fast (cJSON exists) | Moderate | N/A (compiled) |
| Network transfer | Small text | Small text | Platform-specific binary |

**Rationale**: JSON is the sweet spot for weapon definitions — sufficient expressiveness for stats/properties, safe by construction, trivially editable in any text editor, and small enough for network transfer. Complex behaviors (tracking, accumulation) are implemented as engine-side systems parameterized by the JSON data. If we ever need custom logic beyond what the projectile system supports, Lua can be added as a future layer on top of JSON definitions.

### ADR-S03: Single-Room Map Architecture (vs. Portal System)

**Decision**: Studio maps use a single room with no portal visibility culling.

**Alternatives considered**:
- **Auto-partition into rooms**: Analyze mesh connectivity, generate portal graph
- **Manual room painting**: Let user define room boundaries in editor

**Trade-offs**:
| Factor | Single Room | Auto-Partition | Manual Rooms |
|--------|-------------|---------------|-------------|
| Complexity | Trivial | Very high (BSP/convex decomp) | Medium (UX challenge) |
| Performance | GPU renders everything | Culls non-visible rooms | Culls non-visible rooms |
| Map size limit | ~100 objects before GPU bottleneck | Much larger maps | Much larger maps |
| Bot navigation | Simple navmesh | Per-room navmesh with door connections | Same |

**Rationale**: Modern GPUs can render 100+ objects at 60fps trivially. The portal system exists for N64's 4KB RSP DMEM limitation. For Studio's initial release, single-room is massively simpler and sufficient for multiplayer arenas (which are typically 20-50 objects). Room partitioning can be added in a future version if map creators need larger environments.

### ADR-S04: Collision via Geotile Conversion (vs. Mesh Collision)

**Decision**: Convert imported mesh triangles to `struct geotilef` format for integration with the existing collision system.

**Alternatives considered**:
- **Enable mesh collision system**: Use the existing but disabled `meshcollision.c`
- **New collision system**: Custom BVH or spatial hash

**Trade-offs**:
| Factor | Geotile Conversion | Mesh Collision | New System |
|--------|-------------------|----------------|-----------|
| Integration | Direct — existing cdTestVolume/capsuleSweep | Needs re-enablement + debugging | Full rewrite |
| Accuracy | Good for simple geometry | Better for complex meshes | Best |
| Effort | ~300 LOC | ~500 LOC (debug existing) | ~2,000 LOC |
| Risk | Low (proven system) | Medium (disabled for a reason) | High |

**Rationale**: The geotile format is what the game's collision system already consumes. Converting imported triangles to geotiles means `capsuleSweep()` and `cdTestVolume()` work unchanged. This avoids re-enabling the mesh collision system (which was disabled due to unresolved issues per `collision.md`). If mesh collision is re-enabled in a future session (it's marked HIGH PRIORITY), Studio collision can be migrated to it then.

### ADR-S05: Background Thread for Navmesh (vs. Coroutine / Frame-Sliced)

**Decision**: Navmesh generation runs in a separate OS thread.

**Alternatives considered**:
- **Frame-sliced**: Process N cells per frame, complete over many frames
- **Coroutine**: Cooperative multitasking within the main thread

**Trade-offs**:
| Factor | Thread | Frame-Sliced | Coroutine |
|--------|--------|-------------|-----------|
| Latency | 2-10 sec background | 30-60 sec (1 cell/frame) | Same as frame-sliced |
| Complexity | Mutex for shared state | Simple but slow | Moderate |
| UI responsiveness | Full 60fps during gen | Full 60fps | Full 60fps |
| Thread safety | Navmesh data written in thread, read after completion flag | No threading issues | No threading issues |

**Rationale**: Navmesh generation is CPU-intensive (voxelization + region growing). Frame-slicing would take unacceptably long. A background thread completes in seconds while the editor remains responsive. Thread safety is simple: the main thread only reads `navmesh_progress` (atomic float) during generation, and reads the completed navmesh data only after `navmesh_generating` is set to 0.

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| **GBI vertex format conversion produces visual artifacts** | Medium | High | Implement reference mesh (cube, sphere) first; validate vertex positions, UVs, normals against known-good renders. Fall back to wireframe preview if GBI conversion fails. |
| **Custom weapons cause desync in multiplayer** | Medium | High | All weapon behavior is deterministic (same code path on all clients). Server authoritative for damage. Weapon definitions are byte-identical across all clients (distributed via netdistrib). |
| **Navmesh generation produces unnavigable areas** | Medium | Medium | Visualize navmesh in editor (overlay on 3D view). Allow manual navpoint placement as fallback. Test with bot AI from the start — S5 should have a "bot plays custom map" validation step. |
| **Memory usage exceeds budget with many imported assets** | Low | Medium | Lazy loading: only load assets referenced by current map/match. Eviction: assets with `ref_count = 0` can be freed. Texture compression (DXT/BC) in future phase. |
| **Mod network transfer too slow for large maps** | Medium | Low | zlib compression already used. Can add per-asset hashing to skip already-transferred assets. Progress UI already exists in netdistrib. |
| **Editor UI overwhelms ImGui layout** | Low | Medium | Tab-based design keeps each tool focused. Docking (ImGui docking branch) can be added later for power users. |
| **Custom projectile tracking causes aim-bot-like gameplay** | Low | High | Server validates weapon definitions: cap tracking_strength, tracking_turn_rate, tracking_range to sane maximums. Match host can disable custom weapons. |
| **Integration with bondgun.c state machine is fragile** | Medium | High | Minimal modification to bondgun.c: single check `studioWeaponIsCustom()` gates into separate code path. Base weapon behavior untouched. |
| **Collision generation for complex meshes is inaccurate** | Medium | Medium | Test with known geometry (box rooms, ramps). Provide collision debug visualization (wireframe overlay of geotiles). Allow manual collision flag override per object. |
| **Save/load format changes break existing Studio mods** | Low | Medium | Version field in all serialized formats (.pdmesh v1, map.json v1). Migration code reads old versions. Never remove fields, only add. |

---

## Summary

PD2 Studio is a 14-phase, ~35-session project that adds five major capabilities:

1. **Asset Import** (S1-S3): OBJ/glTF meshes, PNG/TGA textures, WAV/OGG audio → engine formats → catalog
2. **Custom Weapons** (S4-S5): JSON-defined weapons with data-driven projectile behaviors
3. **Editor UI** (S6-S7): ImGui-based asset browser, weapon editor, model viewer
4. **Map Editor** (S8-S10): 3D mesh placement, collision generation, navmesh, play-test
5. **Integration** (S11-S14): ADS/auto-aim, mod packaging, hot-reload, network distribution

Every layer builds on existing systems: the Asset Catalog for identity, the mod system for packaging, netdistrib for transfer, ImGui for UI, and the action map for input. No existing system is replaced — Studio extends the platform.

/**
 * assetcatalog.h -- PC port asset catalog system
 *
 * String-keyed hash table for resolving game assets (maps, characters, skins,
 * weapons, etc.) by name. Replaces numeric array indexing with a flexible,
 * extensible catalog that supports both base game and mod content.
 *
 * Core features:
 * - FNV-1a hash table with linear probing (read-heavy, cache-friendly)
 * - CRC32 network identity for asset synchronization
 * - Registration API (catalogRegister*, convenience wrappers per type)
 * - Resolution API (catalogResolve*, catalogResolveByNetHash)
 * - Iteration API (by type, by category)
 * - Query API (has entry, is enabled, get skins for target)
 * - Dynamic growth (hash table, entry pool)
 * - Lifecycle (init, clear, clear mods)
 *
 * Usage:
 *   1. catalogInit() - once at startup, allocates hash table and entry pool
 *   2. catalogRegister*() - populate with assets
 *   3. catalogResolve() - lookup by ID
 *   4. catalogClear() - flush for full reload
 *   5. catalogClearMods() - remove only non-bundled assets
 *
 * Thread safety: Single-threaded. No locking. Call from main thread only.
 *
 * ADR-003 reference: context/ADR-003-asset-catalog-core.md
 */

#ifndef _IN_ASSETCATALOG_H
#define _IN_ASSETCATALOG_H

#include <PR/ultratypes.h>
#include "fs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants & Sizes
 * ======================================================================== */

#define CATALOG_ID_LEN       64     /* max length of asset ID string */
#define CATALOG_CATEGORY_LEN 64     /* max length of category string */

/* ========================================================================
 * Asset Type Enum
 * ======================================================================== */

typedef enum {
    ASSET_NONE = 0,
    ASSET_MAP,
    ASSET_CHARACTER,
    ASSET_SKIN,
    ASSET_BOT_VARIANT,
    ASSET_WEAPON,
    ASSET_TEXTURES,
    ASSET_SFX,
    ASSET_MUSIC,
    ASSET_PROP,
    ASSET_VEHICLE,
    ASSET_MISSION,
    ASSET_UI,
    ASSET_TOOL,
    ASSET_ARENA,
    ASSET_BODY,                /* MP body entry (base game g_MpBodies[] or mod) */
    ASSET_HEAD,                /* MP head entry (base game g_MpHeads[] or mod) */
    ASSET_ANIMATION,           /* animation set (body animation) */
    ASSET_TEXTURE,             /* individual texture entry (not a pack) */
    ASSET_GAMEMODE,            /* multiplayer game mode (scenario) */
    ASSET_AUDIO,               /* audio entry: SFX, music, or voice */
    ASSET_HUD,                 /* HUD element (crosshair, ammo display, radar, etc.) */
    ASSET_EFFECT,              /* visual effect: shader tint, glow, particle, screen-space */
    ASSET_TYPE_COUNT
} asset_type_e;

/* ========================================================================
 * Asset Sub-type Constants
 * ======================================================================== */

/* Map mode flag constants for ext.map.mode (bitmask) */
#define MAP_MODE_MP   (1 << 0)   /* playable in multiplayer (Combat Simulator) */
#define MAP_MODE_SOLO (1 << 1)   /* playable in solo/campaign */
#define MAP_MODE_COOP (1 << 2)   /* playable in co-op */

/* Audio category constants for ext.audio.category */
#define AUDIO_CAT_SFX   0   /* sound effect */
#define AUDIO_CAT_MUSIC 1   /* music track */
#define AUDIO_CAT_VOICE 2   /* voice / dialogue */

/* HUD element type constants for ext.hud.element_type */
#define HUD_ELEM_CROSSHAIR 0   /* aiming reticle */
#define HUD_ELEM_AMMO      1   /* ammo counter */
#define HUD_ELEM_RADAR     2   /* proximity radar */
#define HUD_ELEM_HEALTH    3   /* health bar */

/* Effect type constants for ext.effect.effect_type */
#define EFFECT_TYPE_TINT        0   /* color tint applied to vertices */
#define EFFECT_TYPE_GLOW        1   /* emissive glow on object */
#define EFFECT_TYPE_SHIMMER     2   /* animated shimmer/sparkle */
#define EFFECT_TYPE_DARKEN      3   /* darken/shadow entire scene */
#define EFFECT_TYPE_SCREEN      4   /* full-screen post-process */
#define EFFECT_TYPE_PARTICLE    5   /* particle emitter attached to target */

/* Effect target constants for ext.effect.target */
#define EFFECT_TARGET_SCENE     0   /* applies to full rendered scene */
#define EFFECT_TARGET_PLAYER    1   /* applies to a specific player */
#define EFFECT_TARGET_CHR       2   /* applies to a character/bot */
#define EFFECT_TARGET_PROP      3   /* applies to a prop/object */
#define EFFECT_TARGET_WEAPON    4   /* applies to a weapon model */
#define EFFECT_TARGET_LEVEL     5   /* applies to all level geometry */
#define HUD_ELEM_TIMER     4   /* game timer */
#define HUD_ELEM_SCORE     5   /* score display */

/* ========================================================================
 * Asset Load State
 * ======================================================================== */

/**
 * Lifecycle state of an asset entry.
 *
 * REGISTERED  -- entry exists in catalog, not yet enabled or loaded
 * ENABLED     -- user/system has enabled the asset; eligible for loading
 * LOADED      -- asset data is resident in memory (loaded_data != NULL)
 * ACTIVE      -- asset is actively referenced by the running game
 *
 * Bundled (base game) assets are initialized at LOADED with
 * ref_count = ASSET_REF_BUNDLED and are never evicted.
 */
typedef enum {
    ASSET_STATE_REGISTERED = 0,
    ASSET_STATE_ENABLED,
    ASSET_STATE_LOADED,
    ASSET_STATE_ACTIVE
} asset_load_state_t;

/** Sentinel ref_count for bundled assets: never evicted from memory. */
#define ASSET_REF_BUNDLED 0x7FFFFFFF

/* ========================================================================
 * Asset Entry Structure
 * ======================================================================== */

typedef struct asset_entry {
    /* Identity */
    char id[CATALOG_ID_LEN];           /* "gf64_bond", "base:joanna_dark" */
    u32  id_hash;                      /* FNV-1a of id (hash table slot) */
    u32  net_hash;                     /* CRC32 of id (network identity) */

    /* Classification */
    asset_type_e type;                 /* ASSET_MAP, ASSET_CHARACTER, etc. */
    char category[CATALOG_CATEGORY_LEN]; /* mod id or "base" for ROM assets */

    /* Filesystem */
    char dirpath[FS_MAXPATH];          /* absolute path to component folder */

    /* Common metadata */
    f32  model_scale;                  /* from .ini (default 1.0) */
    s32  enabled;                      /* bool: user toggle (s32, not stdbool) */
    s32  temporary;                    /* bool: session-only download */
    s32  bundled;                      /* bool: shipped with game */

    /* Runtime binding */
    s32  runtime_index;                /* index in relevant runtime array */
                                       /* (g_Stages, g_HeadsAndBodies, etc.) */

    /* Type-specific extension (union keeps entry size bounded) */
    union {
        struct {
            s32 stagenum;              /* logical stage ID (e.g. 0x5e) */
            s32 mode;                  /* mp, solo, coop (flags or bitmask) */
            char music_file[FS_MAXPATH];
        } map;
        struct {
            char bodyfile[FS_MAXPATH];
            char headfile[FS_MAXPATH];
        } character;
        struct {
            char target_id[CATALOG_ID_LEN];  /* soft reference to target char */
        } skin;
        struct {
            char base_type[32];        /* "NormalSim", "DarkSim", etc. */
            f32 accuracy;
            f32 reaction_time;
            f32 aggression;
        } bot_variant;
        struct {
            s32 stagenum;              /* logical stage ID this arena loads */
            u8  requirefeature;        /* unlock check (0 = always available) */
            s32 name_langid;           /* language string ID for display name */
        } arena;
        struct {
            s16 bodynum;               /* global body ID in g_HeadsAndBodies[] */
            s16 name_langid;           /* language string ID for display name */
            s16 headnum;               /* default head ID for this body */
            u8  requirefeature;        /* unlock check (0 = always available) */
        } body;
        struct {
            s16 headnum;               /* global head ID in g_HeadsAndBodies[] */
            u8  requirefeature;        /* unlock check (0 = always available) */
        } head;
        struct {
            s32 weapon_id;             /* MPWEAPON_* constant */
            char name[64];             /* human-readable display name */
            char model_file[128];      /* model file path (empty for base game) */
            f32  damage;               /* base damage value (0 = unknown) */
            f32  fire_rate;            /* rounds per second (0 = unknown) */
            s32  ammo_type;            /* ammo category constant */
            s32  dual_wieldable;       /* bool: can be dual-wielded */
        } weapon;
        struct {
            s32 anim_id;               /* animation table index */
            char name[64];             /* human-readable display name */
            s32 frame_count;           /* number of frames (0 = unknown) */
            char target_body[64];      /* body type this animation targets (empty = generic) */
        } anim;
        struct {
            s32 texture_id;            /* texture table index */
            s32 width;                 /* width in pixels (0 = unknown) */
            s32 height;                /* height in pixels (0 = unknown) */
            s32 format;                /* texture format constant (0 = unknown) */
            char file_path[128];       /* path to texture file */
        } texture;
        struct {
            s32 prop_type;             /* PROPTYPE_* constant */
            char name[64];             /* human-readable display name */
            char model_file[128];      /* model file path (empty for base game) */
            u32  flags;                /* prop flags bitmask */
            f32  health;               /* base health value (0 = indestructible) */
        } prop;
        struct {
            s32 mode_id;               /* MPSCENARIO_* constant */
            char name[64];             /* human-readable display name */
            char description[256];     /* longer description for UI */
            s32 min_players;           /* minimum players required */
            s32 max_players;           /* maximum players supported */
            s32 team_based;            /* bool: requires teams */
        } gamemode;
        struct {
            s32 sound_id;              /* SFX enum value or music track index */
            char name[64];             /* human-readable display name */
            s32 category;              /* AUDIO_CAT_SFX / AUDIO_CAT_MUSIC / AUDIO_CAT_VOICE */
            s32 duration_ms;           /* duration in milliseconds (0 = unknown) */
            char file_path[128];       /* path to audio file (empty = ROM-embedded) */
        } audio;
        struct {
            s32 hud_id;                /* HUD element ID */
            char name[64];             /* human-readable display name */
            s32 element_type;          /* HUD_ELEM_* constant */
            char texture_file[128];    /* texture file path (empty = uses default) */
        } hud;
        struct {
            char name[64];             /* human-readable display name */
            s32 effect_type;           /* EFFECT_TYPE_* constant */
            s32 target;                /* EFFECT_TARGET_* constant */
            char shader_id[64];        /* shader identifier for the renderer */
            f32 intensity;             /* effect strength 0.0-1.0 */
            f32 params[4];             /* generic effect parameters */
        } effect;
    } ext;

    /* Source numeric IDs for reverse-index (C-4 through C-7).
     * Set during registration. -1 means "not applicable to this asset type".
     * Base game bundled entries carry the ROM index they occupy.
     * Mod entries carry the ROM index they override (so the intercept can
     * redirect that filenum to the mod's file path). */
    s32 source_filenum;    /* ROM fileSlots[] index, or -1 */
    s32 source_texnum;     /* ROM textures table index, or -1 */
    s32 source_animnum;    /* ROM animations table index, or -1 */
    s32 source_soundnum;   /* ROM sounds table index, or -1 */

    /* Load state tracking (MEM-1) */
    asset_load_state_t load_state;     /* lifecycle state of this entry */
    void              *loaded_data;    /* pointer to loaded asset data (NULL if not loaded) */
    u32                data_size_bytes;/* size of loaded_data in bytes (0 if not loaded) */
    s32                ref_count;      /* reference count; ASSET_REF_BUNDLED = never evict */

    /* Catalog internals */
    s32 occupied;                      /* bool: hash table slot in use */
} asset_entry_t;

/* ========================================================================
 * Lifecycle API
 * ======================================================================== */

/**
 * Initialize the asset catalog.
 * Allocates hash table (2048 slots) and entry pool (512 entries).
 * Call once during startup, before any registration.
 * Safe to call multiple times (clears state, reallocs).
 */
void assetCatalogInit(void);

/**
 * Clear all entries and reset hash table.
 * Does not free memory (reuses for next population).
 * Call before a full catalog reload.
 */
void assetCatalogClear(void);

/**
 * Remove all entries where bundled == false.
 * Rehashes remaining entries.
 * Call when disabling/toggling mods (partial reload).
 */
void assetCatalogClearMods(void);

/**
 * Get total number of registered entries (base + mods).
 */
s32 assetCatalogGetCount(void);

/**
 * Get number of entries of a specific type.
 * Returns 0 if type is invalid or has no entries.
 */
s32 assetCatalogGetCountByType(asset_type_e type);

/**
 * Direct pool access by pool index.
 * Used by assetcatalog_load.c for reverse-index iteration.
 * Returns NULL if index is out of range or entry is not occupied.
 */
const asset_entry_t *assetCatalogGetByIndex(s32 index);

/**
 * Mutable resolve by string ID.
 * Used exclusively by the lifecycle layer (assetcatalog_load.c) to update
 * loaded_data, data_size_bytes, and ref_count.  Callers must not modify
 * identity or classification fields (id, type, bundled, etc.).
 * Returns NULL if the entry is not found or the catalog is not initialised.
 */
asset_entry_t *assetCatalogGetMutable(const char *id);

/* ========================================================================
 * Registration API
 * ======================================================================== */

/**
 * Register a single asset with minimal fields.
 * Computes both id_hash (FNV-1a) and net_hash (CRC32).
 * If ID already exists (by string match), overwrites it (last-write-wins).
 * Returns pointer to entry, or NULL on allocation failure.
 * Caller should set type-specific union fields via the entry pointer.
 */
asset_entry_t *assetCatalogRegister(const char *id, asset_type_e type);

/**
 * Register a map asset.
 * Convenience wrapper that calls catalogRegister() and sets ext.map fields.
 * Returns entry pointer or NULL.
 */
asset_entry_t *assetCatalogRegisterMap(const char *id, s32 stagenum,
                                        const char *dirpath);

/**
 * Register a character asset.
 * Convenience wrapper that sets ext.character fields.
 */
asset_entry_t *assetCatalogRegisterCharacter(const char *id,
                                             const char *bodyfile,
                                             const char *headfile);

/**
 * Register a skin asset.
 * Convenience wrapper that sets ext.skin.target_id.
 */
asset_entry_t *assetCatalogRegisterSkin(const char *id,
                                        const char *target_id);

/**
 * Register a bot variant asset.
 * Convenience wrapper that sets ext.bot_variant fields.
 */
asset_entry_t *assetCatalogRegisterBotVariant(const char *id,
                                              const char *base_type,
                                              f32 accuracy,
                                              f32 reaction_time,
                                              f32 aggression);

/**
 * Register an arena asset.
 * Convenience wrapper that sets ext.arena fields.
 * An arena is a stage reference used in the MP arena selection menu,
 * with an unlock requirement and a display name (language string ID).
 */
asset_entry_t *assetCatalogRegisterArena(const char *id, s32 stagenum,
                                          u8 requirefeature, s32 name_langid);

/**
 * Register a body asset.
 * Convenience wrapper that sets ext.body fields.
 * A body is an MP-selectable character body with a display name,
 * a default head, and an optional unlock requirement.
 */
asset_entry_t *assetCatalogRegisterBody(const char *id, s16 bodynum,
                                         s16 name_langid, s16 headnum,
                                         u8 requirefeature);

/**
 * Register a head asset.
 * Convenience wrapper that sets ext.head fields.
 * A head is an MP-selectable character head with an optional unlock requirement.
 */
asset_entry_t *assetCatalogRegisterHead(const char *id, s16 headnum,
                                         u8 requirefeature);

/**
 * Register a weapon asset.
 * Convenience wrapper that sets ext.weapon fields.
 * weapon_id should be an MPWEAPON_* constant.
 * model_file, damage, fire_rate may be NULL/"" / 0.0f for base game entries.
 */
asset_entry_t *assetCatalogRegisterWeapon(const char *id, s32 weapon_id,
                                           const char *name,
                                           const char *model_file,
                                           f32 damage, f32 fire_rate,
                                           s32 ammo_type, s32 dual_wieldable);

/**
 * Register an animation asset.
 * Convenience wrapper that sets ext.anim fields.
 * anim_id is the index in the animation table (matches animations.json order).
 * target_body may be NULL or "" for generic animations.
 */
asset_entry_t *assetCatalogRegisterAnimation(const char *id, s32 anim_id,
                                              const char *name,
                                              s32 frame_count,
                                              const char *target_body);

/**
 * Register an individual texture asset.
 * Convenience wrapper that sets ext.texture fields.
 * Distinct from ASSET_TEXTURES (texture pack): this is one named texture.
 * width, height, format may be 0 for base game entries (loaded from ROM).
 */
asset_entry_t *assetCatalogRegisterTexture(const char *id, s32 texture_id,
                                            s32 width, s32 height, s32 format,
                                            const char *file_path);

/**
 * Register a prop asset.
 * Convenience wrapper that sets ext.prop fields.
 * prop_type should be a PROPTYPE_* constant.
 * model_file may be NULL/"" for base game entries.
 */
asset_entry_t *assetCatalogRegisterProp(const char *id, s32 prop_type,
                                         const char *name,
                                         const char *model_file,
                                         u32 flags, f32 health);

/**
 * Register a textures asset (texture pack / replacement set).
 * No type-specific fields — dirpath and category are sufficient.
 */
asset_entry_t *assetCatalogRegisterTextures(const char *id);

/**
 * Register an SFX asset (sound effect pack).
 * No type-specific fields — dirpath and category are sufficient.
 */
asset_entry_t *assetCatalogRegisterSfx(const char *id);

/**
 * Register a game mode asset.
 * Convenience wrapper that sets ext.gamemode fields.
 * mode_id should be an MPSCENARIO_* constant.
 * description may be NULL/"" for terse entries.
 */
asset_entry_t *assetCatalogRegisterGameMode(const char *id, s32 mode_id,
                                             const char *name,
                                             const char *description,
                                             s32 min_players, s32 max_players,
                                             s32 team_based);

/**
 * Register an audio asset.
 * Convenience wrapper that sets ext.audio fields.
 * category: AUDIO_CAT_SFX, AUDIO_CAT_MUSIC, or AUDIO_CAT_VOICE.
 * file_path may be NULL/"" for ROM-embedded sounds.
 */
asset_entry_t *assetCatalogRegisterAudio(const char *id, s32 sound_id,
                                          const char *name, s32 category,
                                          s32 duration_ms,
                                          const char *file_path);

/**
 * Register a HUD element asset.
 * Convenience wrapper that sets ext.hud fields.
 * element_type: HUD_ELEM_CROSSHAIR, HUD_ELEM_AMMO, etc.
 * texture_file may be NULL/"" for elements using the default renderer.
 */
asset_entry_t *assetCatalogRegisterHud(const char *id, s32 hud_id,
                                        const char *name, s32 element_type,
                                        const char *texture_file);

/* ========================================================================
 * Resolution API
 * ======================================================================== */

/**
 * Resolve an asset by string ID.
 * Computes FNV-1a hash, probes hash table, verifies full string match.
 * Returns const pointer to entry, or NULL if not found or not enabled.
 * Pointer valid until next catalogClear() or pool realloc.
 */
const asset_entry_t *assetCatalogResolve(const char *id);

/**
 * Resolve and return the runtime_index for a character asset.
 * Returns runtime_index if found, or -1 if not found / not enabled.
 */
s32 assetCatalogResolveBodyIndex(const char *id);

/**
 * Resolve and return the runtime_index for a map asset.
 * Returns runtime_index if found, or -1 if not found / not enabled.
 */
s32 assetCatalogResolveStageIndex(const char *id);

/**
 * Resolve an asset by CRC32 network hash.
 * Linear scan of entry pool (infrequent, connection-time only).
 * Returns const pointer to first entry with matching net_hash, or NULL.
 */
const asset_entry_t *assetCatalogResolveByNetHash(u32 net_hash);

/* ========================================================================
 * Iteration API
 * ======================================================================== */

/**
 * Callback signature for iteration functions.
 * Called once per matching entry. entry pointer valid during callback only.
 */
typedef void (*asset_iter_fn)(const asset_entry_t *entry, void *userdata);

/**
 * Iterate all entries of a specific asset type.
 * Calls fn for each entry. userdata is passed through unchanged.
 */
void assetCatalogIterateByType(asset_type_e type, asset_iter_fn fn,
                                void *userdata);

/**
 * Iterate all entries with a specific category string.
 * Category matching is exact (case-sensitive).
 */
void assetCatalogIterateByCategory(const char *category, asset_iter_fn fn,
                                    void *userdata);

/* ========================================================================
 * Query API
 * ======================================================================== */

/**
 * Check if an asset is registered (enabled or not).
 */
s32 assetCatalogHasEntry(const char *id);

/**
 * Check if an asset is registered and enabled.
 */
s32 assetCatalogIsEnabled(const char *id);

/**
 * Get all skin assets that target a specific character.
 * Iterates catalog looking for ASSET_SKIN entries with ext.skin.target_id
 * matching the given target_id.
 * Fills out[] with up to maxout entry pointers.
 * Returns number of skins found (may be 0).
 */
s32 assetCatalogGetSkinsForTarget(const char *target_id,
                                   const asset_entry_t **out, s32 maxout);

/* ========================================================================
 * Write API (D3R-6)
 * ======================================================================== */

/**
 * Set the enabled state of an asset entry by string ID.
 * Does nothing if the ID is not found or the catalog is not initialized.
 * This is the only write operation exposed outside the catalog internals.
 * Note: base game (bundled) entries can be disabled via this call for
 * temporary UI purposes, but they re-enable on catalog reset/reload.
 */
void assetCatalogSetEnabled(const char *id, s32 enabled);

/**
 * Enumerate unique category strings across all registered entries.
 * Fills out[][CATALOG_CATEGORY_LEN] with up to maxout distinct strings.
 * Skips the "base" category and empty categories (not user-manageable).
 * Returns number of unique categories found (may be 0 if no mod entries).
 *
 * Typical usage: build the "By Mod" tree in the Mod Manager UI.
 */
s32 assetCatalogGetUniqueCategories(char out[][CATALOG_CATEGORY_LEN], s32 maxout);

/* ========================================================================
 * Load State API (MEM-1)
 * ======================================================================== */

/**
 * Get the current load state of an asset entry by string ID.
 * Returns ASSET_STATE_REGISTERED if the ID is not found.
 */
asset_load_state_t assetCatalogGetLoadState(const char *id);

/**
 * Set the load state of an asset entry by string ID.
 * Does nothing if the ID is not found or the catalog is not initialized.
 * Callers should use this to advance an entry through the lifecycle
 * (ENABLED → LOADED → ACTIVE) as asset data is managed.
 * Note: bundled entries have ref_count = ASSET_REF_BUNDLED; callers must
 * not decrement below that sentinel or force eviction of bundled data.
 */
void assetCatalogSetLoadState(const char *id, asset_load_state_t state);

/* ========================================================================
 * SA-2: Modular Catalog API Layer
 * ======================================================================== */

/* Forward declaration for wire helper signatures (defined in net/netbuf.h). */
struct netbuf;

/**
 * Result struct for body asset resolution.
 * filenum is populated from source_filenum (set at registration from
 * g_HeadsAndBodies[bodynum].filenum).
 * display_name points into the catalog entry id[] -- stable for catalog lifetime.
 * session_id is 0 if the session catalog is not active or entry is absent.
 */
typedef struct {
    const asset_entry_t *entry;        /**< full catalog entry (NULL on failure) */
    s32                  filenum;      /**< runtime filenum for model load calls */
    f32                  model_scale;  /**< from catalog entry (default 1.0) */
    const char          *display_name; /**< points to entry->id */
    u32                  net_hash;     /**< CRC32 for manifest checks */
    u16                  session_id;   /**< session wire ID (0 = not in session) */
} catalog_body_result_t;

/** Heads share the same result layout as bodies. */
typedef catalog_body_result_t catalog_head_result_t;

/**
 * Result struct for stage (map) asset resolution.
 * bgfileid/padsfileid/setupfileid come from g_Stages[runtime_index] when
 * the stage table is loaded (client); all -1 on server or unloaded stages.
 */
typedef struct {
    const asset_entry_t *entry;
    s32                  bgfileid;
    s32                  padsfileid;
    s32                  setupfileid;
    s32                  stagenum;    /**< logical stage ID (e.g. 0x5e) */
    u32                  net_hash;
    u16                  session_id;
} catalog_stage_result_t;

/** Result struct for weapon asset resolution. */
typedef struct {
    const asset_entry_t *entry;
    s32                  filenum;     /**< weapon model file (source_filenum, -1 for base) */
    s32                  weapon_num;  /**< runtime WEAPON_* enum value */
    u32                  net_hash;
    u16                  session_id;
} catalog_weapon_result_t;

/** Result struct for prop asset resolution. */
typedef struct {
    const asset_entry_t *entry;
    s32                  filenum;    /**< prop model file (source_filenum, -1 for base) */
    s32                  prop_type;  /**< runtime PROPTYPE_* value */
    u32                  net_hash;
    u16                  session_id;
} catalog_prop_result_t;

/* ── SA-2: Resolution by catalog string ID ─────────────────────────────── */

/** Resolve a body asset by catalog string ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveBody(const char *id, catalog_body_result_t *out);

/** Resolve a head asset by catalog string ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveHead(const char *id, catalog_head_result_t *out);

/** Resolve a stage (map) asset by catalog string ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveStage(const char *id, catalog_stage_result_t *out);

/** Resolve a weapon asset by catalog string ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveWeapon(const char *id, catalog_weapon_result_t *out);

/** Resolve a prop asset by catalog string ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveProp(const char *id, catalog_prop_result_t *out);

/* ── SA-2: Resolution by session wire ID ───────────────────────────────── */

/** Resolve a body asset by session wire ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveBodyBySession(u16 session_id, catalog_body_result_t *out);

/** Resolve a head asset by session wire ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveHeadBySession(u16 session_id, catalog_head_result_t *out);

/** Resolve a stage (map) asset by session wire ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveStageBySession(u16 session_id, catalog_stage_result_t *out);

/** Resolve a weapon asset by session wire ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveWeaponBySession(u16 session_id, catalog_weapon_result_t *out);

/** Resolve a prop asset by session wire ID. Returns 1 on success, 0 on failure. */
s32 catalogResolvePropBySession(u16 session_id, catalog_prop_result_t *out);

/* ── SA-2: Resolution by CRC32 net_hash ────────────────────────────────── */

/**
 * Resolve an asset entry by CRC32 net_hash.
 * Thin wrapper around assetCatalogResolveByNetHash() -- O(n) linear scan.
 * Use sparingly (manifest checks, connection-time only).
 */
const asset_entry_t *catalogResolveByNetHash(u32 net_hash);

/* ── SA-4: Reverse-index lookup (migration only) ───────────────────────── */

/**
 * Reverse-lookup: find catalog entry by asset type and runtime_index.
 * Used only during save-file migration (SA-4) to convert legacy integer
 * indices to catalog string IDs.  O(n) linear scan -- never call on the
 * hot path.  Logs [CATALOG-ASSERT] and returns NULL if not found.
 *
 * @param type           Asset type (ASSET_BODY, ASSET_HEAD, ASSET_MAP, etc.)
 * @param runtime_index  The integer index stored in asset_entry_t.runtime_index
 * @return  Pointer to the catalog ID string (valid for catalog lifetime), or NULL.
 */
const char *catalogResolveByRuntimeIndex(asset_type_e type, s32 runtime_index);

/* ── SA-5a: Load-site helpers ───────────────────────────────────────────── */

/**
 * SA-5a: Resolve a body model filenum by runtime body index.
 * Mod-override-aware drop-in for g_HeadsAndBodies[bodynum].filenum at model
 * load call sites.  Performs an O(n) catalog scan -- acceptable at load time
 * (called once at match/stage start, not per frame).
 * Falls back to g_HeadsAndBodies[bodynum].filenum on catalog miss so base game
 * behaviour is unchanged when the catalog is not yet populated.
 */
s32 catalogGetBodyFilenumByIndex(s32 bodynum);

/**
 * SA-5a: Resolve a head model filenum by runtime head index.
 * Mod-override-aware drop-in for g_HeadsAndBodies[headnum].filenum at model
 * load call sites.  Same O(n) / fallback behaviour as catalogGetBodyFilenumByIndex.
 */
s32 catalogGetHeadFilenumByIndex(s32 headnum);

/* ── SA-2: Wire helpers ─────────────────────────────────────────────────── */

/**
 * Write a 2-byte session asset reference to a network buffer.
 * The ONLY function that may serialize asset references onto the wire.
 */
void catalogWriteAssetRef(struct netbuf *buf, u16 session_id);

/**
 * Read a 2-byte session asset reference from a network buffer.
 * The ONLY function that may deserialize asset references from the wire.
 * Returns the session wire ID (0 = no asset / not assigned).
 */
u16 catalogReadAssetRef(struct netbuf *buf);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ASSETCATALOG_H */

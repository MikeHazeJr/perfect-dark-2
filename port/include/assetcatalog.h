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
    ASSET_TYPE_COUNT
} asset_type_e;

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
    char category[CATALOG_CATEGORY_LEN]; /* "goldfinger64", "base", etc. */

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
    } ext;

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

#ifdef __cplusplus
}
#endif

#endif /* _IN_ASSETCATALOG_H */

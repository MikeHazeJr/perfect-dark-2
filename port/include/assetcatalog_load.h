/**
 * assetcatalog_load.h -- Catalog intercept layer (C-4 through C-7)
 *
 * Owns the reverse-index maps (filenum/texnum/animnum/soundnum → catalog pool
 * index) and the intercept query functions called from the four gateway
 * functions.
 *
 * Usage:
 *   1. Call assetCatalogInit() + assetCatalogRegisterBaseGame() + ScanComponents()
 *   2. Call catalogLoadInit() — builds reverse-index from the populated catalog
 *   3. Gateway functions call catalogGetFileOverride() etc. on each load
 *   4. On mod enable/disable: assetCatalogClearMods() → re-scan → catalogLoadInit()
 *
 * Thread safety: single-threaded, main thread only (matches the catalog).
 */

#ifndef _IN_ASSETCATALOG_LOAD_H
#define _IN_ASSETCATALOG_LOAD_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Initialization
 * ======================================================================== */

/**
 * Build reverse-index maps from the populated catalog.
 *
 * Scans ALL occupied entries (bundled base-game entries AND enabled mod
 * overrides) and records filenum/texnum/animnum/soundnum → poolIdx mappings
 * so catalogResolveFile() etc. can answer in O(1).
 *
 * Mod overrides (non-bundled, enabled) take priority: if both a bundled and
 * a non-bundled entry map to the same ID, the last (highest pool index) wins.
 * Since base-game entries are registered first and mod entries afterward,
 * mod entries naturally land at higher pool indices and win.
 *
 * Must be called once after catalog population, before any asset loads.
 * Must be called again after any assetCatalogClearMods() + re-scan cycle.
 */
void catalogLoadInit(void);

/* ========================================================================
 * Resolve Result  (primary query result type)
 * ======================================================================== */

/**
 * Returned by all catalogResolve*() functions.
 *
 * path           - mod file path to load from, or NULL (use ROM)
 * catalog_id     - pool index of the matching catalog entry, or -1 if this
 *                  asset is unknown to the catalog
 * is_mod_override - 1 if a non-bundled (mod) entry owns this asset and
 *                  should be loaded from `path`; 0 for base-game ROM assets
 *
 * Decision matrix for callers:
 *   is_mod_override=1, path!=NULL  → load from path
 *   is_mod_override=0, catalog_id>=0 → load from ROM (base-game, cataloged)
 *   is_mod_override=0, catalog_id<0  → load from ROM (not in catalog at all)
 */
typedef struct {
    const char *path;       /* mod file path, or NULL for ROM */
    s32         catalog_id; /* catalog entry pool index, -1 = unknown */
    s32         is_mod_override; /* 1 = mod file, 0 = base-game ROM */
} CatalogResolveResult;

/* ========================================================================
 * Resolve API  (primary interface — use these in new/updated callers)
 * ======================================================================== */

/**
 * C-4: Resolve a ROM filenum to a catalog decision.
 * Returns a CatalogResolveResult describing how to load the asset.
 * Increments the file query counter (same counter as catalogGetFileOverride).
 */
CatalogResolveResult catalogResolveFile(s32 filenum);

/**
 * C-5: Resolve a ROM texnum to a catalog decision.
 */
CatalogResolveResult catalogResolveTexture(s32 texnum);

/**
 * C-6: Resolve a ROM animnum to a catalog decision.
 */
CatalogResolveResult catalogResolveAnim(s32 animnum);

/**
 * C-7: Resolve a ROM soundnum to a catalog decision.
 */
CatalogResolveResult catalogResolveSound(s32 soundnum);

/* ========================================================================
 * Legacy Override Queries  (thin wrappers — kept for backward compatibility)
 *
 * These call the corresponding catalogResolve*() function and return just
 * the path (NULL for ROM, non-NULL for mod override).  New callers should
 * use the resolve API directly to get full catalog context.
 * ======================================================================== */

/**
 * C-4: Returns the mod file path if a non-bundled catalog entry overrides
 * this ROM filenum. Returns NULL if no mod override exists.
 *
 * Wrapper: equivalent to calling catalogResolveFile(filenum).path when
 * catalogResolveFile(filenum).is_mod_override == 1.
 */
const char *catalogGetFileOverride(s32 filenum);

/**
 * C-5: Returns the mod texture file path if a non-bundled catalog entry
 * overrides this ROM texnum. Returns NULL if no mod override exists.
 */
const char *catalogGetTextureOverride(s32 texnum);

/**
 * C-6: Returns the mod animation file path if a non-bundled catalog entry
 * overrides this ROM animnum. Returns NULL if no mod override exists.
 */
const char *catalogGetAnimOverride(s32 animnum);

/**
 * C-7: Returns the mod sound file path if a non-bundled catalog entry
 * overrides this ROM soundnum. Returns NULL if no mod override exists.
 */
const char *catalogGetSoundOverride(s32 soundnum);

/* ========================================================================
 * MEM-2: Asset Lifecycle API
 * ======================================================================== */

/**
 * Load an asset into memory and advance its load state to LOADED.
 *
 * Bundled (base-game) entries are already ROM-resident; this is a no-op
 * retain for them (returns 1 immediately).
 *
 * For non-bundled entries: resolves the file path from the catalog entry,
 * calls fsFileLoad(), stores data in entry->loaded_data, and sets
 * load_state = ASSET_STATE_LOADED.  Increments ref_count on every call.
 *
 * If the entry is already LOADED (from a prior call) only the ref_count
 * is incremented — no double-load.
 *
 * Returns 1 on success, 0 on error (entry not found, not enabled, or
 * file load failure).
 *
 * Log prefix: "CATALOG:" for bundled, "MOD:" for mod assets.
 */
s32 catalogLoadAsset(const char *assetId);

/**
 * Decrement ref_count for a loaded asset.
 *
 * When ref_count reaches 0 and the entry is not bundled, the loaded data
 * is freed (sysMemFree) and the entry reverts to ASSET_STATE_ENABLED.
 * Bundled entries (ASSET_REF_BUNDLED sentinel) are never freed.
 */
void catalogUnloadAsset(const char *assetId);

/**
 * Increment ref_count without triggering a load.
 * The entry must already be at ASSET_STATE_LOADED or higher.
 * Used when transferring ownership of a loaded asset (e.g., stage diff
 * marks an asset as shared between old and new stage).
 * No-op for bundled entries.
 */
void catalogRetainAsset(const char *assetId);

/**
 * Log intercept query counters (file/tex/anim/snd) at LOG_NOTE.
 * Call at shutdown or on demand to confirm the intercept layer is active.
 */
void catalogLoadLogStats(void);

/* ========================================================================
 * MEM-3 / C-9: Stage Transition Diff
 * ======================================================================== */

/**
 * Compute which assets to load and unload for a stage transition.
 *
 * Collects all non-bundled, enabled catalog entries whose category matches
 * the map entry identified by newStageId.  Compares against all currently
 * LOADED non-bundled entries to produce two disjoint sets.
 *
 * @param newStageId   Catalog asset ID of the destination map (e.g.,
 *                     "gf64_map_caverns").  Pass NULL when transitioning
 *                     to a base-game-only stage — all loaded mod assets
 *                     are placed in toUnload and toLoad is empty.
 * @param toLoad       Output array; filled with asset IDs that need loading.
 *                     Pointers are into catalog entry id[] — valid for the
 *                     lifetime of the catalog (until next catalogLoadInit).
 * @param loadCount    Output: number of entries written to toLoad.
 * @param toUnload     Output array; filled with asset IDs that need unload.
 * @param unloadCount  Output: number of entries written to toUnload.
 * @param maxItems     Capacity of toLoad and toUnload arrays (same limit).
 * @return             Total number of changes (loadCount + unloadCount).
 *                     Returns -1 on invalid arguments.
 *
 * Load order in toLoad respects the stage-load sequence:
 *   ASSET_MAP first, then characters, then everything else.
 * Caller drives the actual catalogLoadAsset / catalogUnloadAsset calls.
 */
s32 catalogComputeStageDiff(const char *newStageId,
                            const char **toLoad,  s32 *loadCount,
                            const char **toUnload, s32 *unloadCount,
                            s32 maxItems);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ASSETCATALOG_LOAD_H */

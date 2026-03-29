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
 * Scans all non-bundled, enabled entries in the entry pool. For each entry
 * with source_filenum >= 0, records that filenum → poolIdx mapping so
 * catalogGetFileOverride() can answer in O(1).
 *
 * Must be called once after catalog population, before any asset loads.
 * Must be called again after any assetCatalogClearMods() + re-scan cycle.
 */
void catalogLoadInit(void);

/* ========================================================================
 * Intercept Queries  (called from gateway functions)
 * ======================================================================== */

/**
 * C-4: Returns the mod file path if a non-bundled catalog entry overrides
 * this ROM filenum. Returns NULL if no mod override exists.
 *
 * Returned pointer is into the catalog entry's bodyfile/headfile field and
 * is valid for the lifetime of the catalog (until catalogLoadInit is called
 * again after a ClearMods + rescan).
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

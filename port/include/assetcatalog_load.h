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
#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

struct modeldef;
struct colmesh;
struct animtableentry;

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

/* c3849 Wave 2: seed g_Anims rows for catalog-owned custom anim slots from
 * catalog metadata (0xffffffff data sentinel -> clip-replacement path). Called
 * at the end of catalogLoadInit (mod reloads) and from animsInit (boot). */
void catalogSeedCustomAnimRows(void);

/* ========================================================================
 * Resolve Result  (primary query result type)
 * ======================================================================== */

/**
 * Returned by all catalogResolve*() functions.
 *
 * path           - public file path to load from, or NULL (use ROM/static)
 * catalog_id     - pool index of the matching catalog entry, or -1 if this
 *                  asset is unknown to the catalog
 * is_mod_override - historical name: 1 means this resolve should load from
 *                  `path`, whether that path came from a mod or generated
 *                  base-game source
 * source_only_blocked - 1 when Debug.AssetSourceOnlyType selected this
 *                  family but the catalog entry has no public FileProvider
 *                  source. Actual load callers must refuse fallback.
 *
 * Decision matrix for callers:
 *   source_only_blocked=1            → fail loudly, no fallback
 *   is_mod_override=1, path!=NULL    → load from path
 *   is_mod_override=0, catalog_id>=0 → load from ROM/static (cataloged)
 *   is_mod_override=0, catalog_id<0  → load from ROM/static (uncataloged)
 */
typedef struct {
    const char *path;       /* public file path, or NULL for ROM/static */
    s32         catalog_id; /* catalog entry pool index, -1 = unknown */
    s32         is_mod_override; /* 1 = load from path, 0 = fallback */
    s32         source_only_blocked; /* 1 = selected family lacks file source */
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

/**
 * Resolve a sequenced music track number to a catalog decision.
 *
 * Public .pdsong track audio is routed through the streaming music path before
 * the sequencer. Public sequence.mid, sequence.json, and music.ini sources are
 * proven together before sequence.json is compiled to the compact ALC sequence
 * buffer that the existing sequencer consumes. Source-only audio mode still
 * refuses ROM sequence fallback if public-source compilation fails.
 */
CatalogResolveResult catalogResolveMusicSequence(s32 tracknum);

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
 * Type-checked load wrapper.
 *
 * expected_type must match the resolved catalog entry type. Returns 1 on
 * success, 0 on type mismatch or load failure.
 */
s32 catalogLoadTypedAsset(asset_type_e expected_type, const char *assetId);

/**
 * Acquire/release the current stage's owner-scoped typed reference.
 * These calls are idempotent for one stage and keep the stage ledger separate
 * from UI, editor, manifest, network, and explicit parent lifecycle owners.
 */
s32 catalogLoadStageAsset(asset_type_e expected_type, const char *assetId);
void catalogReleaseStageAsset(asset_type_e expected_type, const char *assetId);

/**
 * Return a catalog-owned activated model payload for a loaded model-like asset.
 * Only entries loaded through the typed lifecycle model path return non-NULL.
 */
struct modeldef *catalogGetLoadedModeldef(const char *assetId);

/**
 * Return a catalog-owned activated collision mesh for a loaded OBJ-backed
 * map/arena/scenario asset. The caller must not free or mutate it.
 */
struct colmesh *catalogGetLoadedColmesh(const char *assetId);

/**
 * Return a catalog-owned generated animation clip for a loaded animation asset.
 * The returned byte buffer is in the same header+frame stream shape consumed by
 * animLoadHeader/animLoadFrame. The caller must not free or mutate it.
 */
const void *catalogGetLoadedAnimationClip(const char *assetId,
                                          const struct animtableentry **out_entry,
                                          u32 *out_size);

/**
 * Type-checked release wrapper. Same validation rule as
 * catalogLoadTypedAsset; mismatches log and leave the asset untouched.
 */
void catalogReleaseTypedAsset(asset_type_e expected_type, const char *assetId);

/**
 * Type-checked retain wrapper. Same validation rule as
 * catalogLoadTypedAsset; mismatches log and leave the asset untouched.
 */
void catalogRetainTypedAsset(asset_type_e expected_type, const char *assetId);

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
 * the map entry identified by newStageId.  Compares against only references
 * owned by the stage-category loader to produce two disjoint sets. Other
 * loaded owners never enter toUnload.
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
 * Caller drives the actual typed catalog load/release calls.
 */
s32 catalogComputeStageDiff(const char *newStageId,
                            const char **toLoad,  s32 *loadCount,
                            const char **toUnload, s32 *unloadCount,
                            s32 maxItems);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ASSETCATALOG_LOAD_H */

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

#ifdef __cplusplus
}
#endif

#endif /* _IN_ASSETCATALOG_LOAD_H */

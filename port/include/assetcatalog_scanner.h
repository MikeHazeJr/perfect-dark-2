/**
 * assetcatalog_scanner.h -- Component scanner and INI loader for Asset Catalog
 *
 * D3R-3: Base game asset registration
 * D3R-4: Component filesystem scanner + INI parser
 *
 * Usage:
 *   1. assetCatalogInit()                    -- allocate catalog (assetcatalog.h)
 *   2. assetCatalogRegisterBaseGame()        -- register all base game assets
 *   3. assetCatalogScanComponents(modsdir)   -- scan mod components, parse INIs
 *   4. Game runs, resolves via assetCatalogResolve()
 */

#ifndef _IN_ASSETCATALOG_SCANNER_H
#define _IN_ASSETCATALOG_SCANNER_H

#include <PR/ultratypes.h>
#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * D3R-3: Base Game Registration
 * ======================================================================== */

/**
 * Register all base game assets in the catalog.
 *
 * Iterates g_Stages[], g_MpBodies[], g_MpHeads[], g_MpArenas[] and creates
 * catalog entries with "base:" prefix IDs (e.g., "base:villa", "base:dark_combat").
 *
 * Must be called after assetCatalogInit() and before mod scanning.
 * Sets bundled=1 and enabled=1 on all base entries.
 *
 * Returns number of base assets registered, or -1 on error.
 */
s32 assetCatalogRegisterBaseGame(void);

/**
 * Register extended base game assets (weapons, animations, textures, props,
 * game modes, audio, HUD elements). Called at the end of
 * assetCatalogRegisterBaseGame().
 *
 * Returns number of extended assets registered.
 */
s32 assetCatalogRegisterBaseGameExtended(void);

/* ========================================================================
 * D3R-4: Component Scanner
 * ======================================================================== */

/**
 * Scan mod component directories and register assets in the catalog.
 *
 * Walks modsdir looking for mod_*\/_components\/{maps,characters,textures}\/
 * subdirectories. For each component folder, parses the .ini manifest and
 * registers an entry in the catalog.
 *
 * @param modsdir  Path to the mods directory (e.g., "mods/")
 * @return Number of mod components registered, or -1 on error.
 */
s32 assetCatalogScanComponents(const char *modsdir);

/**
 * Scan the flat bot_variants/ directory directly under modsdir.
 *
 * Handles user-created bot variants saved by the in-game Bot Customizer:
 *   {modsdir}/bot_variants/{slug}/bot.ini
 *
 * Call after assetCatalogScanComponents() at startup. New variants saved
 * during a session are hot-registered immediately via botVariantSave() and
 * do not require this scan to be called again.
 *
 * Returns number of variants registered, or 0 if the directory doesn't exist.
 */
s32 assetCatalogScanBotVariants(const char *modsdir);

/**
 * INI key-value pair (parsed from .ini file).
 */
typedef struct ini_pair {
	char key[64];
	char value[256];
} ini_pair_t;

/**
 * Parsed INI section.
 */
#define INI_MAX_PAIRS 32

typedef struct ini_section {
	char type[32];           /* section header: "map", "character", "textures", etc. */
	ini_pair_t pairs[INI_MAX_PAIRS];
	s32 count;
} ini_section_t;

/**
 * Parse a .ini file into an ini_section_t.
 * Returns 1 on success, 0 on failure.
 * Only parses the first section (component INIs have one section).
 */
s32 iniParse(const char *filepath, ini_section_t *out);

/**
 * Look up a value by key in a parsed INI section.
 * Returns the value string, or defval if not found.
 */
const char *iniGet(const ini_section_t *ini, const char *key, const char *defval);

/**
 * Look up an integer value by key.
 * Supports hex (0xNN) and decimal.
 * Returns the integer, or defval if not found or parse error.
 */
s32 iniGetInt(const ini_section_t *ini, const char *key, s32 defval);

/**
 * Look up a float value by key.
 * Returns the float, or defval if not found or parse error.
 */
f32 iniGetFloat(const ini_section_t *ini, const char *key, f32 defval);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ASSETCATALOG_SCANNER_H */

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
#include "modarchive.h"

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

/**
 * S484-followup (2026-05-01): register weapon hi_model / lo_model files
 * as ASSET_MODEL catalog entries with source_filenum binding so the
 * bgun load path's catalog lookup resolves them.
 *
 * Must be called AFTER loaderPoolFinalize flips the weapon pool active
 * (i.e., after the universal walker has parsed the .pdwpn envelopes
 * into loader_pool). Idempotent and dedupe-safe (skips
 * any filenum already present as ASSET_MODEL).
 *
 * Returns count of newly-registered weapon model file entries.
 */
s32 assetCatalogRegisterWeaponModelFiles(void);

/**
 * Catalog coverage audit (2026-05-01) Section 3.A closure: register the
 * five per-stage scene file IDs (bgfileid, tilefileid, padsfileid,
 * setupfileid, mpsetupfileid) carried on each `g_Stages[]` entry as
 * ASSET_MODEL catalog rows with `source_filenum` binding.
 *
 * Without this, `catalogResolveFile(stage.bgfileid)` etc. return no
 * catalog entry, so the existing `romdataFileLoad` mod-override path
 * cannot redirect a stage's BG / collision / setup script load to a
 * mod-supplied file. After this registration lands, mods that ship a
 * replacement file for any stage scene asset get picked up
 * automatically through the existing `romdataFileLoad` plumbing (which
 * already consults `catalogResolveFile`).
 *
 * Idempotent and dedupe-safe (skips any filenum already present as
 * ASSET_MODEL via `g_ModelStates[]`, hand model loop, weapon model
 * registration, etc.).
 *
 * Must be called AFTER `assetCatalogRegisterBaseGame()` so the
 * `g_Stages[]` heap table is populated, AND BEFORE `catalogLoadInit()`
 * so the `s_FilenumOverride[]` reverse-index picks the new entries up
 * on its single build pass. Server build is a no-op (g_NumStages == 0
 * server-side per stageTableInit guard).
 *
 * Returns count of newly-registered stage scene file entries.
 */
s32 assetCatalogRegisterStageSceneFiles(void);

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
 * Scan external-format descriptors inside one loose folder mod.
 *
 * Preferred authored content is zip-openable typed .pd* asset archives
 * (heads/foo.pdhead::head.ini + model.gltf, arenas/bar.pdarena::arena.ini
 * + geometry.obj, animations/baz.pdanim::animation.ini + source files).
 * The earlier canonical folder layout (weapons/<id>/weapon.ini,
 * characters/heads/<id>/head.ini, maps/<id>/arena.ini, ...) remains accepted
 * for compatibility. .pdmod transport archives use the same scanner contract
 * without extracting files into the mods folder.
 */
s32 assetCatalogScanExternalLayoutFolder(const char *mod_id, const char *mod_dir);

/**
 * Scan component descriptors that live inside a mounted/readable .pdmod archive.
 *
 * This is the archive-side equivalent of assetCatalogScanComponents(): it
 * accepts old _components category/id INI entries, typed .pd* INI descriptor
 * entries, and the compatibility external-folder layout
 * (weapons/<id>/weapon.ini, audio/sfx/<id>/sound.ini,
 * characters/heads/<id>/head.ini, etc.). Paths registered from archive
 * descriptors are kept archive-relative so fsFileLoad can satisfy them
 * through modVFS.
 *
 * Returns the number of catalog entries registered.
 */
s32 assetCatalogScanComponentsFromArchive(const char *mod_id, mod_archive_t *archive);

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
 * The first section names the asset type; later sections are allowed for
 * grouped metadata and share the same flat key-value map.
 */
s32 iniParse(const char *filepath, ini_section_t *out);

/**
 * Parse a .ini document already loaded in memory. `label` is used only for
 * diagnostics; the input does not need to be NUL-terminated.
 */
s32 iniParseBuffer(const char *label, const char *data, u32 len, ini_section_t *out);

/**
 * Write a parsed INI section back to a caller-provided UTF-8 buffer.
 * `out_len` receives the byte count excluding the trailing NUL when non-NULL.
 */
s32 iniWriteBuffer(const ini_section_t *ini, char *out, u32 out_cap, u32 *out_len);

/**
 * Write a parsed INI section to disk using the same formatting as
 * iniWriteBuffer().
 */
s32 iniWriteFile(const char *filepath, const ini_section_t *ini);

/**
 * Return a commented template for canonical external-format metadata.
 * Supported kinds: weapon, head, body, arena, animation, sfx, voice, music.
 */
const char *modiniTemplateForKind(const char *kind);

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

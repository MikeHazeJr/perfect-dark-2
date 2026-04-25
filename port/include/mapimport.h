/**
 * mapimport.h -- Mod Map Import Pipeline (Layer 3)
 *
 * PD-native binary format importer for community map packs, converted GE maps,
 * and externally-authored content. Validates, normalizes, generates missing
 * metadata (spawns, waypoints, mod.json), emits to mods/ directory, and
 * registers in the Asset Catalog.
 *
 * Pipeline stages:
 *   1. PARSE     -- Read source files, detect what's present
 *   2. NORMALIZE -- Validate formats, bounds-check, sanitize
 *   3. GENERATE  -- Fill missing metadata (spawns, waypoints, setup, mod.json)
 *   4. EMIT      -- Atomic write to mods/imported_<name>/
 *   5. VALIDATE  -- Smoke test: load BG, verify pads, spawn pool, collision
 *   6. REGISTER  -- Trigger modmgrReload(), verify catalog entry
 *
 * Design ref: context/designs/mod-map-import-pipeline-2026-04-13.md
 */

#ifndef _IN_MAPIMPORT_H
#define _IN_MAPIMPORT_H

#include <PR/ultratypes.h>
#include "fs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

#define MAPIMPORT_NAME_LEN    64
#define MAPIMPORT_ERROR_LEN   256
#define MAPIMPORT_MAX_ROOMS   256
#define MAPIMPORT_MAX_PADS    1024
#define MAPIMPORT_IMPORT_PREFIX "imported_"

/* Import result codes */
typedef enum {
	MAPIMPORT_OK = 0,
	MAPIMPORT_ERR_NO_SOURCE_DIR,
	MAPIMPORT_ERR_NO_BG_FILE,
	MAPIMPORT_ERR_CORRUPT_BG,
	MAPIMPORT_ERR_EMPTY_MAP,
	MAPIMPORT_ERR_NO_PADS,
	MAPIMPORT_ERR_CORRUPT_PADS,
	MAPIMPORT_ERR_EMIT_FAILED,
	MAPIMPORT_ERR_VALIDATE_FAILED,
	MAPIMPORT_ERR_REGISTER_FAILED,
	MAPIMPORT_ERR_DUPLICATE_ID,
} mapimport_result_e;

/* ========================================================================
 * Import context -- carries state through the pipeline
 * ======================================================================== */

typedef struct {
	/* Source */
	char source_dir[FS_MAXPATH];
	char map_name[MAPIMPORT_NAME_LEN];

	/* What we found */
	s32 has_bg;           /* BG geometry file present */
	s32 has_pads;         /* Pad location file present */
	s32 has_setup;        /* Setup/entity file present */
	s32 has_waypoints;    /* Waypoint data in setup */
	s32 has_spawns;       /* INTROCMD_SPAWN entries in setup */
	s32 has_textures;     /* Custom texture data present */
	s32 has_modjson;      /* mod.json manifest present */

	/* Parsed counts */
	s32 num_rooms;
	s32 num_pads;
	s32 num_waypoints;
	s32 num_spawn_cmds;

	/* Resolved file paths in source dir */
	char bg_path[FS_MAXPATH];
	char pad_path[FS_MAXPATH];
	char setup_path[FS_MAXPATH];
	char modjson_path[FS_MAXPATH];

	/* Output directory */
	char output_dir[FS_MAXPATH];

	/* Status */
	s32 parse_ok;
	s32 normalize_ok;
	s32 generate_ok;
	s32 emit_ok;
	s32 validate_ok;
	s32 register_ok;

	/* Result / error */
	mapimport_result_e result;
	char error[MAPIMPORT_ERROR_LEN];

	/* Generated metadata flags */
	s32 generated_spawns;     /* spawns were auto-generated */
	s32 generated_modjson;    /* mod.json was auto-generated */
	s32 generated_setup;      /* setup file was auto-generated */
	s32 num_generated_spawns; /* how many spawns were synthesized */
} import_context_t;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * Run the full import pipeline on a source directory.
 *
 * source_dir:  Path to directory containing map files (BG, pads, setup, etc.)
 * map_name:    Human-readable name for the map (used in catalog/mod.json).
 *              If NULL, derived from directory name.
 *
 * Returns MAPIMPORT_OK on success. On failure, ctx->error describes the issue
 * and ctx->result contains the error code.
 *
 * On success, the map is registered in the Asset Catalog and available in
 * Combat Simulator. The output directory is mods/imported_<name>/.
 */
mapimport_result_e mapImport(const char *source_dir, const char *map_name,
                             import_context_t *ctx);

/**
 * Run only the parse stage (non-destructive scan).
 * Populates ctx with what's present in source_dir without writing anything.
 * Useful for UI preview before committing to import.
 */
mapimport_result_e mapImportParse(const char *source_dir, import_context_t *ctx);

/**
 * Check if a map with the given name is already imported.
 * Returns 1 if an imported_<name> directory exists in mods/, 0 otherwise.
 */
s32 mapImportExists(const char *map_name);

/**
 * Get a human-readable string for an import result code.
 */
const char *mapImportResultStr(mapimport_result_e result);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MAPIMPORT_H */

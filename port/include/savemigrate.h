/**
 * savemigrate.h -- Save file version migration framework (D13).
 *
 * Provides a chain-based migration system for save files. When the game
 * loads a save with an older version, it runs the appropriate migration
 * functions to bring it up to the current SAVE_VERSION.
 *
 * Each migration step is a function that transforms the JSON data from
 * version N to version N+1. Before any migration, the original file is
 * backed up as "<name>.vN.bak".
 *
 * Downgrade protection: saves from a NEWER version than the running game
 * are loaded in read-only mode with a user warning.
 */

#ifndef _IN_SAVEMIGRATE_H
#define _IN_SAVEMIGRATE_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Types
 * ======================================================================== */

/**
 * Migration function signature.
 *
 * Takes the full JSON string of the save file (version `from`),
 * modifies it in place or returns a new allocated string (version `to`).
 *
 * Parameters:
 *   json    — mutable buffer containing the JSON (null-terminated)
 *   jsonlen — length of the JSON string
 *   bufsize — total buffer capacity (may be larger than jsonlen)
 *
 * Returns:
 *   Pointer to the (possibly reallocated) JSON buffer on success,
 *   or NULL on failure. If the returned pointer differs from `json`,
 *   the caller will free the old buffer.
 */
typedef char *(*savemigrate_fn)(char *json, s32 jsonlen, s32 bufsize);

/* ========================================================================
 * Registration
 * ======================================================================== */

/**
 * Register a migration function for a specific version transition.
 * Must be called during init, before any saves are loaded.
 *
 * Example:
 *   saveMigrateRegister(SAVETYPE_AGENT, 1, 2, migrateAgent_1to2);
 *   saveMigrateRegister(SAVETYPE_AGENT, 2, 3, migrateAgent_2to3);
 */

typedef enum {
	SAVETYPE_AGENT   = 0,
	SAVETYPE_PLAYER  = 1,
	SAVETYPE_SETUP   = 2,
	SAVETYPE_SYSTEM  = 3,
	SAVETYPE_COUNT,
} savetype_t;

void saveMigrateRegister(savetype_t type, s32 fromVersion, s32 toVersion, savemigrate_fn fn);

/* ========================================================================
 * Migration execution
 * ======================================================================== */

/**
 * Initialize the migration system. Registers all known migrations.
 * Call once at startup, after saveInit().
 */
void saveMigrateInit(void);

/**
 * Attempt to migrate a save file from its current version to targetVersion.
 *
 * Parameters:
 *   filepath      — full path to the save file
 *   type          — save type (agent, player, setup, system)
 *   fileVersion   — version found in the save file
 *   targetVersion — version to migrate to (usually SAVE_VERSION)
 *
 * Returns:
 *   0 on success (file updated on disk, backup created)
 *   1 if no migration needed (already at target version)
 *  -1 on error (backup failed, migration fn failed, etc.)
 *  -2 if file is from a NEWER version (cannot downgrade)
 */
s32 saveMigrateFile(const char *filepath, savetype_t type,
                    s32 fileVersion, s32 targetVersion);

/**
 * Check if a save file needs migration.
 *
 * Returns:
 *   0  — no migration needed
 *   1  — migration available (older version)
 *  -1  — file is from newer version (downgrade warning)
 */
s32 saveMigrateCheck(s32 fileVersion, s32 targetVersion);

/* ========================================================================
 * Backup utilities
 * ======================================================================== */

/**
 * Create a backup of a file: copies "path" to "path.vN.bak"
 * Returns 0 on success, -1 on failure.
 */
s32 saveMigrateBackup(const char *filepath, s32 version);

#ifdef __cplusplus
}
#endif

#endif /* _IN_SAVEMIGRATE_H */

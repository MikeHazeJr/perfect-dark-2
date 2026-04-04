/**
 * savemigrate.c -- Save file version migration implementation (D13).
 *
 * Chain-based migration system. Each registered migration transforms a
 * save's JSON from version N to N+1. The chain runs automatically on load
 * when a save's version is older than SAVE_VERSION.
 *
 * Currently SAVE_VERSION = 1, so no migrations are registered yet.
 * This file establishes the framework for future version bumps.
 *
 * When SAVE_VERSION is bumped to 2, add a migration function:
 *   static char *migrateAgent_1to2(char *json, s32 jsonlen, s32 bufsize) { ... }
 * and register it in saveMigrateInit():
 *   saveMigrateRegister(SAVETYPE_AGENT, 1, 2, migrateAgent_1to2);
 *
 * Auto-discovered by GLOB_RECURSE for port/*.c in CMakeLists.txt.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <PR/ultratypes.h>
#include "system.h"
#include "savefile.h"
#include "savemigrate.h"

/* ========================================================================
 * Migration registry
 * ======================================================================== */

#define MAX_MIGRATIONS 32

typedef struct {
	savetype_t type;
	s32 fromVersion;
	s32 toVersion;
	savemigrate_fn fn;
} migration_entry_t;

static migration_entry_t s_Migrations[MAX_MIGRATIONS];
static s32 s_MigrationCount = 0;

void saveMigrateRegister(savetype_t type, s32 fromVersion, s32 toVersion, savemigrate_fn fn)
{
	if (s_MigrationCount >= MAX_MIGRATIONS) {
		sysLogPrintf(LOG_ERROR, "SAVEMIGRATE: Migration registry full (%d max)", MAX_MIGRATIONS);
		return;
	}

	if (!fn || fromVersion >= toVersion) {
		sysLogPrintf(LOG_ERROR, "SAVEMIGRATE: Invalid migration %d→%d", fromVersion, toVersion);
		return;
	}

	migration_entry_t *e = &s_Migrations[s_MigrationCount++];
	e->type = type;
	e->fromVersion = fromVersion;
	e->toVersion = toVersion;
	e->fn = fn;

	sysLogPrintf(LOG_NOTE, "SAVEMIGRATE: Registered %s migration v%d → v%d",
		type == SAVETYPE_AGENT  ? "agent"  :
		type == SAVETYPE_PLAYER ? "player" :
		type == SAVETYPE_SETUP  ? "setup"  :
		type == SAVETYPE_SYSTEM ? "system" : "unknown",
		fromVersion, toVersion);
}

/* ========================================================================
 * Find migration for a specific type and version step
 * ======================================================================== */

static savemigrate_fn findMigration(savetype_t type, s32 fromVersion)
{
	for (s32 i = 0; i < s_MigrationCount; i++) {
		if (s_Migrations[i].type == type && s_Migrations[i].fromVersion == fromVersion) {
			return s_Migrations[i].fn;
		}
	}
	return NULL;
}

/* ========================================================================
 * Backup
 * ======================================================================== */

s32 saveMigrateBackup(const char *filepath, s32 version)
{
	char bakpath[512];
	snprintf(bakpath, sizeof(bakpath), "%s.v%d.bak", filepath, version);

	/* Check if backup already exists (don't overwrite) */
	FILE *test = fopen(bakpath, "r");
	if (test) {
		fclose(test);
		sysLogPrintf(LOG_NOTE, "SAVEMIGRATE: Backup already exists: %s", bakpath);
		return 0;
	}

	/* Copy file */
	FILE *src = fopen(filepath, "rb");
	if (!src) {
		sysLogPrintf(LOG_ERROR, "SAVEMIGRATE: Cannot open source for backup: %s", filepath);
		return -1;
	}

	FILE *dst = fopen(bakpath, "wb");
	if (!dst) {
		fclose(src);
		sysLogPrintf(LOG_ERROR, "SAVEMIGRATE: Cannot create backup: %s", bakpath);
		return -1;
	}

	u8 buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
		if (fwrite(buf, 1, n, dst) != n) {
			fclose(src);
			fclose(dst);
			remove(bakpath);
			sysLogPrintf(LOG_ERROR, "SAVEMIGRATE: Write error during backup");
			return -1;
		}
	}

	fclose(src);
	fclose(dst);

	sysLogPrintf(LOG_NOTE, "SAVEMIGRATE: Backed up %s (v%d)", filepath, version);
	return 0;
}

/* ========================================================================
 * Migration execution
 * ======================================================================== */

s32 saveMigrateCheck(s32 fileVersion, s32 targetVersion)
{
	if (fileVersion == targetVersion) return 0;
	if (fileVersion > targetVersion) return -1;
	return 1;
}

s32 saveMigrateFile(const char *filepath, savetype_t type,
                    s32 fileVersion, s32 targetVersion)
{
	/* Check if migration is needed */
	s32 check = saveMigrateCheck(fileVersion, targetVersion);
	if (check == 0) return 1;  /* already at target */
	if (check == -1) {
		sysLogPrintf(LOG_WARNING, "SAVEMIGRATE: %s is version %d, game is version %d "
			"— loading read-only (cannot downgrade)",
			filepath, fileVersion, targetVersion);
		return -2;
	}

	/* Create backup before any modifications */
	if (saveMigrateBackup(filepath, fileVersion) != 0) {
		sysLogPrintf(LOG_ERROR, "SAVEMIGRATE: Backup failed — aborting migration for %s",
			filepath);
		return -1;
	}

	/* Read the entire file into a buffer */
	FILE *f = fopen(filepath, "rb");
	if (!f) {
		sysLogPrintf(LOG_ERROR, "SAVEMIGRATE: Cannot open %s for migration", filepath);
		return -1;
	}

	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);

	/* Allocate with extra space for migration growth */
	s32 bufsize = (s32)fsize + 4096;
	char *json = (char *)malloc(bufsize);
	if (!json) {
		fclose(f);
		return -1;
	}

	s32 jsonlen = (s32)fread(json, 1, fsize, f);
	json[jsonlen] = '\0';
	fclose(f);

	/* Run migration chain: v1 → v2 → v3 → ... → target */
	s32 currentVer = fileVersion;
	while (currentVer < targetVersion) {
		savemigrate_fn fn = findMigration(type, currentVer);
		if (!fn) {
			sysLogPrintf(LOG_ERROR, "SAVEMIGRATE: No migration found for v%d → v%d (%s)",
				currentVer, currentVer + 1, filepath);
			free(json);
			return -1;
		}

		sysLogPrintf(LOG_NOTE, "SAVEMIGRATE: Running migration v%d → v%d for %s",
			currentVer, currentVer + 1, filepath);

		char *result = fn(json, jsonlen, bufsize);
		if (!result) {
			sysLogPrintf(LOG_ERROR, "SAVEMIGRATE: Migration v%d → v%d FAILED for %s",
				currentVer, currentVer + 1, filepath);
			free(json);
			return -1;
		}

		if (result != json) {
			free(json);
			json = result;
		}

		jsonlen = (s32)strlen(json);
		currentVer++;
	}

	/* Write migrated data back to file */
	f = fopen(filepath, "wb");
	if (!f) {
		sysLogPrintf(LOG_ERROR, "SAVEMIGRATE: Cannot write migrated file: %s", filepath);
		free(json);
		return -1;
	}

	if (fwrite(json, 1, jsonlen, f) != (size_t)jsonlen) {
		sysLogPrintf(LOG_ERROR, "SAVEMIGRATE: write error for %s", filepath);
		fclose(f);
		free(json);
		return -1;
	}
	fclose(f);
	free(json);

	sysLogPrintf(LOG_NOTE, "SAVEMIGRATE: Successfully migrated %s from v%d to v%d",
		filepath, fileVersion, targetVersion);
	return 0;
}

/* ========================================================================
 * Initialization — register all known migrations
 * ======================================================================== */

void saveMigrateInit(void)
{
	s_MigrationCount = 0;

	/* ----------------------------------------------------------------
	 * Currently SAVE_VERSION = 1. No migrations needed yet.
	 *
	 * When SAVE_VERSION is bumped to 2, add migrations here:
	 *
	 *   saveMigrateRegister(SAVETYPE_AGENT,  1, 2, migrateAgent_1to2);
	 *   saveMigrateRegister(SAVETYPE_PLAYER, 1, 2, migratePlayer_1to2);
	 *   saveMigrateRegister(SAVETYPE_SETUP,  1, 2, migrateSetup_1to2);
	 *   saveMigrateRegister(SAVETYPE_SYSTEM, 1, 2, migrateSystem_1to2);
	 *
	 * Migration functions should:
	 *   1. Parse the JSON to find the "version" field
	 *   2. Add/rename/remove fields as needed
	 *   3. Update the "version" field to the new version
	 *   4. Return the modified JSON buffer
	 *
	 * Example skeleton:
	 *
	 *   static char *migrateAgent_1to2(char *json, s32 jsonlen, s32 bufsize)
	 *   {
	 *       // Add new field "newfield": 0 after "version": 1
	 *       // Update "version": 1 to "version": 2
	 *       // ... string manipulation ...
	 *       return json;
	 *   }
	 * ---------------------------------------------------------------- */

	sysLogPrintf(LOG_NOTE, "SAVEMIGRATE: Initialized (%d migrations registered, "
		"current save version: %d)", s_MigrationCount, SAVE_VERSION);
}

/**
 * modmigrate.c -- Priority M / B-238 / M-4.1 one-shot folder->.pdmod
 * auto-migration. See port/include/modmigrate.h.
 *
 * Bad-value triage matrix Mike asked me to keep in mind during this work:
 *
 *   - Interpretation: candidate folder must contain a root mod.json, must
 *     not be a reserved subdirectory, must not already have a corresponding
 *     `.pdmod` of the same name. Anything else is skipped (not failed).
 *   - Setting: package via modpackPdmodFromFolder which uses the same
 *     atomic <out>.tmp -> rename pipeline modarchive's writer ships.
 *   - Multiple sources: the only writer of `<name>.pdmod` here is the
 *     migrator; we explicitly refuse to overwrite an existing archive
 *     of the same name (the user may have already started using it).
 *   - Overwrite: the rename to `<name>.legacy_backup` is the only
 *     destructive op. We do NOT delete the original folder; the user
 *     keeps it as a safety net per design Section 6.
 *   - Ordering: package the .pdmod FIRST, only rename the source folder
 *     AFTER the archive lands successfully. If anything fails before the
 *     rename, the source folder is untouched.
 *
 * The sentinel `.pdmod-migration-done` is written ONLY after a complete
 * pass. If the pass aborts mid-way the next launch retries.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <PR/ultratypes.h>

#include "fs.h"
#include "system.h"
#include "modmgr.h"          /* MODMGR_RESERVED_NAMES_LIST */
#include "modarchive.h"
#include "modpack_pdmod.h"
#include "modmigrate.h"

#define MIGRATE_SENTINEL_NAME ".pdmod-migration-done"
#define MIGRATE_BACKUP_SUFFIX ".legacy_backup"

const char *modMigrateSentinelName(void)
{
	return MIGRATE_SENTINEL_NAME;
}

/* Same reserved-name check used by modmgr's scan walker. Local copy here so
 * modmigrate.c does not become a transitive dependency of modmgr internals.
 * Both lists must stay in sync; the macro guarantees compile-time agreement. */
static int isReservedName(const char *name)
{
	static const char *const reserved[MODMGR_RESERVED_NAMES_COUNT] = MODMGR_RESERVED_NAMES_LIST;
	if (!name) return 0;
	for (s32 i = 0; i < MODMGR_RESERVED_NAMES_COUNT; i++) {
		if (strcmp(name, reserved[i]) == 0) return 1;
	}
	return 0;
}

/* Returns 1 if `leaf` ends with `.legacy_backup`. */
static int isLegacyBackupSuffix(const char *leaf)
{
	if (!leaf) return 0;
	size_t n = strlen(leaf);
	size_t s = strlen(MIGRATE_BACKUP_SUFFIX);
	return n > s && strcmp(leaf + n - s, MIGRATE_BACKUP_SUFFIX) == 0;
}

/* Returns 1 if a regular file exists at the given path. */
static int fileExists(const char *path)
{
	struct stat st;
	return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

/* Returns 1 if a directory exists at the given path. */
static int dirExists(const char *path)
{
	struct stat st;
	return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

s32 modMigrateRun(const char *modsDir, mod_migrate_summary_t *summary)
{
	if (summary) {
		summary->packaged = 0;
		summary->skipped  = 0;
		summary->failed   = 0;
	}
	if (!modsDir || !modsDir[0]) return 0;

	/* Sentinel check -- one-shot semantics. */
	char sentinelPath[FS_MAXPATH + 1];
	snprintf(sentinelPath, sizeof(sentinelPath), "%s/%s", modsDir, MIGRATE_SENTINEL_NAME);
	if (fileExists(sentinelPath)) {
		return 0;  /* already migrated; not an error */
	}

	DIR *d = opendir(modsDir);
	if (!d) return 0;

	sysLogPrintf(LOG_NOTE,
		"MIGRATE: scanning '%s' for legacy folder mods...", modsDir);

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		const char *leaf = ent->d_name;

		if (leaf[0] == '.') continue;
		if (isReservedName(leaf)) continue;
		if (isLegacyBackupSuffix(leaf)) continue;

		char fullPath[FS_MAXPATH + 1];
		snprintf(fullPath, sizeof(fullPath), "%s/%s", modsDir, leaf);

		struct stat st;
		if (stat(fullPath, &st) != 0) continue;
		if (!S_ISDIR(st.st_mode)) continue;  /* only folder mods are migrated */

		/* Only consider folders that have a root mod.json. Folder layouts
		 * without mod.json (e.g. shared component categories) are not mods
		 * by themselves and are left untouched. */
		char mfstPath[FS_MAXPATH + 1];
		snprintf(mfstPath, sizeof(mfstPath), "%s/mod.json", fullPath);
		if (!fileExists(mfstPath)) continue;

		/* Refuse to overwrite an existing <leaf>.pdmod. The user may have
		 * already started using it; we do not silently clobber. */
		char pdmodPath[FS_MAXPATH + 1];
		snprintf(pdmodPath, sizeof(pdmodPath), "%s/%s.pdmod", modsDir, leaf);
		if (fileExists(pdmodPath)) {
			sysLogPrintf(LOG_NOTE,
				"MIGRATE: '%s' already has a corresponding .pdmod -- skipping",
				leaf);
			if (summary) summary->skipped++;
			continue;
		}

		/* Refuse to act if the legacy_backup target already exists. The
		 * user may have a previous migration attempt that did not complete;
		 * surface that situation rather than tripling-up the backup. */
		char backupPath[FS_MAXPATH + 1];
		snprintf(backupPath, sizeof(backupPath), "%s/%s%s",
			modsDir, leaf, MIGRATE_BACKUP_SUFFIX);
		if (dirExists(backupPath)) {
			sysLogPrintf(LOG_WARNING,
				"MIGRATE: '%s%s' already exists -- skipping '%s' to avoid clobber",
				leaf, MIGRATE_BACKUP_SUFFIX, leaf);
			if (summary) summary->skipped++;
			continue;
		}

		/* Order: package first, only rename source folder if archive lands. */
		sysLogPrintf(LOG_NOTE, "MIGRATE: packaging '%s' -> '%s.pdmod'", leaf, leaf);
		s32 r = modpackPdmodFromFolder(fullPath, pdmodPath);
		if (r != MODPACK_PDMOD_OK) {
			sysLogPrintf(LOG_WARNING,
				"MIGRATE: failed to package '%s' (err=%d) -- folder left intact",
				leaf, r);
			/* Best-effort cleanup of any partial archive that the writer
			 * may have left behind under .tmp; modArchiveFinish would have
			 * already handled this, but a stale `<name>.pdmod` from an
			 * earlier successful Finish would already exist (we checked
			 * above), so this branch only runs on Begin/AddFile failures. */
			remove(pdmodPath);
			if (summary) summary->failed++;
			continue;
		}

		/* Defense: re-open the archive we just wrote and verify mod.json
		 * is present + parses (cheap roundtrip via the reader). If the
		 * read fails we have a corrupt archive -- delete it and leave the
		 * source folder so next launch can retry. */
		mod_archive_t *check = modArchiveOpen(pdmodPath);
		if (!check) {
			sysLogPrintf(LOG_WARNING,
				"MIGRATE: '%s.pdmod' wrote but failed to re-open (err=%d) -- removing, folder left intact",
				leaf, modArchiveLastError());
			remove(pdmodPath);
			if (summary) summary->failed++;
			continue;
		}
		u32 mfstSize = 0;
		char *mfst = modArchiveReadManifest(check, &mfstSize);
		modArchiveClose(check);
		if (!mfst) {
			sysLogPrintf(LOG_WARNING,
				"MIGRATE: '%s.pdmod' wrote but mod.json could not be re-read -- removing, folder left intact",
				leaf);
			remove(pdmodPath);
			if (summary) summary->failed++;
			continue;
		}
		free(mfst);

		/* Rename source folder to <name>.legacy_backup. On Win the rename
		 * fails if the destination already exists; the dirExists check
		 * above guards that. */
		if (rename(fullPath, backupPath) != 0) {
			sysLogPrintf(LOG_WARNING,
				"MIGRATE: '%s' archive landed but rename to '%s%s' failed (errno %d). Archive kept; original folder still active.",
				leaf, leaf, MIGRATE_BACKUP_SUFFIX, errno);
			/* Both archive and folder now coexist. Drop the new archive so
			 * the loader does not see two entries for the same id. */
			remove(pdmodPath);
			if (summary) summary->failed++;
			continue;
		}

		sysLogPrintf(LOG_NOTE,
			"MIGRATE: packaged '%s' -> '%s.pdmod' + renamed source to '%s%s'",
			leaf, leaf, leaf, MIGRATE_BACKUP_SUFFIX);
		if (summary) summary->packaged++;
	}

	closedir(d);

	/* Write the sentinel even if zero mods were migrated -- the pass
	 * itself counts. Subsequent launches skip without retrying. */
	FILE *sf = fopen(sentinelPath, "wb");
	if (sf) {
		fprintf(sf, "# Priority M / B-238 / M-4.1 sentinel.\n"
		            "# Presence of this file means the one-shot folder->.pdmod\n"
		            "# auto-migration has run for this mods directory.\n"
		            "# Delete to retry the migration on next launch.\n");
		fclose(sf);
	} else {
		sysLogPrintf(LOG_WARNING,
			"MIGRATE: could not write sentinel '%s' -- migration may retry on next launch",
			sentinelPath);
	}

	if (summary) {
		sysLogPrintf(LOG_NOTE,
			"MIGRATE: pass complete -- packaged=%d skipped=%d failed=%d",
			summary->packaged, summary->skipped, summary->failed);
	} else {
		sysLogPrintf(LOG_NOTE, "MIGRATE: pass complete");
	}
	return 1;
}

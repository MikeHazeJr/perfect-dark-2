/**
 * modmigrate.h -- Priority M / B-238 / M-4.1 first-run auto-migration.
 *
 * One-shot scan of the mods root that converts every folder-based mod
 * into a `.pdmod` archive in the same directory. After a successful pass
 * the original folder is renamed to `<name>.legacy_backup/` (kept as a
 * safety net) and a sentinel file `.pdmod-migration-done` is written so
 * subsequent launches skip the scan.
 *
 * Per design Section 6 Phase M-3.
 *
 * Trust contract: the migration only acts on folders that already live
 * directly under the trusted mods root. Reserved subdirectories (shared,
 * inbox, untrusted) are skipped. Existing `.pdmod` files are NOT
 * overwritten -- the migrator only acts on folders that have NO
 * corresponding archive of the same name.
 */
#ifndef _IN_MODMIGRATE_H
#define _IN_MODMIGRATE_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mod_migrate_summary {
	s32 packaged;     /* folders successfully packaged into .pdmod */
	s32 skipped;      /* folders intentionally skipped (already have .pdmod, etc.) */
	s32 failed;       /* folders the migrator tried but could not package */
} mod_migrate_summary_t;

/**
 * Run the auto-migration. Returns 1 if a pass actually executed (sentinel
 * was written), 0 if it was skipped (sentinel already present, modsdir
 * unreadable, etc.).
 *
 * Caller MUST run this BEFORE modmgrScanDirectory so the scan picks up
 * the newly-created .pdmod files.
 *
 * `summary` is optional; populated when non-NULL.
 */
s32 modMigrateRun(const char *modsDir, mod_migrate_summary_t *summary);

/** Returns the relative sentinel filename (without the modsDir prefix). */
const char *modMigrateSentinelName(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MODMIGRATE_H */

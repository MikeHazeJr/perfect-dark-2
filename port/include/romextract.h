/**
 * romextract.h -- First-launch ROM extractor (Phase 3 Pass A.2).
 */
#ifndef _IN_ROMEXTRACT_H
#define _IN_ROMEXTRACT_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Walk the ROM file table and write each non-empty file slot to
 * `data/<romid>/files/<name>.bin` (or G_<XXXX>.bin if no name).
 * Idempotent: skips files that already exist with matching size.
 *
 * Must be called AFTER romdataInit (g_RomFile + fileSlots populated)
 * AND BEFORE assetCatalogScanComponents (so extracted bytes are pure
 * ROM, not mod-overridden).
 *
 * Server build: returns 0 immediately (g_RomFile is NULL server-side).
 *
 * Returns: number of files newly written (excludes skipped_existing,
 * skipped_empty, failed); -1 on infrastructure failure (e.g. data dir
 * could not be created).  Per-file failures are logged via
 * LOUDFAIL.EXTRACT but do not abort the walk.
 */
s32 romExtractAllFiles(void);

/**
 * Phase 3 Pass A.4 (2026-05-02): hash-verify-on-launch self-heal.
 *
 * Walks every previously-extracted file under data/<romid>/files/
 * and verifies SHA-256 against the sidecar written at extraction
 * time.  On mismatch: emit LOUDFAIL.LOAD, quarantine the corrupted
 * file to data/<romid>/.quarantine/<unixtime>_<name>, re-extract
 * from g_RomFile, write fresh sidecar.
 *
 * Server build: returns 0 immediately.
 *
 * Returns: number of files self-healed (corrected via re-extract);
 * legacy/baselined entries count separately in the LOG_NOTE summary
 * but not in the return value.
 */
s32 romExtractVerifyAll(void);

#ifdef __cplusplus
}
#endif

#endif

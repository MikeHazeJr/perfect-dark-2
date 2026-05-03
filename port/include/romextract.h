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

/**
 * Build the canonical relative on-disk path for a given ROM filenum,
 * matching the layout written by romExtractAllFiles.  Output:
 *   data/<romid>/files/<sanitized_rom_name>.bin
 *   (or G_<XXXX>.bin if the ROM file slot has no name)
 *
 * Used by Pass B slices to bind catalog entries to disk via
 * catalogSetPrimaryFile after extraction.
 *
 * Returns the length written, or 0 on failure.  Caller buffer must
 * hold at least 1024 bytes.
 */
s32 romExtractRelPathForFilenum(s32 fileNum, char *outRel, s32 outRelLen);

/**
 * Phase 3 Pass B Slices 2/5/6/8/11 (2026-05-02): segment extraction.
 *
 * Walks every loaded ROM segment (sfxctl, sfxtbl, seqctl, seqtbl,
 * sequences, animations, fonts, mp* tables, textures, etc.) and
 * writes the in-memory bytes to data/<romid>/segs/<segname>.bin
 * (no extension suffix beyond .bin; segment names already disambiguate).
 *
 * Idempotent: skips segments whose on-disk size matches the in-memory
 * size.  Sidecar (.bin.sha256) written next to each segment for the
 * Pass A.4-equivalent self-heal verify step.
 *
 * Must be called AFTER romdataInit (segments populated in memory) and
 * BEFORE any consumer reads segment bytes (so the disk image matches
 * what the in-memory loader would have served from ROM).
 *
 * Server build: returns 0 immediately (g_RomFile is NULL server-side
 * and segments are never loaded).
 *
 * Returns: number of segments newly written.  -1 on infrastructure
 * failure (e.g. data dir creation failed).  Per-segment failures emit
 * LOUDFAIL.EXTRACT but do not abort the walk.
 */
s32 romExtractAllSegments(void);

/**
 * Verify each previously-extracted segment against its sidecar; on
 * mismatch quarantine the corrupted file and re-extract from the
 * in-memory segment buffer.  Mirrors romExtractVerifyAll for
 * per-file extraction.
 *
 * Server build: returns 0 immediately.
 *
 * Returns: number of segments self-healed (corrected via re-extract).
 */
s32 romExtractVerifyAllSegments(void);

/**
 * Build the canonical relative on-disk path for a given segment name,
 * matching the layout written by romExtractAllSegments.  Output:
 *   data/<romid>/segs/<segname>.bin
 *
 * Used by romdataInitSegment to check the per-romid path before the
 * legacy `data/segs/<segname>` mod-override path.
 *
 * Returns the length written, or 0 on failure.
 */
s32 romExtractSegmentRelPath(const char *segName, char *outRel, s32 outRelLen);

/**
 * Phase 3 Pass D (2026-05-02): self-heal hardening surfaces.
 *
 * Toast queue drain.  romExtractVerifyAll / romExtractVerifyAllSegments
 * defer per-file/per-seg system toasts during boot because the renderer
 * has not started yet (toasts enqueued during boot would fade out
 * before the first frame).  romExtractToastDrain replays the queued
 * toasts via pdguiToastEnqueue with fresh timestamps so the player
 * actually sees them.  Idempotent: subsequent calls drain an empty
 * queue.  Server build: no-op (PD_SERVER guard).
 *
 * Caller: main.c, AFTER gameInit so the toast renderer is ready.
 */
void romExtractToastDrain(void);

/**
 * Phase 3 Pass D (2026-05-02): boot-time data integrity report.
 *
 * Aggregates the file + segment verify counters into one LOG_NOTE line:
 *   "DATA INTEGRITY: <V> validated, <R> re-extracted, <U> unrecoverable"
 *
 * If R > 0 (any re-extract happened) defers an info system toast with
 * the count.  If U > 0 (any unrecoverable) emits LOG_WARNING and
 * defers a danger system toast directing the user to verify their ROM.
 *
 * Idempotent: subsequent calls within the same boot are no-ops.
 *
 * Caller: main.c, AFTER both verify pairs and BEFORE romdataReleaseRom
 * (the report does not depend on g_RomFile but should run before any
 * destructive boot step that might confuse the counters).
 *
 * Server build: counters stay zero (verify funcs early-return), so the
 * report logs "0 validated, 0 re-extracted, 0 unrecoverable" and skips
 * the toast.  Cheap and harmless.
 */
void romExtractEmitBootIntegrityReport(void);

/**
 * Phase 3 Pass D (2026-05-02): test accessor for the boot integrity
 * counters.  Each pointer may be NULL.
 *
 *   validated     = files+segments that hashed clean (verified +
 *                   baselined sidecars)
 *   recovered     = files+segments re-extracted successfully after
 *                   sha-mismatch quarantine
 *   unrecoverable = files+segments whose re-extract write failed
 *                   (these stay quarantined; user must verify ROM)
 *
 * Returns the sum (validated + recovered + unrecoverable).
 */
s32 romExtractGetBootIntegrity(s32 *validated, s32 *recovered,
                               s32 *unrecoverable);

#ifdef __cplusplus
}
#endif

#endif

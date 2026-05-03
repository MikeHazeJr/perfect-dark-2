/**
 * romextract.c -- First-launch ROM extractor (Phase 3 Pass A.2,
 * 2026-05-02).
 *
 * Walks the ROM file table and writes each non-empty file slot to
 * `data/<romid>/files/<name>.bin` (or `data/<romid>/files/G_<XXXX>.bin`
 * if the slot has no name).  Idempotent: skips files that already
 * exist with a matching size.  Pass A.4 (self-heal) will add SHA-256
 * verification on top of the size check.
 *
 * Architecture: ROM is the bootstrap input.  After extraction, runtime
 * loads can be migrated to the on-disk file (FileProvider) per the
 * Phase 3 plan slices.  This module owns the extraction side; consumer
 * migration happens per-class in Pass B.
 *
 * Boot order: must run AFTER romdataInit (g_RomFile + fileSlots
 * populated) and BEFORE assetCatalogScanComponents (so the extracted
 * bytes are pure ROM content, not mod-overridden bytes mistakenly
 * captured to disk).  See port/src/main.c wiring.
 *
 * Server build: g_RomFile is NULL server-side.  romExtractAllFiles
 * detects this and returns 0 with a single LOG_NOTE.
 *
 * Companion plan:
 *   context/designs/catalog/catalog-rom-once-phase3-plan-2026-05-02.md
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <SDL.h>
#include <SDL_atomic.h>
#include <PR/ultratypes.h>

#include "fs.h"
#include "romdata.h"
#include "romextract.h"
#include "sha256.h"
#include "system.h"
#include "versioninfo.h"

/* Engine Phase 2 (2026-05-03): per-file progress reporting so the boot
 * overlay's bar advances during the long verify pass.  bootProgress*
 * APIs are no-ops when the channel isn't initialised (e.g. dedicated
 * server, or any future caller that runs the extract path post-boot). */
#include "boot_progress.h"

/* Engine Phase 3 (2026-05-03): the verify pass fans out across the boot
 * pool's worker threads.  Workers pull file numbers from a shared cursor
 * and SHA-256 hash the on-disk bytes in parallel.  See
 * context/designs/engine/startup-acceleration.md (Phase 3) for the
 * threading model and correctness invariants. */
#include "boot_pool.h"

/* Pass D (2026-05-02): self-heal hardening surfaces user-facing toasts
 * for hash-mismatch quarantine + recovery outcomes.  Toast headers live
 * in port/fast3d and are not linked into pd-server, so the include
 * follows the established netmsg.c pattern. */
#if !defined(PD_SERVER)
#include "pdgui_toast.h"
#endif

/* Match ROMDATA_MAX_FILES from romdata.c.  If that bound changes,
 * update both. */
#define ROMEXTRACT_MAX_FILES 2048

/* Maximum length for a per-file output path.  fsDataPathFor returns
 * its own static buffer; we copy it locally so we can layer fopen +
 * fwrite without colliding with any other fsDataPathFor caller during
 * the same extraction iteration. */
#define ROMEXTRACT_PATH_LEN 1024

/* Slug-safe a ROM file name.  ROM names are typically alphanumeric +
 * a trailing 'Z' for compressed slots, but defensively replace any
 * character outside [A-Za-z0-9_.-] with '_' so we never hand a path
 * fragment that surprises the filesystem. */
static void romExtractSanitizeName(const char *src, char *dst, s32 dstLen)
{
    s32 i;
    if (dstLen <= 0) {
        return;
    }
    for (i = 0; i + 1 < dstLen && src && src[i]; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-') {
            dst[i] = (char)c;
        } else {
            dst[i] = '_';
        }
    }
    dst[i] = '\0';
}

/* Build the on-disk relative path for a given ROM file.  Returns the
 * length written, or 0 on failure.  The caller's buffer must hold
 * at least ROMEXTRACT_PATH_LEN bytes.  Public alias is
 * `romExtractRelPathForFilenum` (Pass B slices call this to bind
 * catalog entries to disk). */
static s32 romExtractBuildRelPath(s32 fileNum, char *outRel, s32 outRelLen)
{
    char nameBuf[128];
    const char *romName = romdataFileGetName(fileNum);

    if (romName && romName[0]) {
        romExtractSanitizeName(romName, nameBuf, (s32)sizeof(nameBuf));
        return snprintf(outRel, (size_t)outRelLen,
                        "data/%s/files/%s.bin", VERSION_ROMID, nameBuf);
    }

    return snprintf(outRel, (size_t)outRelLen,
                    "data/%s/files/G_%04x.bin", VERSION_ROMID, (u32)fileNum);
}

s32 romExtractRelPathForFilenum(s32 fileNum, char *outRel, s32 outRelLen)
{
    if (outRel == NULL || outRelLen <= 0) return 0;
    s32 len = romExtractBuildRelPath(fileNum, outRel, outRelLen);
    if (len <= 0 || len >= outRelLen) {
        outRel[0] = '\0';
        return 0;
    }
    return len;
}

/* Test whether a path already exists on disk with the requested size.
 * Used as the cheap pre-check inside the extraction walk; Pass A.4
 * verify path consults the SHA-256 sidecar for actual integrity. */
static s32 romExtractFileMatchesSize(const char *fullPath, u32 expectedSize)
{
    struct stat st;
    if (stat(fullPath, &st) != 0) {
        return 0;
    }
    return (st.st_size == (long long)expectedSize) ? 1 : 0;
}

/* Write a SHA-256 sidecar next to an extracted file.  Format: a
 * single line of 64 hex characters followed by a newline (matches
 * the standard `sha256sum` output minus the filename column).  On
 * failure logs LOUDFAIL.EXTRACT and returns 0; sidecar absence does
 * not block extraction since the size check still gates re-write. */
static s32 romExtractWriteSidecar(const char *binRel, const u8 *data, u32 size)
{
    char sidecarRel[ROMEXTRACT_PATH_LEN];
    if (binRel == NULL) return 0;
    snprintf(sidecarRel, sizeof(sidecarRel), "%s.sha256", binRel);

    u8 digest[SHA256_DIGEST_SIZE];
    sha256Hash(data, (size_t)size, digest);

    char hex[SHA256_HEX_SIZE];
    sha256ToHex(digest, hex);

    FILE *f = fsFileOpenWrite(sidecarRel);
    if (f == NULL) {
        sysLoudFailf("EXTRACT",
            "fsFileOpenWrite failed for sidecar \"%s\"", sidecarRel);
        return 0;
    }
    /* 64 hex chars + newline. */
    if (fwrite(hex, 1, 64, f) != 64 || fputc('\n', f) == EOF) {
        sysLoudFailf("EXTRACT", "short write on sidecar \"%s\"", sidecarRel);
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

/* Read a SHA-256 sidecar (if present) and parse its hex digest into
 * dst[SHA256_HEX_SIZE].  Returns 1 on success, 0 if missing or
 * malformed.  Trailing newline tolerated. */
static s32 romExtractReadSidecar(const char *binRel, char dst[SHA256_HEX_SIZE])
{
    char sidecarRel[ROMEXTRACT_PATH_LEN];
    snprintf(sidecarRel, sizeof(sidecarRel), "%s.sha256", binRel);

    u32 size = 0;
    void *bytes = fsFileLoad(sidecarRel, &size);
    if (bytes == NULL || size < 64) {
        if (bytes) sysMemFree(bytes);
        return 0;
    }
    memcpy(dst, bytes, 64);
    dst[64] = '\0';
    sysMemFree(bytes);

    /* Validate hex; reject if any non-hex char in the leading 64. */
    for (s32 i = 0; i < 64; i++) {
        char c = dst[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }
    return 1;
}

/* Move a corrupted extracted file to the quarantine area before
 * re-extracting it.  Quarantine path layout (Pass D, 2026-05-02):
 *   data/_quarantine/<romid>/<unixtime>_<basename>
 *
 * The top-level `data/_quarantine/` directory keeps quarantined files
 * visible to the user (no leading dot to hide on Windows file managers)
 * and outside any per-romid tree that gets wiped on a clean re-extract.
 * The per-romid sub-tier preserves cross-version isolation so a JPN
 * quarantine never collides with an NTSC one.
 *
 * Best-effort: failure to move logs LOUDFAIL but does not block the
 * re-extract that follows. */
static void romExtractQuarantine(const char *binRel, const char *binFull)
{
    char quarTopRel[ROMEXTRACT_PATH_LEN];
    snprintf(quarTopRel, sizeof(quarTopRel), "data/_quarantine");
    fsCreateDir(quarTopRel);

    char quarRel[ROMEXTRACT_PATH_LEN];
    snprintf(quarRel, sizeof(quarRel),
        "data/_quarantine/%s", VERSION_ROMID);
    fsCreateDir(quarRel);

    /* Take basename of binRel (everything after the last slash). */
    const char *base = binRel;
    for (const char *p = binRel; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }

    char destRel[ROMEXTRACT_PATH_LEN];
    snprintf(destRel, sizeof(destRel),
        "data/_quarantine/%s/%lld_%s",
        VERSION_ROMID, (long long)time(NULL), base);

    char destFullBuf[FS_MAXPATH + 1];
    const char *destFull = fsFullPath(destRel, destFullBuf, sizeof(destFullBuf));
    if (destFull == NULL || destFull[0] == '\0') {
        sysLoudFailf("LOAD",
            "quarantine path resolution failed for \"%s\"", destRel);
        return;
    }

    if (rename(binFull, destFull) != 0) {
        sysLoudFailf("LOAD",
            "rename(\"%s\" -> \"%s\") failed; deleting in place instead",
            binFull, destFull);
        remove(binFull);
    }
}

/* ========================================================================
 * Phase 3 Pass D (2026-05-02): self-heal hardening surfaces.
 *
 * Pass A.4 + the segment verify pair already do hash-verify-on-launch
 * with quarantine + re-extract on mismatch.  Pass D layers user-facing
 * polish on top:
 *   1. Per-file UI toasts on recovery / unrecoverable outcomes.
 *   2. Aggregated boot integrity report (single LOG_NOTE + system toast).
 *   3. Toast queue drained from the render-ready phase of boot so
 *      pdguiToastEnqueue timestamps are fresh enough to be visible.
 *
 * pdguiToastInit runs at main.c:233 (well before the verify pair), so
 * the toast array is live during verify.  However, pdguiToastTick /
 * pdguiToastRender don't run until the main game loop is up.  Direct
 * enqueues during boot would stamp `enqueued_ms` with SDL_GetTicks at
 * boot time; by the time the render loop rolls, the toast hold (5 s)
 * has typically expired and the toast fades out before the first frame.
 * Pass D queues toasts in a private buffer; main.c drains the queue
 * with fresh timestamps after gameInit so the player actually sees the
 * notification.
 *
 * Server build: PD_SERVER guard around the queue + pdgui call.  The
 * counter aggregates and the boot-integrity LOG_NOTE compile in both
 * builds (verify functions early-return server-side, so counters stay
 * zero and the report is a no-op summary).
 * ====================================================================== */

#define ROMEXTRACT_TOAST_TITLE_LEN 96
#define ROMEXTRACT_TOAST_BODY_LEN 192
#define ROMEXTRACT_TOAST_QUEUE_MAX 16

/* Cap per-file toasts so wholesale corruption (e.g. user wiped data/
 * mid-session) does not flood the queue.  The aggregate boot-integrity
 * toast carries the totals beyond the cap. */
#define ROMEXTRACT_TOAST_PERFILE_CAP 5

#if !defined(PD_SERVER)
typedef struct {
    u32  category;
    char title[ROMEXTRACT_TOAST_TITLE_LEN];
    char body[ROMEXTRACT_TOAST_BODY_LEN];
} romextract_deferred_toast_t;

static romextract_deferred_toast_t s_BootToasts[ROMEXTRACT_TOAST_QUEUE_MAX];
static s32 s_NumBootToasts = 0;
static s32 s_PerFileToastsEmitted = 0;
#endif

/* Aggregated counters across files + segments.  Set by the verify
 * funcs as they run; consumed by romExtractEmitBootIntegrityReport. */
static s32 s_AggValidated = 0;
static s32 s_AggRecovered = 0;
static s32 s_AggUnrecoverable = 0;
static s32 s_AggReportEmitted = 0;

/* Extract the basename of a relative path (everything after the final
 * separator).  Stable buffer: caller may copy out of the returned
 * pointer because it points into the input string. */
static const char *romExtractBasename(const char *rel)
{
    const char *base = rel;
    if (rel == NULL) return "";
    for (const char *p = rel; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    return base;
}

#if !defined(PD_SERVER)
/* Append a deferred toast to the boot queue.  Drops the oldest entry
 * if the queue is full so the most-recent corruption events are the
 * ones the player sees.  Truncates strings safely. */
static void s_deferBootToast(u32 category, const char *title, const char *body)
{
    if (s_NumBootToasts >= ROMEXTRACT_TOAST_QUEUE_MAX) {
        memmove(&s_BootToasts[0], &s_BootToasts[1],
            (size_t)(ROMEXTRACT_TOAST_QUEUE_MAX - 1)
            * sizeof(s_BootToasts[0]));
        s_NumBootToasts = ROMEXTRACT_TOAST_QUEUE_MAX - 1;
    }
    romextract_deferred_toast_t *t = &s_BootToasts[s_NumBootToasts++];
    t->category = category;
    if (title) {
        strncpy(t->title, title, ROMEXTRACT_TOAST_TITLE_LEN - 1);
        t->title[ROMEXTRACT_TOAST_TITLE_LEN - 1] = '\0';
    } else {
        t->title[0] = '\0';
    }
    if (body) {
        strncpy(t->body, body, ROMEXTRACT_TOAST_BODY_LEN - 1);
        t->body[ROMEXTRACT_TOAST_BODY_LEN - 1] = '\0';
    } else {
        t->body[0] = '\0';
    }
}

static void s_emitPerFileRecoverToast(const char *kind, const char *name)
{
    if (s_PerFileToastsEmitted >= ROMEXTRACT_TOAST_PERFILE_CAP) return;
    char title[ROMEXTRACT_TOAST_TITLE_LEN];
    char body[ROMEXTRACT_TOAST_BODY_LEN];
    snprintf(title, sizeof(title), "%s recovered", kind);
    snprintf(body, sizeof(body),
        "Re-extracted %s from ROM after corruption.", name);
    s_deferBootToast(TOAST_CATEGORY_SYSTEM, title, body);
    s_PerFileToastsEmitted++;
}

static void s_emitPerFileFailToast(const char *kind, const char *name)
{
    if (s_PerFileToastsEmitted >= ROMEXTRACT_TOAST_PERFILE_CAP) return;
    char title[ROMEXTRACT_TOAST_TITLE_LEN];
    char body[ROMEXTRACT_TOAST_BODY_LEN];
    snprintf(title, sizeof(title), "%s unrecoverable", kind);
    snprintf(body, sizeof(body),
        "Could not recover %s. Verify your ROM.", name);
    s_deferBootToast(TOAST_CATEGORY_SYSTEM, title, body);
    s_PerFileToastsEmitted++;
}
#else  /* PD_SERVER */
static void s_emitPerFileRecoverToast(const char *kind, const char *name)
{
    (void)kind; (void)name;
}
static void s_emitPerFileFailToast(const char *kind, const char *name)
{
    (void)kind; (void)name;
}
#endif

s32 romExtractAllFiles(void)
{
    s32 written = 0;
    s32 skippedExisting = 0;
    s32 skippedEmpty = 0;
    s32 failed = 0;

    if (g_RomFile == NULL || g_RomFileSize == 0) {
        sysLogPrintf(LOG_NOTE,
            "ROMEXTRACT: g_RomFile not loaded (server build or pre-romdata-init); "
            "skipping extraction.");
        return 0;
    }

    if (!fsDataDirEnsure()) {
        /* fsDataDirEnsure already emits LOUDFAIL.DATA on failure. */
        return -1;
    }

    /* Ensure data/<romid>/files/ exists.  fsCreateDir is idempotent. */
    {
        char filesRel[ROMEXTRACT_PATH_LEN];
        snprintf(filesRel, sizeof(filesRel),
            "data/%s/files", VERSION_ROMID);
        fsCreateDir(filesRel);
    }

    sysLogPrintf(LOG_NOTE,
        "ROMEXTRACT: starting first-launch extraction (g_RomFileSize=%u, "
        "max_files=%d, target=data/%s/files/)",
        g_RomFileSize, ROMEXTRACT_MAX_FILES, VERSION_ROMID);

    bootProgressUpdate(0, ROMEXTRACT_MAX_FILES);

    for (s32 fileNum = 1; fileNum < ROMEXTRACT_MAX_FILES; fileNum++) {
        /* Update overlay bar every 32 files (cheap mutex; bar is
         * sampled at 60Hz on the main thread anyway). */
        if ((fileNum & 0x1f) == 0) {
            bootProgressUpdate(fileNum, ROMEXTRACT_MAX_FILES);
        }

        u8  *data = romdataFileGetData(fileNum);
        s32  size = romdataFileGetSize(fileNum);

        if (data == NULL || size <= 0) {
            skippedEmpty++;
            continue;
        }

        char outRel[ROMEXTRACT_PATH_LEN];
        s32  relLen = romExtractBuildRelPath(fileNum, outRel, (s32)sizeof(outRel));
        if (relLen <= 0 || relLen >= (s32)sizeof(outRel)) {
            sysLoudFailf("EXTRACT",
                "path build failed for fileNum=%d (relLen=%d)", fileNum, relLen);
            failed++;
            continue;
        }

        char outFullBuf[FS_MAXPATH + 1];
        const char *outFull = fsFullPath(outRel, outFullBuf, sizeof(outFullBuf));
        if (outFull == NULL || outFull[0] == '\0') {
            sysLoudFailf("EXTRACT",
                "fsFullPath returned empty for \"%s\"", outRel);
            failed++;
            continue;
        }

        if (romExtractFileMatchesSize(outFull, (u32)size)) {
            skippedExisting++;
            continue;
        }

        FILE *f = fsFileOpenWrite(outRel);
        if (f == NULL) {
            sysLoudFailf("EXTRACT",
                "fsFileOpenWrite failed for \"%s\" (fileNum=%d size=%d)",
                outFull, fileNum, size);
            failed++;
            continue;
        }

        size_t wrote = fwrite(data, 1, (size_t)size, f);
        fclose(f);

        if (wrote != (size_t)size) {
            sysLoudFailf("EXTRACT",
                "short write for \"%s\" (wrote=%zu, expected=%d)",
                outFull, wrote, size);
            failed++;
            continue;
        }

        /* Pass A.4: write a SHA-256 sidecar so romExtractVerifyAll can
         * detect corruption on subsequent boots and self-heal.  Sidecar
         * write failure is non-fatal (file is still valid; verify will
         * just re-write the sidecar from the file's hash next boot). */
        romExtractWriteSidecar(outRel, data, (u32)size);

        written++;
    }

    sysLogPrintf(LOG_NOTE,
        "ROMEXTRACT: complete. wrote=%d, skipped_existing=%d, "
        "skipped_empty=%d, failed=%d",
        written, skippedExisting, skippedEmpty, failed);

    return written;
}

/* ========================================================================
 * Pass A.4 (2026-05-02): hash-verify-on-launch self-heal
 * ========================================================================
 *
 * Walks every previously-extracted file and verifies its SHA-256
 * digest against the sidecar written at extraction time.  On
 * mismatch:
 *   1. Emit LOUDFAIL.LOAD with file path + expected/actual hashes
 *   2. Move the corrupted file to data/_quarantine/<romid>/<ts>_<name>
 *      (Pass D path, 2026-05-02; see romExtractQuarantine docblock)
 *   3. Re-extract from g_RomFile via romdataFileGetData(filenum) and
 *      rewrite the sidecar
 *
 * Self-heal failure (e.g. ROM no longer available, disk full) does
 * not abort boot.  The corrupted file remains quarantined and the
 * caller (subsequent runtime load) gets a clear diagnostic.
 *
 * Idempotent: if a file's sidecar matches, no work happens.  Files
 * without sidecars (legacy from a pre-A.4 extraction) get a sidecar
 * written from the on-disk content's hash on first verify -- this
 * is a best-effort baseline; if the on-disk content was corrupted
 * BEFORE A.4 shipped, the baseline pins the corruption and only
 * a forced re-extract (e.g. delete `data/<romid>/files/`) recovers.
 *
 * ========================================================================
 * Engine Phase 3 (2026-05-03): parallel verify pass.
 * ========================================================================
 *
 * The serial loop above ran ~8s on Mike's machine: SHA-256 + 8 KiB
 * streaming reads of 2011 files in lockstep.  Phase 3 fans the per-file
 * work out across the boot pool's worker threads.
 *
 * Threading model:
 *   - Shared cursor `next_file_num` (mutex-protected) is the work queue.
 *     Each worker pulls one file at a time -- avoids static partitioning
 *     so a slow file doesn't stall a worker's batch.
 *   - Per-thread accumulators (verify_thread_state_t).  No shared write
 *     contention inside the per-file work.  Workers merge their counters
 *     and event list into the batch under the mutex when their drain ends.
 *   - Failure events deferred: workers append (kind, name, recovered)
 *     records to thread-local lists; the manager replays them serially
 *     after all workers complete so the existing s_PerFileToastsEmitted
 *     cap stays correct.  Per-file LOUDFAIL log lines still fire from
 *     workers (sysLogPrintf is mutex-protected).
 *   - The manager (bootRunCatalogWork) runs on a boot-pool worker
 *     thread; it cannot call bootPoolWaitIdle (would deadlock waiting
 *     on its own in_flight slot).  We use a per-batch latch instead.
 *   - Manager dispatches (worker_count - 1) verifyWorkerFn jobs and
 *     calls verifyWorkerFn directly to saturate the pool.  When
 *     worker_count == 1 the manager is the only worker -- still
 *     correct, no fan-out speedup.
 *
 * Reentrancy audit (per startup-acceleration.md Phase 1+3):
 *   - sha256HashFile / sha256Hash / sha256ToHex: stack-local ctx, own FILE*.
 *   - fsFullPath / fsFileOpenWrite / fsFileLoad: out-buffer form (Phase 1).
 *   - sysLoudFailf / sysLogPrintf: mutex-protected.
 *   - sysMemAlloc / sysMemFree: SDL mutex registered at boot.
 *   - romdataFileGetData / romdataFileGetSize: safe AFTER
 *     romExtractAllFiles loaded every slot; the SRC_UNLOADED branch
 *     does not fire on second visit.
 *   - rename() / mkdir() on per-file paths: independent across files.
 */

#define VERIFY_EVENT_KIND_LEN 16
#define VERIFY_EVENT_NAME_LEN 96
#define VERIFY_THREAD_EVENT_CAP 32
#define VERIFY_BATCH_EVENT_CAP  256

typedef struct {
    char kind[VERIFY_EVENT_KIND_LEN];
    char name[VERIFY_EVENT_NAME_LEN];
    int  recovered;  /* 1 = recovered, 0 = unrecoverable */
} verify_event_t;

typedef struct {
    int verified;
    int corrected;
    int baselined;
    int skippedEmpty;
    int failed;
    int event_count;
    verify_event_t events[VERIFY_THREAD_EVENT_CAP];
} verify_thread_state_t;

typedef struct {
    /* Shared work cursor.  Workers pull next_file_num++ under the mutex
     * until it reaches max_file_num.  One file per pull keeps load
     * balanced across workers. */
    int        next_file_num;
    int        max_file_num;
    SDL_mutex *mutex;

    /* Aggregated counters merged from each worker's thread-local state
     * once that worker drains. */
    int verified;
    int corrected;
    int baselined;
    int skippedEmpty;
    int failed;

    /* Aggregated event list for deferred toast emission. */
    verify_event_t events[VERIFY_BATCH_EVENT_CAP];
    int            event_count;

    /* Atomic running total of files processed.  Workers push the value
     * to the boot progress channel every 32 files so the overlay bar
     * advances visibly without per-file mutex contention. */
    SDL_atomic_t files_done;

    /* Worker-completion latch.  Initialised to total participant count
     * (dispatched workers + the manager itself).  Each worker
     * decrements after merging; the last one signals cv_done. */
    int       workers_remaining;
    SDL_cond *cv_done;
} verify_batch_t;

static void s_verifyAppendEvent(verify_thread_state_t *th, const char *kind,
                                const char *name, int recovered)
{
    if (th->event_count >= VERIFY_THREAD_EVENT_CAP) return;
    verify_event_t *e = &th->events[th->event_count++];
    strncpy(e->kind, kind, VERIFY_EVENT_KIND_LEN - 1);
    e->kind[VERIFY_EVENT_KIND_LEN - 1] = '\0';
    if (name) {
        strncpy(e->name, name, VERIFY_EVENT_NAME_LEN - 1);
        e->name[VERIFY_EVENT_NAME_LEN - 1] = '\0';
    } else {
        e->name[0] = '\0';
    }
    e->recovered = recovered;
}

/* Per-file verify body.  Equivalent to one iteration of the original
 * serial loop, but operates entirely on thread-local accumulators so
 * concurrent workers do not contend on shared writes. */
static void s_verifyOneFile(s32 fileNum, verify_thread_state_t *th)
{
    u8  *romData = romdataFileGetData(fileNum);
    s32  romSize = romdataFileGetSize(fileNum);
    if (romData == NULL || romSize <= 0) {
        th->skippedEmpty++;
        return;
    }

    char outRel[ROMEXTRACT_PATH_LEN];
    s32  relLen = romExtractBuildRelPath(fileNum, outRel, (s32)sizeof(outRel));
    if (relLen <= 0 || relLen >= (s32)sizeof(outRel)) {
        th->failed++;
        return;
    }

    char outFullBuf[FS_MAXPATH + 1];
    const char *outFull = fsFullPath(outRel, outFullBuf, sizeof(outFullBuf));
    if (outFull == NULL || outFull[0] == '\0') {
        th->failed++;
        return;
    }

    /* Skip files that don't exist on disk yet (caller should run
     * romExtractAllFiles first; verify is per-file, not first-run). */
    struct stat st;
    if (stat(outFull, &st) != 0) {
        th->skippedEmpty++;
        return;
    }

    /* Hash the on-disk file. */
    u8 diskDigest[SHA256_DIGEST_SIZE];
    if (sha256HashFile(outFull, diskDigest) != 0) {
        sysLoudFailf("LOAD",
            "sha256HashFile failed for \"%s\"; re-extracting", outFull);
        romExtractQuarantine(outRel, outFull);
        FILE *f = fsFileOpenWrite(outRel);
        if (f) {
            fwrite(romData, 1, (size_t)romSize, f);
            fclose(f);
            romExtractWriteSidecar(outRel, romData, (u32)romSize);
            th->corrected++;
            s_verifyAppendEvent(th, "File", romExtractBasename(outRel), 1);
        } else {
            th->failed++;
            s_verifyAppendEvent(th, "File", romExtractBasename(outRel), 0);
        }
        return;
    }
    char diskHex[SHA256_HEX_SIZE];
    sha256ToHex(diskDigest, diskHex);

    char sideHex[SHA256_HEX_SIZE];
    if (!romExtractReadSidecar(outRel, sideHex)) {
        /* Legacy file from pre-A.4 extraction: write a baseline
         * sidecar from the current on-disk content's hash. */
        romExtractWriteSidecar(outRel, romData, (u32)romSize);
        th->baselined++;
        return;
    }

    if (strncmp(diskHex, sideHex, 64) != 0) {
        sysLoudFailf("LOAD",
            "SHA-256 mismatch on \"%s\" (expected %s, got %s); "
            "quarantining + re-extracting",
            outFull, sideHex, diskHex);
        romExtractQuarantine(outRel, outFull);

        FILE *f = fsFileOpenWrite(outRel);
        if (f == NULL) {
            sysLoudFailf("LOAD",
                "re-extract fopen failed for \"%s\"", outFull);
            th->failed++;
            s_verifyAppendEvent(th, "File", romExtractBasename(outRel), 0);
            return;
        }
        size_t wrote = fwrite(romData, 1, (size_t)romSize, f);
        fclose(f);
        if (wrote != (size_t)romSize) {
            sysLoudFailf("LOAD",
                "re-extract short write for \"%s\"", outFull);
            th->failed++;
            s_verifyAppendEvent(th, "File", romExtractBasename(outRel), 0);
            return;
        }
        romExtractWriteSidecar(outRel, romData, (u32)romSize);
        th->corrected++;
        s_verifyAppendEvent(th, "File", romExtractBasename(outRel), 1);
        return;
    }

    th->verified++;
}

/* Boot-pool worker: drains the verify batch.  Pulls one file at a time
 * from the shared cursor; merges thread-local results into the batch
 * once the cursor is exhausted. */
static void s_verifyWorkerFn(void *arg)
{
    verify_batch_t *b = (verify_batch_t *)arg;
    verify_thread_state_t th;
    memset(&th, 0, sizeof(th));

    for (;;) {
        s32 fileNum;
        SDL_LockMutex(b->mutex);
        if (b->next_file_num >= b->max_file_num) {
            SDL_UnlockMutex(b->mutex);
            break;
        }
        fileNum = b->next_file_num++;
        SDL_UnlockMutex(b->mutex);

        s_verifyOneFile(fileNum, &th);

        /* Push global progress every 32 files; bar redraws at ~30Hz so
         * sub-32 granularity is not visible to the user. */
        int done = SDL_AtomicAdd(&b->files_done, 1) + 1;
        if ((done & 0x1f) == 0) {
            bootProgressUpdate(done, b->max_file_num);
        }
    }

    /* Merge thread-local results into the batch.  One mutex acquisition
     * per worker (not per file) keeps contention negligible. */
    SDL_LockMutex(b->mutex);
    b->verified     += th.verified;
    b->corrected    += th.corrected;
    b->baselined    += th.baselined;
    b->skippedEmpty += th.skippedEmpty;
    b->failed       += th.failed;

    int copy = th.event_count;
    if (b->event_count + copy > VERIFY_BATCH_EVENT_CAP) {
        copy = VERIFY_BATCH_EVENT_CAP - b->event_count;
    }
    if (copy > 0) {
        memcpy(&b->events[b->event_count], th.events,
               (size_t)copy * sizeof(verify_event_t));
        b->event_count += copy;
    }

    b->workers_remaining--;
    if (b->workers_remaining == 0) {
        SDL_CondSignal(b->cv_done);
    }
    SDL_UnlockMutex(b->mutex);
}

s32 romExtractVerifyAll(void)
{
    if (g_RomFile == NULL || g_RomFileSize == 0) {
        sysLogPrintf(LOG_NOTE,
            "ROMEXTRACT.VERIFY: g_RomFile not loaded (server build); skipping.");
        return 0;
    }

    sysLogPrintf(LOG_NOTE, "ROMEXTRACT.VERIFY: scanning data/%s/files/",
                 VERSION_ROMID);

    bootProgressUpdate(0, ROMEXTRACT_MAX_FILES);

    /* worker_count is the boot pool's thread count (cores - 2).  We
     * dispatch (worker_count - 1) parallel jobs and run one worker
     * inline on the manager so the pool is fully saturated.  Floor 1
     * keeps the architecture working on minimal hardware. */
    int worker_count = bootPoolGetWorkerCount();
    if (worker_count < 1) worker_count = 1;

    verify_batch_t batch;
    memset(&batch, 0, sizeof(batch));
    batch.next_file_num     = 1;
    batch.max_file_num      = ROMEXTRACT_MAX_FILES;
    batch.mutex             = SDL_CreateMutex();
    batch.cv_done           = SDL_CreateCond();
    batch.workers_remaining = worker_count;
    SDL_AtomicSet(&batch.files_done, 0);

    /* Dispatch (worker_count - 1) parallel workers.  When worker_count
     * is 1, dispatch nothing -- the manager runs everything inline. */
    for (int w = 0; w < worker_count - 1; w++) {
        bootPoolEnqueue(s_verifyWorkerFn, &batch);
    }

    /* Manager participates so the pool stays saturated and the
     * worker_count == 1 case still drains.  CANNOT use bootPoolWaitIdle
     * here -- this thread is one of the pool's workers and would
     * deadlock waiting on its own in_flight slot. */
    s_verifyWorkerFn(&batch);

    /* Wait for any still-running parallel workers to finish.  cv_done
     * is signalled by the last worker to merge its results. */
    SDL_LockMutex(batch.mutex);
    while (batch.workers_remaining > 0) {
        SDL_CondWait(batch.cv_done, batch.mutex);
    }
    SDL_UnlockMutex(batch.mutex);

    /* Pull merged outcomes off the batch into named locals so the log
     * line + aggregate update read the same as the segment-side path
     * (the structural pin that test_romextract_passd.cpp checks). */
    s32 verified     = batch.verified;
    s32 corrected    = batch.corrected;
    s32 baselined    = batch.baselined;
    s32 skippedEmpty = batch.skippedEmpty;
    s32 failed       = batch.failed;

    sysLogPrintf(LOG_NOTE,
        "ROMEXTRACT.VERIFY: verified=%d, corrected=%d, baselined=%d, "
        "skipped_empty=%d, failed=%d",
        verified, corrected, baselined, skippedEmpty, failed);

    /* Replay deferred per-file events SERIALLY so the existing
     * s_PerFileToastsEmitted cap remains correct.  Per-file LOUDFAIL
     * log lines already fired inside the workers (sysLogPrintf is
     * mutex-protected) so every failure is in the log; the toast
     * cap is intentional UX (don't flood the player). */
    for (int i = 0; i < batch.event_count; i++) {
        const verify_event_t *e = &batch.events[i];
        if (e->recovered) {
            s_emitPerFileRecoverToast(e->kind, e->name);
        } else {
            s_emitPerFileFailToast(e->kind, e->name);
        }
    }

    /* Pass D aggregates: validated = verified + baselined (both clean
     * from the user's perspective; baselined is a one-shot legacy
     * upgrade); recovered = corrected; unrecoverable = failed. */
    s_AggValidated     += verified + baselined;
    s_AggRecovered     += corrected;
    s_AggUnrecoverable += failed;

    /* Final progress push so the bar reaches the phase end before
     * EndPhase claims the full weight. */
    bootProgressUpdate(ROMEXTRACT_MAX_FILES, ROMEXTRACT_MAX_FILES);

    SDL_DestroyMutex(batch.mutex);
    SDL_DestroyCond(batch.cv_done);
    return corrected;
}

/* ========================================================================
 * Phase 3 Pass B Slices 2/5/6/8/11 (2026-05-02): segment extraction.
 * Mirror of romExtractAllFiles + romExtractVerifyAll for ROM segments.
 * Segments live at fixed ROM offsets and are loaded into memory by
 * romdataInitSegment; we walk the loaded buffers and dump them to
 * data/<romid>/segs/<segname>.bin so subsequent boots can read from
 * disk via the per-romid path the segment loader checks first.
 * ======================================================================== */

static s32 romExtractBuildSegRelPath(const char *segName, char *outRel, s32 outRelLen)
{
    char nameBuf[128];
    if (segName == NULL || segName[0] == '\0') {
        return 0;
    }
    romExtractSanitizeName(segName, nameBuf, (s32)sizeof(nameBuf));
    return snprintf(outRel, (size_t)outRelLen,
                    "data/%s/segs/%s.bin", VERSION_ROMID, nameBuf);
}

s32 romExtractSegmentRelPath(const char *segName, char *outRel, s32 outRelLen)
{
    if (outRel == NULL || outRelLen <= 0) return 0;
    s32 len = romExtractBuildSegRelPath(segName, outRel, outRelLen);
    if (len <= 0 || len >= outRelLen) {
        outRel[0] = '\0';
        return 0;
    }
    return len;
}

s32 romExtractAllSegments(void)
{
    s32 written = 0;
    s32 skippedExisting = 0;
    s32 skippedEmpty = 0;
    s32 failed = 0;

    if (g_RomFile == NULL || g_RomFileSize == 0) {
        sysLogPrintf(LOG_NOTE,
            "ROMEXTRACT.SEGS: g_RomFile not loaded (server build or "
            "pre-romdata-init); skipping segment extraction.");
        return 0;
    }

    if (!fsDataDirEnsure()) {
        return -1;
    }

    /* Ensure data/<romid>/segs/ exists. */
    {
        char segsRel[ROMEXTRACT_PATH_LEN];
        snprintf(segsRel, sizeof(segsRel),
            "data/%s/segs", VERSION_ROMID);
        fsCreateDir(segsRel);
    }

    s32 segCount = romdataSegmentCount();
    sysLogPrintf(LOG_NOTE,
        "ROMEXTRACT.SEGS: starting first-launch segment extraction "
        "(segments=%d, target=data/%s/segs/)",
        segCount, VERSION_ROMID);

    bootProgressUpdate(0, segCount);

    for (s32 idx = 0; idx < segCount; idx++) {
        bootProgressUpdate(idx, segCount);

        const u8   *data = romdataSegmentGetData(idx);
        u32         size = romdataSegmentGetSize(idx);
        const char *name = romdataSegmentGetName(idx);

        if (data == NULL || size == 0 || name == NULL || name[0] == '\0') {
            skippedEmpty++;
            continue;
        }

        char outRel[ROMEXTRACT_PATH_LEN];
        s32  relLen = romExtractBuildSegRelPath(name, outRel, (s32)sizeof(outRel));
        if (relLen <= 0 || relLen >= (s32)sizeof(outRel)) {
            sysLoudFailf("EXTRACT",
                "seg path build failed for \"%s\" (relLen=%d)", name, relLen);
            failed++;
            continue;
        }

        char outFullBuf[FS_MAXPATH + 1];
        const char *outFull = fsFullPath(outRel, outFullBuf, sizeof(outFullBuf));
        if (outFull == NULL || outFull[0] == '\0') {
            sysLoudFailf("EXTRACT",
                "fsFullPath returned empty for seg \"%s\"", outRel);
            failed++;
            continue;
        }

        if (romExtractFileMatchesSize(outFull, size)) {
            skippedExisting++;
            continue;
        }

        FILE *f = fsFileOpenWrite(outRel);
        if (f == NULL) {
            sysLoudFailf("EXTRACT",
                "fsFileOpenWrite failed for seg \"%s\" (size=%u)",
                outFull, size);
            failed++;
            continue;
        }

        size_t wrote = fwrite(data, 1, (size_t)size, f);
        fclose(f);

        if (wrote != (size_t)size) {
            sysLoudFailf("EXTRACT",
                "short write for seg \"%s\" (wrote=%zu, expected=%u)",
                outFull, wrote, size);
            failed++;
            continue;
        }

        romExtractWriteSidecar(outRel, data, size);
        written++;
    }

    sysLogPrintf(LOG_NOTE,
        "ROMEXTRACT.SEGS: complete. wrote=%d, skipped_existing=%d, "
        "skipped_empty=%d, failed=%d",
        written, skippedExisting, skippedEmpty, failed);

    return written;
}

s32 romExtractVerifyAllSegments(void)
{
    s32 verified = 0;
    s32 corrected = 0;
    s32 skippedEmpty = 0;
    s32 baselined = 0;
    s32 failed = 0;

    if (g_RomFile == NULL || g_RomFileSize == 0) {
        sysLogPrintf(LOG_NOTE,
            "ROMEXTRACT.SEGS.VERIFY: g_RomFile not loaded; skipping.");
        return 0;
    }

    sysLogPrintf(LOG_NOTE,
        "ROMEXTRACT.SEGS.VERIFY: scanning data/%s/segs/", VERSION_ROMID);

    s32 segCount = romdataSegmentCount();
    bootProgressUpdate(0, segCount);
    for (s32 idx = 0; idx < segCount; idx++) {
        bootProgressUpdate(idx, segCount);

        const u8   *segData = romdataSegmentGetData(idx);
        u32         segSize = romdataSegmentGetSize(idx);
        const char *segName = romdataSegmentGetName(idx);

        if (segData == NULL || segSize == 0 || segName == NULL || segName[0] == '\0') {
            skippedEmpty++;
            continue;
        }

        char outRel[ROMEXTRACT_PATH_LEN];
        s32  relLen = romExtractBuildSegRelPath(segName, outRel, (s32)sizeof(outRel));
        if (relLen <= 0 || relLen >= (s32)sizeof(outRel)) {
            failed++;
            continue;
        }

        char outFullBuf[FS_MAXPATH + 1];
        const char *outFull = fsFullPath(outRel, outFullBuf, sizeof(outFullBuf));
        if (outFull == NULL || outFull[0] == '\0') {
            failed++;
            continue;
        }

        struct stat st;
        if (stat(outFull, &st) != 0) {
            /* Segment not yet on disk -- caller should have run
             * romExtractAllSegments first; not a verify failure. */
            skippedEmpty++;
            continue;
        }

        u8 diskDigest[SHA256_DIGEST_SIZE];
        if (sha256HashFile(outFull, diskDigest) != 0) {
            sysLoudFailf("LOAD",
                "sha256HashFile failed for seg \"%s\"; re-extracting", outFull);
            romExtractQuarantine(outRel, outFull);
            FILE *f = fsFileOpenWrite(outRel);
            if (f) {
                fwrite(segData, 1, (size_t)segSize, f);
                fclose(f);
                romExtractWriteSidecar(outRel, segData, segSize);
                corrected++;
                s_emitPerFileRecoverToast("Segment",
                    romExtractBasename(outRel));
            } else {
                failed++;
                s_emitPerFileFailToast("Segment",
                    romExtractBasename(outRel));
            }
            continue;
        }

        char sideHex[SHA256_HEX_SIZE];
        if (romExtractReadSidecar(outRel, sideHex) == 0) {
            /* No sidecar -- baseline from on-disk content's hash. */
            romExtractWriteSidecar(outRel, segData, segSize);
            baselined++;
            continue;
        }

        char diskHex[SHA256_HEX_SIZE];
        for (s32 i = 0; i < SHA256_DIGEST_SIZE; i++) {
            snprintf(diskHex + i * 2, 3, "%02x", diskDigest[i]);
        }

        if (strncmp(diskHex, sideHex, 64) != 0) {
            sysLoudFailf("LOAD",
                "SHA-256 mismatch on seg \"%s\" (expected %s, got %s); "
                "quarantining + re-extracting",
                outFull, sideHex, diskHex);
            romExtractQuarantine(outRel, outFull);

            FILE *f = fsFileOpenWrite(outRel);
            if (f == NULL) {
                sysLoudFailf("LOAD",
                    "re-extract fopen failed for seg \"%s\"", outFull);
                failed++;
                s_emitPerFileFailToast("Segment",
                    romExtractBasename(outRel));
                continue;
            }
            size_t wrote = fwrite(segData, 1, (size_t)segSize, f);
            fclose(f);
            if (wrote != (size_t)segSize) {
                sysLoudFailf("LOAD",
                    "re-extract short write for seg \"%s\"", outFull);
                failed++;
                s_emitPerFileFailToast("Segment",
                    romExtractBasename(outRel));
                continue;
            }
            romExtractWriteSidecar(outRel, segData, segSize);
            corrected++;
            s_emitPerFileRecoverToast("Segment",
                romExtractBasename(outRel));
            continue;
        }

        verified++;
    }

    sysLogPrintf(LOG_NOTE,
        "ROMEXTRACT.SEGS.VERIFY: verified=%d, corrected=%d, baselined=%d, "
        "skipped_empty=%d, failed=%d",
        verified, corrected, baselined, skippedEmpty, failed);

    /* Pass D aggregates -- shared with the file-side verify counters. */
    s_AggValidated += verified + baselined;
    s_AggRecovered += corrected;
    s_AggUnrecoverable += failed;

    return corrected;
}

/* ========================================================================
 * Phase 3 Pass D (2026-05-02): public surfaces.
 * ======================================================================== */

void romExtractToastDrain(void)
{
#if !defined(PD_SERVER)
    if (s_NumBootToasts == 0) return;

    s32 drained = 0;
    for (s32 i = 0; i < s_NumBootToasts; i++) {
        const romextract_deferred_toast_t *t = &s_BootToasts[i];
        if (pdguiToastEnqueue(0, t->category, t->title, t->body)) {
            drained++;
        }
    }
    sysLogPrintf(LOG_NOTE,
        "ROMEXTRACT.TOAST: drained %d/%d deferred boot toast(s).",
        drained, s_NumBootToasts);
    s_NumBootToasts = 0;
#endif
}

void romExtractEmitBootIntegrityReport(void)
{
    if (s_AggReportEmitted) return;
    s_AggReportEmitted = 1;

    sysLogPrintf(LOG_NOTE,
        "DATA INTEGRITY: %d validated, %d re-extracted, %d unrecoverable",
        s_AggValidated, s_AggRecovered, s_AggUnrecoverable);

    if (s_AggUnrecoverable > 0) {
        sysLogPrintf(LOG_WARNING,
            "DATA INTEGRITY: %d unrecoverable file(s)/segment(s); the "
            "user's ROM may be corrupted or different from the original "
            "extraction. Verify the ROM and the data/_quarantine/ "
            "subdirectory.",
            s_AggUnrecoverable);
#if !defined(PD_SERVER)
        char body[ROMEXTRACT_TOAST_BODY_LEN];
        snprintf(body, sizeof(body),
            "%d asset(s) could not be recovered. Verify your ROM.",
            s_AggUnrecoverable);
        s_deferBootToast(TOAST_CATEGORY_SYSTEM,
            "ROM integrity check failed", body);
#endif
    } else if (s_AggRecovered > 0) {
#if !defined(PD_SERVER)
        char body[ROMEXTRACT_TOAST_BODY_LEN];
        snprintf(body, sizeof(body),
            "%d asset(s) re-extracted from ROM after corruption.",
            s_AggRecovered);
        s_deferBootToast(TOAST_CATEGORY_SYSTEM,
            "Boot data integrity recovered", body);
#endif
    }
    /* All clean -- the LOG_NOTE line is the silent confirmation; no
     * toast is shown so the player is not nagged on every boot. */
}

s32 romExtractGetBootIntegrity(s32 *validated, s32 *recovered,
                               s32 *unrecoverable)
{
    if (validated) *validated = s_AggValidated;
    if (recovered) *recovered = s_AggRecovered;
    if (unrecoverable) *unrecoverable = s_AggUnrecoverable;
    return s_AggValidated + s_AggRecovered + s_AggUnrecoverable;
}

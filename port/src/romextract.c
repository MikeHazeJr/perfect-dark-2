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
#include <PR/ultratypes.h>

#include "fs.h"
#include "romdata.h"
#include "romextract.h"
#include "sha256.h"
#include "system.h"
#include "versioninfo.h"

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
 * re-extracting it.  Quarantine path layout:
 *   data/<romid>/.quarantine/<unixtime>_<basename>
 * Best-effort: failure to move logs LOUDFAIL but does not block the
 * re-extract that follows. */
static void romExtractQuarantine(const char *binRel, const char *binFull)
{
    char quarRel[ROMEXTRACT_PATH_LEN];
    snprintf(quarRel, sizeof(quarRel), "data/%s/.quarantine", VERSION_ROMID);
    fsCreateDir(quarRel);

    /* Take basename of binRel (everything after the last slash). */
    const char *base = binRel;
    for (const char *p = binRel; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }

    char destRel[ROMEXTRACT_PATH_LEN];
    snprintf(destRel, sizeof(destRel),
        "data/%s/.quarantine/%lld_%s",
        VERSION_ROMID, (long long)time(NULL), base);

    const char *destFull = fsFullPath(destRel);
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

    for (s32 fileNum = 1; fileNum < ROMEXTRACT_MAX_FILES; fileNum++) {
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

        const char *outFull = fsFullPath(outRel);
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
 *   2. Move the corrupted file to data/<romid>/.quarantine/<ts>_<name>
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
 */
s32 romExtractVerifyAll(void)
{
    s32 verified = 0;
    s32 corrected = 0;
    s32 skippedEmpty = 0;
    s32 baselined = 0;
    s32 failed = 0;

    if (g_RomFile == NULL || g_RomFileSize == 0) {
        sysLogPrintf(LOG_NOTE,
            "ROMEXTRACT.VERIFY: g_RomFile not loaded (server build); skipping.");
        return 0;
    }

    sysLogPrintf(LOG_NOTE, "ROMEXTRACT.VERIFY: scanning data/%s/files/",
                 VERSION_ROMID);

    for (s32 fileNum = 1; fileNum < ROMEXTRACT_MAX_FILES; fileNum++) {
        u8  *romData = romdataFileGetData(fileNum);
        s32  romSize = romdataFileGetSize(fileNum);
        if (romData == NULL || romSize <= 0) {
            skippedEmpty++;
            continue;
        }

        char outRel[ROMEXTRACT_PATH_LEN];
        s32  relLen = romExtractBuildRelPath(fileNum, outRel, (s32)sizeof(outRel));
        if (relLen <= 0 || relLen >= (s32)sizeof(outRel)) {
            failed++;
            continue;
        }

        const char *outFull = fsFullPath(outRel);
        if (outFull == NULL || outFull[0] == '\0') {
            failed++;
            continue;
        }

        /* Skip files that don't exist on disk yet (caller should run
         * romExtractAllFiles first; verify is per-file, not first-run). */
        struct stat st;
        if (stat(outFull, &st) != 0) {
            skippedEmpty++;
            continue;
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
                corrected++;
            } else {
                failed++;
            }
            continue;
        }
        char diskHex[SHA256_HEX_SIZE];
        sha256ToHex(diskDigest, diskHex);

        char sideHex[SHA256_HEX_SIZE];
        if (!romExtractReadSidecar(outRel, sideHex)) {
            /* Legacy file from pre-A.4 extraction: write a baseline
             * sidecar from the current on-disk content's hash. */
            romExtractWriteSidecar(outRel, romData, (u32)romSize);
            baselined++;
            continue;
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
                failed++;
                continue;
            }
            size_t wrote = fwrite(romData, 1, (size_t)romSize, f);
            fclose(f);
            if (wrote != (size_t)romSize) {
                sysLoudFailf("LOAD",
                    "re-extract short write for \"%s\"", outFull);
                failed++;
                continue;
            }
            romExtractWriteSidecar(outRel, romData, (u32)romSize);
            corrected++;
            continue;
        }

        verified++;
    }

    sysLogPrintf(LOG_NOTE,
        "ROMEXTRACT.VERIFY: verified=%d, corrected=%d, baselined=%d, "
        "skipped_empty=%d, failed=%d",
        verified, corrected, baselined, skippedEmpty, failed);

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

    for (s32 idx = 0; idx < segCount; idx++) {
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

        const char *outFull = fsFullPath(outRel);
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
    for (s32 idx = 0; idx < segCount; idx++) {
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

        const char *outFull = fsFullPath(outRel);
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
            } else {
                failed++;
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
                continue;
            }
            size_t wrote = fwrite(segData, 1, (size_t)segSize, f);
            fclose(f);
            if (wrote != (size_t)segSize) {
                sysLoudFailf("LOAD",
                    "re-extract short write for seg \"%s\"", outFull);
                failed++;
                continue;
            }
            romExtractWriteSidecar(outRel, segData, segSize);
            corrected++;
            continue;
        }

        verified++;
    }

    sysLogPrintf(LOG_NOTE,
        "ROMEXTRACT.SEGS.VERIFY: verified=%d, corrected=%d, baselined=%d, "
        "skipped_empty=%d, failed=%d",
        verified, corrected, baselined, skippedEmpty, failed);

    return corrected;
}

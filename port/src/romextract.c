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
#include <sys/stat.h>
#include <PR/ultratypes.h>

#include "fs.h"
#include "romdata.h"
#include "romextract.h"
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
 * at least ROMEXTRACT_PATH_LEN bytes. */
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

/* Test whether a path already exists on disk with the requested size.
 * Pass A.2 uses size-only as the idempotency check; Pass A.4 will
 * upgrade this to SHA-256 verification with quarantine + re-extract. */
static s32 romExtractFileMatchesSize(const char *fullPath, u32 expectedSize)
{
    struct stat st;
    if (stat(fullPath, &st) != 0) {
        return 0;
    }
    return (st.st_size == (long long)expectedSize) ? 1 : 0;
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

        written++;
    }

    sysLogPrintf(LOG_NOTE,
        "ROMEXTRACT: complete. wrote=%d, skipped_existing=%d, "
        "skipped_empty=%d, failed=%d",
        written, skippedExisting, skippedEmpty, failed);

    return written;
}

/**
 * assetcatalog_cache.c -- ROM integrity hash cache (C-1)
 *
 * Hashes the ROM with SHA-256, stores in $S/catalog-hash-cache.json,
 * and verifies on subsequent launches.
 *
 * Cache file format (hand-written JSON, parsed with simple string scanning):
 *   {"version":1,"rom_hash":"<64 hex chars>"}
 *
 * Intentionally minimal: no external JSON library, no dynamic allocation.
 * The file is small (<100 bytes) so it is read into a fixed stack buffer.
 *
 * Auto-discovered by CMake glob. No build system changes needed.
 */

#include <stdio.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "types.h"
#include "assetcatalog_cache.h"
#include "sha256.h"
#include "fs.h"
#include "system.h"

/* ========================================================================
 * Constants
 * ======================================================================== */

#define CACHE_VERSION      1
#define CACHE_RELPATH      "$S/catalog-hash-cache.json"
/* Buffer large enough for the entire JSON file (well under 256 bytes). */
#define CACHE_BUF_SIZE     256

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

/**
 * Read the stored ROM hash from the cache file.
 * Returns 1 and fills storedHex[65] on success, 0 otherwise.
 * storedHex must have room for SHA256_HEX_SIZE (65) chars.
 */
static s32 cacheRead(char storedHex[SHA256_HEX_SIZE])
{
    const char *path = fsFullPath(CACHE_RELPATH);
    FILE *f = fopen(path, "r");
    if (!f) {
        return 0;
    }

    char buf[CACHE_BUF_SIZE];
    size_t nread = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    if (nread == 0) {
        return 0;
    }
    buf[nread] = '\0';

    /* Locate "rom_hash":"<hex>" by scanning for the key. */
    const char *key = "\"rom_hash\"";
    const char *pos = strstr(buf, key);
    if (!pos) {
        return 0;
    }

    /* Advance past the key to the colon. */
    pos += strlen(key);
    while (*pos == ' ' || *pos == '\t' || *pos == ':') {
        pos++;
    }
    if (*pos != '"') {
        return 0;
    }
    pos++; /* skip opening quote */

    /* Copy exactly 64 hex characters. */
    for (s32 i = 0; i < 64; i++) {
        if (!pos[i] || pos[i] == '"') {
            return 0; /* truncated */
        }
        storedHex[i] = pos[i];
    }
    storedHex[64] = '\0';

    return 1;
}

/**
 * Write a new cache file with the given ROM hash hex string.
 * Returns 1 on success, 0 on failure.
 */
static s32 cacheWrite(const char *romHashHex)
{
    const char *path = fsFullPath(CACHE_RELPATH);
    FILE *f = fopen(path, "w");
    if (!f) {
        return 0;
    }

    fprintf(f, "{\"version\":%d,\"rom_hash\":\"%s\"}\n", CACHE_VERSION, romHashHex);
    fclose(f);
    return 1;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

s32 catalogCacheVerifyRom(const char *romPath, char *romHashHexOut)
{
    if (!romPath || !romPath[0]) {
        return -1;
    }

    /* Hash the ROM file. */
    u8 digest[SHA256_DIGEST_SIZE];
    if (sha256HashFile(romPath, digest) != 0) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG: ROM hash cache: could not hash '%s'", romPath);
        return -1;
    }

    char currentHex[SHA256_HEX_SIZE];
    sha256ToHex(digest, currentHex);

    if (romHashHexOut) {
        strncpy(romHashHexOut, currentHex, SHA256_HEX_SIZE - 1);
        romHashHexOut[SHA256_HEX_SIZE - 1] = '\0';
    }

    /* Try to read the stored hash. */
    char storedHex[SHA256_HEX_SIZE];
    if (!cacheRead(storedHex)) {
        /* No cache yet — write it and report first-run. */
        if (!cacheWrite(currentHex)) {
            sysLogPrintf(LOG_WARNING,
                         "CATALOG: ROM hash cache: could not write cache file");
        } else {
            sysLogPrintf(LOG_NOTE,
                         "CATALOG: ROM hash cached (%s)", currentHex);
        }
        return 1;
    }

    /* Compare. */
    if (strncmp(currentHex, storedHex, 64) == 0) {
        sysLogPrintf(LOG_NOTE, "CATALOG: ROM hash verified");
        return 1;
    }

    /* Mismatch — ROM has changed (update or corruption). */
    sysLogPrintf(LOG_WARNING,
                 "CATALOG: ROM hash changed — expected %.8s... got %.8s... "
                 "(ROM replaced or corrupted; re-registering catalog)",
                 storedHex, currentHex);

    /* Update cache to the new hash so we don't warn on every subsequent boot. */
    if (!cacheWrite(currentHex)) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG: ROM hash cache: could not update cache file");
    }

    return 0;
}

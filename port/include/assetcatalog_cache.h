/**
 * assetcatalog_cache.h -- ROM integrity hash cache (C-1)
 *
 * Hashes the ROM file with SHA-256 on first launch, stores the result in
 * $S/catalog-hash-cache.json, and verifies it on subsequent launches.
 *
 * Purpose:
 *   - Detect ROM corruption or swapped ROMs at startup.
 *   - Provide a stable ROM identity token for future cache-skipping logic
 *     (full catalog serialization is out of scope for this module).
 *
 * Logging:
 *   "CATALOG: ROM hash verified"   — hash matched stored value
 *   "CATALOG: ROM hash cached"     — first run, cache written
 *   "CATALOG: ROM hash changed"    — mismatch (warning)
 *   "CATALOG: ROM hash cache I/O"  — can't read/write cache file (warning)
 *
 * Auto-discovered by CMake glob. No build system changes needed.
 */

#ifndef _IN_ASSETCATALOG_CACHE_H
#define _IN_ASSETCATALOG_CACHE_H

#include <PR/ultratypes.h>
#include "sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Verify or initialise the ROM hash cache.
 *
 * Hashes romPath with SHA-256 and compares against
 * $S/catalog-hash-cache.json.
 *
 *   First run (no cache file):
 *     Creates the cache, writes the hash, returns 1.
 *
 *   Subsequent runs, hash matches:
 *     Logs "ROM hash verified", returns 1.
 *
 *   Hash mismatch (ROM changed or corrupted):
 *     Logs a warning, overwrites the cache with the new hash, returns 0.
 *
 *   File I/O error (ROM unreadable or save dir unavailable):
 *     Logs a warning, returns -1.  The hash output param is not filled.
 *
 * @param romPath      Path to the ROM file to hash.
 * @param romHashHexOut  Optional 65-byte buffer.  If non-NULL and the hash
 *                       succeeds, filled with the lowercase hex digest of
 *                       the current ROM (64 chars + NUL).
 * @return  1  — hash verified or cache created (good to go)
 *          0  — hash changed (proceed with caution; re-registration needed)
 *         -1  — I/O error (cannot hash ROM or read/write cache)
 */
s32 catalogCacheVerifyRom(const char *romPath, char *romHashHexOut);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ASSETCATALOG_CACHE_H */

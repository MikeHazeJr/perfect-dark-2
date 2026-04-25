/**
 * modvfs.h -- Priority M / B-238 virtual file system for `.pdmod` archives.
 *
 * Per-archive mounts. Asset paths inside an archive are resolved through
 * the central-directory index established at modArchiveOpen(); decompressed
 * bytes are kept in a global LRU cache so subsequent reads for the same
 * entry skip the inflate pass.
 *
 * Single source of truth: the catalog. The VFS layer is invoked from
 * fsFileLoad / fsFileSize when the requested path matches a mounted mod's
 * asset namespace. The catalog itself still owns asset registration; the
 * VFS just answers "do you have these bytes for me?" calls.
 *
 * Trust gate: a mod must be present in the modmgr registry AND enabled +
 * loaded for its mount to be considered. modmgrLoadMod calls modVfsMount;
 * modmgrUnloadAllMods (and per-mod unloads) call modVfsUnmount.
 *
 * Threading: not thread-safe. The engine's asset path is single-threaded;
 * the audio worker uses its own load helpers that bypass this layer.
 */

#ifndef _IN_MODVFS_H
#define _IN_MODVFS_H

#include <PR/ultratypes.h>
#include <stddef.h>
#include "modarchive.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Init / shutdown. modVfsInit reads Mods.AssetCacheMB from config and sets
 * the global cap. Safe to call multiple times. */
void modVfsInit(void);
void modVfsShutdown(void);

/**
 * Register an archive under the logical mod id. Subsequent
 * modVfsResolveAlloc / modVfsResolveAnyAlloc calls can find entries inside.
 * The caller retains ownership of the archive handle; modVfsUnmount does
 * NOT close the handle. The mount is identified by `mod_id` (case-sensitive
 * exact match).
 *
 * Returns 1 on success, 0 on failure (table full, NULL inputs, duplicate id).
 */
s32 modVfsMount(const char *mod_id, mod_archive_t *archive);

/** Drop the mount + flush any cached entries owned by it. Safe on absent ids. */
void modVfsUnmount(const char *mod_id);

/** Drop every mount and free the entire cache. */
void modVfsUnmountAll(void);

/** Returns 1 if any mount currently holds an entry matching `relPath`. */
s32 modVfsCanResolve(const char *relPath);

/** Returns the uncompressed size of the entry at `relPath` from any mount,
 *  or -1 if no mount holds it. Cheap lookup -- does not decompress. */
s32 modVfsGetSize(const char *relPath);

/**
 * Decompress the entry at `relPath` from a specific mount. Caller owns
 * the returned malloc'd buffer; `*outSize` receives the byte count. The
 * buffer is one byte longer than `*outSize` and the trailing byte is NUL
 * so the result is safe to use with C-string APIs.
 *
 * Returns NULL if the mount or entry does not exist. Hits the cache when
 * possible; misses go through modArchiveExtractAlloc.
 */
void *modVfsResolveAllocFromMount(const char *mod_id, const char *relPath, u32 *outSize);

/**
 * Same as modVfsResolveAllocFromMount but searches every mount in mount
 * order (matches the legacy modmgrResolvePath load-order semantics). On
 * hit, optionally writes the satisfying mod id to `outModId` (NUL terminated,
 * truncated to outModIdCap-1).
 */
void *modVfsResolveAnyAlloc(const char *relPath, u32 *outSize,
                            char *outModId, s32 outModIdCap);

/* ---------------------------------------------------------------- Stats */

typedef struct mod_vfs_stats {
	u64 hits;
	u64 misses;
	u64 bytes_resident;
	u64 bytes_evicted_total;
	u64 cap_bytes;
	s32 entries_resident;
	s32 mounts_active;
} mod_vfs_stats_t;

void modVfsGetStats(mod_vfs_stats_t *out);

/** Override the cache cap. Default is 256 MiB; safe to change at runtime. */
void modVfsSetCacheCapMB(s32 mb);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MODVFS_H */

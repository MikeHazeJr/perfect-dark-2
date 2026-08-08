/**
 * assetprovider_file.c -- FileProvider (loose files on disk).
 *
 * Serves asset bytes from loose files via `fsFileLoad`. Paths are interned
 * into a single char pool so handles remain small (opaque[0] = offset in
 * pool). Re-interning the same path returns the same offset, so handles
 * compare equal for identical paths.
 *
 * The interning pool is sized for the public base asset catalog plus loose
 * mod paths. Base typed archives register thousands of data/<romid>/...
 * FileProvider paths, so this must be a PC-scale registry, not a small mod
 * side cache.
 *
 * Design doc: context/designs/direct-file-access-design-2026-04-17.md
 */

#include <PR/ultratypes.h>
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include "assetprovider.h"
#include "fs.h"
#include "system.h"

/* ========================================================================
 * Path interning pool
 * ======================================================================== */

/* PC-only sizing: enough for the generated base archive catalog plus a large
 * enabled-mod set. Handles still carry one offset, so runtime handle size does
 * not grow with the pool. */
#define FILE_PROVIDER_POOL_BYTES   (1024 * 1024)
#define FILE_PROVIDER_MAX_PATHS    16384

static char s_PathPool[FILE_PROVIDER_POOL_BYTES];
static s32  s_PathPoolUsed = 1;  /* offset 0 reserved as "null" sentinel */
static s32  s_PathOffsets[FILE_PROVIDER_MAX_PATHS];
static s32  s_PathCount = 0;
static s32  s_Warned = 0;
static SDL_mutex *s_PathMutex = NULL;
static SDL_SpinLock s_PathMutexInitLock = 0;

static void fileProviderEnsureMutex(void)
{
    if (s_PathMutex == NULL) {
        SDL_AtomicLock(&s_PathMutexInitLock);
        if (s_PathMutex == NULL) {
            s_PathMutex = SDL_CreateMutex();
        }
        SDL_AtomicUnlock(&s_PathMutexInitLock);
    }
}

static s32 fileProviderInternPath(const char *path)
{
    if (!path || !path[0]) {
        return 0;
    }
    if (strlen(path) >= FS_MAXPATH) {
        sysLogPrintf(LOG_WARNING,
            "FileProvider: rejecting over-capacity public asset path (%u >= %u)",
            (unsigned)strlen(path), (unsigned)FS_MAXPATH);
        return 0;
    }

    fileProviderEnsureMutex();
    if (s_PathMutex) {
        SDL_LockMutex(s_PathMutex);
    }

    /* Dedup: scan the offsets table. O(n) but n is small (< 1024 in any
     * realistic install) and only runs at catalog registration time. */
    for (s32 i = 0; i < s_PathCount; i++) {
        if (strcmp(&s_PathPool[s_PathOffsets[i]], path) == 0) {
            s32 offset = s_PathOffsets[i];
            if (s_PathMutex) {
                SDL_UnlockMutex(s_PathMutex);
            }
            return offset;
        }
    }

    s32 need = (s32)strlen(path) + 1;
    if (s_PathPoolUsed + need > FILE_PROVIDER_POOL_BYTES ||
            s_PathCount >= FILE_PROVIDER_MAX_PATHS) {
        if (!s_Warned) {
            sysLogPrintf(LOG_WARNING,
                "FileProvider: path intern pool exhausted (%d paths, %d bytes used, need %d more). "
                "Further mod paths will return null handles until reset.",
                s_PathCount, s_PathPoolUsed, need);
            s_Warned = 1;
        }
        if (s_PathMutex) {
            SDL_UnlockMutex(s_PathMutex);
        }
        return 0;
    }

    s32 offset = s_PathPoolUsed;
    memcpy(&s_PathPool[offset], path, (size_t)need);
    s_PathPoolUsed += need;
    s_PathOffsets[s_PathCount++] = offset;
    if (s_PathMutex) {
        SDL_UnlockMutex(s_PathMutex);
    }
    return offset;
}

static const char *fileProviderGetPath(s32 offset)
{
    if (offset <= 0 || offset >= s_PathPoolUsed) {
        return NULL;
    }
    return &s_PathPool[offset];
}

/* ========================================================================
 * Vtable impls
 * ======================================================================== */

static s32 file_resolve_size(const asset_provider_t *self,
                             asset_data_handle_t handle)
{
    (void)self;
    const char *path = fileProviderGetPath((s32)handle.opaque[0]);
    if (!path) {
        return 0;
    }
    s32 size = fsFileSize(path);
    return (size < 0) ? 0 : size;
}

static s32 file_load(const asset_provider_t *self,
                     asset_data_handle_t handle,
                     void *buf, s32 buf_size)
{
    (void)self;

    if (!buf || buf_size <= 0) {
        return -1;
    }

    const char *path = fileProviderGetPath((s32)handle.opaque[0]);
    if (!path) {
        return -1;
    }

    u32 size = 0;
    void *data = fsFileLoad(path, &size);
    if (!data) {
        return -1;
    }

    s32 to_copy = (s32)size;
    if (to_copy > buf_size) {
        to_copy = buf_size;
    }
    memcpy(buf, data, (size_t)to_copy);
    sysMemFree(data);
    return to_copy;
}

static void file_unload(const asset_provider_t *self,
                        asset_data_handle_t handle)
{
    (void)self;
    (void)handle;
    /* Each load allocates a fresh buffer that the caller owns — nothing
     * to release on the provider side. */
}

static const char *file_describe(const asset_provider_t *self,
                                 asset_data_handle_t handle,
                                 char *buf, s32 buf_size)
{
    (void)self;
    if (!buf || buf_size <= 0) {
        return NULL;
    }
    const char *path = fileProviderGetPath((s32)handle.opaque[0]);
    snprintf(buf, (size_t)buf_size, "FileProvider:%s",
             path ? path : "(null)");
    return buf;
}

/* ========================================================================
 * Singleton
 * ======================================================================== */

static const asset_provider_t s_FileProvider = {
    "FileProvider",
    file_resolve_size,
    file_load,
    file_unload,
    file_describe,
};

const asset_provider_t *fileProvider(void)
{
    return &s_FileProvider;
}

asset_data_handle_t fileProviderHandle(const char *path)
{
    asset_data_handle_t h;
    h.provider = &s_FileProvider;
    h.opaque[0] = (u64)(u32)fileProviderInternPath(path);
    h.opaque[1] = 0;
    if (h.opaque[0] == 0) {
        /* Interning failed (null path or pool exhausted): null the handle
         * so callers that check `assetHandleIsNull` notice immediately. */
        h.provider = NULL;
    }
    return h;
}

const char *fileProviderPath(asset_data_handle_t h)
{
    if (h.provider != &s_FileProvider) {
        return NULL;
    }
    return fileProviderGetPath((s32)h.opaque[0]);
}

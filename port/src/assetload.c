/**
 * assetload.c -- Provider-aware asset load dispatcher.
 *
 * `assetLoad` / `assetLoadToNew` / `assetLoadToAddr` / `assetUnload` /
 * `assetDescribe` look up the provider vtable on the handle and forward to
 * the right impl.
 *
 * For RomProvider, `assetLoadToNew` and `assetLoadToAddr` delegate to
 * `fileLoadRomToNew` / `fileLoadRomToAddr` (in `src/game/file.c`) which own
 * the legacy ROM load pipeline (mempAlloc MEMPOOL_STAGE for the New variant,
 * rzipInflate, romdataFilePreprocess, g_FileInfo[] tracking). Those workers
 * are exported specifically so the dispatcher can reach them without
 * recursing back through any public wrapper. Phase 3 (2026-04-19) deleted
 * the previous public wrappers `fileLoadToNew` / `fileLoadToAddr` -- game
 * code now calls `assetLoadRomToNew` / `assetLoadRomToAddr` directly.
 *
 * For FileProvider, the dispatcher allocates a MEMPOOL_STAGE buffer sized
 * by `resolve_size` and asks the provider to fill it via its `load` fn.
 * Any file preprocessing (rzip inflate, endian swap, etc.) is not applied
 * because FileProvider assets are not exercised through this path yet --
 * Phase 2 mods continue to load through the legacy `romdataFileLoad`
 * pipeline that does its own preprocessing.
 *
 * Design doc: context/designs/direct-file-access-design-2026-04-17.md
 */

#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>
#include "assetprovider.h"
#include "assetprovider_internal.h"
#include "assetload.h"
#include "constants.h"
#include "lib/memp.h"
#include "game/file.h"
#include "system.h"

/* ========================================================================
 * Dispatcher
 * ======================================================================== */

s32 assetLoad(asset_data_handle_t handle, void *buf, s32 buf_size)
{
    if (assetHandleIsNull(handle) || !handle.provider->load) {
        return -1;
    }
    return handle.provider->load(handle.provider, handle, buf, buf_size);
}

s32 assetLoadGetInflatedSize(asset_data_handle_t handle, u32 loadtype)
{
    if (assetHandleIsNull(handle)) {
        return 0;
    }

    if (handle.provider == romProvider()) {
        s32 filenum = romProviderFilenum(handle);
        if (filenum < 0) {
            return 0;
        }
        return (s32)fileGetInflatedSize(filenum, loadtype);
    }

    return handle.provider->resolve_size
        ? handle.provider->resolve_size(handle.provider, handle) : 0;
}

s32 assetLoadGetLoadedSize(asset_data_handle_t handle)
{
    if (assetHandleIsNull(handle)) {
        return 0;
    }

    if (handle.provider == romProvider()) {
        s32 filenum = romProviderFilenum(handle);
        if (filenum < 0) {
            return 0;
        }
        return (s32)fileGetLoadedSize(filenum);
    }

    return handle.provider->resolve_size
        ? handle.provider->resolve_size(handle.provider, handle) : 0;
}

void *assetLoadToNew(asset_data_handle_t handle, u32 method, u32 loadtype)
{
    if (assetHandleIsNull(handle)) {
        return NULL;
    }

    /* Fast path: RomProvider handles back the entire legacy load pipeline
     * (inflate + preprocess + mempAlloc tracking in g_FileInfo[]). Delegate
     * to the game-layer impl verbatim so Phase 1 is byte-identical. */
    if (handle.provider == romProvider()) {
        s32 filenum = romProviderFilenum(handle);
        if (filenum < 0) {
            return NULL;
        }
        return fileLoadRomToNew(filenum, method, loadtype);
    }

    /* Generic path: ask the provider for the size, allocate from the stage
     * pool, and copy bytes into the allocation. No inflate/preprocess --
     * providers that need that (currently none) can wrap this. */
    s32 size = handle.provider->resolve_size
        ? handle.provider->resolve_size(handle.provider, handle) : 0;
    if (size <= 0) {
        sysLogPrintf(LOG_WARNING, "assetLoadToNew: %s reports zero size",
            handle.provider->name ? handle.provider->name : "?");
        return NULL;
    }

    /* Align to 16 bytes like the legacy path does. */
    s32 alloc = (size + 0x20) & 0xfffffff0;
    if (method == FILELOADMETHOD_EXTRAMEM) {
        alloc += 0x8000;
    }

    void *buf = mempAlloc((u32)alloc, MEMPOOL_STAGE);
    if (!buf) {
        return NULL;
    }

    s32 n = handle.provider->load(handle.provider, handle, buf, alloc);
    if (n <= 0) {
        /* Can't undo the mempAlloc cleanly (MEMPOOL_STAGE is pool-reset),
         * but leaking into the stage pool is harmless since it's wiped at
         * stage transition. */
        return NULL;
    }

    (void)loadtype;
    return buf;
}

void assetUnload(asset_data_handle_t handle)
{
    if (assetHandleIsNull(handle) || !handle.provider->unload) {
        return;
    }
    handle.provider->unload(handle.provider, handle);
}

const char *assetDescribe(asset_data_handle_t handle, char *buf, s32 buf_size)
{
    if (!buf || buf_size <= 0) {
        return NULL;
    }
    if (assetHandleIsNull(handle)) {
        snprintf(buf, (size_t)buf_size, "(null handle)");
        return buf;
    }
    if (!handle.provider->describe) {
        snprintf(buf, (size_t)buf_size, "%s",
            handle.provider->name ? handle.provider->name : "?");
        return buf;
    }
    return handle.provider->describe(handle.provider, handle, buf, buf_size);
}

/* Phase 4: public ROM-load helper so game code never needs to call
 * romProviderHandle() directly.  Identical behavior to
 * assetLoadToNew(romProviderHandle(filenum), method, loadtype). */
void *assetLoadRomToNew(s32 filenum, u32 method, u32 loadtype)
{
    return assetLoadToNew(romProviderHandle(filenum), method, loadtype);
}

/* ========================================================================
 * Caller-allocated-buffer dispatcher
 * ======================================================================== */

void *assetLoadToAddr(asset_data_handle_t handle, u32 method, void *buf, u32 size)
{
    if (assetHandleIsNull(handle) || !buf || size == 0) {
        return NULL;
    }

    /* Fast path: RomProvider handles run through the legacy
     * `fileLoad` pipeline (rzip inflate + romdataFilePreprocess +
     * g_FileInfo[] tracking). Delegate verbatim so behaviour matches the
     * pre-AP `fileLoadToAddr` path byte-for-byte. */
    if (handle.provider == romProvider()) {
        s32 filenum = romProviderFilenum(handle);
        if (filenum < 0) {
            return NULL;
        }
        return fileLoadRomToAddr(filenum, method, (u8 *)buf, size);
    }

    /* Generic path (unused in Phase 3 -- no FileProvider game-code callers
     * yet). Asks the provider to fill the buffer directly without inflate
     * or preprocess. Providers needing those steps must wrap this call. */
    s32 n = handle.provider->load
        ? handle.provider->load(handle.provider, handle, buf, (s32)size)
        : -1;
    if (n <= 0) {
        sysLogPrintf(LOG_WARNING, "assetLoadToAddr: %s load returned %d (size=%u)",
            handle.provider->name ? handle.provider->name : "?", n, size);
        return NULL;
    }
    (void)method;
    return buf;
}

void *assetLoadRomToAddr(s32 filenum, u32 method, void *buf, u32 size)
{
    return assetLoadToAddr(romProviderHandle(filenum), method, buf, size);
}

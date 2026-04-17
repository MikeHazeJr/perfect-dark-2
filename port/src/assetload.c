/**
 * assetload.c -- Provider-aware asset load dispatcher.
 *
 * `assetLoad` / `assetLoadToNew` / `assetUnload` / `assetDescribe` look up the
 * provider vtable on the handle and forward to the right impl.
 *
 * For RomProvider, `assetLoadToNew` delegates to `fileLoadRomToNew` which
 * owns the legacy ROM load pipeline (mempAlloc MEMPOOL_STAGE, rzipInflate,
 * romdataFilePreprocess). That function is exported by `src/game/file.c`
 * specifically so the provider dispatcher can reach it without introducing
 * a circular dependency with `fileLoadToNew` (which itself is now a wrapper
 * around `assetLoadToNew`).
 *
 * For FileProvider, the dispatcher allocates a MEMPOOL_STAGE buffer sized
 * by `resolve_size` and asks the provider to fill it via its `load` fn.
 * Any file preprocessing (rzip inflate, endian swap, etc.) is not applied
 * because FileProvider assets are currently not exercised through this
 * path in Phase 1 — Phase 2 mods continue to load through the legacy
 * `romdataFileLoad` pipeline that does its own preprocessing.
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
     * pool, and copy bytes into the allocation. No inflate/preprocess —
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

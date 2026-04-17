/**
 * assetprovider_rom.c -- RomProvider (wraps the existing ROM load path).
 *
 * `opaque[0]` = filenum. `resolve_size` returns the raw ROM size; `load`
 * delegates to `romdataFileLoad` + memcpy. `unload` is a no-op (ROM bytes
 * are mmap-resident for the process lifetime).
 *
 * Design doc: context/designs/direct-file-access-design-2026-04-17.md
 */

#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>
#include "assetprovider.h"
#include "romdata.h"
#include "system.h"

/* ========================================================================
 * Vtable impls
 * ======================================================================== */

static s32 rom_resolve_size(const asset_provider_t *self,
                            asset_data_handle_t handle)
{
    (void)self;
    s32 filenum = (s32)handle.opaque[0];
    return romdataFileGetSize(filenum);
}

static s32 rom_load(const asset_provider_t *self,
                    asset_data_handle_t handle,
                    void *buf, s32 buf_size)
{
    (void)self;

    if (!buf || buf_size <= 0) {
        return -1;
    }

    s32 filenum = (s32)handle.opaque[0];
    u32 size = 0;
    u8 *src = romdataFileLoad(filenum, &size);
    if (!src || size == 0) {
        return -1;
    }

    s32 to_copy = (s32)size;
    if (to_copy > buf_size) {
        to_copy = buf_size;
    }
    memcpy(buf, src, (size_t)to_copy);
    return to_copy;
}

static void rom_unload(const asset_provider_t *self,
                       asset_data_handle_t handle)
{
    (void)self;
    (void)handle;
    /* ROM bytes are mmap-resident; nothing to release. */
}

static const char *rom_describe(const asset_provider_t *self,
                                asset_data_handle_t handle,
                                char *buf, s32 buf_size)
{
    (void)self;
    if (!buf || buf_size <= 0) {
        return NULL;
    }

    s32 filenum = (s32)handle.opaque[0];
    const char *name = romdataFileGetName(filenum);
    if (name) {
        snprintf(buf, (size_t)buf_size, "RomProvider:filenum=%d (%s)", filenum, name);
    } else {
        snprintf(buf, (size_t)buf_size, "RomProvider:filenum=%d", filenum);
    }
    return buf;
}

/* ========================================================================
 * Singleton
 * ======================================================================== */

static const asset_provider_t s_RomProvider = {
    "RomProvider",
    rom_resolve_size,
    rom_load,
    rom_unload,
    rom_describe,
};

const asset_provider_t *romProvider(void)
{
    return &s_RomProvider;
}

asset_data_handle_t romProviderHandle(s32 filenum)
{
    asset_data_handle_t h;
    h.provider = &s_RomProvider;
    h.opaque[0] = (u64)(u32)filenum;
    h.opaque[1] = 0;
    return h;
}

s32 romProviderFilenum(asset_data_handle_t h)
{
    if (h.provider != &s_RomProvider) {
        return -1;
    }
    return (s32)h.opaque[0];
}

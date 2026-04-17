/**
 * assetprovider.h -- Asset Provider abstraction (Direct File Access design)
 *
 * Formalizes "where do an asset's bytes come from?" as a typed interface.
 *
 * Every catalog entry can carry an `asset_data_handle_t` that binds a provider
 * vtable to provider-private opaque data. Call `assetLoad`/`assetLoadToNew`
 * with the handle and the dispatcher routes to the right provider:
 *
 *   - RomProvider: opaque[0] = filenum; wraps existing `romdataFileLoad` path.
 *   - FileProvider: opaque[0] = interned-path offset; wraps `fsFileLoad`.
 *   - (Future) ArchiveProvider: opaque = (archive_id, entry_index).
 *
 * Phase 1 (this header) introduces the types and singletons but does NOT
 * change any existing load behaviour — `fileLoadToNew(filenum,...)` delegates
 * to `assetLoadToNew(romProviderHandle(filenum),...)` which in turn delegates
 * to the same game-layer impl as before.
 *
 * Design doc: context/designs/direct-file-access-design-2026-04-17.md
 */

#ifndef _IN_ASSETPROVIDER_H
#define _IN_ASSETPROVIDER_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Data handle — opaque, provider-defined interpretation of the payload bits.
 * ======================================================================== */

struct asset_provider_s;

typedef struct {
    const struct asset_provider_s *provider;  /* NULL = null handle */
    u64                            opaque[2]; /* provider-private data */
} asset_data_handle_t;

/* Null handle sentinel (struct literal, usable wherever an initializer is
 * accepted). Equivalent to {NULL, {0, 0}}. */
#define ASSET_HANDLE_NULL_INIT { NULL, { 0, 0 } }

static inline s32 assetHandleIsNull(asset_data_handle_t h)
{
    return h.provider == NULL ? 1 : 0;
}

/* ========================================================================
 * Provider vtable
 * ======================================================================== */

typedef struct asset_provider_s {
    const char *name;     /* "RomProvider", "FileProvider", ... */

    /* Resolve: produce a size estimate for the asset (0 if unknown).
     * Pure query — does not load bytes. */
    s32  (*resolve_size)(const struct asset_provider_s *self,
                         asset_data_handle_t handle);

    /* Load: copy the asset bytes into caller-allocated `buf` of size
     * `buf_size`. Returns bytes actually written, or -1 on error.
     * Provider does not own `buf`. */
    s32  (*load)(const struct asset_provider_s *self,
                 asset_data_handle_t handle,
                 void *buf, s32 buf_size);

    /* Unload: any provider-side cleanup (close handles, dec ref counts).
     * Called when an asset is evicted. No-op for ROM since ROM is mmap'd
     * for the process lifetime. */
    void (*unload)(const struct asset_provider_s *self,
                   asset_data_handle_t handle);

    /* Describe: human-readable source for diagnostics / Catalog UI.
     * Writes up to `buf_size - 1` chars into `buf` and returns `buf`. */
    const char *(*describe)(const struct asset_provider_s *self,
                            asset_data_handle_t handle,
                            char *buf, s32 buf_size);
} asset_provider_t;

/* ========================================================================
 * Built-in provider singletons
 * ======================================================================== */

/* RomProvider — serves bytes from the mmap'd ROM file via the existing
 * `romdataFileLoad` path. opaque[0] = ROM filenum.
 *
 * romProvider() — singleton getter (public: used by the dispatcher in assetload.c
 * to detect RomProvider handles and fast-path to fileLoadRomToNew).
 *
 * romProviderHandle() / romProviderFilenum() — INTERNAL to the catalog/provider
 * layer.  Declared in assetprovider_internal.h.  Game code must not call these;
 * use fileLoadToNew() or assetLoadRomToNew() instead. */
const asset_provider_t *romProvider(void);

/* FileProvider — serves bytes from a loose file on disk via `fsFileLoad`.
 * Path interning keeps handles small and stable across catalog growth.
 * opaque[0] = byte offset into the interned path pool.
 *
 * `fileProviderHandle(NULL)` or a path that cannot be interned returns a
 * null handle. Calling with the same path twice returns the same handle
 * (strings are deduplicated). */
const asset_provider_t *fileProvider(void);
asset_data_handle_t      fileProviderHandle(const char *path);
const char              *fileProviderPath(asset_data_handle_t h);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ASSETPROVIDER_H */

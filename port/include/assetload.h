/**
 * assetload.h -- Provider-aware asset load dispatcher.
 *
 * Entry points the catalog (and the legacy `fileLoadToNew` wrapper) use to
 * load asset bytes through the provider abstraction. The dispatcher reads
 * the provider vtable on the handle and forwards to the right impl —
 * callers never care which provider serves the bytes.
 *
 * Design doc: context/designs/direct-file-access-design-2026-04-17.md
 */

#ifndef _IN_ASSETLOAD_H
#define _IN_ASSETLOAD_H

#include <PR/ultratypes.h>
#include "assetprovider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load asset bytes into a caller-allocated buffer.
 * Returns bytes written on success, -1 on error (null handle, no provider).
 */
s32 assetLoad(asset_data_handle_t handle, void *buf, s32 buf_size);

/**
 * Provider-aware replacement for `fileLoadToNew`. For a RomProvider handle
 * the behaviour is byte-identical to the pre-existing `fileLoadToNew` path
 * (mempAlloc MEMPOOL_STAGE, inflate, preprocess). For a FileProvider
 * handle the bytes are loaded from disk via `fsFileLoad` and copied into a
 * MEMPOOL_STAGE allocation. Returns the allocation on success, NULL on
 * error.
 *
 * The `method` and `loadtype` arguments mirror the legacy
 * `fileLoadToNew(s32 filenum, u32 method, u32 loadtype)` contract so this
 * function can back the wrapper at src/game/file.c without rewriting any
 * call sites. See `FILELOADMETHOD_*` and `LOADTYPE_*` in the game headers.
 */
void *assetLoadToNew(asset_data_handle_t handle, u32 method, u32 loadtype);

/**
 * Provider-side cleanup when an asset is evicted. No-op for ROM; future
 * providers may release cached handles / ref counts.
 */
void assetUnload(asset_data_handle_t handle);

/**
 * Fill `buf` with a human-readable source description for the handle
 * ("RomProvider:filenum=123", "FileProvider:/mods/.../body.bin"). Returns
 * `buf` so call sites can use it inline in log statements.
 */
const char *assetDescribe(asset_data_handle_t handle, char *buf, s32 buf_size);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ASSETLOAD_H */

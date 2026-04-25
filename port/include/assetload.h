/**
 * assetload.h -- Provider-aware asset load dispatcher.
 *
 * Entry points game code uses to load asset bytes through the provider
 * abstraction. The dispatcher reads the provider vtable on the handle and
 * forwards to the right impl — callers never care which provider serves the
 * bytes.
 *
 * AP Phase 3 (2026-04-19) finished the call-site migration: every game-code
 * caller of the legacy `fileLoadToNew` / `fileLoadToAddr` / `fileLoadPartToAddr`
 * wrappers was moved to `assetLoadRomToNew` / `assetLoadRomToAddr` (or, for
 * bg.c partial reads, `romdataFileLoad` directly per the S374 B-185 fix).
 * The legacy wrappers are gone; only the internal RomProvider workers
 * `fileLoadRomToNew` / `fileLoadRomToAddr` remain (declared in game/file.h
 * for the dispatcher's fast-path use only).
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
 * Allocate-and-load: dispatcher allocates a MEMPOOL_STAGE buffer sized by the
 * provider's `resolve_size` and asks the provider to fill it. RomProvider
 * fast-paths to the legacy `fileLoad` pipeline (rzip inflate +
 * romdataFilePreprocess + g_FileInfo[] tracking) so behaviour is
 * byte-identical to the pre-AP load path. Returns the allocation on success,
 * NULL on error (null handle, missing asset, zero load size).
 *
 * `method` and `loadtype` mirror the legacy contract — see `FILELOADMETHOD_*`
 * and `LOADTYPE_*` in the game headers.
 */
void *assetLoadToNew(asset_data_handle_t handle, u32 method, u32 loadtype);

/**
 * Convenience wrapper for ROM filenums — equivalent to
 * `assetLoadToNew(romProviderHandle(filenum), method, loadtype)` but does
 * not require the caller to know about the provider abstraction. Game code
 * uses this directly (Phase 3 replacement for the deleted `fileLoadToNew`
 * wrapper).
 */
void *assetLoadRomToNew(s32 filenum, u32 method, u32 loadtype);

/**
 * Provider-aware load into a caller-allocated buffer. Mirrors `assetLoadToNew`
 * for fixed-size destinations: the provider fills `buf` (up to `size` bytes)
 * with the inflated/preprocessed asset payload. RomProvider handles fast-path
 * to the legacy `fileLoad` pipeline (rzip + preprocess) so behaviour is
 * byte-identical to the pre-AP load path. Returns `buf` on success, NULL on
 * failure (null handle, missing asset, zero load size).
 */
void *assetLoadToAddr(asset_data_handle_t handle, u32 method, void *buf, u32 size);

/**
 * Convenience wrapper for ROM filenums — equivalent to
 * `assetLoadToAddr(romProviderHandle(filenum), method, buf, size)`. Game
 * code uses this directly (Phase 3 replacement for the deleted
 * `fileLoadToAddr` wrapper).
 */
void *assetLoadRomToAddr(s32 filenum, u32 method, void *buf, u32 size);

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

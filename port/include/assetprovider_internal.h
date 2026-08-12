/**
 * assetprovider_internal.h -- Internal RomProvider entry points.
 *
 * INTERNAL USE ONLY.  Not part of the public catalog API.
 *
 * `romProviderHandle` and `romProviderFilenum` are bridges between a raw ROM
 * file index (filenum) and the provider-handle abstraction.  They exist so
 * the catalog/provider layer can construct handles from filenums internally
 * without exposing that ability to game code.
 *
 * Game code MUST NOT include this header.  To load a ROM file, call:
 *   - `assetLoadRomToNew(filenum, method, loadtype)`        -- inflate + alloc
 *   - `assetLoadRomToAddr(filenum, method, buf, size)`      -- into buffer
 *
 * Files allowed to include this header:
 *   port/src/assetload.c
 *   port/src/assetprovider_file.c
 *   port/src/assetprovider_rom.c
 *   port/src/assetcatalog_api.c
 *   port/src/assetcatalog_base.c
 *   port/src/assetcatalog_base_extended.c
 *   port/src/assetcatalog_scanner.c
 *   port/src/server_stubs.c
 */

#ifndef _IN_ASSETPROVIDER_INTERNAL_H
#define _IN_ASSETPROVIDER_INTERNAL_H

#include "assetprovider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Construct a RomProvider handle for the given ROM filenum.
 * INTERNAL — catalog/provider layer only.  Game code must not call this.
 */
asset_data_handle_t romProviderHandle(s32 filenum);

/**
 * Extract the ROM filenum from a RomProvider handle.
 * Returns -1 if the handle is not a RomProvider handle.
 * INTERNAL — catalog/provider layer only.
 */
s32 romProviderFilenum(asset_data_handle_t h);

typedef struct file_provider_checkpoint {
	s32 pool_used;
	s32 path_count;
	s32 warned;
} file_provider_checkpoint_t;

/* Scanner admission can intern several source paths before a later sibling
 * rejects. These internal checkpoints make that append-only mutation part of
 * the same catalog transaction. */
s32 fileProviderCheckpointCreate(file_provider_checkpoint_t *checkpoint);
s32 fileProviderCheckpointRestore(const file_provider_checkpoint_t *checkpoint);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ASSETPROVIDER_INTERNAL_H */

/**
 * asset_source_debug.h -- Debug-only asset source enforcement.
 *
 * Lets Settings -> Debug force one catalog asset family at a time to load
 * only through public extracted/generated file sources. ROM/static fallback is
 * treated as a migration failure for the selected family.
 */
#ifndef _IN_ASSET_SOURCE_DEBUG_H
#define _IN_ASSET_SOURCE_DEBUG_H

#include <PR/ultratypes.h>

#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

asset_type_e assetSourceDebugOnlyType(void);
void assetSourceDebugSetOnlyType(asset_type_e type);
s32 assetSourceDebugIsEnabledFor(asset_type_e type);
s32 assetSourceDebugEntryUsesPublicFileSource(const asset_entry_t *entry);
s32 assetSourceDebugEntryRequiresPublicFileSource(const asset_entry_t *entry);
const char *assetSourceDebugTypeLabel(asset_type_e type);

#ifdef __cplusplus
}
#endif

#endif

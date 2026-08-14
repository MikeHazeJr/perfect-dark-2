#ifndef _IN_NET_MANIFEST_TYPE_H
#define _IN_NET_MANIFEST_TYPE_H

#include <stdbool.h>
#include <PR/ultratypes.h>
#include "assetcatalog.h"
#include "net/netmanifest.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Return the canonical MANIFEST_TYPE_* token for one concrete catalog type. */
u8 netManifestTypeForCatalogAsset(asset_type_e asset_type);

/**
 * Return true when a transported manifest token represents the exact concrete
 * catalog type. Many-to-one tokens such as STAGE and AUDIO accept each of
 * their declared concrete families. Generic ASSET tokens additionally require
 * slot_index to carry the exact asset_type_e value.
 */
bool netManifestTypeAcceptsCatalogAsset(u8 manifest_type, u8 slot_index,
		asset_type_e asset_type);

#ifdef __cplusplus
}
#endif

#endif /* _IN_NET_MANIFEST_TYPE_H */

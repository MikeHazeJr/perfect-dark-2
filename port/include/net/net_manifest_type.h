#ifndef _IN_NET_MANIFEST_TYPE_H
#define _IN_NET_MANIFEST_TYPE_H

#include <PR/ultratypes.h>
#include "assetcatalog.h"
#include "net/netmanifest.h"

/* SP-75: included by C and C++ files, so the boolean result is spelled s32
 * (0 or 1). Never include <stdbool.h> here: it switched `bool` to the 1-byte
 * _Bool for the rest of netmanifest.c and theater.c, while netmsg.c saw this
 * prototype with a 4-byte return. */

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
s32 netManifestTypeAcceptsCatalogAsset(u8 manifest_type, u8 slot_index,
		asset_type_e asset_type);

#ifdef __cplusplus
}
#endif

#endif /* _IN_NET_MANIFEST_TYPE_H */

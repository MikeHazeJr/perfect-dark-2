/**
 * loader_walker_body.c -- Catalog universality pivot Step 4 + 5
 * (2026-05-03).
 *
 * Walks data/<romid>/bodies/*.pdbody. Step 5: feeds the body_data_t
 * payload pool via loaderPoolParseBodyJson alongside the catalog row
 * registration.
 *
 * The .pdbody schema does not carry a default head pairing or langid at
 * top level (those are derived from in-binary tables); the catalog row
 * stays minimal here while the pool slot carries the full body_data_t
 * payload that catalog_mgr_bodies surfaces.
 */

#include <stddef.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "loader_pool.h"
#include "loader_walker.h"
#include "loader_walker_common.h"

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind; (void)file_path;

    s64 bodynum = 0;
    s64 requirefeature = 0;
    loaderWalkerEnvelopeInt(manifest, manifest_len, "bodynum", &bodynum);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "requirefeature", &requirefeature);

    asset_entry_t *e = NULL;
    if (assetCatalogResolve(id) == NULL) {
        e = assetCatalogRegisterBody(
            id, (s16)bodynum,
            /* name_langid: */ 0,
            /* headnum:    */ 0,
            (u8)requirefeature);
    } else {
        e = (asset_entry_t *)assetCatalogResolve(id);
    }

    loaderPoolParseBodyJson(manifest, manifest_len);

    return e ? 1 : -1;
}

void loaderWalkerScanBodies(const char *tier_dir,
                             loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "body", "bodies", ".pdbody", /* always_invoke: */ 1,
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}

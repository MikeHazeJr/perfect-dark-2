/**
 * loader_walker_body.c -- Step 4 (2026-05-03).
 *
 * Walks data/<romid>/bodies/*.pdbody and registers each as ASSET_BODY.
 *
 * The .pdbody schema does not carry a default head pairing or langid at
 * top level (those are derived from in-binary tables today). Pass 0 for
 * those fields; the .pdbase fallback path still populates the typed
 * loader_pdbase pool with full body_data_t records so manager accessors
 * keep returning the legacy values.
 */

#include <stddef.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
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

    asset_entry_t *e = assetCatalogRegisterBody(
        id, (s16)bodynum,
        /* name_langid: */ 0,
        /* headnum:    */ 0,
        (u8)requirefeature);
    return e ? 1 : -1;
}

void loaderWalkerScanBodies(const char *tier_dir,
                             loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "body", "bodies", ".pdbody",
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}

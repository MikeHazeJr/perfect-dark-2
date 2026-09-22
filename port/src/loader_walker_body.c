/* Specialized public body archive walker. Identity and authored values come
 * from body.ini; optional private metadata supplies generated slot provenance.
 * Shared scanner admission publishes the typed native pool transactionally. */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "body_head_source_bind.h"
#include "assetcatalog_body_head_slots.h"  /* c3844 Gate 2: private custom-slot allocator */
#include "catalog_mgr_bodies.h"
#include "catalog_readable_ids.h"
#include "fs.h"
#include "loader_enum_reverse.h"
#include "loader_pool.h"
#include "loader_walker.h"
#include "loader_walker_common.h"
#include "system.h"

static s32 s_register(const char *manifest, size_t manifest_len,
    const char *pd_kind, const char *id, const char *file_path)
{
    s64 hint = -1;
    char error[256];
    (void)pd_kind;
    /* Private metadata locates an unseeded base slot only; every authored
     * scalar and dependency path comes from the selected public descriptor. */
    loaderWalkerEnvelopeInt(manifest, manifest_len, "bodynum", &hint);
    if (hint < 0 || hint >= CATALOG_MGR_BODY_COUNT) hint = -1;
    if (!assetCatalogRegisterBodyHeadArchive(file_path, id, 0, (s32)hint,
            1, error, sizeof(error))) {
        sysLoudFailf("LOAD.BODYHEAD.PUBLIC_SOURCE", "%s: %s", id, error);
        return -1;
    }
    return 1;
}
void loaderWalkerScanBodies(const char *tier_dir,
                             loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "body", "bodies", ".pdbody", /* always_invoke: */ 1, "body.ini", "body",
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}

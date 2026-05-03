/**
 * loader_walker_lang.c -- Step 4 (2026-05-03).
 *
 * Walks data/<romid>/lang/*.pdlang and registers each as ASSET_LANG.
 * The .pdlang manifest carries `locale` and `category` (stage / mp_ui /
 * system) at top level per Section 2.13 of the schema doc.
 */

#include <stddef.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "loader_walker.h"
#include "loader_walker_common.h"

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind; (void)file_path;

    asset_entry_t *e = assetCatalogRegister(id, ASSET_LANG);
    if (!e) return -1;

    char category[32];
    if (loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "category",
                                     category, sizeof(category))) {
        size_t n = strlen(category);
        if (n >= sizeof(e->category)) n = sizeof(e->category) - 1;
        memcpy(e->category, category, n);
        e->category[n] = '\0';
    }
    return 1;
}

void loaderWalkerScanLangs(const char *tier_dir,
                            loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "lang", "lang", ".pdlang",
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}

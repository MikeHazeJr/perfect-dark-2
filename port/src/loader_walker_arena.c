/**
 * loader_walker_arena.c -- Step 4 (2026-05-03).
 *
 * Walks data/<romid>/arenas/*.pdarena and registers each as ASSET_ARENA.
 * The .pdarena envelope carries stagenum + requirefeature + name_langid
 * at top level per Section 2.4 of the schema doc.
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

    s64 stagenum = 0;
    s64 requirefeature = 0;
    s64 name_langid = 0;
    loaderWalkerEnvelopeInt(manifest, manifest_len, "stagenum", &stagenum);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "requirefeature", &requirefeature);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "name_langid", &name_langid);

    asset_entry_t *e = assetCatalogRegisterArena(
        id, (s32)stagenum, (u8)requirefeature, (s32)name_langid);
    return e ? 1 : -1;
}

void loaderWalkerScanArenas(const char *tier_dir,
                             loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "arena", "arenas", ".pdarena",
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}

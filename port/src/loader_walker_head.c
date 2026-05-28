/**
 * loader_walker_head.c -- Catalog universality pivot Step 4 + 5
 * (2026-05-03).
 *
 * Walks data/<romid>/heads/*.pdhead. Step 5: feeds the head_data_t
 * payload pool via loaderPoolParseHeadJson alongside the catalog row
 * registration.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "fs.h"
#include "loader_pool.h"
#include "loader_walker.h"
#include "loader_walker_common.h"

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind;

    s64 headnum = 0;
    s64 requirefeature = 0;
    char mesh_member[128];
    char mesh_archive_path[FS_MAXPATH + 1];
    char source_path[FS_MAXPATH + 1];
    loaderWalkerEnvelopeInt(manifest, manifest_len, "headnum", &headnum);
    loaderWalkerEnvelopeInt(manifest, manifest_len, "requirefeature", &requirefeature);
    if (!loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "mesh_archive",
                                     mesh_member, sizeof(mesh_member))) {
        strncpy(mesh_member, "mesh.pdmesh", sizeof(mesh_member) - 1);
        mesh_member[sizeof(mesh_member) - 1] = '\0';
    }

    asset_entry_t *e = NULL;
    if (assetCatalogResolve(id) == NULL) {
        e = assetCatalogRegisterHead(id, (s16)headnum, (u8)requirefeature);
    } else {
        e = (asset_entry_t *)assetCatalogResolve(id);
    }

    loaderWalkerMarkBaseArchiveEntry(e);
    if (e && loaderWalkerArchiveMemberPath(file_path, mesh_member,
                                           mesh_archive_path, sizeof(mesh_archive_path))) {
        snprintf(source_path, sizeof(source_path), "%s::model.obj", mesh_archive_path);
        catalogSetPrimaryFile(e, source_path);
    }

    loaderPoolParseHeadJson(manifest, manifest_len);

    return e ? 1 : -1;
}

void loaderWalkerScanHeads(const char *tier_dir,
                            loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "head", "heads", ".pdhead", /* always_invoke: */ 1,
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}

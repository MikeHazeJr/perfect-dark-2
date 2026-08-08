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
#include "assetcatalog_body_head_slots.h"  /* c3844 Gate 2: private custom-slot allocator */
#include "catalog_mgr_heads.h"
#include "fs.h"
#include "loader_pool.h"
#include "loader_walker.h"
#include "loader_walker_common.h"
#include "system.h"

static void s_sourceModelPathFromMeshArchive(const char *mesh_archive,
                                             char *out,
                                             size_t outsz)
{
    static const char *candidates[] = {
        "model.obj",
        "model.gltf",
        "model.glb",
    };
    size_t i;

    if (!out || outsz == 0) {
        return;
    }

    out[0] = '\0';
    if (!mesh_archive || !mesh_archive[0]) {
        return;
    }

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        snprintf(out, outsz, "%s::%s", mesh_archive, candidates[i]);
        out[outsz - 1] = '\0';
        if (fsFileSize(out) > 0) {
            return;
        }
    }

    snprintf(out, outsz, "%s::model.obj", mesh_archive);
    out[outsz - 1] = '\0';
}

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind;

    s64 headnum = -1;
    s64 requirefeature = 0;
    char mesh_member[FS_MAXPATH];
    char mesh_archive_path[FS_MAXPATH + 1];
    char source_path[FS_MAXPATH + 1];
    if (!loaderWalkerEnvelopeInt(manifest, manifest_len, "headnum", &headnum)
            || headnum < 0 || headnum >= CATALOG_MGR_HEAD_COUNT) {
        /* c3844 Gate 2: a fully-new custom .pdhead has no legacy headnum.
         * Allocate a catalog-owned private slot (>= 152) so the integer
         * render path assembles it from the public mesh source. */
        s32 custom = assetCatalogResolveHeadPrivateSlot(id);
        if (custom >= 0) {
            sysLogPrintf(LOG_NOTE,
                "LOADER.WALKER.HEAD.CUSTOM_SLOT: id=%s slot=%d path=%s",
                id ? id : "(null)", custom, file_path ? file_path : "(null)");
            headnum = custom;
        } else {
            sysLogPrintf(LOG_WARNING,
                "LOADER.WALKER.HEAD.RUNTIME_SLOT_MISSING: id=%s headnum=%d path=%s",
                id ? id : "(null)", (s32)headnum,
                file_path ? file_path : "(null)");
            headnum = -1;
        }
    }
    loaderWalkerEnvelopeInt(manifest, manifest_len, "requirefeature", &requirefeature);
    if (!loaderWalkerEnvelopePathCopy(manifest, manifest_len, "mesh_archive",
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
    /* c3844 Gate 2: bind the runtime slot so catalogHeadIdByHeadnum reverse-
     * resolves a custom head. Only set when unset (custom); base heads keep
     * their base-registration runtime_index. */
    if (e && headnum >= 0 && e->runtime_index < 0) {
        e->runtime_index = (s32)headnum;
    }
    if (e && loaderWalkerArchiveMemberPath(file_path, mesh_member,
                                           mesh_archive_path, sizeof(mesh_archive_path))) {
        s_sourceModelPathFromMeshArchive(mesh_archive_path,
            source_path, sizeof(source_path));
        catalogSetPrimaryFile(e, source_path);
    }

    /* c3844 Gate 2: custom heads have no manifest headnum, so feed the loader
     * pool the allocated private slot directly. */
    if (headnum >= CATALOG_MGR_HEAD_CUSTOM_START) {
        loaderPoolParseHeadJsonForSlot(manifest, manifest_len, (s32)headnum);
    } else {
        loaderPoolParseHeadJson(manifest, manifest_len);
    }

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

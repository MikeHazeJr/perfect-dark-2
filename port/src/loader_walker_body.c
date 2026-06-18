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
#include <stdio.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "assetcatalog_body_head_slots.h"  /* c3844 Gate 2: private custom-slot allocator */
#include "catalog_mgr_bodies.h"
#include "catalog_readable_ids.h"
#include "fs.h"
#include "loader_enum_reverse.h"
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

static s32 s_bodyWalkerManifestFileEnum(const char *manifest,
                                        size_t manifest_len,
                                        const char *key)
{
    char symbol[64];
    s64 value = -1;

    if (loaderWalkerEnvelopeStrCopy(manifest, manifest_len, key,
                                    symbol, sizeof(symbol))) {
        return loaderEnumResolveFileEnum(symbol, -1);
    }

    if (loaderWalkerEnvelopeInt(manifest, manifest_len, key, &value)
            && value > 0) {
        return (s32)value;
    }

    return -1;
}

static void s_bindBodyHandModelSource(const char *manifest, size_t manifest_len,
                                      asset_entry_t *body_entry,
                                      const char *body_archive_path,
                                      const char *hand_member)
{
    char hand_archive_path[FS_MAXPATH + 1];
    char hand_source_path[FS_MAXPATH + 1];
    char hand_model_id[CATALOG_ID_LEN];
    s32 hand_filenum;
    asset_entry_t *hand_entry;

    if (!body_entry || !body_archive_path || !hand_member || !hand_member[0]) {
        return;
    }

    hand_filenum = s_bodyWalkerManifestFileEnum(manifest, manifest_len, "hand");
    if (hand_filenum <= 0) {
        return;
    }

    if (!loaderWalkerArchiveMemberPath(body_archive_path, hand_member,
                                       hand_archive_path,
                                       sizeof(hand_archive_path))) {
        return;
    }

    catalogReadableModelIdForFile(hand_filenum, "hand", "hand",
        hand_model_id, sizeof(hand_model_id));

    hand_entry = (asset_entry_t *)assetCatalogResolve(hand_model_id);
    if (!hand_entry) {
        hand_entry = assetCatalogRegister(hand_model_id, ASSET_MODEL);
    }
    if (!hand_entry) {
        return;
    }

    loaderWalkerMarkBaseArchiveEntry(hand_entry);
    if (hand_entry->runtime_index == -1) {
        hand_entry->runtime_index = -hand_filenum;
    }
    hand_entry->source_filenum = hand_filenum;

    s_sourceModelPathFromMeshArchive(hand_archive_path,
        hand_source_path, sizeof(hand_source_path));
    catalogSetPrimaryFile(hand_entry, hand_source_path);
}

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind;

    s64 bodynum = -1;
    s64 requirefeature = 0;
    char mesh_member[128];
    char hand_member[128];
    char mesh_archive_path[FS_MAXPATH + 1];
    char source_path[FS_MAXPATH + 1];
    if (!loaderWalkerEnvelopeInt(manifest, manifest_len, "bodynum", &bodynum)
            || bodynum < 0 || bodynum >= CATALOG_MGR_BODY_COUNT) {
        /* c3844 Gate 2: a fully-new custom .pdbody has no legacy bodynum.
         * Allocate a catalog-owned private slot (>= 152) instead of dropping
         * it, so the existing integer render path assembles it from the
         * public mesh source. The private slot never crosses a public
         * boundary; catalog ID strings stay identity. */
        s32 custom = assetCatalogResolveBodyPrivateSlot(id);
        if (custom >= 0) {
            sysLogPrintf(LOG_NOTE,
                "LOADER.WALKER.BODY.CUSTOM_SLOT: id=%s slot=%d path=%s",
                id ? id : "(null)", custom, file_path ? file_path : "(null)");
            bodynum = custom;
        } else {
            sysLogPrintf(LOG_WARNING,
                "LOADER.WALKER.BODY.RUNTIME_SLOT_MISSING: id=%s bodynum=%d path=%s",
                id ? id : "(null)", (s32)bodynum,
                file_path ? file_path : "(null)");
            bodynum = -1;
        }
    }
    loaderWalkerEnvelopeInt(manifest, manifest_len, "requirefeature", &requirefeature);
    if (!loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "mesh_archive",
                                     mesh_member, sizeof(mesh_member))) {
        strncpy(mesh_member, "mesh.pdmesh", sizeof(mesh_member) - 1);
        mesh_member[sizeof(mesh_member) - 1] = '\0';
    }
    if (!loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "hand_archive",
                                     hand_member, sizeof(hand_member))) {
        hand_member[0] = '\0';
    }

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

    loaderWalkerMarkBaseArchiveEntry(e);
    /* c3844 Gate 2: bind the runtime slot so catalogBodyIdByBodynum reverse-
     * resolves a custom body. Base bodies already have runtime_index set by
     * base registration; only set it when unset (custom). */
    if (e && bodynum >= 0 && e->runtime_index < 0) {
        e->runtime_index = (s32)bodynum;
    }
    if (e && loaderWalkerArchiveMemberPath(file_path, mesh_member,
                                           mesh_archive_path, sizeof(mesh_archive_path))) {
        s_sourceModelPathFromMeshArchive(mesh_archive_path,
            source_path, sizeof(source_path));
        strncpy(e->ext.body.mesh_archive, mesh_archive_path,
            sizeof(e->ext.body.mesh_archive) - 1);
        e->ext.body.mesh_archive[sizeof(e->ext.body.mesh_archive) - 1] = '\0';
        catalogSetPrimaryFile(e, source_path);
    }
    if (e && hand_member[0]) {
        char hand_archive_path[FS_MAXPATH + 1];
        if (loaderWalkerArchiveMemberPath(file_path, hand_member,
                                          hand_archive_path,
                                          sizeof(hand_archive_path))) {
            strncpy(e->ext.body.hand_archive, hand_archive_path,
                sizeof(e->ext.body.hand_archive) - 1);
            e->ext.body.hand_archive[
                sizeof(e->ext.body.hand_archive) - 1] = '\0';
        }
        s_bindBodyHandModelSource(manifest, manifest_len, e, file_path,
            hand_member);
    }

    /* c3844 Gate 2: custom bodies have no manifest bodynum, so feed the
     * loader pool the allocated private slot directly. */
    if (bodynum >= CATALOG_MGR_BODY_CUSTOM_START) {
        loaderPoolParseBodyJsonForSlot(manifest, manifest_len, (s32)bodynum);
    } else {
        loaderPoolParseBodyJson(manifest, manifest_len);
    }

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

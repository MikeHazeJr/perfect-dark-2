/**
 * loader_walker_mesh.c -- Step 4 (2026-05-03).
 *
 * Walks data/<romid>/meshes/*.pdmesh and registers each as ASSET_MODEL.
 * .pdmesh files are ZIP compounds carrying _meta/manifest.json plus standard
 * model payloads such as model.obj/model.mtl. The walker scaffold handles
 * the ZIP open + manifest extraction transparently; this callback only sees
 * the manifest envelope bytes.
 *
 * The catalog row is the universality bridge -- consumers route
 * model loads through assetCatalogResolve(id) -> entry.runtime_index ->
 * legacy file load or generated runtime cache. The walker does not unpack
 * the authored model payload here; compilers/importers consume the standard
 * source files through the mounted archive/VFS path.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "fs.h"
#include "loader_walker.h"
#include "loader_walker_common.h"
#include "loader_walker_mesh_source.h"
#include "system.h"

static s32 meshPublicGeometry(const char *mesh_ini, size_t mesh_ini_len,
        char *geometry, size_t geometry_cap)
{
    for (size_t i = 0; i < loaderWalkerMeshPublicGeometryKeyCount(); i++) {
        const char *key = loaderWalkerMeshPublicGeometryKey(i);
        char present[2];
        if (!loaderWalkerIniValueCopy(mesh_ini, mesh_ini_len, "model",
                key, present, sizeof(present))) {
            continue;
        }
        if (!loaderWalkerIniPathCopy(mesh_ini, mesh_ini_len, "model",
                key, geometry, geometry_cap) || !geometry[0]) {
            return -1;
        }
        return 1;
    }
    snprintf(geometry, geometry_cap, "%s", "model.obj");
    return 1;
}

static s32 meshPublicSourceFields(const char *archive_path,
        const char *expected_id, char *geometry, size_t geometry_cap,
        char *err, size_t err_cap)
{
    char *mesh_ini = NULL;
    size_t mesh_ini_len = 0;
    char public_id[CATALOG_ID_LEN];
    char public_kind[32];
    s32 ok = 0;

    if (!geometry || geometry_cap == 0) return 0;
    geometry[0] = '\0';
    public_id[0] = '\0';
    public_kind[0] = '\0';
    if (!loaderWalkerArchiveTextMember(archive_path, "mesh.ini",
            &mesh_ini, &mesh_ini_len)) {
        snprintf(err, err_cap, "public mesh.ini is missing from %s",
            archive_path ? archive_path : "");
        goto done;
    }
    if ((!loaderWalkerIniValueCopy(mesh_ini, mesh_ini_len, "model",
                "catalog_id", public_id, sizeof(public_id))
            && !loaderWalkerIniValueCopy(mesh_ini, mesh_ini_len, "model",
                "id", public_id, sizeof(public_id)))
            || !public_id[0] || !expected_id
            || strcmp(public_id, expected_id) != 0) {
        snprintf(err, err_cap,
            "public mesh.ini catalog ID does not match %s",
            expected_id ? expected_id : "");
        goto done;
    }
    if (!loaderWalkerIniValueCopy(mesh_ini, mesh_ini_len, "model", "kind",
            public_kind, sizeof(public_kind))
            || strcmp(public_kind, "mesh") != 0) {
        snprintf(err, err_cap,
            "public mesh.ini kind is not mesh for %s", expected_id);
        goto done;
    }
    if (meshPublicGeometry(mesh_ini, mesh_ini_len, geometry,
            geometry_cap) <= 0) {
        snprintf(err, err_cap,
            "public mesh.ini geometry is empty or too long for %s",
            expected_id);
        goto done;
    }
    ok = 1;

done:
    if (mesh_ini) sysMemFree(mesh_ini);
    if (err && err_cap) err[err_cap - 1] = '\0';
    return ok;
}

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind;

    loader_walker_mesh_source_plan_t source_plan;
    char public_geometry[FS_MAXPATH];
    char source_error[256] = {0};
    const asset_entry_t *existing = assetCatalogResolve(id);
    asset_entry_t *e;
    s32 created = 0;

    if (existing && existing->type != ASSET_MODEL) {
        sysLoudFailf("LOAD.MESH.SOURCE_COLLISION",
            "typed mesh id=%s collides with catalog type=%d",
            id, (s32)existing->type);
        return -1;
    }
    if (!meshPublicSourceFields(file_path, id, public_geometry,
            sizeof(public_geometry), source_error, sizeof(source_error))) {
        sysLoudFailf("LOAD.MESH.PUBLIC_SOURCE",
            "typed mesh id=%s public source rejected: %s", id,
            source_error[0] ? source_error : "mesh.ini unavailable");
        return -1;
    }
    if (!loaderWalkerMeshSourcePlanManifest(manifest, manifest_len, id,
            file_path, public_geometry,
            existing ? existing->source_filenum : -1,
            &source_plan, source_error, sizeof(source_error))) {
        sysLoudFailf("LOAD.MESH.SOURCE_PLAN",
            "typed mesh id=%s source rejected: %s", id, source_error);
        return -1;
    }

    e = existing ? assetCatalogGetMutable(id)
                 : assetCatalogRegister(id, ASSET_MODEL);
    created = existing ? 0 : 1;
    if (!e || !loaderWalkerBindMeshSource(e, &source_plan, 1,
            source_error, sizeof(source_error))) {
        if (created) assetCatalogUnregister(id);
        sysLoudFailf("LOAD.MESH.SOURCE_BIND",
            "typed mesh id=%s source bind failed: %s", id,
            source_error[0] ? source_error : "catalog row unavailable");
        return -1;
    }
    loaderWalkerMarkBaseArchiveEntry(e);
    return 1;
}

void loaderWalkerScanMeshes(const char *tier_dir,
                             loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "mesh", "meshes", ".pdmesh", /* always_invoke: */ 1,
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}

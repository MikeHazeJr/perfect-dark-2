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
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "fs.h"
#include "loader_enum_reverse.h"
#include "loader_walker.h"
#include "loader_walker_common.h"

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind;

    char geometry_member[FS_MAXPATH];
    char source_symbol[64];
    char source_path[FS_MAXPATH + 1];
    s32 source_filenum = -1;
    const asset_entry_t *existing = assetCatalogResolve(id);
    s32 preserved_runtime_index = existing ? existing->runtime_index : -1;
    s16 preserved_mp_index = existing ? existing->mp_index : -1;
    f32 preserved_model_scale = existing ? existing->model_scale : 1.0f;
    s32 preserved_source_filenum = existing ? existing->source_filenum : -1;

    if (!loaderWalkerEnvelopePathCopy(manifest, manifest_len, "geometry",
                                     geometry_member, sizeof(geometry_member))) {
        strncpy(geometry_member, "model.obj", sizeof(geometry_member) - 1);
        geometry_member[sizeof(geometry_member) - 1] = '\0';
    }

    if (loaderWalkerEnvelopeStrCopy(manifest, manifest_len,
                                    "source_filenum_symbol",
                                    source_symbol, sizeof(source_symbol))) {
        source_filenum = loaderEnumResolveFileEnum(source_symbol, -1);
    }

    asset_entry_t *e = assetCatalogRegister(id, ASSET_MODEL);
    loaderWalkerMarkBaseArchiveEntry(e);
    if (e && loaderWalkerArchiveMemberPath(file_path, geometry_member,
                                           source_path, sizeof(source_path))) {
        e->runtime_index = preserved_runtime_index;
        e->mp_index = preserved_mp_index;
        e->model_scale = preserved_model_scale;
        catalogSetPrimaryFile(e, source_path);
        if (source_filenum > 0) {
            e->source_filenum = source_filenum;
        } else if (preserved_source_filenum > 0) {
            e->source_filenum = preserved_source_filenum;
        }
    }
    return e ? 1 : -1;
}

void loaderWalkerScanMeshes(const char *tier_dir,
                             loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "mesh", "meshes", ".pdmesh", /* always_invoke: */ 1,
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}

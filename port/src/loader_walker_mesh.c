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
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "loader_walker.h"
#include "loader_walker_common.h"

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)manifest; (void)manifest_len;
    (void)pd_kind;  (void)file_path;

    asset_entry_t *e = assetCatalogRegister(id, ASSET_MODEL);
    return e ? 1 : -1;
}

void loaderWalkerScanMeshes(const char *tier_dir,
                             loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "mesh", "meshes", ".pdmesh",
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}

#include "model_source_path.h"
#include "asset_archive_policy.h"
#include "asset_path_contract.h"
#include "body_head_source.h"
#include "fs.h"
#include "loader_walker_mesh_source.h"
#include "modarchive.h"
#include "modasset_compiler.h"
#include "system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

s32 modelSourceResolvePath(const char *source, char *out, size_t capacity,
    char *error, size_t error_capacity)
{
    void *archive = NULL;
    char *descriptor = NULL;
    ini_section_t *ini = NULL;
    loader_walker_mesh_source_plan_t plan = {0};
    char candidate[FS_MAXPATH + 1];
    u32 size = 0, descriptor_size = 0;
    s32 ok = 0;
    if (error && error_capacity) error[0] = 0;
    if (!out || !capacity) return 0;
    out[0] = 0;
    if (!source || !source[0]) goto done;
    if (modAssetCompilerIsExternalSource(source)) {
        if (!assetPathCopyChecked(candidate, sizeof(candidate), source)) goto done;
    } else {
        if (assetArchiveTypeForPath(source) != ASSET_MODEL) goto done;
        archive = fsFileLoad(source, &size);
        ini = calloc(1, sizeof(*ini));
        if (!archive || !ini || assetArchiveValidateBytes(archive, size, source,
                ASSET_ARCHIVE_VALIDATE_RELEASE, error, error_capacity) != 0) goto done;
        descriptor = modArchiveExtractMemAlloc(archive, size, "mesh.ini", &descriptor_size);
        if (!bodyHeadSourceReadIniBytes(descriptor, descriptor_size, "model", ini,
                error, error_capacity)) goto done;
        const char *id = iniGet(ini, "catalog_id", NULL);
        const char *alias = iniGet(ini, "id", NULL);
        if ((id && alias && strcmp(id, alias)) || (!id && !(id = alias)) ||
                !bodyHeadSourceCatalogIdValid(id) || strcmp(iniGet(ini, "kind", ""), "mesh")) goto done;
        const char *geometry = NULL;
        for (size_t i = 0; i < loaderWalkerMeshPublicGeometryKeyCount(); ++i) {
            const char *value = iniGet(ini, loaderWalkerMeshPublicGeometryKey(i), NULL);
            if (!value) continue;
            if (!value[0] || (geometry && strcmp(geometry, value))) goto done;
            geometry = value;
        }
        /* Public omission uses the schema default. Private cache geometry,
         * IDs and native provenance never select or veto this runtime path. */
        if (!loaderWalkerMeshSourcePlanManifest("{}", 2, NULL, source,
                geometry ? geometry : "model.obj", -1, &plan, error, error_capacity) ||
                !assetPathCopyChecked(candidate, sizeof(candidate), plan.source_path)) goto done;
    }
    if (!modAssetCompilerIsExternalSource(candidate) || fsFileSize(candidate) <= 0 ||
            !assetPathCopyChecked(out, capacity, candidate)) goto done;
    ok = 1;
done:
    if (!ok) {
        out[0] = 0;
        if (error && error_capacity && !error[0])
            snprintf(error, error_capacity, "invalid or missing public model source: %s", source ? source : "");
    }
    free(ini);
    free(descriptor);
    if (archive) sysMemFree(archive);
    return ok;
}

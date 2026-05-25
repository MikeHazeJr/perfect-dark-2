#ifndef _IN_ASSET_RUNTIME_H
#define _IN_ASSET_RUNTIME_H

#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ASSET_RUNTIME_MAX_BINDINGS 256

typedef struct asset_runtime_binding {
    s32 active;
    asset_type_e type;
    char id[CATALOG_ID_LEN];
    char primary_path[FS_MAXPATH];
    char authored_file[FS_MAXPATH];
    char dependency_a[FS_MAXPATH];
    char dependency_b[FS_MAXPATH];
    char target_id[CATALOG_ID_LEN];
    char shader_id[64];
    s32 runtime_id;
    s32 kind;
    s32 target_kind;
    f32 value0;
    f32 params[4];
} asset_runtime_binding_t;

void assetRuntimeReset(void);
s32 assetRuntimeSupportsType(asset_type_e type);
s32 assetRuntimeActivateCatalogEntry(const asset_entry_t *entry,
                                     const char *primary_path);
void assetRuntimeReleaseCatalogEntry(const char *asset_id);
const asset_runtime_binding_t *assetRuntimeFind(const char *asset_id);
const asset_runtime_binding_t *assetRuntimeFindByTypeAndId(asset_type_e type,
                                                          const char *asset_id);
const asset_runtime_binding_t *assetRuntimeFindByTypeKind(asset_type_e type,
                                                         s32 kind);
const asset_runtime_binding_t *assetRuntimeFindByTarget(asset_type_e type,
                                                       const char *target_id);
s32 assetRuntimePrimaryFileAccessible(const asset_runtime_binding_t *binding);
void *assetRuntimeLoadPrimaryFile(const asset_runtime_binding_t *binding,
                                  u32 *out_size);
s32 assetRuntimeCount(asset_type_e type);
s32 assetRuntimeAccessibleCount(asset_type_e type);

#ifdef __cplusplus
}
#endif

#endif

#ifndef PD_EFFECT_DEPENDENCIES_H
#define PD_EFFECT_DEPENDENCIES_H

#include <stddef.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct effect_dependency {
	asset_type_e type;
	char catalog_id[CATALOG_ID_LEN];
} effect_dependency_t;

typedef struct effect_dependency_list {
	effect_dependency_t *items;
	size_t count;
	size_t capacity;
} effect_dependency_list_t;

/* Strict public-source dependency enumeration. Returns a count, or -1. */
s32 effectDependenciesCollectArchiveFile(const char *archive_path,
	const char *expected_catalog_id, effect_dependency_list_t *out,
	char *error, size_t error_cap);
s32 effectDependenciesCollectArchiveBytes(const void *archive_bytes, u32 archive_size,
	const char *expected_catalog_id, effect_dependency_list_t *out,
	char *error, size_t error_cap);
void effectDependenciesFree(effect_dependency_list_t *list);

#ifdef __cplusplus
}
#endif

#endif

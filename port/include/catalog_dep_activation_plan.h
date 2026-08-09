/** Growable dependency activation plan shared by composite catalog types. */
#ifndef PD_CATALOG_DEP_ACTIVATION_PLAN_H
#define PD_CATALOG_DEP_ACTIVATION_PLAN_H

#include <stddef.h>
#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct catalog_dep_activation_node {
	char id[CATALOG_ID_LEN];
	asset_type_e expected_type;
} catalog_dep_activation_node_t;

typedef struct catalog_dep_activation_plan {
	catalog_dep_activation_node_t *nodes;
	size_t count;
	size_t capacity;
	s32 failed;
} catalog_dep_activation_plan_t;

typedef s32 (*catalog_dep_activation_resolve_fn)(const char *asset_id,
	asset_type_e *out_actual_type, void *userdata);
typedef void (*catalog_dep_activation_release_fn)(const char *asset_id,
	asset_type_e expected_type, void *userdata);

void catalogDepActivationPlanCollect(const char *dep_id, void *userdata);
s32 catalogDepActivationPlanBuild(catalog_dep_activation_plan_t *plan,
	const char *root_id, asset_type_e root_type,
	catalog_dep_activation_resolve_fn resolve_fn, void *resolve_userdata,
	char *error, size_t error_cap);
void catalogDepActivationPlanRollback(const catalog_dep_activation_plan_t *plan,
	size_t loaded_count, catalog_dep_activation_release_fn release_fn,
	void *userdata);
void catalogDepActivationPlanFree(catalog_dep_activation_plan_t *plan);

#ifdef __cplusplus
}
#endif

#endif

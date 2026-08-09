#ifndef _IN_CATALOG_RESET_PLAN_H
#define _IN_CATALOG_RESET_PLAN_H

#include <stddef.h>
#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct catalog_reset_plan_row {
    const char *id;
    asset_type_e type;
} catalog_reset_plan_row_t;

typedef s32 (*catalog_reset_plan_contains_fn)(const char *parent_id,
                                              const char *dependency_id,
                                              void *userdata);

/* Build a growable parent-first retirement order. Rows are not mutated and
 * output indices refer back to the caller's snapshot. Returns zero for bad
 * input, allocation failure, or a cycle. */
s32 catalogResetPlanBuild(const catalog_reset_plan_row_t *rows,
                          size_t count,
                          catalog_reset_plan_contains_fn contains,
                          void *userdata,
                          size_t *out_order,
                          char *error,
                          size_t error_cap);

#ifdef __cplusplus
}
#endif

#endif

#include "catalog_reset_plan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void s_error(char *error, size_t cap, const char *message)
{
    if (!error || cap == 0) return;
    snprintf(error, cap, "%s", message ? message : "reset plan failed");
    error[cap - 1] = '\0';
}

s32 catalogResetPlanBuild(const catalog_reset_plan_row_t *rows,
                          size_t count,
                          catalog_reset_plan_contains_fn contains,
                          void *userdata,
                          size_t *out_order,
                          char *error,
                          size_t error_cap)
{
    unsigned char *retired;

    if (error && error_cap) error[0] = '\0';
    if ((count && (!rows || !out_order)) || !contains) {
        s_error(error, error_cap, "invalid reset plan arguments");
        return 0;
    }
    if (count == 0) return 1;

    retired = (unsigned char *)calloc(count, sizeof(*retired));
    if (!retired) {
        s_error(error, error_cap, "out of memory building reset plan");
        return 0;
    }

    for (size_t written = 0; written < count; written++) {
        size_t candidate = count;
        for (size_t i = 0; i < count && candidate == count; i++) {
            s32 has_unretired_parent = 0;
            if (retired[i]) continue;
            if (!rows[i].id || !rows[i].id[0]
                    || rows[i].type <= ASSET_NONE
                    || rows[i].type >= ASSET_TYPE_COUNT) {
                free(retired);
                s_error(error, error_cap, "invalid typed reset row");
                return 0;
            }
            for (size_t parent = 0; parent < count; parent++) {
                if (parent == i || retired[parent]) continue;
                if (contains(rows[parent].id, rows[i].id, userdata)) {
                    has_unretired_parent = 1;
                    break;
                }
            }
            if (!has_unretired_parent) candidate = i;
        }
        if (candidate == count) {
            free(retired);
            s_error(error, error_cap, "typed dependency cycle prevents reset");
            return 0;
        }
        retired[candidate] = 1;
        out_order[written] = candidate;
    }

    free(retired);
    return 1;
}

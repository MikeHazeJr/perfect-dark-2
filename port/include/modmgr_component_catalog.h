#ifndef PD_MODMGR_COMPONENT_CATALOG_H
#define PD_MODMGR_COMPONENT_CATALOG_H
#include <stddef.h>
#include "assetcatalog.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct modmgr_component_result {
    size_t examined, applied, unchanged, excluded, unresolved;
    int source_missing, saved, catalog_result;
    char id[CATALOG_ID_LEN];
    char error[256];
} modmgr_component_result_t;

/* Call on the menu/boot owner thread with the current session package source
 * directories. Snapshot callbacks never re-enter the catalog or mutate it.
 * A missing save destination is allowed only without persistent catalog choices;
 * replay without a destination means no optional saved state exists.
 * Return 1 on complete success, 0 with an owned error/partial replay result. */
int modmgrSaveCatalogComponentState(const char *path,
    const char *const *session_dirs, size_t session_count,
    modmgr_component_result_t *result);
int modmgrReplayCatalogComponentState(const char *path,
    const char *const *session_dirs, size_t session_count,
    modmgr_component_result_t *result);

/* Mod Manager wrappers resolve its actual mods path and session registry.
 * The file/catalog adapters above are independently testable production seams. */
int modmgrSaveComponentStateChecked(modmgr_component_result_t *result);
int modmgrLoadComponentStateChecked(modmgr_component_result_t *result);
/* Prepared component intent owns its IDs/path and never borrows catalog rows.
 * Prepare before publishing installed-mod intent; failed preparation is read-only.
 * Retain the plan across a failed Apply and free it on success or explicit discard.
 * The caller must guard installed package identity and Agent identity on retry.
 * Overrides must name persistent rows in the initial real catalog snapshot.
 * Saving the plan merges unresolved IDs and performs no runtime toggles. */
typedef struct modmgr_component_plan modmgr_component_plan_t;
typedef struct modmgr_component_override {
    char id[CATALOG_ID_LEN];
    int enabled;
} modmgr_component_override_t;
int modmgrPrepareCatalogComponentPlan(const char *path,
    const char *const *session_dirs, size_t session_count,
    const modmgr_component_override_t *overrides, size_t override_count,
    modmgr_component_plan_t **plan, modmgr_component_result_t *result);
int modmgrSaveCatalogComponentPlan(const modmgr_component_plan_t *plan,
    modmgr_component_result_t *result);
void modmgrFreeCatalogComponentPlan(modmgr_component_plan_t *plan);
/* Core wrapper resolves the same real mods directory/session registry as
 * modmgrSaveComponentStateChecked; it does not persist or publish selection. */
int modmgrPrepareComponentPlan(const modmgr_component_override_t *overrides,
    size_t override_count, modmgr_component_plan_t **plan,
    modmgr_component_result_t *result);

#ifdef __cplusplus
}
#endif
#endif

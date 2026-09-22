#ifndef PD_MODMGR_APPLY_H
#define PD_MODMGR_APPLY_H
#include "modmgr_component_catalog.h"
#include "modmgr_save_status.h"
#ifdef __cplusplus
extern "C" {
#endif
enum modmgr_apply_phase {
    MODMGR_APPLY_PREPARE, MODMGR_APPLY_COMPONENT_SAVE, MODMGR_APPLY_CONFIG_SAVE,
    MODMGR_APPLY_RETIRE, MODMGR_APPLY_COMPONENT_SCAN, MODMGR_APPLY_BOT_SCAN,
    MODMGR_APPLY_PACKAGE_LOAD, MODMGR_APPLY_COMPONENT_REPLAY, MODMGR_APPLY_COMPLETE
};
typedef struct modmgr_apply_result {
    enum modmgr_apply_phase phase;
    int runtime_started, runtime_completed, restart_count;
    /* This installed-activation operation published disabled -> enabled.
     * Retained across retries; does not imply other effects were rolled back. */
    int activation_published;
    modmgr_component_result_t component;
    modmgr_save_result_t config;
    char id[CATALOG_ID_LEN];
    char error[256];
} modmgr_apply_result_t;
typedef struct modmgr_apply_plan modmgr_apply_plan_t;
/* Prepare before installed selection publication. Keep the owned plan through
 * retry: it captures Agent identity, component intent, package identities and
 * original loaded states for requires-restart deferrals. It does not save or
 * mutate runtime. A transient plan performs no machine/Agent persistence.
 * Return1 only when the requested operation succeeds; failure owns phase and
 * partial effects. runtime_started includes operations which may change runtime;
 * runtime_completed means all checked rebuild stages completed, not visual proof. */
int modmgrPrepareApplyPlan(int persistent,
    const modmgr_component_override_t *changes, size_t change_count,
    modmgr_apply_plan_t **plan, modmgr_apply_result_t *result);
int modmgrApplyPreparedChanges(const modmgr_apply_plan_t *plan, modmgr_apply_result_t *result);
void modmgrFreeApplyPlan(modmgr_apply_plan_t *plan);
int modmgrApplyChangesChecked(int persistent, modmgr_apply_result_t *result);
/* Rebuild current registry without saving unrelated choices. Failure retains
 * dirty state and reports partial runtime effects. Reload unloads first and
 * changes stage only after success; sync does neither. */
int modmgrSyncCatalogToRegistryChecked(modmgr_apply_result_t *result);
int modmgrReloadChecked(modmgr_apply_result_t *result);
/* Prepare persistent activation BEFORE publishing enabled state. *plan is NULL
 * on failure; success transfers ownership to the caller. Apply/retry that SAME
 * plan until success or explicit discard, then free it. Discard is not rollback.
 * Already-enabled packages still receive a persistent plan. For refresh only,
 * use the separate sync API. */
int modmgrPrepareInstalledActivation(int index, modmgr_apply_plan_t **plan,
    modmgr_apply_result_t *result);
#ifdef __cplusplus
}
#endif
#endif

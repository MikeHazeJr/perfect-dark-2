#ifndef PD_MODMGR_SAVE_STATUS_H
#define PD_MODMGR_SAVE_STATUS_H
#include <stddef.h>
#include "agent_profile_codec.h"
#ifdef __cplusplus
extern "C" {
#endif
enum modmgr_save_phase {
    MODMGR_SAVE_PREPARE, MODMGR_SAVE_ENABLED_JSON,
    MODMGR_SAVE_MACHINE, MODMGR_SAVE_AGENT, MODMGR_SAVE_COMPLETE
};
enum modmgr_saved_destination {
    MODMGR_SAVED_ENABLED_JSON = 1, MODMGR_SAVED_MACHINE = 2, MODMGR_SAVED_AGENT = 4
};
typedef struct modmgr_save_result {
    enum modmgr_save_phase phase;
    unsigned saved;
    char agent[AGENT_PROFILE_NAME_MAX];
    char error[256];
} modmgr_save_result_t;
/* Synchronous owner-thread adapters return 1 on success, 0 on failure.
 * Saved bits describe destinations confirmed by this attempt, not rollback.
 * Agent persistence may succeed without a write when its snapshot is unchanged.
 * expected_agent is NULL for a new operation; retries pass the owned prior
 * result.agent so an identity change cannot save another Agent accidentally. */
typedef struct modmgr_save_callbacks {
    const char *(*active_agent)(void *user);
    int (*save)(enum modmgr_save_phase phase, const char *expected_agent,
        char *error, size_t capacity, void *user);
    void *user;
} modmgr_save_callbacks_t;
int modmgrRunConfigSave(const char *expected_agent,
    const modmgr_save_callbacks_t *callbacks, modmgr_save_result_t *result);
int modmgrSaveConfigChecked(const char *expected_agent, modmgr_save_result_t *result);
#ifdef __cplusplus
}
#endif
#endif

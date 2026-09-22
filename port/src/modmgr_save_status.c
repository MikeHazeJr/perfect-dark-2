#include "modmgr_save_status.h"
#include <stdio.h>
#include <string.h>

int modmgrRunConfigSave(const char *expected_agent,
    const modmgr_save_callbacks_t *callbacks, modmgr_save_result_t *result)
{
    /* A caller may retry with expected_agent pointing into the previous result. */
    char expected[AGENT_PROFILE_NAME_MAX];
    int expected_valid = 1;
    if (expected_agent) {
        size_t length = strlen(expected_agent);
        expected_valid = length < sizeof(expected);
        if (expected_valid) memcpy(expected, expected_agent, length + 1);
    }
    if (!result) return 0;
    memset(result, 0, sizeof(*result));
    if (!expected_valid || !callbacks || !callbacks->active_agent || !callbacks->save) {
        snprintf(result->error, sizeof(result->error), "Cannot prepare mod settings save.");
        return 0;
    }
    const char *active = callbacks->active_agent(callbacks->user);
    if (!active || strlen(active) >= sizeof(result->agent)) {
        snprintf(result->error, sizeof(result->error), "Invalid active Agent identity.");
        return 0;
    }
    snprintf(result->agent, sizeof(result->agent), "%s", expected_agent ? expected : active);
    if (strcmp(active, result->agent) != 0) {
        snprintf(result->error, sizeof(result->error), "Active Agent changed; mod settings were not saved.");
        return 0;
    }
    for (int stage = MODMGR_SAVE_PREPARE; stage <= MODMGR_SAVE_AGENT; ++stage) {
        result->phase = (enum modmgr_save_phase)stage;
        active = callbacks->active_agent(callbacks->user);
        if (!active || strcmp(active, result->agent) != 0) {
            snprintf(result->error, sizeof(result->error),
                "Active Agent changed during save; earlier saves remain.");
            return 0;
        }
        if (stage == MODMGR_SAVE_AGENT && !result->agent[0]) continue;
        if (!callbacks->save(result->phase, result->agent, result->error,
                sizeof(result->error), callbacks->user)) {
            if (!result->error[0]) snprintf(result->error, sizeof(result->error),
                "Could not save mod settings; earlier saves remain.");
            return 0;
        }
        if (stage > MODMGR_SAVE_PREPARE) result->saved |= 1u << (stage - 1);
    }
    active = callbacks->active_agent(callbacks->user);
    if (!active || strcmp(active, result->agent) != 0) {
        snprintf(result->error, sizeof(result->error),
            "Active Agent changed during save; earlier saves remain.");
        return 0;
    }
    result->phase = MODMGR_SAVE_COMPLETE;
    return 1;
}

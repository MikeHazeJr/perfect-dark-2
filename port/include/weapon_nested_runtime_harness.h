#ifndef PD_WEAPON_NESTED_RUNTIME_HARNESS_H
#define PD_WEAPON_NESTED_RUNTIME_HARNESS_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Run the production-linked T-ASSETS-022 plan. Each non-comment line is:
 * accept|owner_id|archive_path|expected_audio_id|expected_animation_id
 * reject|owner_id|archive_path|comma-separated IDs that must not leak
 * Returns 1 only when every case passes. */
s32 weaponNestedRuntimeHarnessRun(const char *plan_path);

#ifdef __cplusplus
}
#endif

#endif

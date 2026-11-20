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
struct gset; struct prop; struct hit; struct wg_v2_delivery;
struct wg_v2_observation;
/* Explicitly armed installed-client witnesses; inert in ordinary launches.
 * Observers never synthesize collision hits or damage. */
void weaponGraphWorldHitTick(void);
void weaponGraphWorldHitDispatch(const struct wg_v2_delivery *, struct gset *);
int weaponGraphWorldHitAim(const struct wg_v2_delivery *);
void weaponGraphWorldHitCloseout(const struct wg_v2_delivery *);
void weaponGraphWorldHitObserve(const struct wg_v2_delivery *, struct gset *, struct hit *, int after);
int weaponGraphObservationArmed(void);
void weaponGraphObserve(const struct wg_v2_observation *);

#ifdef __cplusplus
}
#endif

#endif

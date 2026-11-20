#ifndef PD_WEAPON_GRAPH_V2_RUNTIME_H
#define PD_WEAPON_GRAPH_V2_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
#include "weapon_graph_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
struct player; struct gset; struct weapon; struct weaponfunc; struct modeldef;
/* Main-client-thread adapter. Exact v2 identities remain classified while
 * retained copies live; no v1/default behavior fallback for classified slots. */
int wgV2RuntimeIsWeapon(int runtime_weapon);
int wgV2RuntimeRegisterArchive(int runtime_weapon, const char *id, const char *path, char *error, size_t cap);
void wgV2RuntimeRetireWeapon(int runtime_weapon, const char *id);
void wgV2RuntimeReset(void);
const struct weapon *wgV2RuntimeWeapon(int runtime_weapon);
const struct weapon *wgV2RuntimeGsetWeapon(const struct gset *);
const struct weaponfunc *wgV2RuntimeGsetFunction(const struct gset *);
const weapon_graph_held_function_t *wgV2RuntimeGsetHeld(const struct gset *);
struct modeldef *wgV2RuntimeModel(int runtime_weapon);
struct modeldef *wgV2RuntimeCasing(const struct gset *);
int wgV2RuntimeCanEnterUse(int network, int theater, char *error, size_t cap);
void wgV2RuntimePlayerBegin(struct player *);
void wgV2RuntimeRetireHand(struct player *, int hand, const char *reason);
void wgV2RuntimeRetirePlayers(void);
/* Explicit producers from physical trigger, native idle and attack seams. */
int wgV2RuntimeEnsureHand(struct player *, int hand);
void wgV2RuntimeInput(struct player *, int hand, int pressed, int released);
int wgV2RuntimeIdle(struct player *, int hand);
int wgV2RuntimeAdvance(struct player *, int hand);
int wgV2RuntimeDeliveryPending(struct player *, int hand);
/* A value receipt, never a borrowed Host pointer. Each hand binding gets its
 * own identity even when allocator storage and action ticket are reused. */
typedef struct wg_v2_delivery {
    struct player *owner;
    uint64_t owner_generation, hand_generation, source_generation, action_ticket;
    uint64_t reservation_generation, noise_generation;
    int hand, burstbullets;
} wg_v2_delivery;
/* Reserve before native side effects; begin also retains the selected dispatch
 * gset. A retained source is data ownership, not permission to deliver it. */
int wgV2RuntimeShotBegin(struct player *, int hand, struct gset *dispatch,
    uint64_t *scope, wg_v2_delivery *receipt);
int wgV2RuntimeDeliveryValid(const wg_v2_delivery *receipt);
/* Commit once immediately before the first native side effect. After this,
 * interruption closes delivery terminally; it must never authorize a retry. */
int wgV2RuntimeShotEffectsBegin(const wg_v2_delivery *receipt);
void wgV2RuntimeShotComplete(const wg_v2_delivery *receipt);
/* Pre-effect abort permits a fresh reservation identity. Post-effect abort
 * terminally consumes the shot half even if current-player context changed. */
void wgV2RuntimeShotAbort(const wg_v2_delivery *receipt);
int wgV2RuntimeNoiseBegin(struct player *, int hand, wg_v2_delivery *receipt);
void wgV2RuntimeNoiseComplete(const wg_v2_delivery *receipt);
/* Destination is initialized by native population. Open/copy source identity
 * explicitly, close every return. Zero for base or failure; failure logs and
 * leaves classified v2 reads invalid, never substitutes current catalog data. */
uint64_t wgV2RuntimeScopeOpen(struct player *, int hand, struct gset *destination);
uint64_t wgV2RuntimeScopeCopy(const struct gset *source, struct gset *destination,
    uint64_t owner_generation);
void wgV2RuntimeScopeClose(uint64_t token);
/* Explicitly armed, main-thread value observations. No borrowed Host or source
 * pointers cross a callback. Counters describe actual boundaries, not requests. */
typedef struct wg_v2_runtime_snapshot {
    wg_v2_delivery delivery;
    uint64_t press_sequence, queued_press, temporary_scopes;
    uint64_t consumptions, noise_increments, effect_commits, hit_effects, native_shots, debits[2];
    int mode, state, firing, pending, shot, noise, shot_reserved, noise_reserved;
    int host_present;
    int weapon, native_mode, state_minor, state_cycles, state_frames, state_flags;
    int trigger_on, trigger_previous, trigger_released, shots_to_take, attack_type;
    int ammo[2];
    float noise_radius;
} wg_v2_runtime_snapshot;
typedef enum wg_v2_observation_kind {
    WG_V2_OBS_PRESS, WG_V2_OBS_BUSY_PRESS, WG_V2_OBS_COALESCED, WG_V2_OBS_RELEASED,
    WG_V2_OBS_ADMITTED, WG_V2_OBS_BLOCKED, WG_V2_OBS_REFUSED, WG_V2_OBS_DEBIT,
    WG_V2_OBS_SHOT_BEGIN, WG_V2_OBS_NOISE_BEGIN, WG_V2_OBS_NOISE_INCREMENT,
    WG_V2_OBS_EFFECT, WG_V2_OBS_CONSUMED, WG_V2_OBS_RETIRED, WG_V2_OBS_HIT_EFFECT, WG_V2_OBS_SHOT_STATS
} wg_v2_observation_kind;
typedef struct wg_v2_observation {
    wg_v2_observation_kind kind;
    wg_v2_delivery receipt; /* Original producer identity, even when obsolete. */
    wg_v2_runtime_snapshot value;
    int ammo_before[2], ammo_after[2];
    float noise_before, noise_after, increment, maximum;
    int shots_delta, region_delta;
    uint64_t scope_token;
    uintptr_t scope_destination; /* Address value only, never dereferenced later. */
} wg_v2_observation;
int wgV2RuntimeSnapshot(struct player *, int hand, wg_v2_runtime_snapshot *);
void wgV2RuntimeObserveDebit(const wg_v2_delivery *, const int before[2], const int after[2]);
void wgV2RuntimeObserveNoise(const wg_v2_delivery *, float before, float after,
    float increment, float maximum);
void wgV2RuntimeObserveHit(const wg_v2_delivery *);
void wgV2RuntimeObserveShotStats(const wg_v2_delivery *, int shots_delta, int region_delta);
/* Native module operations; defined beside the existing hand state machine.
 * start only accepts; tick is the sole native firing/ammo/animation lifecycle. */
int bgunGraphV2Accept(struct player *, int hand);
int bgunGraphV2Tick(struct player *, int hand);
void bgunGraphV2Cancel(struct player *, int hand);
#ifdef __cplusplus
}
#endif
#endif

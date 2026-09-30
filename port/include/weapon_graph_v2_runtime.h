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
int wgV2RuntimeNoiseAdmit(struct player *, int hand);
void wgV2RuntimeNoiseConsumed(struct player *, int hand);
void wgV2RuntimeShotConsumed(struct player *, int hand);
/* Destination is initialized by native population. Open/copy source identity
 * explicitly, close every return. Zero for base or failure; failure logs and
 * leaves classified v2 reads invalid, never substitutes current catalog data. */
uint64_t wgV2RuntimeScopeOpen(struct player *, int hand, struct gset *destination);
void wgV2RuntimeScopeClose(uint64_t token);
/* Native module operations; defined beside the existing hand state machine.
 * start only accepts; tick is the sole native firing/ammo/animation lifecycle. */
int bgunGraphV2Accept(struct player *, int hand);
int bgunGraphV2Tick(struct player *, int hand);
void bgunGraphV2Cancel(struct player *, int hand);
#ifdef __cplusplus
}
#endif
#endif

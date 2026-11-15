#ifndef IN_GAME_BONDGUN_MOD_H
#define IN_GAME_BONDGUN_MOD_H

/*
 * Crystal pistol (T-MODDING-008): gameplay for public (mod) weapons that the
 * native weapon table cannot express: self-loading and regrowing magazines,
 * per-round seat visuals on the held model, and (later in this file) the
 * committed charge launch and the cluster detonation. Every entry point
 * returns early for base weapons, so native behaviour is unchanged.
 * Design: context/designs/modding/crystal-pistol-engine.md.
 */

#include <ultra64.h>
#include "types.h"
#include "loader_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mod magazine rules for weaponnum's ammo slot, or NULL (base weapon, or a
 * slot the mod does not declare). */
const loader_pool_mod_ammo_t *bgunModAmmo(s32 weaponnum, s32 ammoindex);
bool bgunModAmmoIsSelfLoading(s32 weaponnum, s32 ammoindex);

/* HANDSTATE_RELOAD for an ammo slot with ammoN_reload = regrow: one round per
 * regrow_ticks60 until the clip is full; firing (once a round is loaded) or a
 * pending weapon switch ends it. Returns like the other bgunTickInc* states. */
s32 bgunModTickIncReloadRegrow(struct handweaponinfo *info, s32 handnum,
		struct hand *hand, s32 lvupdate, s32 ammoindex,
		const loader_pool_mod_ammo_t *mod);

/* Per frame, after the held model's matrices are computed: scale each round's
 * seat (1 loaded, 0 spent, 0..1 while it regrows). */
void bgunModApplyHeldModel(struct hand *hand, struct modeldef *modeldef,
		Mtxf *matrices);

/* A mod projectile has stuck to something or come to rest (propobj.c). Arms
 * an entity proxy with proxy_trigger arm_on = land, and applies the
 * projectile's sticky_attach cluster rule: once cluster_count landed
 * projectiles of the same asset and owner lie within cluster_radius of the
 * one that just landed, they all detonate. */
void bgunModProjectileLanded(struct weaponobj *weapon);
/* objFree of a mod weapon object: forget it if it was registered as landed. */
void bgunModProjectileForget(struct weaponobj *weapon);
/* Stage load (setupResetProxyMines): clear stage-lifetime mod weapon state. */
void bgunModResetStage(void);

struct weapon_graph_held_function;

/* HANDSTATE_ATTACK for a function with charge_time_ticks60 (a committed charge
 * launch). The first tick commits every loaded round of the function's ammo
 * slot and starts the charge; the hand then stays busy until the release.
 * Returns true when the attack is finished. */
bool bgunModTickIncAttackCharge(struct handweaponinfo *info, s32 handnum,
		struct hand *hand, const struct weapon_graph_held_function *held);
/* Once per frame for the current player (lv.c, after handsTickAttack), alive
 * or dead: releases a charge whose deadline has passed. Each committed round
 * launches from its own seat part along its aim part; the fan is centred on
 * the crosshair and keeps the gun's roll. */
void bgunModChargeTick(void);

/* Once per frame from the current player's input (bondmove.c): ease the held
 * gun's roll toward the weapon's gun_roll_deg (left negative, right positive)
 * while a roll action is held, and back to level when neither is. */
void bgunModRollTick(bool left, bool right);
/* The current player's gun roll in radians (bondgun.c pose, innermost). */
f32 bgunModGetRoll(void);

#ifdef __cplusplus
}
#endif

#endif

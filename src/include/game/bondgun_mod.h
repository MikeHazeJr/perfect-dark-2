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
/* gset is a mod weapon function that spends rounds from a self-loading
 * magazine. Its projectiles are spent rounds, never pickups: there is no
 * reserve to refill, and collecting one would hand over the whole weapon. */
bool bgunModGsetFiresSpentRounds(struct gset *gset);

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

/* Third person (chr0f022214): a chr's held mod gun. Before its matrices are
 * built, bgunModHeldRoll3P composes the owner's gun roll into the hand
 * matrix (returns false when there is none); afterwards
 * bgunModApplyHeldModel3P scales the seats from the owner's clip and sets
 * the charge toggles from the owner's charge. */
bool bgunModHeldRoll3P(struct chrdata *chr, struct prop *weaponprop, Mtxf *in, Mtxf *out);
void bgunModApplyHeldModel3P(struct chrdata *chr, struct prop *weaponprop, struct model *model);

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
/* Diagnostic for projectileFindCollidingProp: a mod projectile's segment
 * passed within 150 units of a character. result -1 skipped (perim off or
 * hidden), 0 tested without a hit, 1 hit. Logs
 * BONDGUN.PROJECTILE.CHRTEST, at most 48 lines per stage. */
void bgunModTraceProjectileChrTest(struct prop *projectile, struct prop *chrprop,
		const struct coord *pos1, const struct coord *pos2, s32 result);

struct weapon_graph_held_function;

/* HANDSTATE_ATTACK for a function with charge_time_ticks60 (a committed charge
 * launch). The first tick commits every loaded round of the function's ammo
 * slot and starts the charge; the hand then stays busy until the release.
 * Returns true when the attack is finished. */
bool bgunModTickIncAttackCharge(struct handweaponinfo *info, s32 handnum,
		struct hand *hand, const struct weapon_graph_held_function *held, s32 chargetime60);
/* Once per frame for the current player (lv.c, after handsTickAttack), alive
 * or dead: releases a charge whose deadline has passed. Each committed round
 * launches from its own seat part; the rounds fan evenly from -launch_fan_deg
 * to +launch_fan_deg (15 by default) along the gun's spine (the line of the
 * seats' aim parts), centred on the crosshair and rolled with the gun. */
void bgunModChargeTick(void);

/* Bots (chrTickShoot): for a charge function, the shot commits every loaded
 * round instead (true: do not fire this shot). bgunModBotChargeTick releases
 * due bot charges once a frame: a vertical fan of mines from the bot's gun at
 * its target, alive or dead. */
bool bgunModBotChargeShot(struct chrdata *chr, s32 handnum, struct gset *gset);
void bgunModBotChargeTick(void);
/* botinvTick, once it has picked weaponnum/funcnum: a bot holding a mod
 * weapon whose secondary is a committed charge takes the charge with a full
 * magazine and its target 150..1500 units away, then fires a primary round
 * before it charges again. Returns the function to use. */
s32 bgunModBotChooseFunction(struct chrdata *chr, s32 weaponnum, s32 funcnum);
/* chrTickShoot, each round a bot fires: a primary round of a charge weapon
 * re-allows the charge. Logs BONDGUN.BOT: fired (first round per bot, weapon
 * and function each stage) for mod weapons. */
void bgunModBotFired(struct chrdata *chr, s32 handnum, struct gset *gset);
/* botinvSwitchToWeapon: both functions load from the same self-loading
 * magazine, so a switch between them keeps its loaded rounds. */
bool bgunModFunctionsShareSelfLoadingAmmo(s32 weaponnum, s32 funcnum1, s32 funcnum2);

/* Once per frame from the current player's input (bondmove.c): ease the held
 * gun's roll toward the weapon's gun_roll_deg (left negative, right positive)
 * while a roll action is held, and back to level when neither is. */
void bgunModRollTick(bool left, bool right);
/* The current player's gun roll in radians (bondgun.c pose, innermost). */
f32 bgunModGetRoll(void);
/* v63: the current player's roll actions as UCMD_GUNROLL_* bits, for its
 * move (bondmove.c records them; bmoveProcessRemoteInput replays them into
 * bgunModRollTick for a remote player). */
u32 bgunModRollUcmd(void);

/* For coordTriggerProxies: a mod mine's own radius and owner/team/target
 * filters. chr is the candidate (NULL for a moving object). Returns 1 to
 * trigger, 0 not to, -1 when the native fixed-range rule applies.
 * held-weapon-events.md slice 6b: `filtered` is the verdict of the filter
 * its program's arm names (1 passes, 0 refused), which then replaces the
 * record's filters within its radius; -1 when its arm names none. */
s32 bgunModProxyDecision(struct weaponobj *weapon, struct chrdata *chr, struct coord *pos, s32 filtered);

#ifdef __cplusplus
}
#endif

#endif

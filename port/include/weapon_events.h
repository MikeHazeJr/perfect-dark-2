#ifndef _IN_WEAPON_EVENTS_H
#define _IN_WEAPON_EVENTS_H

#include <PR/ultratypes.h>

/*
 * Held weapon events at the game's seams
 * (context/designs/modding/held-weapon-events.md, slices 2 and 3). The seams
 * in bondgun.c call these for the current player's hands; nothing here runs a
 * hand's state machine, which stays bondgun.c's.
 *
 * The rule events are fired where the idle hand decides what it does
 * (bgunTickIncIdle), after the native ammunition check:
 *
 *   event.weapon.trigger_pressed   the trigger is down and a press arrived
 *                                  since the hand last acted
 *   event.weapon.ready_while_held  the trigger is down and was already down
 *   event.weapon.clip_empty        the trigger is up and the clip is below
 *                                  what a shot needs while there is reserve
 *
 * When the hand's function has a program (pd.weapon_graph.v2) the program
 * chooses (a trigger: og.weapon.fire_once, og.weapon.burst or
 * og.weapon.automatic; og.weapon.dry_fire; og.weapon.reload); with none (a
 * v1 graph) the answer is the original's (`native`, weaponRulesNativeIdle).
 * The gates are answered from the hand (weapon_rules.h). og.weapon.set_state
 * writes the hand's named state as the walk reaches it, so a later gate of
 * the same walk sees it.
 *
 * Slice 3: the trigger a walk reaches first, with its settings, and
 * og.weapon.use_ammo's rounds are the plan of the attack the seam starts;
 * the hand keeps it and the cadence code runs it (weaponHandTrigger). A
 * function with no program has the original's plan for its record.
 * Simulants and guards fire the same programs: their ticks per shot are the
 * rate of the automatic a press starts (weaponProgramTicksPerShot).
 *
 * The parity probe counts each choice a program made where the original has
 * a rule (rules.compared) and the ones that differ (rules.differ, the first
 * eight logged as "WEAPON.RULE differs: ..."): the modules chosen and, when
 * both start an attack, the plan. The base programs must leave rules.differ
 * at 0. The first choice for a weapon, function and event logs
 * "WEAPON.RULE: <event> weapon=... func=... program=yes modules=... native=...
 * plan=... native_plan=...". A mod weapon's number has no original rule: its
 * choices are logged, not counted.
 */

struct hand;
struct weapon_rule_function;
struct weapon_rule_trigger;
struct weapon_rule_reload;
struct weapon_rule_lock;

/* bgunSetTriggerOn: the hand's trigger is `on` now and was `previous`. A
 * press waits for the idle seam; a release drops a waiting press and is the
 * record event event.weapon.trigger_released. */
void weaponEventTrigger(s32 handnum, struct hand *hand, s32 on, s32 previous);

/* Whether a press is waiting for this hand; the idle seam takes it (the
 * waiting press is gone after the call). */
s32 weaponEventTakePress(s32 handnum);

/* The idle seam: which idle action the rule event chooses for this hand
 * (WEAPON_MODULE_* bits: a trigger, og.weapon.dry_fire or
 * og.weapon.reload). ammo_status is the native ammunition check the seam
 * just made; record is the hand's function as the cadence code reads it.
 * When a trigger is chosen the hand keeps the attack's plan. */
u32 weaponIdleModules(s32 event_kind, s32 handnum, struct hand *hand, s32 ammo_status, u32 native,
	const struct weapon_rule_function *record);

/* The plan of the attack the hand is in, for the cadence code: the one its
 * idle seam chose while the hand holds that weapon and function, else the
 * original's for the record. Never NULL. */
const struct weapon_rule_trigger *weaponHandTrigger(struct hand *hand, const struct weapon_rule_function *record);

/* slice 4b: the reload the hand is in, for bgunTickIncReload: the one its
 * idle seam chose while the hand holds that weapon and function, else the
 * original's for the record. Never NULL. */
const struct weapon_rule_reload *weaponHandReload(struct hand *hand, const struct weapon_rule_function *record);

/* PR 31 review: whether a function's program regrows its clip
 * (og.weapon.reload style regrow), so its reload needs no reserve. 0 with no
 * program. */
s32 weaponProgramRegrows(s32 weaponnum, s32 func);

/* The attack a press starts for a simulant or a guard: the function's
 * program walked for a ready hand with a loaded clip, reserve to reload
 * from and every named state false (the hand a player takes the weapon
 * with). 1 with *out filled when the program starts one, 0 when the
 * function has no program or a press starts nothing. */
s32 weaponProgramPressPlan(s32 weaponnum, s32 func, struct weapon_rule_trigger *out);

/* The ticks between a simulant's or a guard's shots
 * (weaponGetNumTicksPerShot): the rate of the automatic a press starts, 0
 * for any other trigger; `native` for a function with no program. A base
 * function whose answer differs from `native` logs "WEAPON.RULE differs:
 * ticks_per_shot ...". */
s32 weaponProgramTicksPerShot(s32 weaponnum, s32 func, s32 native);

/* slice 7a: a simulant's shot interval (botactGetShootInterval60): the
 * kick and return frames of the recoil a press plans, `native` for a
 * function with no program or one whose press plans no recoil. */
s32 weaponProgramRecoilFrames(s32 weaponnum, s32 func, s32 native);

/* slice 7b: the casing a function's shots eject (CASING_*, -1 none): its
 * program's og.weapon.casing, else its ammo's; the gun loads that casing's
 * model when it is equipped. */
s32 weaponFunctionCasing(s32 weaponnum, s32 func);

/* slice 7b: a simulant's or a guard's shot sound and the frames its fire
 * slot keeps it (gsetGetSingleShootSound, gsetGetFireslotDuration): the
 * og.weapon.shot_sound a press plans, `native` for a function with no
 * program or a press that starts no attack. */
s32 weaponProgramShotSound(s32 weaponnum, s32 func, s32 native);
s32 weaponProgramShotDuration(s32 weaponnum, s32 func, s32 native);

/* A simulant's burst (bot.c): the rounds of the og.weapon.burst a press
 * starts, 0 for any other trigger; `native` for a function with no
 * program. */
s32 weaponProgramBurstRounds(s32 weaponnum, s32 func, s32 native);

/* slice 4: a weapon function's plan as simulants, guards and damage read
 * it (the shot's settings: a Farsight's shield damage, a simulant's Farsight
 * through walls): the press plan when the function has a program that
 * starts one, else the original's for its record. */
void weaponFunctionPlan(s32 weaponnum, s32 func, struct weapon_rule_trigger *out);

/* slice 5: the weapon's charge that builds while its function is selected
 * (og.weapon.charge, start while_selected; the Mauler's secondary): its
 * function number plus one with its plan, 0 when it has none. The charge
 * level is the weapon's (the hand's matmot1, in steps): every function's shot
 * carries it, as the original's Mauler did. */
s32 weaponWeaponCharge(s32 weaponnum, struct weapon_rule_trigger *out);

/* slice 4: whether one of the weapon's functions toggles the cloak
 * (og.weapon.mode_toggle, mode cloak): 1 with its drain in rounds a second
 * and its clip's ammo slot. */
s32 weaponCloakToggle(s32 weaponnum, f32 *drain, s32 *ammo_slot);

/* bgunSetState: the hand left its reload for idle (event.weapon.reload_done,
 * a record). */
void weaponEventReloadDone(s32 handnum, struct hand *hand);

/* slice 6b: the sight's lock for the current player's right hand (the sight
 * is the right hand's). While the player aims (`aiming`, sightTick), its
 * function's program walks event.weapon.aiming and the og.weapon.lock_on it
 * reaches is the lock (none reached: no lock, the box follows the aim); the
 * hand keeps it while it holds that function, for the sight and auto-aim
 * between walks. A function with no program, or whose program does not
 * handle the event, has the original's for its weapon's aim settings
 * (weaponRulesNativeLock). A base function's walked lock is compared with
 * the original's (rules.compared, rules.differ). Never NULL. */
const struct weapon_rule_lock *weaponSightLock(s32 aiming);

/* slice 6c: sightTick took a target into the lock's slot (0 up): counted for
 * WEAPON.EVENTS (locks=N), and the stage's first sixteen logged as
 * "SIGHT.LOCK: player=0 weapon=... func=... slot=1 of 1 kind=vehicle
 * occupied=1 relation=enemy". */
struct prop;
void weaponSightLockTaken(s32 slot, struct prop *prop);

/* Logs "WEAPON.EVENTS: <why> trigger_pressed=N ready_while_held=N
 * clip_empty=N trigger_released=N reload_done=N rules.compared=N
 * rules.differ=N aiming=N locks=N" and starts the counts again. */
void weaponEventsReport(const char *why);

#endif

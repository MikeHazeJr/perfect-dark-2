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

/* A simulant's burst (bot.c): the rounds of the og.weapon.burst a press
 * starts, 0 for any other trigger; `native` for a function with no
 * program. */
s32 weaponProgramBurstRounds(s32 weaponnum, s32 func, s32 native);

/* bgunSetState: the hand left its reload for idle (event.weapon.reload_done,
 * a record). */
void weaponEventReloadDone(s32 handnum, struct hand *hand);

/* Logs "WEAPON.EVENTS: <why> trigger_pressed=N ready_while_held=N
 * clip_empty=N trigger_released=N reload_done=N rules.compared=N
 * rules.differ=N" and starts the counts again. */
void weaponEventsReport(const char *why);

#endif

#ifndef _IN_WEAPON_EVENTS_H
#define _IN_WEAPON_EVENTS_H

#include <PR/ultratypes.h>

/*
 * Held weapon events at the game's seams
 * (context/designs/modding/held-weapon-events.md, slice 2). The seams in
 * bondgun.c call these for the current player's hands; nothing here runs a
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
 * chooses (og.weapon.fire, og.weapon.dry_fire, og.weapon.reload); with none
 * (a v1 graph) the answer is the original's (`native`,
 * weaponRulesNativeIdle). The gates are answered from the hand
 * (weapon_rules.h). og.weapon.set_state writes the hand's named state as the
 * walk reaches it, so a later gate of the same walk sees it.
 *
 * The parity probe counts each choice a program made where the original has
 * a rule (rules.compared) and the ones that differ (rules.differ, the first
 * eight logged as "WEAPON.RULE differs: ..."); the base programs must leave
 * rules.differ at 0. The first choice for a weapon, function and event logs
 * "WEAPON.RULE: <event> weapon=... func=... program=yes|no modules=... native=...".
 * A mod weapon's number has no original rule: its choices are logged, not
 * counted.
 */

struct hand;

/* bgunSetTriggerOn: the hand's trigger is `on` now and was `previous`. A
 * press waits for the idle seam; a release drops a waiting press and is the
 * record event event.weapon.trigger_released. */
void weaponEventTrigger(s32 handnum, struct hand *hand, s32 on, s32 previous);

/* Whether a press is waiting for this hand; the idle seam takes it (the
 * waiting press is gone after the call). */
s32 weaponEventTakePress(s32 handnum);

/* The idle seam: which of og.weapon.fire, og.weapon.dry_fire and
 * og.weapon.reload the rule event chooses for this hand (WEAPON_MODULE_*
 * bits). ammo_status is the native ammunition check the seam just made. */
u32 weaponIdleModules(s32 event_kind, s32 handnum, struct hand *hand, s32 ammo_status, u32 native);

/* bgunSetState: the hand left its reload for idle (event.weapon.reload_done,
 * a record). */
void weaponEventReloadDone(s32 handnum, struct hand *hand);

/* Logs "WEAPON.EVENTS: <why> trigger_pressed=N ready_while_held=N
 * clip_empty=N trigger_released=N reload_done=N rules.compared=N
 * rules.differ=N" and starts the counts again. */
void weaponEventsReport(const char *why);

#endif

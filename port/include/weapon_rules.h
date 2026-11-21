#ifndef _IN_WEAPON_RULES_H
#define _IN_WEAPON_RULES_H

#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "constants.h"
#include "mission_graph_runner.h"

/*
 * The modules and gates a held weapon program can run, the events that apply
 * them, and the original game's rules that the base programs wire
 * (context/designs/modding/held-weapon-events.md, slice 2). No game state
 * here, so the game, the extractor and pd-tests all read one table:
 *
 * - the extractor writes each base function's program from it;
 * - the game's seams compare a program's choice with it (the parity probe)
 *   and use it for a function with no program (a v1 graph);
 * - registration refuses a module wired to an event that does not apply it,
 *   because nothing would happen.
 *
 * As for projectiles, a module does not run code where the program fires it.
 * It is a choice: the seam that fired the event then runs the original code
 * for the modules chosen, where that code has always run.
 *
 * The table is header-only (static inline) so every target that includes it,
 * pd-tests included, has it without a source list of its own.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* The modules, as bits. */
#define WEAPON_MODULE_FIRE      (1u << 0) /* og.weapon.fire: the hand starts its attack with its function */
#define WEAPON_MODULE_DRY_FIRE  (1u << 1) /* og.weapon.dry_fire: the hand clicks on an empty clip */
#define WEAPON_MODULE_RELOAD    (1u << 2) /* og.weapon.reload: the hand reloads its function's clip */
#define WEAPON_MODULE_SET_STATE (1u << 3) /* og.weapon.set_state: a named state of the hand ("key") becomes "value" */
#define WEAPON_MODULE_COUNT 4
#define WEAPON_MODULE_ALL ((1u << WEAPON_MODULE_COUNT) - 1)
/* The modules that decide what an idle hand does; the first chosen in this
 * order runs. */
#define WEAPON_MODULE_IDLE_ACTIONS (WEAPON_MODULE_FIRE | WEAPON_MODULE_DRY_FIRE | WEAPON_MODULE_RELOAD)

/*
 * The gates. A gate reads the hand, so it can be answered at every weapon
 * event.
 *
 * - has_ammo: the clip holds what a shot needs. Its whole-number setting
 *   "rounds" (1 or more, default 1) is the least the clip must hold; a
 *   function that uses no ammunition, or whose clip is full, always passes.
 *   The original needs 2 for the Shotgun's double blast and 4 for the
 *   Tranquilizer's sedative shot, which the base programs set.
 * - has_reserve: there is ammunition to reload from (or the clip is a mod
 *   weapon's self-loading magazine); a function that uses none passes.
 * - ready_to_fire: the hand is idle (not attacking, reloading or switching).
 * - state_is: a named state of the hand ("key") is "equals" (true or false;
 *   every state is false when the hand takes the weapon).
 */
#define WEAPON_RULE_GATE_HAS_AMMO      (1u << 24)
#define WEAPON_RULE_GATE_HAS_RESERVE   (1u << 25)
#define WEAPON_RULE_GATE_READY_TO_FIRE (1u << 26)
#define WEAPON_RULE_GATE_STATE_IS      (1u << 27)
#define WEAPON_GATE_COUNT 4
#define WEAPON_RULE_GATE_ALL (((1u << WEAPON_GATE_COUNT) - 1) << 24)
#define WEAPON_GATE_HAS_AMMO_KIND "gate.weapon.has_ammo"
#define WEAPON_GATE_HAS_RESERVE_KIND "gate.weapon.has_reserve"
#define WEAPON_GATE_READY_TO_FIRE_KIND "gate.weapon.ready_to_fire"
#define WEAPON_GATE_STATE_IS_KIND "gate.weapon.state_is"

/* The node settings the table's modules and gates read. */
#define WEAPON_RULE_ROUNDS_PARAM "rounds"
#define WEAPON_RULE_KEY_PARAM "key"
#define WEAPON_RULE_EQUALS_PARAM "equals"
#define WEAPON_RULE_VALUE_PARAM "value"
#define WEAPON_RULE_MAX_ROUNDS 255
#define WEAPON_RULE_STATE_KEY_LEN 64
/* The distinct state keys one function's program may name. */
#define WEAPON_RULE_MAX_STATES 8

/* The modules by bit, in bit order. */
static inline const char *weaponRulesModuleKind(u32 bit)
{
	static const char *const kinds[WEAPON_MODULE_COUNT] = {
		"og.weapon.fire", "og.weapon.dry_fire", "og.weapon.reload", "og.weapon.set_state",
	};
	for (s32 i = 0; i < WEAPON_MODULE_COUNT; i++) {
		if (bit == (1u << i)) return kinds[i];
	}
	return NULL;
}

/* The bit of a module kind, 0 when it is not a weapon module. */
static inline u32 weaponRulesModuleBit(const char *kind)
{
	if (!kind) return 0;
	for (s32 i = 0; i < WEAPON_MODULE_COUNT; i++) {
		if (strcmp(weaponRulesModuleKind(1u << i), kind) == 0) return 1u << i;
	}
	return 0;
}

/* "gate.weapon.has_ammo" for WEAPON_RULE_GATE_HAS_AMMO; NULL unless the
 * argument is exactly one gate bit. */
static inline const char *weaponRulesGateKind(u32 gate)
{
	static const char *const kinds[WEAPON_GATE_COUNT] = {
		WEAPON_GATE_HAS_AMMO_KIND, WEAPON_GATE_HAS_RESERVE_KIND,
		WEAPON_GATE_READY_TO_FIRE_KIND, WEAPON_GATE_STATE_IS_KIND,
	};
	for (s32 i = 0; i < WEAPON_GATE_COUNT; i++) {
		if (gate == (WEAPON_RULE_GATE_HAS_AMMO << i)) return kinds[i];
	}
	return NULL;
}

/* The bit of a gate kind, 0 when it is not a weapon gate. */
static inline u32 weaponRulesGateBit(const char *kind)
{
	if (!kind) return 0;
	for (s32 i = 0; i < WEAPON_GATE_COUNT; i++) {
		if (strcmp(weaponRulesGateKind(WEAPON_RULE_GATE_HAS_AMMO << i), kind) == 0) {
			return WEAPON_RULE_GATE_HAS_AMMO << i;
		}
	}
	return 0;
}

/* The weapon events, in kind order (MISSION_GRAPH_EVENT_WEAPON_*). */
#define WEAPON_RULE_EVENT_COUNT \
	(MISSION_GRAPH_EVENT_WEAPON_RELOAD_DONE - MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED + 1)

/* The index-th weapon event's kind, or -1 past the end. */
static inline s32 weaponRulesEventAt(s32 index)
{
	return index >= 0 && index < WEAPON_RULE_EVENT_COUNT
		? MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED + index : -1;
}

/* "event.weapon.trigger_pressed" for its kind; NULL for no weapon event. */
static inline const char *weaponRulesEventName(s32 event_kind)
{
	static const char *const names[WEAPON_RULE_EVENT_COUNT] = {
		"event.weapon.trigger_pressed", "event.weapon.ready_while_held",
		"event.weapon.clip_empty", "event.weapon.trigger_released",
		"event.weapon.reload_done",
	};
	const s32 index = event_kind - MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED;

	return index >= 0 && index < WEAPON_RULE_EVENT_COUNT ? names[index] : NULL;
}

/* The modules the event's seam applies. The idle seam's three rule events
 * (trigger_pressed, ready_while_held, clip_empty) apply the idle actions and
 * set_state; the records (trigger_released, reload_done) apply set_state
 * only. */
static inline u32 weaponRulesEventModules(s32 event_kind)
{
	switch (event_kind) {
	case MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED:
	case MISSION_GRAPH_EVENT_WEAPON_READY_WHILE_HELD:
	case MISSION_GRAPH_EVENT_WEAPON_CLIP_EMPTY:
		return WEAPON_MODULE_IDLE_ACTIONS | WEAPON_MODULE_SET_STATE;
	case MISSION_GRAPH_EVENT_WEAPON_TRIGGER_RELEASED:
	case MISSION_GRAPH_EVENT_WEAPON_RELOAD_DONE:
		return WEAPON_MODULE_SET_STATE;
	}
	return 0;
}

/*
 * The original's choice at the idle seam (bgunTickIncIdle). ammo_status is
 * the native ammunition check (bgun0f098ca0): below 0 nothing to fire or
 * reload, 0 the clip is below what a shot needs but there is reserve, 1 or
 * more the clip can fire. At trigger_pressed and ready_while_held the
 * original fires when the clip can, clicks when it is empty with reserve
 * (unless the hand holds no weapon, when it reloads), and does nothing when
 * there is nothing at all; at clip_empty it reloads. 0 elsewhere.
 */
static inline u32 weaponRulesNativeIdle(s32 event_kind, s32 ammo_status, s32 weapon_is_none)
{
	switch (event_kind) {
	case MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED:
	case MISSION_GRAPH_EVENT_WEAPON_READY_WHILE_HELD:
		if (ammo_status > 0) return weapon_is_none ? 0 : WEAPON_MODULE_FIRE;
		if (ammo_status == 0) return weapon_is_none ? WEAPON_MODULE_RELOAD : WEAPON_MODULE_DRY_FIRE;
		return 0;
	case MISSION_GRAPH_EVENT_WEAPON_CLIP_EMPTY:
		return WEAPON_MODULE_RELOAD;
	}
	return 0;
}

/* The rounds a shot of this weapon number and function needs, by the
 * original (bgun0f098ca0's minimum): 2 for the Shotgun's secondary, 4 for
 * the Tranquilizer's, 1 for everything else. The base programs set it on
 * their has_ammo gate. */
static inline s32 weaponRulesNativeRounds(s32 weaponnum, s32 func)
{
	if (weaponnum == WEAPON_SHOTGUN && func == FUNC_SECONDARY) return 2;
	if (weaponnum == WEAPON_TRANQUILIZER && func == FUNC_SECONDARY) return 4;
	return 1;
}

/* The rounds a has_ammo node sets: its "rounds" text, NULL for none.
 * Returns the number (1 to WEAPON_RULE_MAX_ROUNDS), 1 for NULL, and -1 for
 * anything else. */
static inline s32 weaponRulesRoundsFromText(const char *text)
{
	long value;
	char *end = NULL;

	if (!text) return 1;
	if (!text[0]) return -1;
	for (const char *p = text; *p; p++) {
		if (*p < '0' || *p > '9') return -1;
	}
	value = strtol(text, &end, 10);
	if (!end || *end || value < 1 || value > WEAPON_RULE_MAX_ROUNDS) return -1;
	return (s32)value;
}

/* A state_is "equals" or set_state "value" text: 1 for "true", 0 for
 * "false", -1 for anything else (NULL included). */
static inline s32 weaponRulesBoolFromText(const char *text)
{
	if (!text) return -1;
	if (strcmp(text, "true") == 0) return 1;
	if (strcmp(text, "false") == 0) return 0;
	return -1;
}

/*
 * The hand as the gates read it. The seam fills it from the hand and its
 * function as the native ammunition check reads them; pd-tests fill it by
 * hand.
 */
typedef struct weapon_rule_hand {
	s32 uses_ammo; /* the function has a clip: an ammo slot whose ammo type exists */
	s32 loaded;    /* rounds in the clip */
	s32 clipsize;  /* the clip's size */
	s32 reserve;   /* there is ammunition to reload from, or the clip loads itself */
	s32 idle;      /* the hand is idle */
} weapon_rule_hand_t;

static inline s32 weaponRulesHasAmmo(const weapon_rule_hand_t *hand, s32 rounds)
{
	if (!hand) return 0;
	if (rounds < 1) rounds = 1;
	if (!hand->uses_ammo || hand->loaded >= hand->clipsize) return 1;
	return hand->loaded >= rounds;
}

static inline s32 weaponRulesHasReserve(const weapon_rule_hand_t *hand)
{
	if (!hand) return 0;
	return !hand->uses_ammo || hand->reserve;
}

/* The native ammunition check (bgun0f098ca0) of a hand that holds a
 * function, from the same reads the gates make: -1 nothing to fire or reload,
 * 0 below `rounds` with reserve, 1 or 2 can fire (2 with no reserve), 3 a
 * full clip or no ammunition. pd-tests compare the base wiring with the
 * table through it. */
static inline s32 weaponRulesNativeAmmoStatus(const weapon_rule_hand_t *hand, s32 rounds)
{
	if (!hand || !hand->uses_ammo || hand->loaded >= hand->clipsize) return 3;
	if (hand->loaded < rounds) return hand->reserve ? 0 : -1;
	return hand->reserve ? 1 : 2;
}

#ifdef __cplusplus
}
#endif

#endif

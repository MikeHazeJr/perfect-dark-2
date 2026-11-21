/*
 * weapon_events.c -- held weapon events at the game's seams: see
 * weapon_events.h and context/designs/modding/held-weapon-events.md
 * (slice 2).
 */

#include <stdio.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "types.h"
#include "constants.h"
#include "bss.h"
#include "system.h"
#include "game/game_0b0fd0.h"
#include "game/bondgun_mod.h"
#include "weapondata_authored.h"
#include "weapon_graph_runtime.h"
#include "mission_graph_runner.h"
#include "weapon_rules.h"
#include "weapon_events.h"

/* Each player hand: a press waiting for the idle seam, and the named states
 * of the function it holds (every state is false when the hand takes a
 * weapon or changes function). */
typedef struct weapon_hand_record {
	u8 press;
	u8 count;
	s16 weaponnum;
	s8 func;
	char keys[WEAPON_RULE_MAX_STATES][WEAPON_RULE_STATE_KEY_LEN];
	u8 values[WEAPON_RULE_MAX_STATES];
} weapon_hand_record_t;
static weapon_hand_record_t s_WeaponHands[MAX_PLAYERS][2];

/* The stage's totals, by event, and the parity counters. */
#define WEAPON_EVENT_TOTALS 5
static s32 s_WeaponEventTotals[WEAPON_EVENT_TOTALS];
static s32 s_WeaponRuleCompared;
static s32 s_WeaponRuleDiffer;

/* The first choice for a weapon, function and event logs a line; later ones
 * only count. Bounded, so an automatic weapon cannot flood the log. */
#define WEAPON_EVENT_SEEN_MAX 512
typedef struct weapon_event_seen {
	s16 weaponnum;
	s8 func;
	s8 kind;
} weapon_event_seen_t;
static weapon_event_seen_t s_WeaponEventSeen[WEAPON_EVENT_SEEN_MAX];
static s32 s_WeaponEventSeenCount;

static s32 weaponEventTotalIndex(s32 event_kind)
{
	switch (event_kind) {
	case MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED: return 0;
	case MISSION_GRAPH_EVENT_WEAPON_READY_WHILE_HELD: return 1;
	case MISSION_GRAPH_EVENT_WEAPON_CLIP_EMPTY: return 2;
	case MISSION_GRAPH_EVENT_WEAPON_TRIGGER_RELEASED: return 3;
	case MISSION_GRAPH_EVENT_WEAPON_RELOAD_DONE: return 4;
	}
	return -1;
}

static const char *weaponEventWeaponId(s32 weaponnum)
{
	if (weaponnum >= 0 && weaponnum < g_WeaponDataCount && g_WeaponDataCatalogIds[weaponnum]) {
		return g_WeaponDataCatalogIds[weaponnum];
	}
	return "?";
}

static s32 weaponEventFirst(s32 weaponnum, s32 func, s32 kind)
{
	for (s32 i = 0; i < s_WeaponEventSeenCount; i++) {
		if (s_WeaponEventSeen[i].weaponnum == weaponnum && s_WeaponEventSeen[i].func == func
				&& s_WeaponEventSeen[i].kind == kind) {
			return 0;
		}
	}
	if (s_WeaponEventSeenCount >= WEAPON_EVENT_SEEN_MAX) {
		return 0;
	}
	s_WeaponEventSeen[s_WeaponEventSeenCount].weaponnum = (s16)weaponnum;
	s_WeaponEventSeen[s_WeaponEventSeenCount].func = (s8)func;
	s_WeaponEventSeen[s_WeaponEventSeenCount].kind = (s8)kind;
	s_WeaponEventSeenCount++;
	return 1;
}

/* The current player's record of this hand, its states reset when the hand
 * holds another weapon or function than they belong to. */
static weapon_hand_record_t *weaponHandRecord(s32 handnum, const struct hand *hand)
{
	weapon_hand_record_t *record;

	if (handnum < 0 || handnum > 1 || g_Vars.currentplayernum < 0
			|| g_Vars.currentplayernum >= MAX_PLAYERS) {
		return NULL;
	}
	record = &s_WeaponHands[g_Vars.currentplayernum][handnum];
	if (hand && (record->weaponnum != hand->gset.weaponnum || record->func != (s8)hand->gset.weaponfunc)) {
		const u8 press = record->press;

		memset(record, 0, sizeof(*record));
		record->press = press;
		record->weaponnum = (s16)hand->gset.weaponnum;
		record->func = (s8)hand->gset.weaponfunc;
	}
	return record;
}

static s32 weaponHandStateIndex(const weapon_hand_record_t *record, const char *key)
{
	for (s32 i = 0; i < record->count; i++) {
		if (strcmp(record->keys[i], key) == 0) return i;
	}
	return -1;
}

/* The hand as the gates read it, as the native ammunition check
 * (bgun0f098ca0) reads it. */
static void weaponRuleHandOf(const struct hand *hand, weapon_rule_hand_t *out)
{
	struct weaponfunc *func = weaponGetFunction((struct gset *)&hand->gset, hand->gset.weaponfunc);

	memset(out, 0, sizeof(*out));
	out->idle = hand->state == HANDSTATE_IDLE;
	if (func && func->ammoindex != -1) {
		const s32 ammoindex = func->ammoindex;
		const s32 type = g_Vars.currentplayer->gunctrl.ammotypes[ammoindex];

		if (type >= 0) {
			out->uses_ammo = 1;
			out->loaded = hand->loadedammo[ammoindex];
			out->clipsize = hand->clipsizes[ammoindex];
			out->reserve = g_Vars.currentplayer->ammoheldarr[type] != 0
				|| bgunModAmmoIsSelfLoading(hand->gset.weaponnum, ammoindex);
		}
	}
}

/* What a held program's walk chose for one event. */
typedef struct weapon_rule {
	u32 modules;
	s32 handnum;
	const struct hand *hand;
	weapon_hand_record_t *record;
} weapon_rule_t;

/* The held programs' modules and gates (weapon_rules.h lists the kinds,
 * and weapon_graph_runtime.c's heldProgramModuleKnown accepts the same). */
static s32 weaponProgramModuleRun(const char *kind, const char *node_id,
		const mission_graph_params_t *params, const mission_graph_event_t *event, void *user)
{
	weapon_rule_t *rule = (weapon_rule_t *)user;
	const u32 bit = weaponRulesModuleBit(kind);
	const u32 gate = weaponRulesGateBit(kind);
	const s32 weaponnum = rule && rule->hand ? rule->hand->gset.weaponnum : -1;
	char buf[WEAPON_RULE_STATE_KEY_LEN];
	char vbuf[16];
	weapon_rule_hand_t hand;

	(void)node_id;
	(void)event;
	if (!rule || !rule->hand || !kind) {
		return 0;
	}
	if (gate) {
		weaponRuleHandOf(rule->hand, &hand);
		switch (gate) {
		case WEAPON_RULE_GATE_HAS_AMMO: {
			const s32 rounds = weaponRulesRoundsFromText(weaponGraphRuntimeHeldParamText(weaponnum, params,
				WEAPON_RULE_ROUNDS_PARAM, vbuf, sizeof(vbuf)));

			return weaponRulesHasAmmo(&hand, rounds > 0 ? rounds : 1) ? 1 : 0;
		}
		case WEAPON_RULE_GATE_HAS_RESERVE:
			return weaponRulesHasReserve(&hand) ? 1 : 0;
		case WEAPON_RULE_GATE_READY_TO_FIRE:
			return hand.idle ? 1 : 0;
		case WEAPON_RULE_GATE_STATE_IS: {
			const char *key = weaponGraphRuntimeHeldParamText(weaponnum, params, WEAPON_RULE_KEY_PARAM,
				buf, sizeof(buf));
			const s32 equals = weaponRulesBoolFromText(weaponGraphRuntimeHeldParamText(weaponnum, params,
				WEAPON_RULE_EQUALS_PARAM, vbuf, sizeof(vbuf)));
			const s32 at = key && rule->record ? weaponHandStateIndex(rule->record, key) : -1;
			const s32 value = at >= 0 ? rule->record->values[at] : 0;

			return equals >= 0 && value == equals ? 1 : 0;
		}
		}
		return 0;
	}
	if (bit == WEAPON_MODULE_SET_STATE) {
		const char *key = weaponGraphRuntimeHeldParamText(weaponnum, params, WEAPON_RULE_KEY_PARAM,
			buf, sizeof(buf));
		const s32 value = weaponRulesBoolFromText(weaponGraphRuntimeHeldParamText(weaponnum, params,
			WEAPON_RULE_VALUE_PARAM, vbuf, sizeof(vbuf)));
		s32 at;

		/* Registration refused a node without a key or a boolean value and a
		 * program with more keys than the table holds. */
		if (!key || !key[0] || value < 0 || !rule->record) {
			return 0;
		}
		at = weaponHandStateIndex(rule->record, key);
		if (at < 0) {
			if (rule->record->count >= WEAPON_RULE_MAX_STATES) return 0;
			at = rule->record->count++;
			snprintf(rule->record->keys[at], sizeof(rule->record->keys[at]), "%s", key);
		}
		rule->record->values[at] = (u8)value;
		rule->modules |= bit;
		return 1;
	}
	if (bit) {
		rule->modules |= bit;
		return 1;
	}
	return 0;
}

/* "trigger_pressed" for MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED. */
static const char *weaponRuleName(s32 kind)
{
	const char *name = weaponRulesEventName(kind);

	return name ? name + strlen("event.weapon.") : "?";
}

/* "fire" for a set of modules, "-" for none. */
static const char *weaponModulesText(u32 modules, char *buf, size_t cap)
{
	size_t used = 0;

	buf[0] = '\0';
	for (s32 m = 0; m < WEAPON_MODULE_COUNT; m++) {
		const char *kind = (modules & (1u << m)) ? weaponRulesModuleKind(1u << m) : NULL;

		if (kind) {
			const int n = snprintf(buf + used, cap - used, "%s%s", used ? "+" : "",
				kind + strlen("og.weapon."));

			if (n < 0 || (size_t)n >= cap - used) break;
			used += (size_t)n;
		}
	}
	if (!used) snprintf(buf, cap, "-");
	return buf;
}

/* One event for a hand: the program walks it (gates, set_state), and the
 * modules it chose are returned; with no program, `native`. */
static u32 weaponRuleDecide(s32 kind, s32 handnum, struct hand *hand, u32 native)
{
	const s32 weaponnum = hand->gset.weaponnum;
	const s32 func = hand->gset.weaponfunc;
	const mission_graph_program_t *program = weaponGraphRuntimeHeldProgram(weaponnum, func);
	const s32 index = weaponEventTotalIndex(kind);
	const u32 applied = weaponRulesEventModules(kind);
	mission_graph_event_t event;
	weapon_rule_t rule;
	s32 handled;

	if (index >= 0) {
		s_WeaponEventTotals[index]++;
	}
	if (!program) {
		return native; /* a v1 function: the original decides */
	}
	memset(&rule, 0, sizeof(rule));
	rule.handnum = handnum;
	rule.hand = hand;
	rule.record = weaponHandRecord(handnum, hand);
	handled = missionGraphProgramHandlesEvent(program, kind);
	if (handled) {
		memset(&event, 0, sizeof(event));
		event.kind = kind;
		event.player = g_Vars.currentplayernum;
		event.catalog_id = weaponEventWeaponId(weaponnum);
		event.instance = hand;
		if (missionGraphProgramFire(program, &event, weaponProgramModuleRun, &rule) < 0) {
			rule.modules = 0; /* a module refused: nothing is chosen */
		}
		rule.modules &= applied;
	}
	if ((applied & WEAPON_MODULE_IDLE_ACTIONS) && (handled || native)) {
		char chose[64];
		char orig[64];
		const u32 chosen = rule.modules & WEAPON_MODULE_IDLE_ACTIONS;
		/* A mod weapon's number has no original rule: its choices are
		 * logged and not counted. */
		const s32 counted = weaponnum < WEAPON_CUSTOM_START;

		s_WeaponRuleCompared += counted;
		if (counted && chosen != native) {
			s_WeaponRuleDiffer++;
			if (s_WeaponRuleDiffer <= 8) {
				sysLogPrintf(LOG_WARNING,
					"WEAPON.RULE differs: %s weapon=%s func=%s hand=%d modules=%s native=%s",
					weaponRuleName(kind), weaponEventWeaponId(weaponnum),
					func == FUNC_SECONDARY ? "secondary" : "primary", handnum,
					weaponModulesText(chosen, chose, sizeof(chose)),
					weaponModulesText(native, orig, sizeof(orig)));
			}
		}
		if (weaponEventFirst(weaponnum, func, (s32)(kind - MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED))) {
			sysLogPrintf(LOG_NOTE,
				"WEAPON.RULE: %s weapon=%s func=%s program=yes modules=%s native=%s",
				weaponRuleName(kind), weaponEventWeaponId(weaponnum),
				func == FUNC_SECONDARY ? "secondary" : "primary",
				weaponModulesText(chosen, chose, sizeof(chose)),
				weaponModulesText(native, orig, sizeof(orig)));
		}
	}
	return rule.modules;
}

void weaponEventTrigger(s32 handnum, struct hand *hand, s32 on, s32 previous)
{
	weapon_hand_record_t *record = weaponHandRecord(handnum, hand);

	if (!record || !hand) {
		return;
	}
	if (on && !previous) {
		record->press = 1;
	} else if (!on && previous) {
		record->press = 0;
		if (hand->inuse) {
			weaponRuleDecide(MISSION_GRAPH_EVENT_WEAPON_TRIGGER_RELEASED, handnum, hand, 0);
		}
	}
}

s32 weaponEventTakePress(s32 handnum)
{
	weapon_hand_record_t *record = weaponHandRecord(handnum, NULL);
	s32 press;

	if (!record) {
		return 0;
	}
	press = record->press;
	record->press = 0;
	return press;
}

u32 weaponIdleModules(s32 event_kind, s32 handnum, struct hand *hand, s32 ammo_status, u32 native)
{
	(void)ammo_status;
	if (!hand) {
		return native;
	}
	return weaponRuleDecide(event_kind, handnum, hand, native & WEAPON_MODULE_IDLE_ACTIONS)
		& WEAPON_MODULE_IDLE_ACTIONS;
}

void weaponEventReloadDone(s32 handnum, struct hand *hand)
{
	if (hand && hand->inuse) {
		weaponRuleDecide(MISSION_GRAPH_EVENT_WEAPON_RELOAD_DONE, handnum, hand, 0);
	}
}

void weaponEventsReport(const char *why)
{
	sysLogPrintf(LOG_NOTE,
		"WEAPON.EVENTS: %s trigger_pressed=%d ready_while_held=%d clip_empty=%d"
		" trigger_released=%d reload_done=%d rules.compared=%d rules.differ=%d",
		why ? why : "report",
		s_WeaponEventTotals[0], s_WeaponEventTotals[1], s_WeaponEventTotals[2],
		s_WeaponEventTotals[3], s_WeaponEventTotals[4],
		s_WeaponRuleCompared, s_WeaponRuleDiffer);
	memset(s_WeaponEventTotals, 0, sizeof(s_WeaponEventTotals));
	s_WeaponRuleCompared = 0;
	s_WeaponRuleDiffer = 0;
	s_WeaponEventSeenCount = 0;
}

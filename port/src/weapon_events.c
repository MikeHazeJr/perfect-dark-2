/*
 * weapon_events.c -- held weapon events at the game's seams: see
 * weapon_events.h and context/designs/modding/held-weapon-events.md
 * (slices 2 and 3). The walk itself is weapon_rules.h's (weaponRulesWalk),
 * which pd-tests run too.
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

/* Each player hand: a press waiting for the idle seam, the named states of
 * the function it holds (every state is false when the hand takes a weapon
 * or changes function), and the plan of the attack its idle seam last
 * started for that function (slice 3). */
typedef struct weapon_hand_record {
	u8 press;
	u8 planned;
	s16 weaponnum;
	s8 func;
	weapon_rule_states_t states;
	weapon_rule_trigger_t plan;
	weapon_rule_trigger_t native; /* the original's plan, when none was kept */
} weapon_hand_record_t;
static weapon_hand_record_t s_WeaponHands[MAX_PLAYERS][2];

/* The stage's totals, by event, and the parity counters. */
#define WEAPON_EVENT_TOTALS 5
static s32 s_WeaponEventTotals[WEAPON_EVENT_TOTALS];
static s32 s_WeaponRuleCompared;
static s32 s_WeaponRuleDiffer;

/* The first choice for a weapon, function and event logs a line; later ones
 * only count. Bounded, so an automatic weapon cannot flood the log. The
 * ticks per shot of a simulant's or a guard's weapon use kind
 * WEAPON_RULE_EVENT_COUNT. */
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

/* The current player's record of this hand, its states and plan reset when
 * the hand holds another weapon or function than they belong to. */
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

/* "trigger_pressed" for MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED. */
static const char *weaponRuleName(s32 kind)
{
	const char *name = weaponRulesEventName(kind);

	return name ? name + strlen("event.weapon.") : "?";
}

/* "fire_once+dry_fire" for a set of modules, "-" for none. */
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

/* "automatic rate=750 start_rate=750 ..." for a plan, "-" for none. */
static const char *weaponPlanText(const weapon_rule_trigger_t *plan, char *buf, size_t cap)
{
	if (!plan || !plan->module) {
		snprintf(buf, cap, "-");
	} else if (plan->module == WEAPON_MODULE_AUTOMATIC) {
		snprintf(buf, cap,
			"automatic rate=%.9g start_rate=%.9g spin_up=%d spin_down=%d burst=%d aimed_burst=%s rounds=%d",
			(double)plan->rate, (double)plan->start_rate, plan->spin_up, plan->spin_down, plan->burst,
			plan->aimed_burst ? "true" : "false", plan->rounds);
	} else if (plan->module == WEAPON_MODULE_BURST) {
		snprintf(buf, cap, "burst count=%d gap=%d rounds=%d", plan->count, plan->gap, plan->rounds);
	} else {
		snprintf(buf, cap, "fire_once rounds=%d", plan->rounds);
	}
	return buf;
}

/* One event for a hand: the program walks it (gates, set_state), and the
 * modules it chose are returned; with no program, `native`. A trigger's
 * plan goes to `plan`: the program's, or the original's (`native_plan`)
 * with no program. */
static u32 weaponRuleDecide(s32 kind, s32 handnum, struct hand *hand, u32 native,
	const weapon_rule_function_t *function, const weapon_rule_trigger_t *native_plan,
	weapon_rule_trigger_t *plan)
{
	const s32 weaponnum = hand->gset.weaponnum;
	const s32 func = hand->gset.weaponfunc;
	const mission_graph_program_t *program = weaponGraphRuntimeHeldProgram(weaponnum, func);
	const s32 index = weaponEventTotalIndex(kind);
	const u32 applied = weaponRulesEventModules(kind);
	weapon_hand_record_t *record;
	weapon_rule_walk_t walk;
	s32 handled;

	if (index >= 0) {
		s_WeaponEventTotals[index]++;
	}
	if (plan && native_plan) {
		*plan = *native_plan;
	}
	if (!program) {
		return native; /* a v1 function: the original decides */
	}
	record = weaponHandRecord(handnum, hand);
	memset(&walk, 0, sizeof(walk));
	walk.weaponnum = weaponnum;
	weaponRuleHandOf(hand, &walk.hand);
	walk.states = record ? &record->states : NULL;
	walk.function = function;
	walk.param_text = weaponGraphRuntimeHeldParamText;
	handled = weaponRulesWalk(program, kind, g_Vars.currentplayernum, weaponEventWeaponId(weaponnum), hand, &walk);
	if (plan && (walk.modules & WEAPON_MODULE_TRIGGERS)) {
		*plan = walk.plan;
	}
	if ((applied & WEAPON_MODULE_IDLE_ACTIONS) && (handled || native)) {
		char chose[96];
		char orig[96];
		char chose_plan[192];
		char orig_plan[192];
		const u32 chosen = walk.modules & WEAPON_MODULE_IDLE_ACTIONS;
		/* slice 3: when both start an attack, the attacks are compared too:
		 * the trigger, its settings and the rounds a shot takes. */
		const s32 plans_differ = native_plan && (chosen & WEAPON_MODULE_TRIGGERS)
			&& (native & WEAPON_MODULE_TRIGGERS) && !weaponRulesTriggerEqual(&walk.plan, native_plan);
		/* A mod weapon's number has no original rule: its choices are
		 * logged and not counted. */
		const s32 counted = weaponnum < WEAPON_CUSTOM_START;

		s_WeaponRuleCompared += counted;
		if (counted && (chosen != native || plans_differ)) {
			s_WeaponRuleDiffer++;
			if (s_WeaponRuleDiffer <= 8) {
				sysLogPrintf(LOG_WARNING,
					"WEAPON.RULE differs: %s weapon=%s func=%s hand=%d modules=%s native=%s"
					" plan=%s native_plan=%s",
					weaponRuleName(kind), weaponEventWeaponId(weaponnum),
					func == FUNC_SECONDARY ? "secondary" : "primary", handnum,
					weaponModulesText(chosen, chose, sizeof(chose)),
					weaponModulesText(native, orig, sizeof(orig)),
					weaponPlanText(&walk.plan, chose_plan, sizeof(chose_plan)),
					weaponPlanText((native & WEAPON_MODULE_TRIGGERS) ? native_plan : NULL,
						orig_plan, sizeof(orig_plan)));
			}
		}
		if (weaponEventFirst(weaponnum, func, (s32)(kind - MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED))) {
			sysLogPrintf(LOG_NOTE,
				"WEAPON.RULE: %s weapon=%s func=%s program=yes modules=%s native=%s plan=%s native_plan=%s",
				weaponRuleName(kind), weaponEventWeaponId(weaponnum),
				func == FUNC_SECONDARY ? "secondary" : "primary",
				weaponModulesText(chosen, chose, sizeof(chose)),
				weaponModulesText(native, orig, sizeof(orig)),
				weaponPlanText(&walk.plan, chose_plan, sizeof(chose_plan)),
				weaponPlanText((native & WEAPON_MODULE_TRIGGERS) ? native_plan : NULL,
					orig_plan, sizeof(orig_plan)));
		}
	}
	return walk.modules;
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
			weaponRuleDecide(MISSION_GRAPH_EVENT_WEAPON_TRIGGER_RELEASED, handnum, hand, 0, NULL, NULL, NULL);
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

u32 weaponIdleModules(s32 event_kind, s32 handnum, struct hand *hand, s32 ammo_status, u32 native,
	const struct weapon_rule_function *record)
{
	weapon_rule_trigger_t native_plan;
	weapon_rule_trigger_t plan;
	weapon_hand_record_t *kept;
	u32 chosen;

	(void)ammo_status;
	if (!hand) {
		return native;
	}
	weaponRulesNativeTrigger(record, &native_plan);
	/* The original's attack is its plan's trigger. */
	native &= WEAPON_MODULE_IDLE_ACTIONS;
	if (native & WEAPON_MODULE_TRIGGERS) {
		native = (native & ~WEAPON_MODULE_TRIGGERS) | native_plan.module;
	}
	chosen = weaponRuleDecide(event_kind, handnum, hand, native, record, &native_plan, &plan)
		& WEAPON_MODULE_IDLE_ACTIONS;
	kept = weaponHandRecord(handnum, hand);
	if (kept && (chosen & WEAPON_MODULE_TRIGGERS)) {
		kept->plan = plan;
		kept->planned = 1;
	}
	return chosen;
}

const struct weapon_rule_trigger *weaponHandTrigger(struct hand *hand, const struct weapon_rule_function *record)
{
	static weapon_rule_trigger_t s_Native;
	weapon_hand_record_t *kept = NULL;

	if (hand && g_Vars.currentplayer) {
		if (hand == &g_Vars.currentplayer->hands[HAND_RIGHT]) {
			kept = weaponHandRecord(HAND_RIGHT, hand);
		} else if (hand == &g_Vars.currentplayer->hands[HAND_LEFT]) {
			kept = weaponHandRecord(HAND_LEFT, hand);
		}
	}
	if (kept && kept->planned) {
		return &kept->plan;
	}
	if (kept) {
		weaponRulesNativeTrigger(record, &kept->native);
		return &kept->native;
	}
	weaponRulesNativeTrigger(record, &s_Native);
	return &s_Native;
}

/* A function's record as the cadence code reads it, from its held graph
 * record (a function with a program always has one). */
static void weaponRuleFunctionOfHeld(s32 weaponnum, s32 func, weapon_rule_function_t *out)
{
	const weapon_graph_held_function_t *held = weaponGraphRuntimeGetHeldFunctionForGameplay(weaponnum, func);

	memset(out, 0, sizeof(*out));
	out->weaponnum = weaponnum;
	out->func = func;
	out->ammo_slot = -1;
	if (held) {
		out->functype = held->has_function_type_id ? held->function_type_id : 0;
		out->flags = held->flags;
		out->ammo_slot = held->ammo_slot;
		out->has_rate = held->has_max_rpm;
		out->initial_rpm = held->has_initial_rpm ? held->initial_rpm : 0.0f;
		out->max_rpm = held->has_max_rpm ? held->max_rpm : 0.0f;
		out->accel = held->has_turret_accel ? held->turret_accel : 0;
		out->decel = held->has_turret_decel ? held->turret_decel : 0;
	}
}

/* The press plan of each weapon function, kept while its program stays
 * (no program was registered or dropped since: a new program can take a
 * freed one's address). */
typedef struct weapon_press_cache {
	const mission_graph_program_t *program;
	u32 generation;
	s32 started;
	weapon_rule_trigger_t plan;
} weapon_press_cache_t;
static weapon_press_cache_t s_WeaponPress[WEAPON_CUSTOM_END][2];

s32 weaponProgramPressPlan(s32 weaponnum, s32 func, struct weapon_rule_trigger *out)
{
	const mission_graph_program_t *program;
	weapon_press_cache_t *cache;
	const u32 generation = weaponGraphRuntimeHeldProgramsGeneration();

	if (weaponnum < 0 || weaponnum >= WEAPON_CUSTOM_END || func < 0 || func > 1) {
		return 0;
	}
	program = weaponGraphRuntimeHeldProgram(weaponnum, func);
	if (!program) {
		return 0;
	}
	cache = &s_WeaponPress[weaponnum][func];
	if (cache->program != program || cache->generation != generation) {
		weapon_rule_function_t record;
		weapon_rule_walk_t walk;

		weaponRuleFunctionOfHeld(weaponnum, func, &record);
		memset(&walk, 0, sizeof(walk));
		walk.weaponnum = weaponnum;
		weaponRulesReadyHand(&walk.hand);
		walk.states = NULL;
		walk.function = &record;
		walk.param_text = weaponGraphRuntimeHeldParamText;
		weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, -1, weaponEventWeaponId(weaponnum),
			&walk, &walk);
		cache->program = program;
		cache->generation = generation;
		cache->started = (walk.modules & WEAPON_MODULE_TRIGGERS) != 0;
		cache->plan = walk.plan;
	}
	if (cache->started && out) {
		*out = cache->plan;
	}
	return cache->started;
}

s32 weaponProgramTicksPerShot(s32 weaponnum, s32 func, s32 native)
{
	weapon_rule_trigger_t plan;
	s32 ticks;

	if (!weaponGraphRuntimeHeldProgram(weaponnum, func)) {
		return native;
	}
	ticks = weaponProgramPressPlan(weaponnum, func, &plan) ? weaponRulesTicksPerShot(&plan) : 0;
	if (ticks != native && weaponnum < WEAPON_CUSTOM_START
			&& weaponEventFirst(weaponnum, func, WEAPON_RULE_EVENT_COUNT)) {
		sysLogPrintf(LOG_WARNING, "WEAPON.RULE differs: ticks_per_shot weapon=%s func=%s ticks=%d native=%d",
			weaponEventWeaponId(weaponnum), func == FUNC_SECONDARY ? "secondary" : "primary", ticks, native);
	}
	return ticks;
}

s32 weaponProgramBurstRounds(s32 weaponnum, s32 func, s32 native)
{
	weapon_rule_trigger_t plan;

	if (!weaponGraphRuntimeHeldProgram(weaponnum, func)) {
		return native;
	}
	if (!weaponProgramPressPlan(weaponnum, func, &plan) || plan.module != WEAPON_MODULE_BURST) {
		return 0;
	}
	return plan.count;
}

void weaponEventReloadDone(s32 handnum, struct hand *hand)
{
	if (hand && hand->inuse) {
		weaponRuleDecide(MISSION_GRAPH_EVENT_WEAPON_RELOAD_DONE, handnum, hand, 0, NULL, NULL, NULL);
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

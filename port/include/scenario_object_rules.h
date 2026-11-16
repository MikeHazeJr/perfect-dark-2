#ifndef _IN_SCENARIO_OBJECT_RULES_H
#define _IN_SCENARIO_OBJECT_RULES_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Object rules (context/designs/modding/object-rules.md): optional fields on
 * a row of a scenario's objects.json (pd2.scenario.objects.v1), the stage
 * checks of the original that are about one object. Each field is allowed
 * only on the kind of row whose code reads it.
 *
 *   "coop": false                 hover_car rows: left out when the stage
 *                                 runs in co-op or counter-op
 *   "enemy_rockets_exempt": [...] weapon rows: the difficulties (agent,
 *                                 special_agent, perfect_agent,
 *                                 perfect_dark) at which the Enemy Rockets
 *                                 cheat leaves the weapon alone
 *   "engine_hum", "thrust_hum"    chopper rows: sound effect catalog ids for
 *                                 the two hums (default: the interceptors')
 *   "fall": "crash" | "drop"      chopper rows: "drop" falls with no
 *                                 collision checks and is removed below
 *                                 y -30000 (default "crash": collides and
 *                                 explodes on the ground)
 */

/* At most this many rows of one setup carry a rule. */
#define SCENARIO_OBJECT_RULE_ROWS_MAX 256

typedef struct scenario_object_rules {
	s32 present;
	s32 coop_excluded;
	u32 enemy_rockets_exempt_mask; /* bit (1 << DIFF_*) */
	s16 engine_hum_sound;          /* 0: the default */
	s16 thrust_hum_sound;          /* 0: the default */
	s32 fall_drop;
} scenario_object_rules_t;

/* The rules on setup command `index` of the setup the scenario runtime built
 * last (the stage built from scenario source). Returns 1 and fills *out when
 * that row carries one; returns 0 and clears *out otherwise. */
s32 scenarioSourceSetupRecordRules(s32 index, scenario_object_rules_t *out);

/* Difficulty names in DIFF_* order; NULL past the end. */
const char *scenarioObjectRuleDifficultyName(s32 diff);

#ifdef __cplusplus
}
#endif

#endif

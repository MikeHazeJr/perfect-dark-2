#ifndef _IN_SCENARIO_INTRO_H
#define _IN_SCENARIO_INTRO_H

#include <PR/ultratypes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The stage intro (CORE, 2026-11-15): the member intro.json
 * (pd2.scenario.intro.v1) of a stage's .pdscenario. It carries the commands
 * of the setup file's intro script other than the spawn pads (those stay in
 * spawns.json): what the player starts with and the multiplayer placements.
 * The runtime rebuilds the intro script from spawns.json plus these rows, in
 * the order written, so playerReset, the inventory reset and scenarioReset
 * read the same commands the game read from the ROM setup.
 *
 * rows: [ { "cmd": <name>, ...fields } ], in authored order:
 *   weapon        weapon_catalog_id (a catalog weapon) or weapon_selector
 *                 "none"; optional left_weapon_catalog_id (a second, left-hand
 *                 gun); player (0 = the player; other values are kept and the
 *                 game skips them)
 *   ammo          ammo_type 0..32 (AMMOTYPE_*), quantity 0..1000000, player
 *   outfit        outfit: an outfit name (the stage.json names, OUTFIT_* order)
 *   watch_time    hours 0..23, minutes 0..59
 *   case          team 0..3, pad_ref "pad_NNNN": a team's briefcase (Capture
 *                 the Case home, Hacker Central and Hold the Briefcase banks)
 *   case_respawn  team 0..3, pad_ref: where that team's case can respawn
 *   hill          pad_ref: a King of the Hill hill
 *   legacy_3, legacy_4, legacy_6
 *                 words: the command's 7, 1 or 9 parameters as they are; no
 *                 PD code reads them (GoldenEye leftovers), they are carried
 *                 so nothing in a setup is lost
 * An absent intro.json means no such commands. A row the reader does not know,
 * or a value it cannot use, refuses the file, and the stage does not load
 * (as with an unreadable spawns.json). The setup's credit-offset command
 * points into setup data the scenario does not carry, so the extractor
 * refuses a setup that has one; no base setup does.
 */

#define SCENARIO_INTRO_SCHEMA "pd2.scenario.intro.v1"
#define SCENARIO_INTRO_MAX_ROWS 1024
#define SCENARIO_INTRO_MAX_CASE_TEAMS 4

typedef struct scenario_intro_counts {
	s32 weapons;
	s32 ammo;
	s32 outfits;
	s32 watch_times;
	s32 cases;
	s32 case_respawns;
	s32 hills;
	s32 legacy;
} scenario_intro_counts_t;

typedef struct scenario_intro {
	s32 *words;        /* encoded intro commands, without spawns or the end */
	s32 word_count;
	s32 row_count;
	scenario_intro_counts_t counts;
} scenario_intro_t;

/* Resolves a catalog weapon id to the game's weapon number; returns 1 on
 * success. The runtime passes its catalog resolver; tests pass their own. */
typedef s32 (*scenario_intro_weapon_resolver_t)(const char *catalog_id, s32 *out_weaponnum);

/* Parse intro.json. Returns 1 and fills out (free it with scenarioIntroFree),
 * or 0 with the reason in err and out empty. */
s32 scenarioIntroParse(const char *json, size_t json_size,
	scenario_intro_weapon_resolver_t resolve_weapon,
	scenario_intro_t *out, char *err, size_t err_cap);
void scenarioIntroFree(scenario_intro_t *intro);

/* The number of s32 words of an intro command, 0 for an unknown one. */
s32 scenarioIntroCommandWords(s32 cmd);

#ifdef __cplusplus
}
#endif

#endif

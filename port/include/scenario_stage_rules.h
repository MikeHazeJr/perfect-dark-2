#ifndef _IN_SCENARIO_STAGE_RULES_H
#define _IN_SCENARIO_STAGE_RULES_H

#include <PR/ultratypes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Stage-wide rules (Mike's decision 3, CORE, 2026-11-15): the member
 * stage.json (pd2.scenario.stage.v1) of a stage's .pdscenario, so the rules
 * travel packed in the level's archive. Each family is an optional key; an
 * absent key means the stage has no such rule. A key the reader does not know,
 * or a value it cannot use, refuses the whole file (nothing is half applied).
 *
 * "weather": the stage's weather setup (the game kept it in g_WeatherConfig,
 * src/game/weather.c). Rain or snow and its intensity are started by the
 * stage's AI commands; this sets where and how it falls.
 *   wind_speed, y_min, y_max      numbers; y_min below y_max
 *   z_max                         number; 0 = no limit, otherwise no weather
 *                                 while the camera is below this z
 *   cutscene_only                 true = weather only during cutscenes
 *   weather_rooms | dry_rooms     exactly one: the only rooms with weather, or
 *                                 the rooms without it (room numbers 1..32767,
 *                                 at most SCENARIO_STAGE_MAX_WEATHER_ROOMS)
 *   wind_direction                optional {angle, speed_x, speed_z}: a fixed
 *                                 wind instead of the changing one
 *
 * "player": what the stage dresses the player in (the game kept these as
 * stage checks in src/game/player.c and src/game/bondgun.c).
 *   perfect_agent_outfit          an outfit name worn at Perfect Agent and up
 *   disguise                      {outfit, untargetable}: the outfit worn while
 *                                 disguised, and whether a disguised player
 *                                 cannot be targeted
 *   coop_spawn_distance           how far the co-op buddy starts from the
 *                                 player (1..1000; 60 when absent)
 *
 * "ai": stage-wide guard behaviour (src/game/chrai.c).
 *   dark_room_lists               false = guards never switch to their dark
 *                                 room AI list (true when absent)
 *
 * "ending": how the stage presents its outcome (src/game/endscreen.c,
 * menu.c, menugfx.c).
 *   outcome                       "unknown": the end screen reports the
 *                                 mission "Unknown" and the agent "Missing",
 *                                 turns white instead of the success colour,
 *                                 the menus' particles are gray and slow, and
 *                                 a completion plays the mission-unknown tune
 *                                 (Defense); "known" is the default
 *
 * "hub": true marks a stage the player spends time in between missions (the
 * Carrington Institute): cheats are not applied there, no mission timer is
 * shown, corpses fade as in the original, the player takes no damage outside
 * a training session, a death respawns the player instead of ending the stage,
 * co-op AI buddies do not spawn, and its end screen offers no next mission.
 * The hub's own training content (devices, holograms, firing range) stays with
 * that stage, and the main menu backdrop is a separate matter.
 * Outfit names, in OUTFIT_* order: default, elvis, trent, trench,
 * frock_ripped, frock, leather, deepsea, wetsuit, snow, lab, stewardess,
 * mrblonde, maian, negotiator.
 */

#define SCENARIO_STAGE_RULES_SCHEMA "pd2.scenario.stage.v1"
/* The game's table ends its room list with a zero, so one slot stays free. */
#define SCENARIO_STAGE_MAX_WEATHER_ROOMS 127

typedef struct scenario_stage_weather {
	s32 present;
	f32 wind_speed;
	f32 y_min;
	f32 y_max;
	f32 z_max;
	s32 cutscene_only;
	s32 rooms_have_weather; /* 1: rooms are weather_rooms; 0: dry_rooms */
	s32 room_count;
	s16 rooms[SCENARIO_STAGE_MAX_WEATHER_ROOMS];
	s32 has_wind_direction;
	f32 wind_angle;
	f32 wind_speed_x;
	f32 wind_speed_z;
} scenario_stage_weather_t;

typedef struct scenario_stage_player {
	s32 present;
	s32 perfect_agent_outfit; /* OUTFIT_* value, or -1 for none */
	s32 disguise_outfit;      /* OUTFIT_* value, or -1 for none */
	s32 disguise_untargetable;
	f32 coop_spawn_distance;  /* 0 when absent (the game's 60) */
} scenario_stage_player_t;

typedef struct scenario_stage_ai {
	s32 present;
	s32 no_dark_room_lists;   /* dark_room_lists: false */
} scenario_stage_ai_t;

typedef struct scenario_stage_ending {
	s32 present;
	s32 outcome_unknown;      /* outcome: "unknown" */
} scenario_stage_ending_t;

typedef struct scenario_stage_hub {
	s32 present;
	s32 hub;                  /* hub: true */
} scenario_stage_hub_t;

/* "stars": the stage's starfield (stars.c), present only on a stage that has
 * one: "count" stars (1-8192) on a sky cube of "grid" cells per face side
 * (1-8), and "below_horizon" when they fill the whole sky. */
typedef struct scenario_stage_stars {
	s32 present;
	s32 count;
	s32 grid;
	s32 below_horizon;
} scenario_stage_stars_t;

typedef struct scenario_stage_rules {
	scenario_stage_weather_t weather;
	scenario_stage_player_t player;
	scenario_stage_ai_t ai;
	scenario_stage_ending_t ending;
	scenario_stage_hub_t hub;
	scenario_stage_stars_t stars;
} scenario_stage_rules_t;

/* The outfit name for an OUTFIT_* value, or NULL; and the value for a name,
 * or -1. The order is the game's (a test checks it against constants.h). */
const char *scenarioStageOutfitName(s32 outfit);
s32 scenarioStageOutfitFromName(const char *name);

/* Parse stage.json. Returns 1 and fills out (families it does not name are
 * zeroed), or 0 with the reason in err and out zeroed. */
s32 scenarioStageRulesParse(const char *json, size_t json_size,
	scenario_stage_rules_t *out, char *err, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif

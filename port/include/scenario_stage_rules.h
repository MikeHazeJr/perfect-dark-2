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
 * src/game/weather.c). This sets where and how weather falls.
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
 * "weather_start": optional {kind, intensity} to start weather on load.
 *   kind is "rain" (intensity 1..3) or "snow" (intensity 1). Without a
 *   "weather" object, the native wind and particle bounds apply in all rooms.
 *   Stages without a startup rule still start weather through AI commands.
 *
 * "player": what the stage dresses the player in and leaves them (the game
 * kept these as stage checks in src/game/player.c and src/game/bondgun.c).
 *   perfect_agent_outfit          an outfit name worn at Perfect Agent and up
 *   disguise                      {outfit, untargetable}: the outfit worn while
 *                                 disguised, and whether a disguised player
 *                                 cannot be targeted
 *   coop_spawn_distance           how far the co-op buddy starts from the
 *                                 player (1..1000; 60 when absent)
 *   unlimited_ammo_excludes       weapons (catalog ids, 1 to
 *                                 SCENARIO_STAGE_MAX_AMMO_EXCLUDES) whose
 *                                 primary ammo the unlimited ammo cheats do
 *                                 not refill on the stage
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

enum {
	SCENARIO_STAGE_WEATHER_START_NONE = 0,
	SCENARIO_STAGE_WEATHER_START_RAIN,
	SCENARIO_STAGE_WEATHER_START_SNOW,
};

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
	s32 startup_kind; /* SCENARIO_STAGE_WEATHER_START_* */
	s32 startup_intensity;
} scenario_stage_weather_t;

#define SCENARIO_STAGE_MAX_AMMO_EXCLUDES 8
#define SCENARIO_STAGE_ID_LEN 64 /* CATALOG_ID_LEN */

typedef struct scenario_stage_player {
	s32 present;
	s32 perfect_agent_outfit; /* OUTFIT_* value, or -1 for none */
	s32 disguise_outfit;      /* OUTFIT_* value, or -1 for none */
	s32 disguise_untargetable;
	f32 coop_spawn_distance;  /* 0 when absent (the game's 60) */
	s32 unlimited_ammo_exclude_count;
	char unlimited_ammo_excludes[SCENARIO_STAGE_MAX_AMMO_EXCLUDES][SCENARIO_STAGE_ID_LEN];
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

/* "sparks": {"flash_lights": false} keeps the stage's environmental sparks
 * (sparks.c's SPARKTYPE_ENVIRONMENTAL types) from flashing the room lights
 * (Crash Site); flash_lights is required, true is the default. */
typedef struct scenario_stage_sparks {
	s32 present;
	s32 no_light_flash;
} scenario_stage_sparks_t;

/* "gas": a gas cloud drawn over the screen (nbomb.c gasRender; Escape's gas).
 *   rooms          room numbers where the gas shows (at most
 *                  SCENARIO_STAGE_MAX_GAS_ROOMS)
 *   sphere         {center [x, y, z], radius, door_tag}: outside those rooms,
 *                  within radius of center the gas shows, thinner toward the
 *                  edge and scaled by how far door_tag's door is open
 *                  (door_tag optional: full strength)
 *   door_rooms     [{room, door_tags, add}]: inside a gas room that is also
 *                  room, the gas follows the most open of door_tags plus add
 *   cutscene_fade  {start_frame, full_frame}: in a cutscene the gas is hidden
 *                  before start_frame and fades in until full_frame
 * At least one of rooms and sphere. */
#define SCENARIO_STAGE_MAX_GAS_ROOMS 32
#define SCENARIO_STAGE_MAX_GAS_DOOR_ROOMS 4
#define SCENARIO_STAGE_MAX_GAS_DOOR_TAGS 4

typedef struct scenario_stage_gas_door_room {
	s16 room;
	s32 tag_count;
	s32 tags[SCENARIO_STAGE_MAX_GAS_DOOR_TAGS];
	f32 add;
} scenario_stage_gas_door_room_t;

typedef struct scenario_stage_gas {
	s32 present;
	s32 room_count;
	s16 rooms[SCENARIO_STAGE_MAX_GAS_ROOMS];
	s32 has_sphere;
	f32 sphere_center[3];
	f32 sphere_radius;
	s32 sphere_door_tag;      /* -1: none */
	s32 door_room_count;
	scenario_stage_gas_door_room_t door_rooms[SCENARIO_STAGE_MAX_GAS_DOOR_ROOMS];
	s32 has_cutscene_fade;
	s32 fade_start;
	s32 fade_full;
} scenario_stage_gas_t;

/* Optional stage atmosphere. Fog uses thousandths of view distance. A
 * room's explicit base_brightness takes priority over ambient_light. */
typedef struct scenario_stage_atmosphere {
	s32 present;
	s32 has_sky;
	u8 sky_rgb[3];
	s32 has_fog;
	s32 fog_start;
	s32 fog_end;
	s32 has_ambient;
	s32 ambient_light;
	char primary_music[SCENARIO_STAGE_ID_LEN];
} scenario_stage_atmosphere_t;

typedef struct scenario_stage_rules {
	scenario_stage_weather_t weather;
	scenario_stage_player_t player;
	scenario_stage_ai_t ai;
	scenario_stage_ending_t ending;
	scenario_stage_hub_t hub;
	scenario_stage_stars_t stars;
	scenario_stage_sparks_t sparks;
	scenario_stage_gas_t gas;
	scenario_stage_atmosphere_t atmosphere;
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

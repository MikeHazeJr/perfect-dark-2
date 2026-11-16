#include <ultra64.h>
#include <string.h>
#include "constants.h"
#include "game/weather.h"
#include "bss.h"
#include "lib/memp.h"
#include "data.h"
#include "types.h"
#include "game/bg.h"
#include "scenario_source_runtime.h"
#include "scenario_stage_rules.h"

/* A stage built from scenario source takes its weather setup from its
 * stage.json (decision 3); this is that stage's copy of the table row. */
static struct weathercfg s_ScenarioWeatherConfig;

static void weatherConfigFromStageRules(const scenario_stage_weather_t *rule,
	struct weathercfg *cfg)
{
	s32 i;

	memset(cfg, 0, sizeof(*cfg));
	cfg->stagenum = g_Stages[g_StageIndex].id;
	cfg->flags = (rule->rooms_have_weather ? WEATHERFLAG_INCLUDE : 0) |
		(rule->cutscene_only ? WEATHERFLAG_CUTSCENE_ONLY : 0) |
		(rule->has_wind_direction ? WEATHERFLAG_FORCE_WINDDIR : 0);
	cfg->windspeed = rule->wind_speed;
	cfg->ymin = rule->y_min;
	cfg->ymax = rule->y_max;
	cfg->zmax = rule->z_max;
	for (i = 0; i < rule->room_count && i < WEATHERCFG_MAX_SKIPROOMS - 1; i++) {
		cfg->skiprooms[i] = rule->rooms[i];
	}
	cfg->windanglerad = rule->wind_angle;
	cfg->windspeedx = rule->wind_speed_x;
	cfg->windspeedz = rule->wind_speed_z;
}

s32 g_WeatherActive = false;
u32 var800623f4 = 0x00000000;
u32 var800623f8 = 0x00000000;
u32 var800623fc = 0x00000000;


static void weatherResetRooms(void)
{
	if (g_CurWeatherConfig->flags & WEATHERFLAG_INCLUDE) {
		// weather is present only in skiprooms, so mark every room as weatherproof
		for (s32 i = 0; i < g_Vars.roomcount; ++i) {
			g_Rooms[i].extra_flags |= ROOMFLAG_EX_WEATHERPROOF;
		}
		// then unmark skiprooms
		for (s32 i = 0; i < WEATHERCFG_MAX_SKIPROOMS && g_CurWeatherConfig->skiprooms[i]; ++i) {
			const RoomNum room = g_CurWeatherConfig->skiprooms[i];
			if (room >= 0 && room < g_Vars.roomcount) {
				g_Rooms[room].extra_flags &= ~ROOMFLAG_EX_WEATHERPROOF;
			}
		}
	} else {
		// weather is present in all rooms except skiprooms
		for (s32 i = 0; i < WEATHERCFG_MAX_SKIPROOMS && g_CurWeatherConfig->skiprooms[i]; ++i) {
			const RoomNum room = g_CurWeatherConfig->skiprooms[i];
			if (room >= 0 && room < g_Vars.roomcount) {
				g_Rooms[room].extra_flags |= ROOMFLAG_EX_WEATHERPROOF;
			}
		}
	}
}


void weatherReset(void)
{
	g_WeatherActive = false;
	g_WeatherData = NULL;

	// find the weather config for this stage, if any
	g_CurWeatherConfig = &g_DefaultWeatherConfig;
	if (bgUsesScenarioSource()) {
		// a stage built from scenario source: the weather of its stage.json
		scenario_stage_weather_t rule;

		if (scenarioSourceStageWeather(&rule)) {
			weatherConfigFromStageRules(&rule, &s_ScenarioWeatherConfig);
			g_CurWeatherConfig = &s_ScenarioWeatherConfig;
		}
	} else {
		for (s32 i = 0; i < ARRAYCOUNT(g_WeatherConfig) && g_WeatherConfig[i].stagenum; ++i) {
			if (g_WeatherConfig[i].stagenum == g_Stages[g_StageIndex].id) {
				g_CurWeatherConfig = &g_WeatherConfig[i];
				break;
			}
		}
	}

	if (g_CurWeatherConfig != &g_DefaultWeatherConfig)
	{
		g_WeatherData = mempAlloc(sizeof(struct weatherdata), MEMPOOL_STAGE);
		g_WeatherData->particledata[0] = weatherAllocateParticles();
		g_WeatherData->type = -1;
		g_WeatherData->windanglerad = 0;
		g_WeatherData->unk0c = 0;
		g_WeatherData->unk10 = 1;

		g_WeatherData->windspeed = g_CurWeatherConfig->windspeed;
		// set up the weatherproof flags in all rooms
		if (g_Vars.stagenum != STAGE_TITLE) {
			weatherResetRooms();
		}

		g_WeatherData->audiohandles[0] = 0;
		g_WeatherData->audiohandles[1] = 0;
		g_WeatherData->audiohandles[2] = 0;
		g_WeatherData->audiohandles[3] = 0;
		g_WeatherData->unk44 = 0;
		g_WeatherData->unk94 = -1;
		g_WeatherData->unk48 = 1;
		g_WeatherData->unk4c = 0;
		g_WeatherData->unk50 = 0;
		g_WeatherData->unk54 = 0;
		g_WeatherData->unk58[0].unk00 = 0;
		g_WeatherData->unk58[1].unk00 = 0;
		g_WeatherData->unk58[2].unk00 = 0;
		g_WeatherData->unk58[3].unk00 = 1;
		g_WeatherData->unk58[0].unk04 = 1;
		g_WeatherData->unk58[0].unk08 = 10;

		if (g_WeatherData->unk58[0].unk08 < 0) {
			g_WeatherData->unk58[0].unk08 = -g_WeatherData->unk58[0].unk08;
		}

		g_WeatherData->unk58[0].unk08 = 0;
		g_WeatherData->unk58[1].unk08 = 0;
		g_WeatherData->unk58[2].unk08 = 0;
		g_WeatherData->unk58[3].unk08 = 0;
		g_WeatherData->unkb8 = 150;
		g_WeatherData->unkc0 = 0;
		g_WeatherData->unkc4 = 0;
		g_WeatherData->unkc8 = 15;
		g_WeatherData->unk88 = 1;
		g_WeatherData->unk90 = 0;
		g_WeatherData->intensity = 0;
		g_WeatherData->unkd0 = 0;
		g_WeatherData->unkd4 = 0;

		g_WeatherActive = true;
	}
}

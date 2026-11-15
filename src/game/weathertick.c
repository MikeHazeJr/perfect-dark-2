#include <ultra64.h>
#include "constants.h"
#include "game/weather.h"
#include "bss.h"
#include "lib/main.h"
#include "data.h"
#include "types.h"
#include "game/bg.h"

u32 var80062950 = 100;
bool g_WeatherTickEnabled = true;

void weatherTick(void)
{
	if (!g_WeatherData) {
		return;
	}

	// A stage built from scenario source takes both limits from its stage.json
	// weather (decision 3): z_max and cutscene_only hold the values the game
	// checked by stage for Air Base and G5. The stage checks stay for a stage
	// built from the ROM background.
	if (bgUsesScenarioSource()) {
		if ((g_CurWeatherConfig->zmax && g_Vars.currentplayer->cam_pos.z < g_CurWeatherConfig->zmax)
				|| ((g_CurWeatherConfig->flags & WEATHERFLAG_CUTSCENE_ONLY) && g_Vars.tickmode != TICKMODE_CUTSCENE)) {
			return;
		}
	} else if ((g_StageIndex == STAGEINDEX_AIRBASE && g_Vars.currentplayer->cam_pos.z < -2000.0f)
			|| (g_StageIndex == STAGEINDEX_G5BUILDING && g_Vars.tickmode != TICKMODE_CUTSCENE)) {
		return;
	}

	mainOverrideVariable("wettick", (u32 *)&g_WeatherTickEnabled);
	mainOverrideVariable("windspeed", &var80062950);

	if (g_WeatherTickEnabled) {
		switch (g_WeatherData->type) {
		case WEATHERTYPE_RAIN:
			weatherTickRain(g_WeatherData);
			break;
		case WEATHERTYPE_SNOW:
			weatherTickSnow(g_WeatherData);
			break;
		}
	}
}

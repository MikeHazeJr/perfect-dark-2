#include <ultra64.h>
#include "constants.h"
#include "game/chrai.h"
#include "game/bot.h"
#include "bss.h"
#include "data.h"
#include "types.h"
#include "scenario_source_runtime.h"

/**
 * @cmd 0185
 */
bool aiMpInitSimulants(void)
{
	/* On a stage built from scenario source the graph twin spawns the wave
	 * (1: complete, 2: not yet). */
	s32 graph_result = scenarioSourceAiGraphExecuteMpInitSimulants();

	if (graph_result == 2
			|| (graph_result == 0 && botSpawnAll() != BOT_SPAWN_WAVE_COMPLETE)) {
		/* Leave the opcode in place and end this entity's tick: the next AI
		 * pass retries the retained wave cursor (the failed bot and its
		 * uncommitted suffix). B-1255: this returned false, and the B-949
		 * guard in chraiExecute skips a command that returns false without
		 * moving, so the retry never ran. */
		return true;
	}

	g_Vars.aioffset += 2;
	return false;
}

/**
 * @cmd 0176
 */
bool aiIfBotRespawning(void)
{
	u8 *cmd = g_Vars.ailist + g_Vars.aioffset;
	struct aibot *aibot;

	if (scenarioSourceAiGraphExecuteIfBotRespawning(g_Vars.chrdata, cmd[2])) {
		return false;
	}

	/* B-1254: the original went on without a return here and read aibot
	 * through a null pointer. No chr, or one that is not a simulant, is
	 * false. */
	if (!g_Vars.chrdata || !g_Vars.chrdata->aibot) {
		g_Vars.aioffset += 3;
		return false;
	}

	aibot = g_Vars.chrdata->aibot;

	if (aibot->respawning == true) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist, g_Vars.aioffset, cmd[2]);
		aibot->respawning = false;
	} else {
		g_Vars.aioffset += 3;
	}

	return false;
}

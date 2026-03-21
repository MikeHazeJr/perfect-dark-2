#include <ultra64.h>
#include "constants.h"
#include "game/challenge.h"
#include "game/mplayer/mplayer.h"
#include "bss.h"
#include "data.h"
#include "types.h"
#include "system.h"

void challengesInit(void)
{
	struct mpconfigfull *mpconfig;
	u8 buffer[0x1ca];
	s32 i;

	sysLogPrintf(LOG_NOTE, "CHALLENGES: starting init, %d challenges", (s32)ARRAYCOUNT(g_MpChallenges));

	for (i = 0; i < ARRAYCOUNT(g_MpChallenges); i++) {
		g_MpChallenges[i].availability = 0;
		g_MpChallenges[i].completions[0] = 0;
		g_MpChallenges[i].completions[1] = 0;
		g_MpChallenges[i].completions[2] = 0;
		g_MpChallenges[i].completions[3] = 0;

		sysLogPrintf(LOG_NOTE, "CHALLENGES: [%d] loading confignum=%d", i, g_MpChallenges[i].confignum);
		mpconfig = challengeLoad(i, buffer, 0x1ca);
		sysLogPrintf(LOG_NOTE, "CHALLENGES: [%d] loaded, mpconfig=%p", i, (void *)mpconfig);
		challengeForceUnlockConfigFeatures(&mpconfig->config, g_MpChallenges[i].unlockfeatures, 16, i);
		sysLogPrintf(LOG_NOTE, "CHALLENGES: [%d] unlockFeatures done", i);
	}

	sysLogPrintf(LOG_NOTE, "CHALLENGES: challenges loop done, starting presets");

	for (i = 0; i < mpGetNumPresets(); i++) {
		sysLogPrintf(LOG_NOTE, "CHALLENGES: preset[%d] confignum=%d", i, g_MpPresets[i].confignum);
		mpconfig = challengeLoadConfig(g_MpPresets[i].confignum, buffer, 0x1ca);
		sysLogPrintf(LOG_NOTE, "CHALLENGES: preset[%d] loaded", i);
		challengeForceUnlockConfigFeatures(&mpconfig->config, g_MpPresets[i].requirefeatures, 16, -1);
	}

	sysLogPrintf(LOG_NOTE, "CHALLENGES: presets done, calling determinUnlockedFeatures");
	challengeDetermineUnlockedFeatures();
	sysLogPrintf(LOG_NOTE, "CHALLENGES: init complete");
}

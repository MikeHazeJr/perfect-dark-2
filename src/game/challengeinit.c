#include <ultra64.h>
#include "constants.h"
#include "game/challenge.h"
#include "game/mplayer/mplayer.h"
#include "bss.h"
#include "data.h"
#include "system.h"
#include "types.h"


void challengesInit(void)
{
	const u64 start = sysGetMicroseconds();
	struct challenge_bot_unlock_cache cache = {0};
	struct mpconfigfull *mpconfig;
	/* Buffer must hold a full struct mpconfigfull + 16 bytes for DMA alignment.
	 * Original code used 0x1ca (458 bytes) which was sized for MAX_BOTS=8.
	 * With MAX_BOTS=24 the structs are ~888 bytes — the old buffer overflowed. */
	u8 buffer[sizeof(struct mpconfigfull) + 16];
	s32 i;

	for (i = 0; i < ARRAYCOUNT(g_MpChallenges); i++) {
		g_MpChallenges[i].availability = 0;
		g_MpChallenges[i].completions[0] = 0;
		g_MpChallenges[i].completions[1] = 0;
		g_MpChallenges[i].completions[2] = 0;
		g_MpChallenges[i].completions[3] = 0;

		mpconfig = challengeLoad(i, buffer, sizeof(buffer));
		challengeForceUnlockConfigFeatures(&mpconfig->config,
			g_MpChallenges[i].unlockfeatures, 16, i, &cache);
	}

	for (i = 0; i < mpGetNumPresets(); i++) {
		mpconfig = challengeLoadConfig(g_MpPresets[i].confignum, buffer, sizeof(buffer));
		challengeForceUnlockConfigFeatures(&mpconfig->config,
			g_MpPresets[i].requirefeatures, 16, -1, &cache);
	}

	challengeDetermineUnlockedFeatures();
	sysLogPrintf(LOG_VERBOSE, "INIT: challengesInit complete elapsed_ms=%llu profile_searches=%d",
		(unsigned long long)((sysGetMicroseconds() - start) / 1000), cache.profile_searches);
}

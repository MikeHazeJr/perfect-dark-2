#include <ultra64.h>
#include "constants.h"
#include "system.h"
#include "memsizes.h"
#include "game/game_00b820.h"
#include "game/title.h"
#include "bss.h"
#include "lib/memp.h"
#include "data.h"
#include "types.h"
#include "video.h"

void chrmgrReset(void)
{
	s32 i;

	var80062968 = 1;
	var8006296c = 0;
	g_SelectedAnimNum = 0;
	var80062974 = 0;
	var80062978 = 0;
	var8006297c = 0;
	g_NextChrnum = 5000;
	g_ChrSlots = NULL;
	g_NumChrSlots = 0;

	g_ShieldHits = mempAlloc(sizeof(struct shieldhit) * 20, MEMPOOL_STAGE);

	for (i = 0; i < 20; i++) {
		g_ShieldHits[i].prop = NULL;
	}

	g_ShieldHitActive = 0;
	g_NumChrs = 0;
	g_Chrnums = NULL;
	g_ChrIndexes = NULL;
	var80062960 = mempAlloc(ALIGN16(CHR_MANAGER_SLOTS * sizeof(struct var80062960)), MEMPOOL_STAGE);

	for (i = 0; i < ARRAYCOUNT(var8009ccc0); i++) {
		if (!var8009ccc0[i]) {
			var8009ccc0[i] = videoCreateFramebuffer(16, 16, false, false);
		}
	}

	resetSomeStageThings();
}

/* B-952: dynamic-spawn headroom above the stage's declared (setup) chr count.
 * The original "+10" was an N64-era constant -- "N64 only had 8 total characters
 * so the +10 buffer in chrmgrConfigure was always enough" (setup.c). On modern
 * hardware, stages like base:skedarruins spawn DOZENS of reinforcement chrs at
 * RUNTIME (chrnum >= g_NextChrnum = 5000) that are NOT in the setup count:
 * skedarruins declares numchrs=53 yet spawns 52+ reinforcements. With only +10
 * headroom, the 11th runtime spawn onward fails chrInit's slot scan and hits
 * "out of chr slots" -- the spawn is silently dropped, so most reinforcements
 * never appear and the mission plays wrong. This grows the small chrdata slot
 * array generously for modern HW (256 covers any realistic wave; ~370 KB of the
 * 40 MB stage pool). NOTE: this is a CORRECTNESS fix for missing reinforcements,
 * NOT the B-952 crash fix -- the crash chr (slot 51) is in-bounds even at +10.
 * The crash itself is a stale-recycled-slot bug fixed in chrInit (chr.c). */
#define CHR_DYNAMIC_SPAWN_HEADROOM 256

void chrmgrConfigure(s32 numchrs)
{
	s32 i;

	g_NumChrSlots = PLAYERCOUNT() + numchrs + CHR_DYNAMIC_SPAWN_HEADROOM;

	sysLogPrintf(LOG_NOTE, "CHRSLOTS: chrmgrConfigure numchrs=%d PLAYERCOUNT=%d => g_NumChrSlots=%d (sizeof chrdata=%d, total=%d bytes)",
		numchrs, PLAYERCOUNT(), g_NumChrSlots, (s32)sizeof(struct chrdata),
		(s32)(g_NumChrSlots * sizeof(struct chrdata)));

	g_ChrSlots = mempAlloc(ALIGN16(g_NumChrSlots * sizeof(struct chrdata)), MEMPOOL_STAGE);

	for (i = 0; i < g_NumChrSlots; i++) {
		g_ChrSlots[i].chrnum = -1;
		g_ChrSlots[i].model = NULL;
		g_ChrSlots[i].prop = NULL;
	}

	g_NumChrs = 0;
	g_Chrnums = mempAlloc(ALIGN16(g_NumChrSlots * sizeof(g_Chrnums[0])), MEMPOOL_STAGE);
	g_ChrIndexes = mempAlloc(ALIGN16(g_NumChrSlots * sizeof(g_ChrIndexes[0])), MEMPOOL_STAGE);

	for (i = 0; i < g_NumChrSlots; i++) {
		g_Chrnums[i] = -1;
		g_ChrIndexes[i] = -1;
	}
}

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

/* Chr-slot headroom above the stage's declared (setup) chr count, for runtime
 * spawns (reinforcements, chrnum >= g_NextChrnum).
 *
 * Restored to the vanilla +10 on 2026-07-05: my earlier +256 "correctness fix"
 * (B-952) was WRONG. I assumed runtime spawns beyond +10 fail, but instrumenting
 * base:skedarruins shows only ~52 chrs alive SIMULTANEOUSLY (mostly the 53 setup
 * chrs -- the idle auto-campaign player kills nothing, 0 death events), which fits
 * the +10 pool (PLAYERCOUNT + numchrs + 10 = 64). Vanilla never overflowed;
 * "out of chr slots" was never logged. The pool ALREADY scales with the mod's
 * declared numchrs, so the magic +256 bought nothing and masked the real issue.
 *
 * TODO (Mike directive "limits should be dynamic, not hard-coded"): replace this
 * fixed headroom with genuinely dynamic growth so large mods grow as needed.
 * Constraint: chrs are pointer-referenced (prop->chr) at 97+ g_ChrSlots[] index
 * sites, so the backing store cannot realloc-move without dangling those pointers
 * -- needs a stable growable allocator (block list), a scoped refactor. */
#define CHR_DYNAMIC_SPAWN_HEADROOM 10

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

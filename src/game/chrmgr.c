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
#include "memarena.h"

/* Chr pool backing (task #38, 2026-07-06). The three parallel chr arrays are
 * reserve-and-commit arenas so the pool GROWS at runtime (Mike's dynamic-limits
 * directive) while staying pointer-stable AND contiguous -- both are hard
 * requirements: ~100 g_ChrSlots[i] index sites, and `chr - g_ChrSlots` pointer
 * arithmetic that feeds the network protocol (serialized chrindex), alert
 * fan-out, and chr tracking. A realloc-style array would dangle every live
 * prop->chr; a linked block-list would break the pointer arithmetic. The arena
 * reserves a large VIRTUAL range up front (zero RAM until committed) and commits
 * pages on demand, so the base never moves. The reservation is an address-space
 * budget, not a content cap: sized ~1250x vanilla, it grows freely within it and
 * fails LOUD (not silent truncation) if ever exhausted -- a one-line bump. */
#define CHR_SLOTS_RESERVE_MAX 65536  /* virtual reservation ceiling; bump freely */
#define CHR_SLOTS_GROW_CHUNK  64     /* grow granularity on runtime overflow     */

static struct memarena s_ChrSlotsArena;
static struct memarena s_ChrnumsArena;
static struct memarena s_ChrIndexesArena;

/* Reserve the three parallel chr arenas once (idempotent across stages). */
static bool chrmgrReserveArenas(void)
{
	if (s_ChrSlotsArena.base != NULL) {
		return true;
	}
	return arenaReserve(&s_ChrSlotsArena, "chrslots", sizeof(struct chrdata), CHR_SLOTS_RESERVE_MAX)
		&& arenaReserve(&s_ChrnumsArena, "chrnums", sizeof(g_Chrnums[0]), CHR_SLOTS_RESERVE_MAX)
		&& arenaReserve(&s_ChrIndexesArena, "chrindexes", sizeof(g_ChrIndexes[0]), CHR_SLOTS_RESERVE_MAX);
}

/* Commit all three arenas to at least `count` slots and point the globals at
 * their (stable) bases. Returns false if the reservation is exhausted. */
static bool chrmgrCommitSlots(s32 count)
{
	struct chrdata *slots = arenaEnsure(&s_ChrSlotsArena, count);
	s16 *chrnums = arenaEnsure(&s_ChrnumsArena, count);
	s16 *indexes = arenaEnsure(&s_ChrIndexesArena, count);

	if (slots == NULL || chrnums == NULL || indexes == NULL) {
		return false;
	}

	g_ChrSlots = slots;
	g_Chrnums = chrnums;
	g_ChrIndexes = indexes;
	return true;
}

/* Grow the chr pool by one chunk when chrInit finds no free slot (runtime
 * reinforcements past the load-time size). Commits more arena pages -- the bases
 * DO NOT move, so every live chr pointer and g_ChrSlots[i] index stays valid --
 * inits the new slots to "free", and returns the index of the first new slot, or
 * -1 if the reservation is exhausted. */
s32 chrmgrGrowSlots(void)
{
	s32 oldcount = g_NumChrSlots;
	s32 newcount = oldcount + CHR_SLOTS_GROW_CHUNK;
	s32 i;

	if (!chrmgrCommitSlots(newcount)) {
		sysLogPrintf(LOG_ERROR,
			"chrmgrGrowSlots: cannot grow chr pool past %d (reservation of %d exhausted -- raise CHR_SLOTS_RESERVE_MAX)",
			oldcount, CHR_SLOTS_RESERVE_MAX);
		return -1;
	}

	for (i = oldcount; i < newcount; i++) {
		g_ChrSlots[i].chrnum = -1;
		g_ChrSlots[i].model = NULL;
		g_ChrSlots[i].prop = NULL;
		g_Chrnums[i] = -1;
		g_ChrIndexes[i] = -1;
	}

	g_NumChrSlots = newcount;
	sysLogPrintf(LOG_NOTE, "CHRSLOTS: grew chr pool %d -> %d (runtime reinforcement overflow)", oldcount, newcount);
	return oldcount;
}

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

	/* Decommit the chr pool arenas but keep their virtual reservation for reuse;
	 * chrmgrConfigure re-commits them to the new stage's size. Safe no-op before
	 * the first reserve. */
	arenaReset(&s_ChrSlotsArena);
	arenaReset(&s_ChrnumsArena);
	arenaReset(&s_ChrIndexesArena);

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
 * DONE (2026-07-06, task #38): the chr pool is now arena-backed (memarena,
 * pointer-stable reserve-and-commit) and GROWS via chrmgrGrowSlots() when a
 * runtime spawn finds no free slot. This +10 is now only an INITIAL buffer (it
 * avoids growing on the first reinforcement); it is no longer a hard limit --
 * overflow grows the pool instead of failing. The stable-growable-allocator the
 * old TODO called for is the arena above (contiguous, so `chr - g_ChrSlots` and
 * the ~100 g_ChrSlots[i] sites keep working, unlike a block-list). */
#define CHR_DYNAMIC_SPAWN_HEADROOM 10

void chrmgrConfigure(s32 numchrs)
{
	s32 i;

	g_NumChrSlots = PLAYERCOUNT() + numchrs + CHR_DYNAMIC_SPAWN_HEADROOM;

	sysLogPrintf(LOG_NOTE, "CHRSLOTS: chrmgrConfigure numchrs=%d PLAYERCOUNT=%d => g_NumChrSlots=%d (sizeof chrdata=%d, total=%d bytes)",
		numchrs, PLAYERCOUNT(), g_NumChrSlots, (s32)sizeof(struct chrdata),
		(s32)(g_NumChrSlots * sizeof(struct chrdata)));

	/* Arena-backed (task #38): reserve the virtual range once, then commit to the
	 * load-time size. Growth past this happens in chrmgrGrowSlots() on overflow. */
	if (!chrmgrReserveArenas() || !chrmgrCommitSlots(g_NumChrSlots)) {
		sysLogPrintf(LOG_ERROR,
			"chrmgrConfigure: chr pool arena reserve/commit FAILED for %d slots", g_NumChrSlots);
		g_ChrSlots = NULL;
		g_Chrnums = NULL;
		g_ChrIndexes = NULL;
		g_NumChrSlots = 0;
		g_NumChrs = 0;
		return;
	}

	for (i = 0; i < g_NumChrSlots; i++) {
		g_ChrSlots[i].chrnum = -1;
		g_ChrSlots[i].model = NULL;
		g_ChrSlots[i].prop = NULL;
		g_Chrnums[i] = -1;
		g_ChrIndexes[i] = -1;
	}

	g_NumChrs = 0;
}

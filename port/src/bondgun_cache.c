/*
 * port/src/bondgun_cache.c -- B-246 round-10 Phase A.
 *
 * Pure predicate for the gun-matrix cache staleness check. See
 * port/include/bondgun_cache.h for the rationale and decision rule.
 *
 * Phase A (this commit): the predicate exists and is locked by tests.
 * It is NOT yet called from bgun0f0a5550 -- runtime behaviour is
 * unchanged. Mike will approve the Phase B wiring before activation.
 */

#include <PR/ultratypes.h>
#include <stdbool.h>
#include "bondgun_cache.h"

/* Half-frame tolerance for "same anim, same frame" comparison. At
 * 60 Hz this is 8.3 ms of slack, well below any visible animation
 * progression on a modern frame budget. */
#define BGUN_CACHE_FRAME_EPSILON 0.5f

bool bgunMatrixCacheIsStale(s32 cache_animnum, f32 cache_animframe,
                            s32 cur_animnum,   f32 cur_animframe)
{
	f32 delta;

	if (cache_animnum != cur_animnum) {
		return true;
	}

	delta = cache_animframe - cur_animframe;
	if (delta < 0.0f) {
		delta = -delta;
	}

	return delta >= BGUN_CACHE_FRAME_EPSILON;
}

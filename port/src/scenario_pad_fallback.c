/*
 * scenario_pad_fallback.c -- B-1125: spaced extra placement pads for
 * multiplayer scenarios. See port/include/scenario_pad_fallback.h.
 */

#include <stddef.h>

#include "scenario_pad_fallback.h"

static const f32 s_Spacings[SCENARIO_PAD_FALLBACK_NUM_SPACINGS] = {
	SCENARIO_PAD_FALLBACK_WIDEST_SPACING, 500.0f, 250.0f, 125.0f, 0.0f,
};

static s32 scenarioPadIsChosen(const struct scenariopadpick *chosen, s32 count, s16 padnum)
{
	s32 i;

	for (i = 0; i < count; i++) {
		if (chosen[i].padnum == padnum) {
			return 1;
		}
	}

	return 0;
}

static s32 scenarioPadIsSpaced(const struct scenariopadpick *chosen, s32 count,
		const struct scenariopadpick *cand, f32 mindistsq)
{
	s32 i;

	if (mindistsq <= 0.0f) {
		return 1;
	}

	for (i = 0; i < count; i++) {
		f32 dx = cand->x - chosen[i].x;
		f32 dz = cand->z - chosen[i].z;

		if (dx * dx + dz * dz < mindistsq) {
			return 0;
		}
	}

	return 1;
}

s32 scenarioPadPickSpaced(struct scenariopadpick *chosen, s32 count,
		s32 capacity, s32 target,
		const struct scenariopadpick *cands, s32 numcands,
		scenariopadrng rng)
{
	s16 order[SCENARIO_PAD_FALLBACK_MAX_CANDIDATES];
	s32 tier;
	s32 i;

	if (chosen == NULL || count < 0) {
		return count < 0 ? 0 : count;
	}

	if (target > capacity) {
		target = capacity;
	}

	if (count >= target || cands == NULL || numcands <= 0 || rng == NULL) {
		return count;
	}

	if (numcands > SCENARIO_PAD_FALLBACK_MAX_CANDIDATES) {
		numcands = SCENARIO_PAD_FALLBACK_MAX_CANDIDATES;
	}

	// Shuffle once (Fisher-Yates); every tier walks the same order
	for (i = 0; i < numcands; i++) {
		order[i] = (s16)i;
	}

	for (i = numcands - 1; i > 0; i--) {
		s32 j = (s32)(rng() % (u32)(i + 1));
		s16 tmp = order[i];
		order[i] = order[j];
		order[j] = tmp;
	}

	for (tier = 0; tier < SCENARIO_PAD_FALLBACK_NUM_SPACINGS && count < target; tier++) {
		f32 mindistsq = s_Spacings[tier] * s_Spacings[tier];

		for (i = 0; i < numcands && count < target; i++) {
			const struct scenariopadpick *cand = &cands[order[i]];

			if (cand->padnum <= 0
					|| scenarioPadIsChosen(chosen, count, cand->padnum)
					|| !scenarioPadIsSpaced(chosen, count, cand, mindistsq)) {
				continue;
			}

			chosen[count] = *cand;
			count++;
		}
	}

	return count;
}

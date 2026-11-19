/*
 * scenario_pad_fallback.c -- B-1125, B-1126: extra placement pads for
 * multiplayer scenarios. See port/include/scenario_pad_fallback.h.
 */

#include <stddef.h>
#include <string.h>

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

s32 scenarioPadPickFarthest(struct scenariopadpick *chosen, s32 count,
		s32 capacity, s32 target,
		const struct scenariopadpick *cands, s32 numcands,
		scenariopadrng rng)
{
	s32 i;

	if (chosen == NULL || count < 0) {
		return count < 0 ? 0 : count;
	}

	if (target > capacity) {
		target = capacity;
	}

	if (count >= target || cands == NULL || numcands <= 0) {
		return count;
	}

	if (numcands > SCENARIO_PAD_FALLBACK_MAX_CANDIDATES) {
		numcands = SCENARIO_PAD_FALLBACK_MAX_CANDIDATES;
	}

	// With nothing chosen yet, start from a random usable candidate
	if (count == 0) {
		s32 numusable = 0;
		s32 pick;

		for (i = 0; i < numcands; i++) {
			if (cands[i].padnum > 0) {
				numusable++;
			}
		}

		if (numusable == 0 || rng == NULL) {
			return 0;
		}

		pick = (s32)(rng() % (u32)numusable);

		for (i = 0; i < numcands; i++) {
			if (cands[i].padnum <= 0) {
				continue;
			}

			if (pick == 0) {
				chosen[0] = cands[i];
				count = 1;
				break;
			}

			pick--;
		}
	}

	// Then take the candidate farthest from everything chosen so far
	while (count < target) {
		s32 best = -1;
		f32 bestdistsq = -1.0f;

		for (i = 0; i < numcands; i++) {
			const struct scenariopadpick *cand = &cands[i];
			f32 nearestsq = -1.0f;
			s32 j;

			if (cand->padnum <= 0 || scenarioPadIsChosen(chosen, count, cand->padnum)) {
				continue;
			}

			for (j = 0; j < count; j++) {
				f32 dx = cand->x - chosen[j].x;
				f32 dz = cand->z - chosen[j].z;
				f32 distsq = dx * dx + dz * dz;

				if (nearestsq < 0.0f || distsq < nearestsq) {
					nearestsq = distsq;
				}
			}

			if (nearestsq > bestdistsq) {
				best = i;
				bestdistsq = nearestsq;
			}
		}

		if (best < 0) {
			break;
		}

		chosen[count] = cands[best];
		count++;
	}

	return count;
}

static f32 scenarioPadDistSq3(const struct scenariopadpick *a, const struct scenariopadpick *b)
{
	f32 dx = a->x - b->x;
	f32 dy = a->y - b->y;
	f32 dz = a->z - b->z;

	return dx * dx + dy * dy + dz * dz;
}

/* Whether cand is nearer to anchors[owner] than to any other anchor (a tie
 * goes to the lower anchor index). */
static s32 scenarioPadBelongsTo(const struct scenariopadpick *cand, s32 owner,
		const struct scenariopadpick *anchors, s32 numanchors, f32 *owndistsq)
{
	s32 j;

	*owndistsq = scenarioPadDistSq3(cand, &anchors[owner]);

	for (j = 0; j < numanchors; j++) {
		f32 distsq;

		if (j == owner) {
			continue;
		}

		distsq = scenarioPadDistSq3(cand, &anchors[j]);

		if (distsq < *owndistsq || (distsq == *owndistsq && j < owner)) {
			return 0;
		}
	}

	return 1;
}

s32 scenarioPadPickNearest(s16 *out, s32 maxout, s32 owner,
		const struct scenariopadpick *anchors, s32 numanchors,
		const struct scenariopadpick *cands, s32 numcands)
{
	s32 count = 0;

	if (out == NULL || maxout <= 0 || anchors == NULL || numanchors <= 0
			|| owner < 0 || owner >= numanchors || cands == NULL || numcands <= 0) {
		return 0;
	}

	if (numcands > SCENARIO_PAD_FALLBACK_MAX_CANDIDATES) {
		numcands = SCENARIO_PAD_FALLBACK_MAX_CANDIDATES;
	}

	while (count < maxout) {
		s32 best = -1;
		f32 bestdistsq = 0.0f;
		s32 i;

		for (i = 0; i < numcands; i++) {
			const struct scenariopadpick *cand = &cands[i];
			f32 owndistsq;
			s32 j;

			if (cand->padnum <= 0) {
				continue;
			}

			for (j = 0; j < numanchors; j++) {
				if (anchors[j].padnum == cand->padnum) {
					break;
				}
			}

			if (j < numanchors) {
				continue;
			}

			for (j = 0; j < count; j++) {
				if (out[j] == cand->padnum) {
					break;
				}
			}

			if (j < count) {
				continue;
			}

			if (!scenarioPadBelongsTo(cand, owner, anchors, numanchors, &owndistsq)) {
				continue;
			}

			if (best < 0 || owndistsq < bestdistsq) {
				best = i;
				bestdistsq = owndistsq;
			}
		}

		if (best < 0) {
			break;
		}

		out[count] = cands[best].padnum;
		count++;
	}

	return count;
}

s32 scenarioPadReduceToRooms(struct scenariopadpick *cands, s32 numcands,
		s32 minperroom)
{
	static struct scenariopadpick s_Copy[SCENARIO_PAD_FALLBACK_MAX_CANDIDATES];
	s32 numout = 0;
	s32 i;

	if (cands == NULL || numcands <= 0) {
		return 0;
	}

	if (numcands > SCENARIO_PAD_FALLBACK_MAX_CANDIDATES) {
		numcands = SCENARIO_PAD_FALLBACK_MAX_CANDIDATES;
	}

	memcpy(s_Copy, cands, (size_t)numcands * sizeof(*cands));

	for (i = 0; i < numcands; i++) {
		const struct scenariopadpick *first = &s_Copy[i];
		f32 sumx = 0.0f;
		f32 sumz = 0.0f;
		f32 meanx;
		f32 meanz;
		f32 bestdistsq = 0.0f;
		s32 inroom = 0;
		s32 best = -1;
		s32 j;

		if (first->padnum <= 0 || first->room <= 0) {
			continue;
		}

		// Handle each room once, at its first candidate
		for (j = 0; j < i; j++) {
			if (s_Copy[j].padnum > 0 && s_Copy[j].room == first->room) {
				break;
			}
		}

		if (j < i) {
			continue;
		}

		for (j = i; j < numcands; j++) {
			if (s_Copy[j].padnum > 0 && s_Copy[j].room == first->room) {
				sumx += s_Copy[j].x;
				sumz += s_Copy[j].z;
				inroom++;
			}
		}

		if (inroom < minperroom) {
			continue;
		}

		meanx = sumx / (f32)inroom;
		meanz = sumz / (f32)inroom;

		for (j = i; j < numcands; j++) {
			const struct scenariopadpick *cand = &s_Copy[j];
			f32 dx;
			f32 dz;
			f32 distsq;

			if (cand->padnum <= 0 || cand->room != first->room) {
				continue;
			}

			dx = cand->x - meanx;
			dz = cand->z - meanz;
			distsq = dx * dx + dz * dz;

			if (best < 0 || distsq < bestdistsq) {
				best = j;
				bestdistsq = distsq;
			}
		}

		cands[numout] = s_Copy[best];
		numout++;
	}

	return numout;
}

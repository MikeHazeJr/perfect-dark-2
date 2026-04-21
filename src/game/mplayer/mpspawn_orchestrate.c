/*
 * mpspawn_orchestrate.c -- Match-start spawn orchestration for MP.
 *
 * Team anchors (max-spread among pool points) + Voronoi soft regions,
 * global min-cost assignment (Hungarian on padded square matrix), optional
 * bottleneck swap refinement, origin/duplicate relax, then human apply +
 * bot pool indices. Deterministic participant order (player num, bot slot)
 * for lockstep across host-fed config + spawn pool seed.
 */

#include <ultra64.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "constants.h"
#include "types.h"
#include "data.h"
#include "game/spawnpool.h"
#include "game/mplayer/mplayer.h"
#include "game/mplayer/mpspawn_orchestrate.h"
#include "game/mplayer/mpspawn_hungarian.h"
#include "game/player.h"
#include "game/chr.h"
#include "game/prop.h"
#include "game/bot.h"
#include "system.h"

bool g_MpOrchestrateInitialSpawnDone;

s32 g_MpOrchestrateBotPoolIdx[MAX_BOTS];

#define ORCH_MAX_PART MAX_MPCHRS
#define ORCH_ORIGIN_TOL_START 85.0f
#define ORCH_MIN_SEP_START 145.0f
#define ORCH_RELAX_ITERS 10
/* Soft Voronoi mismatch penalty (s64 cost); must dominate typical XZ dist^2 */
#define ORCH_VORONOI_PENALTY ((int64_t)1000000000LL)
#define ORCH_ORIGIN_COST ((int64_t)500000000LL)
#define ORCH_REFINE_SWAPS 96

struct orch_part {
	struct chrdata *chr;
	struct prop *prop;
	s32 playernum;
	s32 aibotnum;
	s32 team_sector;
};

static struct orch_part s_Parts[ORCH_MAX_PART];
static s32 s_PartCount;
static s32 s_AssignedPool[ORCH_MAX_PART];
static struct coord s_PlacedPos[ORCH_MAX_PART];

static int64_t s_HungSquare[SPAWNPOOL_MAX * SPAWNPOOL_MAX];
static s32 s_VoronoiOwner[SPAWNPOOL_MAX];
static struct coord s_Anchors[4];
static s32 s_HungMatch[SPAWNPOOL_MAX];

void mpOrchestrateReset(void)
{
	s32 i;

	g_MpOrchestrateInitialSpawnDone = false;
	for (i = 0; i < MAX_BOTS; i++) {
		g_MpOrchestrateBotPoolIdx[i] = -1;
	}
	s_PartCount = 0;
}

static f32 orch_min_dist_sq_to_placed(const struct coord *p, s32 nplaced, s32 skip_part_idx)
{
	f32 best = 1.0e30f;
	s32 k;

	for (k = 0; k < nplaced; k++) {
		f32 dx;
		f32 dz;
		f32 d;

		if (skip_part_idx >= 0 && k == skip_part_idx) {
			continue;
		}
		dx = p->x - s_PlacedPos[k].x;
		dz = p->z - s_PlacedPos[k].z;
		d = dx * dx + dz * dz;

		if (d < best) {
			best = d;
		}
	}
	return best;
}

static bool orch_near_origin_xz(const struct coord *p, f32 tol)
{
	return (p->x * p->x + p->z * p->z) < tol * tol;
}

static s32 orch_active_team_mask(void)
{
	u32 mask = 0;
	s32 i;

	for (i = 0; i < MAX_PLAYERS; i++) {
		if (g_Vars.players[i] && g_Vars.players[i]->prop && g_Vars.players[i]->prop->chr) {
			mask |= (u32)g_Vars.players[i]->prop->chr->team;
		}
	}
	for (i = 0; i < g_BotCount; i++) {
		if (g_MpBotChrPtrs[i]) {
			mask |= (u32)g_MpBotChrPtrs[i]->team;
		}
	}
	return (s32)mask;
}

static s32 orch_team_bits_to_sector(u32 team_bits, u32 active_mask)
{
	s32 idx = 0;
	s32 b;

	for (b = 0; b < 4; b++) {
		if (active_mask & (1u << b)) {
			if (team_bits & (1u << b)) {
				return idx;
			}
			idx++;
		}
	}
	return 0;
}

static f32 orch_dist2_xz(const struct coord *a, const struct coord *b)
{
	f32 dx = a->x - b->x;
	f32 dz = a->z - b->z;

	return dx * dx + dz * dz;
}

static int orch_part_cmp(const void *va, const void *vb)
{
	const struct orch_part *a = va;
	const struct orch_part *b = vb;
	s32 ka;
	s32 kb;

	ka = (a->playernum >= 0) ? a->playernum : (0x10000 + a->aibotnum);
	kb = (b->playernum >= 0) ? b->playernum : (0x10000 + b->aibotnum);
	if (ka < kb) {
		return -1;
	}
	if (ka > kb) {
		return 1;
	}
	return 0;
}

static void orch_sort_parts_deterministic(void)
{
	if (s_PartCount <= 1) {
		return;
	}
	qsort(s_Parts, (size_t)s_PartCount, sizeof(s_Parts[0]), orch_part_cmp);
}

/* Pick K anchor XZ positions from pool points: first farthest from AABB
 * center, then greedily maximize min distance to existing anchors. */
static void orch_compute_team_anchors(const spawn_pool_t *pool,
		struct coord *center, s32 k_anchors, struct coord *out_anchor)
{
	s32 t;
	s32 pi;
	s32 s;
	f32 best;
	f32 mind;
	f32 dx;
	f32 dz;
	s32 best_pi;

	if (k_anchors <= 0 || !pool || pool->count <= 0) {
		return;
	}
	if (k_anchors == 1) {
		out_anchor[0].x = center->x;
		out_anchor[0].y = center->y;
		out_anchor[0].z = center->z;
		return;
	}

	best = -1.0f;
	best_pi = 0;
	for (pi = 0; pi < pool->count; pi++) {
		f32 d;

		d = orch_dist2_xz(&pool->points[pi].pos, center);
		if (d > best) {
			best = d;
			best_pi = pi;
		}
	}
	out_anchor[0] = pool->points[best_pi].pos;

	for (t = 1; t < k_anchors; t++) {
		best = -1.0f;
		best_pi = 0;
		for (pi = 0; pi < pool->count; pi++) {
			mind = 1.0e30f;
			for (s = 0; s < t; s++) {
				dx = pool->points[pi].pos.x - out_anchor[s].x;
				dz = pool->points[pi].pos.z - out_anchor[s].z;
				d = dx * dx + dz * dz;
				if (d < mind) {
					mind = d;
				}
			}
			if (mind > best) {
				best = mind;
				best_pi = pi;
			}
		}
		out_anchor[t] = pool->points[best_pi].pos;
	}
}

static s32 orch_voronoi_nearest_anchor(const struct coord *pos,
		const struct coord *anchors, s32 k)
{
	s32 best = 0;
	f32 best_d = 1.0e30f;
	s32 t;

	for (t = 0; t < k; t++) {
		f32 d = orch_dist2_xz(pos, &anchors[t]);

		if (d < best_d) {
			best_d = d;
			best = t;
		}
	}
	return best;
}

static f32 orch_bottleneck_min_sep_sq(const spawn_pool_t *pool,
		const s32 *assign_pool_idx, s32 n)
{
	f32 worst = 1.0e30f;
	s32 p;
	s32 q;
	s32 ia;
	s32 ib;
	f32 dx;
	f32 dz;
	f32 sq;

	for (p = 0; p < n; p++) {
		ia = assign_pool_idx[p];
		if (ia < 0) {
			continue;
		}
		for (q = p + 1; q < n; q++) {
			ib = assign_pool_idx[q];
			if (ib < 0) {
				continue;
			}
			dx = pool->points[ia].pos.x - pool->points[ib].pos.x;
			dz = pool->points[ia].pos.z - pool->points[ib].pos.z;
			sq = dx * dx + dz * dz;
			if (sq < worst) {
				worst = sq;
			}
		}
	}
	return worst;
}

static void orch_refine_bottleneck_swaps(const spawn_pool_t *pool, s32 n)
{
	s32 iter;
	s32 p;
	s32 q;
	f32 best;
	f32 nb;
	s32 tmp;
	bool improved;

	for (iter = 0; iter < ORCH_REFINE_SWAPS; iter++) {
		improved = false;
		best = orch_bottleneck_min_sep_sq(pool, s_AssignedPool, n);
		for (p = 0; p < n; p++) {
			for (q = p + 1; q < n; q++) {
				tmp = s_AssignedPool[p];
				s_AssignedPool[p] = s_AssignedPool[q];
				s_AssignedPool[q] = tmp;
				nb = orch_bottleneck_min_sep_sq(pool, s_AssignedPool, n);
				if (nb > best + 4.0f) {
					best = nb;
					improved = true;
				} else {
					tmp = s_AssignedPool[p];
					s_AssignedPool[p] = s_AssignedPool[q];
					s_AssignedPool[q] = tmp;
				}
			}
		}
		if (!improved) {
			break;
		}
	}
}

static s32 orch_pick_any_unused(const spawn_pool_t *pool, const bool *used_pool,
		s32 nplaced, f32 min_sep_sq, s32 skip_part_idx)
{
	f32 best_score = -1.0e30f;
	s32 best_pi = -1;
	s32 pi;

	for (pi = 0; pi < pool->count; pi++) {
		f32 sep;
		f32 score;

		if (used_pool[pi]) {
			continue;
		}
		sep = orch_min_dist_sq_to_placed(&pool->points[pi].pos, nplaced, skip_part_idx);
		if (sep < min_sep_sq) {
			continue;
		}
		score = sep;
		if (score > best_score) {
			best_score = score;
			best_pi = pi;
		}
	}
	return best_pi;
}

static s32 orch_pick_force_any(const spawn_pool_t *pool, const bool *used_pool)
{
	s32 pi;

	for (pi = 0; pi < pool->count; pi++) {
		if (!used_pool[pi]) {
			return pi;
		}
	}
	return -1;
}

void mpOrchestrateMatchStartSpawns(void)
{
	spawn_aabb_t aabb;
	struct coord center;
	const spawn_pool_t *pool;
	bool used_pool[SPAWNPOOL_MAX];
	u32 active_mask;
	s32 num_sectors;
	s32 nplaced;
	s32 pi;
	s32 i;
	s32 p;
	s32 k_anchors;
	s32 dim;
	s32 n_part;
	f32 origin_tol = ORCH_ORIGIN_TOL_START;
	f32 min_sep = ORCH_MIN_SEP_START;
	s32 relax_iter;
	f32 dx;
	f32 dz;
	int64_t c;

	if (!g_Vars.mplayerisrunning || !spawnPoolIsReady()) {
		return;
	}

	if (g_MpOrchestrateInitialSpawnDone) {
		return;
	}

	pool = spawnPoolGet();
	if (!pool || pool->count <= 0) {
		return;
	}

	spawnPoolComputeAABB(&aabb);
	if (!aabb.valid) {
		return;
	}
	center.x = (aabb.min.x + aabb.max.x) * 0.5f;
	center.y = (aabb.min.y + aabb.max.y) * 0.5f;
	center.z = (aabb.min.z + aabb.max.z) * 0.5f;

	s_PartCount = 0;
	for (i = 0; i < MAX_PLAYERS; i++) {
		if (!g_Vars.players[i] || !g_Vars.players[i]->prop || !g_Vars.players[i]->prop->chr) {
			continue;
		}
		if (s_PartCount >= ORCH_MAX_PART) {
			break;
		}
		s_Parts[s_PartCount].chr = g_Vars.players[i]->prop->chr;
		s_Parts[s_PartCount].prop = g_Vars.players[i]->prop;
		s_Parts[s_PartCount].playernum = i;
		s_Parts[s_PartCount].aibotnum = -1;
		s_PartCount++;
	}
	for (i = 0; i < g_BotCount; i++) {
		if (!g_MpBotChrPtrs[i] || !g_MpBotChrPtrs[i]->prop) {
			continue;
		}
		if (s_PartCount >= ORCH_MAX_PART) {
			break;
		}
		s_Parts[s_PartCount].chr = g_MpBotChrPtrs[i];
		s_Parts[s_PartCount].prop = g_MpBotChrPtrs[i]->prop;
		s_Parts[s_PartCount].playernum = -1;
		s_Parts[s_PartCount].aibotnum = g_MpBotChrPtrs[i]->aibot
			? (s32)g_MpBotChrPtrs[i]->aibot->aibotnum : i;
		s_PartCount++;
	}

	if (s_PartCount <= 0) {
		return;
	}

	orch_sort_parts_deterministic();

	active_mask = (u32)orch_active_team_mask();
	num_sectors = 0;
	for (i = 0; i < 4; i++) {
		if (active_mask & (1u << i)) {
			num_sectors++;
		}
	}
	if (!(g_MpSetup.options & MPOPTION_TEAMSENABLED) || num_sectors < 2) {
		num_sectors = 1;
	}
	if (num_sectors > 4) {
		num_sectors = 4;
	}

	for (i = 0; i < s_PartCount; i++) {
		if (num_sectors >= 2) {
			s_Parts[i].team_sector = orch_team_bits_to_sector(
					(u32)s_Parts[i].chr->team, active_mask);
		} else {
			s_Parts[i].team_sector = 0;
		}
	}

	k_anchors = num_sectors;
	orch_compute_team_anchors(pool, &center, k_anchors, s_Anchors);

	for (pi = 0; pi < pool->count && pi < SPAWNPOOL_MAX; pi++) {
		s_VoronoiOwner[pi] = orch_voronoi_nearest_anchor(
				&pool->points[pi].pos, s_Anchors, k_anchors);
	}

	n_part = s_PartCount;
	dim = pool->count;
	if (n_part > dim) {
		sysLogPrintf(LOG_WARNING,
			"SPAWN.ORCH: participants %d > pool %d - truncating",
			n_part, dim);
		n_part = dim;
	}

	for (i = 0; i < dim * dim; i++) {
		s_HungSquare[i] = (int64_t)0;
	}
	for (i = 0; i < n_part; i++) {
		s32 tsec = s_Parts[i].team_sector;

		if (tsec < 0) {
			tsec = 0;
		}
		if (tsec >= k_anchors) {
			tsec = k_anchors - 1;
		}
		for (pi = 0; pi < dim; pi++) {
			dx = pool->points[pi].pos.x - s_Anchors[tsec].x;
			dz = pool->points[pi].pos.z - s_Anchors[tsec].z;
			c = (int64_t)(dx * dx + dz * dz);
			if (num_sectors >= 2 && s_VoronoiOwner[pi] != tsec) {
				c += ORCH_VORONOI_PENALTY;
			}
			if (orch_near_origin_xz(&pool->points[pi].pos, ORCH_ORIGIN_TOL_START)) {
				c += ORCH_ORIGIN_COST;
			}
			s_HungSquare[i * dim + pi] = c;
		}
	}

	(void)mpHungarianMinSquare(dim, s_HungSquare, s_HungMatch);
	for (i = 0; i < s_PartCount; i++) {
		s_AssignedPool[i] = -1;
	}
	for (i = 0; i < n_part; i++) {
		s_AssignedPool[i] = s_HungMatch[i];
	}
	for (; i < s_PartCount; i++) {
		s_AssignedPool[i] = -1;
	}

	orch_refine_bottleneck_swaps(pool, n_part);

	sysLogPrintf(LOG_NOTE,
		"SPAWN.ORCH: Hungarian dim=%d parts=%d k_anchors=%d (sum cost, lockstep host seed)",
		dim, n_part, k_anchors);

	memset(used_pool, 0, sizeof(used_pool));
	for (p = 0; p < s_PartCount; p++) {
		s32 ai = s_AssignedPool[p];

		if (ai < 0 || ai >= pool->count || used_pool[ai]) {
			ai = orch_pick_force_any(pool, used_pool);
			s_AssignedPool[p] = ai;
		}
		if (ai >= 0 && ai < pool->count) {
			used_pool[ai] = true;
			s_PlacedPos[p] = pool->points[ai].pos;
		} else {
			sysLogPrintf(LOG_WARNING,
				"SPAWN.ORCH: no pool slot for part %d/%d",
				p, s_PartCount);
		}
	}
	nplaced = s_PartCount;

	/* Validate origin / duplicates; relax */
	for (relax_iter = 0; relax_iter < ORCH_RELAX_ITERS; relax_iter++) {
		s32 viol_a = -1;
		s32 viol_b = -1;
		s32 viol_kind = 0; /* 1=dup 2=origin */

		origin_tol = ORCH_ORIGIN_TOL_START + (f32)relax_iter * 45.0f;
		min_sep = ORCH_MIN_SEP_START * (1.0f - (f32)relax_iter * 0.07f);
		if (min_sep < 35.0f) {
			min_sep = 35.0f;
		}

		for (p = 0; p < s_PartCount; p++) {
			s32 pidx = s_AssignedPool[p];

			if (pidx < 0) {
				continue;
			}
			if (orch_near_origin_xz(&pool->points[pidx].pos, origin_tol)) {
				viol_kind = 2;
				viol_a = p;
				break;
			}
		}

		if (viol_kind == 0) {
			f32 worst_dup_sq = 1.0e30f;

			for (p = 0; p < s_PartCount; p++) {
				s32 q;
				s32 pidx = s_AssignedPool[p];

				if (pidx < 0) {
					continue;
				}
				for (q = p + 1; q < s_PartCount; q++) {
					s32 qidx = s_AssignedPool[q];
					f32 dx;
					f32 dz;
					f32 sq;

					if (qidx < 0) {
						continue;
					}
					dx = pool->points[pidx].pos.x - pool->points[qidx].pos.x;
					dz = pool->points[pidx].pos.z - pool->points[qidx].pos.z;
					sq = dx * dx + dz * dz;
					if (sq < min_sep * min_sep && sq < worst_dup_sq) {
						worst_dup_sq = sq;
						viol_kind = 1;
						viol_a = p;
						viol_b = q;
					}
				}
			}
		}

		if (viol_kind == 0) {
			break;
		}

		{
			s32 fix = (viol_kind == 1) ? viol_b : viol_a;
			s32 old_idx = s_AssignedPool[fix];
			s32 new_pi;
			f32 min_sep_sq = min_sep * min_sep;
			u32 teams_mask_on_point = 0;
			s32 count_on_point = 0;
			struct coord zone_pt;

			if (old_idx >= 0 && old_idx < pool->count) {
				used_pool[old_idx] = false;
			}

			new_pi = orch_pick_any_unused(pool, used_pool, nplaced, min_sep_sq, fix);
			if (new_pi < 0) {
				new_pi = orch_pick_force_any(pool, used_pool);
			}
			if (new_pi < 0) {
				sysLogPrintf(LOG_WARNING,
					"SPAWN.ORCH: relax iter=%d failed to find alternate slot",
					relax_iter);
				break;
			}

			for (i = 0; i < s_PartCount; i++) {
				if (old_idx >= 0 && s_AssignedPool[i] == old_idx) {
					count_on_point++;
					teams_mask_on_point |= (u32)s_Parts[i].chr->team;
				}
			}
			zone_pt.x = center.x;
			zone_pt.y = center.y;
			zone_pt.z = center.z;
			if (num_sectors >= 2 && fix >= 0 && fix < s_PartCount) {
				s32 ts = s_Parts[fix].team_sector;

				if (ts >= 0 && ts < k_anchors) {
					zone_pt = s_Anchors[ts];
				}
			}

			sysLogPrintf(LOG_WARNING,
				"SPAWN.ORCH: RELAX iter=%d kind=%s old_pool=%d new_pool=%d "
				"min_sep=%.0f origin_tol=%.0f count_on_pool_idx=%d "
				"teams_mask_on_point=0x%x team_zone=(%.0f,%.0f,%.0f) old_pos=(%.0f,%.0f,%.0f)",
				relax_iter,
				viol_kind == 1 ? "duplicate" : "origin",
				old_idx, new_pi, min_sep, origin_tol,
				count_on_point,
				(unsigned)teams_mask_on_point,
				zone_pt.x, zone_pt.y, zone_pt.z,
				old_idx >= 0 ? pool->points[old_idx].pos.x : 0.0f,
				old_idx >= 0 ? pool->points[old_idx].pos.y : 0.0f,
				old_idx >= 0 ? pool->points[old_idx].pos.z : 0.0f);

			used_pool[new_pi] = true;
			s_AssignedPool[fix] = new_pi;
			/* refresh placed list */
			nplaced = 0;
			for (i = 0; i < s_PartCount; i++) {
				s32 ai = s_AssignedPool[i];

				if (ai >= 0) {
					s_PlacedPos[nplaced++] = pool->points[ai].pos;
				}
			}
		}
	}

	spawnPoolClearReservations();

	for (p = 0; p < s_PartCount; p++) {
		s32 ai = s_AssignedPool[p];

		if (ai < 0) {
			continue;
		}
		if (s_Parts[p].playernum >= 0) {
			playerApplyOrchestratedSpawnFromPool(s_Parts[p].playernum, ai);
		} else if (s_Parts[p].aibotnum >= 0 && s_Parts[p].aibotnum < MAX_BOTS) {
			g_MpOrchestrateBotPoolIdx[s_Parts[p].aibotnum] = ai;
		}
	}

	g_MpOrchestrateInitialSpawnDone = true;

	sysLogPrintf(LOG_NOTE,
		"SPAWN.ORCH: done participants=%d pool=%d sectors=%d teams_mask=0x%x",
		s_PartCount, pool->count, num_sectors, (unsigned)active_mask);
}

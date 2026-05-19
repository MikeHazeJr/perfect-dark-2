/**
 * PC: Swept capsule collision system
 *
 * Modern replacement for N64-era collision workarounds. Projects a capsule
 * along a movement path before translating, finding precise collision points.
 *
 * Uses the existing geometry collection and testing infrastructure (cdTestVolume,
 * cdFindGroundInfoAtCyl, cdFindCeilingRoomYColourFlagsAtPos) but samples at
 * sub-step intervals along the movement path. This is a "stepped sweep" —
 * trivially fast on modern x86_64 even with many sub-steps.
 */

#include <ultra64.h>
#include "constants.h"
#include "game/bg.h"
#include "game/chr.h"
#include "game/prop.h"
#include "game/propobj.h"
#include "game/bondmove.h"
#include "game/player.h"
#include "game/playermgr.h"
#include "bss.h"
#include "lib/collision.h"
#include "lib/capsule.h"
#include "lib/meshcollision.h"
#include "lib/mtx.h"
#include "data.h"
#include "types.h"
#include "system.h"

/* CAPSULE: log markers (Track G prep, c038).
 *
 * Canonical instrumentation for the physics-collision pillar.  Each marker
 * uses the "CAPSULE:" prefix so smoke-verify assertions can grep a single
 * stable token. Runtime emission honors Debug.JumpLogging so normal dev /
 * prerelease builds do not flood the client log.
 *
 * Gated on PD_DEV_BUILD: dev / prerelease / local builds get the lines,
 * stable release builds compile them out entirely (no printf cost).
 * capsule.c links into pd only (never pd-server), so the gate is sound. */
#if defined(PD_DEV_BUILD)
extern s32 g_JumpLoggingEnabled;
#define CAPSULE_LOG(...) do { \
	if (g_JumpLoggingEnabled) { \
		sysLogPrintf(LOG_NOTE, "CAPSULE: " __VA_ARGS__); \
	} \
} while (0)
#else
#define CAPSULE_LOG(...) ((void)0)
#endif

/* Number of sub-steps for the sweep. 16 gives ~2-unit resolution for typical
 * jump moves of ~30 units. On modern hardware this is trivially cheap. */
#define CAPSULE_SWEEP_STEPS 16

/* Maximum probe distance for floor/ceiling searches (units) */
#define CAPSULE_FLOOR_PROBE   2000.0f
#define CAPSULE_CEILING_PROBE 2000.0f

/* Sub-step size for floor/ceiling binary search */
#define CAPSULE_BSEARCH_ITERS 12

#define CAPSULE_RENDERED_SAMPLE_YS     3
#define CAPSULE_RENDERED_SAMPLE_SKINS  5
#define CAPSULE_RENDERED_HITMASK_ALL \
	((1u << CAPSULE_HIT_FLOOR) | (1u << CAPSULE_HIT_CEILING) | (1u << CAPSULE_HIT_WALL))
#define CAPSULE_RENDERED_HITMASK_FLOOR   (1u << CAPSULE_HIT_FLOOR)
#define CAPSULE_RENDERED_HITMASK_CEILING (1u << CAPSULE_HIT_CEILING)
#define CAPSULE_RENDERED_HITMASK_BLOCKING \
	((1u << CAPSULE_HIT_CEILING) | (1u << CAPSULE_HIT_WALL))

static struct prop *capsuleCurrentPlayerProp(void)
{
	if (g_Vars.currentplayer) {
		return g_Vars.currentplayer->prop;
	}
	return NULL;
}

static void capsuleSetSelfPerim(struct prop *selfprop, bool enable)
{
	if (selfprop) {
		propSetPerimEnabled(selfprop, enable);
	}
}

static void capsuleFindRoomsForPos(struct prop *selfprop, const struct coord *start,
		const RoomNum *startrooms, const struct coord *pos, RoomNum *outrooms)
{
	roomsCopy((RoomNum *)startrooms, outrooms);
	func0f065e74((struct coord *)start, (RoomNum *)startrooms,
		(struct coord *)pos, outrooms);

	if (!selfprop) {
		return;
	}

	if (selfprop->type == PROPTYPE_PLAYER && g_Vars.currentplayer
			&& selfprop == g_Vars.currentplayer->prop) {
		bmoveFindEnteredRoomsByPos(g_Vars.currentplayer, (struct coord *)pos, outrooms);
	} else if (selfprop->type == PROPTYPE_CHR && selfprop->chr) {
		chr0f021fa8(selfprop->chr, (struct coord *)pos, outrooms);
	}
}

static void capsuleNormalFromMove(const struct coord *move, struct coord *normal)
{
	f32 movelen2 = move->x * move->x + move->y * move->y + move->z * move->z;
	f32 invlen = 1.0f;

	if (movelen2 > 0.001f) {
		invlen = 1.0f / sqrtf(movelen2);
	}

	normal->x = -move->x * invlen;
	normal->y = -move->y * invlen;
	normal->z = -move->z * invlen;
}

static s32 capsuleClassifyStage1Hit(const struct capsulecast *cast,
		struct prop *obstacle, u16 geoflags)
{
	if (cast->move.y > 0.1f && (geoflags & (GEOFLAG_FLOOR1 | GEOFLAG_FLOOR2))) {
		return CAPSULE_HIT_CEILING;
	}
	if (cast->move.y < -0.1f && (geoflags & (GEOFLAG_FLOOR1 | GEOFLAG_FLOOR2))) {
		return CAPSULE_HIT_FLOOR;
	}
	if (geoflags & GEOFLAG_WALL) {
		return CAPSULE_HIT_WALL;
	}
	if (obstacle) {
		return CAPSULE_HIT_PROP;
	}
	if (cast->move.y > 0.1f) {
		return CAPSULE_HIT_CEILING;
	}
	if (cast->move.y < -0.1f) {
		return CAPSULE_HIT_FLOOR;
	}
	return CAPSULE_HIT_WALL;
}

static f32 capsuleMeshRayCast(const struct coord *from, const struct coord *to,
		const RoomNum *rooms, struct prop *selfprop, struct coord *out_normal,
		struct prop **out_prop)
{
	struct coord normal = {0.0f, 0.0f, 0.0f};
	struct prop *prop = NULL;
	f32 best_frac = capsuleStage2RayCast(from, to, rooms, &normal);

	if (best_frac < 1.0f && out_normal) {
		*out_normal = normal;
	}

	if (meshRayCastDynamicProps(from, to, rooms, selfprop,
				&best_frac, &normal, &prop)) {
		if (out_normal) {
			*out_normal = normal;
		}
		if (out_prop) {
			*out_prop = prop;
		}
	} else if (out_prop) {
		*out_prop = NULL;
	}

	if (best_frac < 1.0f && out_normal) {
		struct coord move;
		move.x = to->x - from->x;
		move.y = to->y - from->y;
		move.z = to->z - from->z;
		capsuleOrientNormalAgainstMove(out_normal, &move);
	}

	return best_frac;
}

static f32 capsuleMeshSweepSamples(struct capsulecast *cast, u32 hitmask,
		struct coord *out_normal, s32 *out_type, struct prop **out_prop)
{
	const f32 mid = (cast->ymin_offset + cast->ymax_offset) * 0.5f;
	const f32 ys[CAPSULE_RENDERED_SAMPLE_YS] = {
		cast->ymax_offset,
		mid,
		cast->ymin_offset,
	};
	const f32 skin = cast->radius > 1.0f ? cast->radius * 0.85f : cast->radius;
	const f32 skins[CAPSULE_RENDERED_SAMPLE_SKINS][2] = {
		{ 0.0f, 0.0f },
		{ skin,  0.0f },
		{-skin,  0.0f },
		{ 0.0f,  skin },
		{ 0.0f, -skin },
	};

	f32 best_frac = 1.0f;
	struct coord best_normal = {0.0f, 0.0f, 0.0f};
	struct prop *best_prop = NULL;
	s32 best_type = CAPSULE_HIT_NONE;

	for (s32 yi = 0; yi < CAPSULE_RENDERED_SAMPLE_YS; yi++) {
		for (s32 si = 0; si < CAPSULE_RENDERED_SAMPLE_SKINS; si++) {
			struct coord from;
			struct coord to;
			RoomNum rayrooms[8];
			struct coord raynormal = {0.0f, 0.0f, 0.0f};
			struct prop *rayprop = NULL;

			from.x = cast->start.x + skins[si][0];
			from.y = cast->start.y + ys[yi];
			from.z = cast->start.z + skins[si][1];
			to.x = from.x + cast->move.x;
			to.y = from.y + cast->move.y;
			to.z = from.z + cast->move.z;

			capsuleFindRoomsForPos(cast->selfprop, &cast->start,
				cast->rooms, &to, rayrooms);

			f32 frac = capsuleMeshRayCast(&from, &to, rayrooms,
				cast->selfprop, &raynormal, &rayprop);
			if (frac >= best_frac) {
				continue;
			}

			capsuleOrientNormalAgainstMove(&raynormal, &cast->move);
			s32 hittype = capsuleClassifyNormal(&raynormal);
			if ((hitmask & (1u << hittype)) == 0) {
				continue;
			}

			best_frac = frac;
			best_normal = raynormal;
			best_type = rayprop ? CAPSULE_HIT_PROP : hittype;
			best_prop = rayprop;
		}
	}

	if (best_frac < 1.0f) {
		if (out_normal) *out_normal = best_normal;
		if (out_type) *out_type = best_type;
		if (out_prop) *out_prop = best_prop;
	}

	return best_frac;
}

static void capsuleApplyMeshHit(struct capsulecast *cast, f32 frac,
		const struct coord *normal, s32 hittype, struct prop *hitprop)
{
	cast->hitfrac = frac;
	cast->hitpos.x = cast->start.x + cast->move.x * frac;
	cast->hitpos.y = cast->start.y + cast->move.y * frac;
	cast->hitpos.z = cast->start.z + cast->move.z * frac;
	cast->hitnormal = *normal;
	cast->hitprop = hitprop;
	cast->hitgeoflags = 0;
	cast->hittype = hitprop ? CAPSULE_HIT_PROP : hittype;
	cast->hitfromrendered = 1;
}

f32 capsuleSweep(struct capsulecast *cast)
{
	f32 stepfrac = 1.0f / (f32)CAPSULE_SWEEP_STEPS;
	struct coord testpos;
	RoomNum testrooms[8];
	s32 i;
	struct prop *selfprop = cast->selfprop;

	cast->hittype = CAPSULE_HIT_NONE;
	cast->hitfrac = 1.0f;
	cast->hitprop = NULL;
	cast->hitgeoflags = 0;
	cast->hitfromrendered = 0;

	/* Trivial case: no movement */
	f32 movelen2 = cast->move.x * cast->move.x
	             + cast->move.y * cast->move.y
	             + cast->move.z * cast->move.z;
	if (movelen2 < 0.001f) {
		return 1.0f;
	}

	CAPSULE_LOG("sweep enter start=(%.1f,%.1f,%.1f) move=(%.1f,%.1f,%.1f) radius=%.1f ymin=%.1f ymax=%.1f",
			cast->start.x, cast->start.y, cast->start.z,
			cast->move.x, cast->move.y, cast->move.z,
			cast->radius, cast->ymin_offset, cast->ymax_offset);

	/* Disable own perim so we don't collide with ourselves */
	capsuleSetSelfPerim(selfprop, false);

	for (i = 1; i <= CAPSULE_SWEEP_STEPS; i++) {
		f32 frac = stepfrac * (f32)i;

		testpos.x = cast->start.x + cast->move.x * frac;
		testpos.y = cast->start.y + cast->move.y * frac;
		testpos.z = cast->start.z + cast->move.z * frac;

		/* Determine rooms at test position */
		capsuleFindRoomsForPos(selfprop, &cast->start, cast->rooms, &testpos, testrooms);

		/* Test the capsule volume at this position */
		s32 result = cdTestVolume(&testpos, cast->radius, testrooms,
				cast->cdtypes, CHECKVERTICAL_YES,
				cast->ymax_offset, cast->ymin_offset);

		if (result == CDRESULT_COLLISION) {
			/* Stage 1 found a collision. Keep this as authoritative unless
			 * rendered geometry reports an even earlier hit. */
			f32 safefrac = stepfrac * (f32)(i - 1);
			struct coord stage2normal = {0.0f, 0.0f, 0.0f};
			struct prop *stage2prop = NULL;
			s32 stage2type = CAPSULE_HIT_NONE;
			f32 stage2frac = capsuleMeshSweepSamples(cast,
				CAPSULE_RENDERED_HITMASK_ALL, &stage2normal,
				&stage2type, &stage2prop);

			cast->hitfrac = safefrac;

			/* Compute approximate Stage 1 hit position */
			cast->hitpos.x = cast->start.x + cast->move.x * frac;
			cast->hitpos.y = cast->start.y + cast->move.y * frac;
			cast->hitpos.z = cast->start.z + cast->move.z * frac;

			/* Identify what we hit */
			struct prop *obstacle = cdGetObstacleProp();
			cast->hitprop = obstacle;
			cast->hitgeoflags = cdGetGeoFlags();
			cast->hittype = capsuleClassifyStage1Hit(cast, obstacle, cast->hitgeoflags);
			capsuleNormalFromMove(&cast->move, &cast->hitnormal);

			if (stage2frac < safefrac) {
				capsuleApplyMeshHit(cast, stage2frac, &stage2normal,
					stage2type, stage2prop);
				safefrac = stage2frac;
			}

			capsuleSetSelfPerim(selfprop, true);

#if defined(PD_DEV_BUILD)
			{
				const char *hitclass;
				switch (cast->hittype) {
				case CAPSULE_HIT_FLOOR:   hitclass = "FLOOR";   break;
				case CAPSULE_HIT_CEILING: hitclass = "CEILING"; break;
				case CAPSULE_HIT_WALL:    hitclass = "WALL";    break;
				case CAPSULE_HIT_PROP:    hitclass = "PROP";    break;
				default:                  hitclass = "NONE";    break;
				}
				CAPSULE_LOG("sweep result=BLOCKED type=%s dist=%.3f geoflags=0x%04x rendered=%d step=%d/%d",
						hitclass, safefrac, cast->hitgeoflags,
						cast->hitfromrendered, i, CAPSULE_SWEEP_STEPS);
			}
#endif

			return safefrac;
		}
	}

	capsuleSetSelfPerim(selfprop, true);

	{
		struct coord normal = {0.0f, 0.0f, 0.0f};
		struct prop *renderedprop = NULL;
		s32 renderedtype = CAPSULE_HIT_NONE;
		f32 stage2_frac = capsuleMeshSweepSamples(cast,
			CAPSULE_RENDERED_HITMASK_ALL, &normal, &renderedtype,
			&renderedprop);
		if (stage2_frac < 1.0f) {
			capsuleApplyMeshHit(cast, stage2_frac, &normal,
				renderedtype, renderedprop);
			CAPSULE_LOG("sweep result=BLOCKED_STAGE2 dist=%.3f hittype=%d prop=%d (Stage 1 clear)",
				stage2_frac, cast->hittype, renderedprop != NULL);
			return stage2_frac;
		}
	}

	CAPSULE_LOG("sweep result=CLEAR dist=1.000");
	return 1.0f;
}

/* ============================================================
 * Stage 2 (c038/B-339): collision-owned mesh ray cast + floor probe.
 *
 * Bridges the gap where Stage 1 (cdTestVolume) misses geometry that
 * is modeled/rendered but not present in legacy geoblocks. Two common cases reported
 * by Mike (2026-05-17):
 *   - Sloped ceilings: jumping straight up passes through the sloped
 *     ceiling triangle because no GEOFLAG_WALL tile exists.
 *   - Table tops / half walls: standing on them, the player falls
 *     through because cdFindGroundInfoAtCyl only finds GEOFLAG_FLOOR
 *     tiles and these surfaces are just rendered display-list triangles.
 *
 * Movement callers use capsuleMeshSweepSamples above: top / middle / bottom
 * heights crossed with center and lateral skin rays. The public single-ray
 * function remains for diagnostics and targeted systems that already have a
 * precise ray. Mesh data is owned by meshcollision.c and dynamic props are
 * transformed from prop/defaultobj state, not render frame matrices.
 * ============================================================ */

f32 capsuleStage2RayCast(const struct coord *from, const struct coord *to,
                         const RoomNum *rooms, struct coord *out_normal)
{
	if (!from || !to || !rooms) {
		return 1.0f;
	}

	f32 mx = to->x - from->x;
	f32 my = to->y - from->y;
	f32 mz = to->z - from->z;
	f32 movelen2 = mx * mx + my * my + mz * mz;
	if (movelen2 < 0.001f) {
		return 1.0f;
	}

	f32 best_frac = meshRayCastWorld(from, to, rooms, out_normal);

	if (best_frac < 1.0f) {
		CAPSULE_LOG("stage2 raycast hit frac=%.3f from=(%.1f,%.1f,%.1f) to=(%.1f,%.1f,%.1f)",
			best_frac, from->x, from->y, from->z, to->x, to->y, to->z);
	}

	return best_frac;
}

static f32 capsuleFindMeshVertical(struct prop *selfprop, const struct coord *pos,
		f32 radius, f32 yoff, const RoomNum *rooms, f32 distance,
		f32 direction, u32 hitmask, struct coord *out_normal)
{
	if (!pos || !rooms || distance <= 0.0f) {
		return -30000.0f;
	}

	const f32 skin = radius > 1.0f ? radius * 0.85f : radius;
	const f32 skins[CAPSULE_RENDERED_SAMPLE_SKINS][2] = {
		{ 0.0f, 0.0f },
		{ skin,  0.0f },
		{-skin,  0.0f },
		{ 0.0f,  skin },
		{ 0.0f, -skin },
	};
	f32 best_frac = 1.0f;
	struct coord best_normal = {0.0f, 0.0f, 0.0f};
	struct coord move = {0.0f, direction * distance, 0.0f};

	for (s32 i = 0; i < CAPSULE_RENDERED_SAMPLE_SKINS; i++) {
		struct coord from;
		struct coord to;
		struct coord normal = {0.0f, 0.0f, 0.0f};

		from.x = pos->x + skins[i][0];
		from.y = pos->y + yoff + (direction < 0.0f ? 2.0f : -2.0f);
		from.z = pos->z + skins[i][1];
		to.x = from.x;
		to.y = from.y + direction * distance;
		to.z = from.z;

		f32 frac = capsuleMeshRayCast(&from, &to, rooms, selfprop,
			&normal, NULL);
		if (frac >= best_frac) {
			continue;
		}

		capsuleOrientNormalAgainstMove(&normal, &move);
		s32 type = capsuleClassifyNormal(&normal);
		if ((hitmask & (1u << type)) == 0) {
			continue;
		}

		best_frac = frac;
		best_normal = normal;
	}

	if (best_frac >= 1.0f) {
		return -30000.0f;
	}

	if (out_normal) {
		*out_normal = best_normal;
	}

	f32 hit_y = pos->y + yoff + (direction < 0.0f ? 2.0f : -2.0f)
		+ direction * distance * best_frac;
	CAPSULE_LOG("stage2 vertical probe pos=(%.1f,%.1f,%.1f) yoff=%.1f hit_y=%.1f normal=(%.2f,%.2f,%.2f)",
		pos->x, pos->y, pos->z, yoff, hit_y,
		best_normal.x, best_normal.y, best_normal.z);
	return hit_y;
}

f32 capsuleFindRenderedFloor(struct prop *selfprop, const struct coord *pos,
                     f32 radius, f32 ymin_off, const RoomNum *rooms,
                     f32 maxdepth, struct coord *out_normal)
{
	return capsuleFindMeshVertical(selfprop, pos, radius, ymin_off, rooms,
		maxdepth, -1.0f, CAPSULE_RENDERED_HITMASK_FLOOR, out_normal);
}

f32 capsuleFindRenderedCeiling(struct prop *selfprop, const struct coord *pos,
                     f32 radius, f32 ymax_off, const RoomNum *rooms,
                     f32 maxheight, struct coord *out_normal)
{
	return capsuleFindMeshVertical(selfprop, pos, radius, ymax_off, rooms,
		maxheight, 1.0f, CAPSULE_RENDERED_HITMASK_BLOCKING, out_normal);
}

f32 capsuleStage2FloorProbe(const struct coord *pos, const RoomNum *rooms,
                            f32 maxdepth)
{
	if (!pos || !rooms || maxdepth <= 0.0f) {
		return -30000.0f;
	}

	struct coord from = *pos;
	struct coord to;
	to.x = pos->x;
	to.y = pos->y - maxdepth;
	to.z = pos->z;

	struct coord normal = {0.0f, 0.0f, 0.0f};
	struct prop *prop = NULL;
	f32 frac = capsuleMeshRayCast(&from, &to, rooms, NULL, &normal, &prop);
	if (frac >= 1.0f) {
		return -30000.0f;
	}

	struct coord move = {0.0f, -maxdepth, 0.0f};
	capsuleOrientNormalAgainstMove(&normal, &move);
	if (capsuleClassifyNormal(&normal) != CAPSULE_HIT_FLOOR) {
		return -30000.0f;
	}

	f32 hit_y = pos->y - maxdepth * frac;
	CAPSULE_LOG("stage2 floor probe pos=(%.1f,%.1f,%.1f) hit_y=%.1f normal=(%.2f,%.2f,%.2f) prop=%d",
		pos->x, pos->y, pos->z, hit_y, normal.x, normal.y, normal.z, prop != NULL);
	return hit_y;
}

f32 capsuleFindFloorForProp(struct prop *selfprop, struct coord *pos,
                     f32 radius, f32 ymin_off, f32 ymax_off,
                     RoomNum *rooms, u32 cdtypes,
                     struct prop **out_prop, u16 *out_flags)
{
	if (out_prop) *out_prop = NULL;
	if (out_flags) *out_flags = 0;

	/* Legacy floor detection */
	struct coord testpos;
	RoomNum testrooms[8];
	f32 bgGround;
	f32 propFloor = -30000.0f;
	u16 floorcol = 0;
	u8 floortype = 0;
	u16 floorflags = 0;
	RoomNum floorroom = -1;
	s32 inlift = 0;
	struct prop *lift = NULL;

	testpos = *pos;
	roomsCopy(rooms, testrooms);
	bgGround = cdFindGroundInfoAtCyl(&testpos, radius, testrooms,
			&floorcol, &floortype, &floorflags, &floorroom, &inlift, &lift);

	if (out_flags) *out_flags = floorflags;

	/* Probe downward with capsule volume to find prop surfaces */
	f32 feetY = pos->y + ymin_off;

	if (feetY > bgGround + 2.0f) {
		f32 top = feetY;
		f32 bot = bgGround;

		capsuleSetSelfPerim(selfprop, false);

		f32 mid = (top + bot) * 0.5f;
		testpos.x = pos->x;
		testpos.y = mid - ymin_off;
		testpos.z = pos->z;
		capsuleFindRoomsForPos(selfprop, pos, rooms, &testpos, testrooms);

		s32 midResult = cdTestVolume(&testpos, radius, testrooms,
				cdtypes, CHECKVERTICAL_YES, ymax_off, ymin_off);

		if (midResult == CDRESULT_COLLISION) {
			for (s32 iter = 0; iter < CAPSULE_BSEARCH_ITERS; iter++) {
				f32 test = (top + bot) * 0.5f;
				testpos.y = test - ymin_off;
				capsuleFindRoomsForPos(selfprop, pos, rooms, &testpos, testrooms);

				s32 r = cdTestVolume(&testpos, radius, testrooms,
						cdtypes, CHECKVERTICAL_YES, ymax_off, ymin_off);

				if (r == CDRESULT_COLLISION) {
					bot = test;
				} else {
					top = test;
				}
			}

			propFloor = top;

			if (out_prop) {
				*out_prop = cdGetObstacleProp();
			}
		}

		capsuleSetSelfPerim(selfprop, true);
	}

	{
		struct coord rendered_normal = {0.0f, 0.0f, 0.0f};
		f32 rendered_floor = capsuleFindRenderedFloor(selfprop, pos, radius,
			ymin_off, rooms, CAPSULE_FLOOR_PROBE, &rendered_normal);
		if (rendered_floor > propFloor + 1.0f && rendered_floor > bgGround + 1.0f
				&& rendered_floor <= feetY + 20.0f) {
			CAPSULE_LOG("findFloor result=RENDERED y=%.2f bgGround=%.2f propFloor=%.2f normal=(%.2f,%.2f,%.2f)",
				rendered_floor, bgGround, propFloor,
				rendered_normal.x, rendered_normal.y, rendered_normal.z);
			return rendered_floor;
		}
	}

	if (propFloor > bgGround + 1.0f) {
		CAPSULE_LOG("findFloor result=PROP y=%.2f bgGround=%.2f flags=0x%04x",
				propFloor, bgGround, floorflags);
		return propFloor;
	}

	CAPSULE_LOG("findFloor result=BG y=%.2f flags=0x%04x", bgGround, floorflags);
	return bgGround;
}

f32 capsuleFindFloor(struct coord *pos, f32 radius, f32 ymin_off, f32 ymax_off,
                     RoomNum *rooms, u32 cdtypes,
                     struct prop **out_prop, u16 *out_flags)
{
	return capsuleFindFloorForProp(capsuleCurrentPlayerProp(), pos, radius,
		ymin_off, ymax_off, rooms, cdtypes, out_prop, out_flags);
}

f32 capsuleFindCeilingForProp(struct prop *selfprop, struct coord *pos,
                       f32 radius, f32 ymin_off, f32 ymax_off,
                       RoomNum *rooms, u32 cdtypes,
                       struct prop **out_prop)
{
	if (out_prop) *out_prop = NULL;

	/* Legacy ceiling detection */
	f32 bgCeiling = 99999.0f;
	f32 propCeiling = 99999.0f;
	struct coord testpos;
	RoomNum testrooms[8];

	testpos = *pos;
	cdFindCeilingRoomYColourFlagsAtPos(&testpos, rooms, &bgCeiling, NULL, NULL);

	/* Probe upward with capsule volume for prop/wall ceilings */
	f32 headY = pos->y + ymax_off;
	f32 maxProbe = headY + CAPSULE_CEILING_PROBE;

	if (maxProbe > headY + 10.0f) {
		f32 bot = headY;
		f32 top = (bgCeiling < maxProbe) ? bgCeiling : maxProbe;

		if (top > bot + 5.0f) {
			capsuleSetSelfPerim(selfprop, false);

			f32 probeY = bot + 10.0f;
			testpos.x = pos->x;
			testpos.y = probeY - ymax_off;
			testpos.z = pos->z;
			capsuleFindRoomsForPos(selfprop, pos, rooms, &testpos, testrooms);

			s32 probeResult = cdTestVolume(&testpos, radius, testrooms,
					cdtypes, CHECKVERTICAL_YES, ymax_off, ymin_off);

			if (probeResult == CDRESULT_COLLISION) {
				propCeiling = headY;
				if (out_prop) *out_prop = cdGetObstacleProp();
			} else {
				for (s32 iter = 0; iter < CAPSULE_BSEARCH_ITERS; iter++) {
					f32 test = (bot + top) * 0.5f;
					testpos.y = test - ymax_off;
					capsuleFindRoomsForPos(selfprop, pos, rooms, &testpos, testrooms);

					s32 r = cdTestVolume(&testpos, radius, testrooms,
							cdtypes, CHECKVERTICAL_YES, ymax_off, ymin_off);

					if (r == CDRESULT_COLLISION) {
						top = test;
					} else {
						bot = test;
					}
				}

				if (top < bgCeiling - 1.0f) {
					propCeiling = top;
					if (out_prop) *out_prop = cdGetObstacleProp();
				}
			}

			capsuleSetSelfPerim(selfprop, true);
		}
	}

	{
		f32 result = (propCeiling < bgCeiling) ? propCeiling : bgCeiling;
		struct coord rendered_normal = {0.0f, 0.0f, 0.0f};
		f32 rendered = capsuleFindRenderedCeiling(selfprop, pos, radius,
			ymax_off, rooms, CAPSULE_CEILING_PROBE, &rendered_normal);
		if (rendered > -29999.0f && rendered < result - 1.0f) {
			result = rendered;
			CAPSULE_LOG("findCeiling result=RENDERED y=%.2f normal=(%.2f,%.2f,%.2f)",
				rendered, rendered_normal.x, rendered_normal.y,
				rendered_normal.z);
			return result;
		}

		CAPSULE_LOG("findCeiling result=%s y=%.2f bgCeiling=%.2f propCeiling=%.2f",
				(propCeiling < bgCeiling) ? "PROP" : "BG",
				result, bgCeiling, propCeiling);
		return result;
	}
}

f32 capsuleFindCeiling(struct coord *pos, f32 radius, f32 ymin_off, f32 ymax_off,
                       RoomNum *rooms, u32 cdtypes,
                       struct prop **out_prop)
{
	return capsuleFindCeilingForProp(capsuleCurrentPlayerProp(), pos, radius,
		ymin_off, ymax_off, rooms, cdtypes, out_prop);
}

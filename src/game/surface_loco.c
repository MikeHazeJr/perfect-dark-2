#include <ultra64.h>
#include <math.h>
#include "constants.h"
#include "data.h"
#include "types.h"
#include "system.h"
#include "game/surface_loco.h"
#include "lib/collision.h"
#include "lib/mtx.h"

/* Surface-normal locomotion (S594h-B Slice 1+2).
 * See src/include/game/surface_loco.h for the full design notes.
 *
 * Slice 1 contributes: chrSurfaceLocoInit, chrSurfaceLocoIsEnabled,
 *   chrSurfaceLocoForce{Enabled,Disabled}, chrSurfaceLocoClearOverride.
 * Slice 2 contributes: chrSurfaceLocoSampleFloorNormal,
 *   chrSurfaceLocoBuildTiltMtx, g_SurfaceLocoActiveChr.
 */

struct chrdata *g_SurfaceLocoActiveChr = NULL;

void chrSurfaceLocoInit(struct chrdata *chr)
{
	if (!chr) {
		return;
	}

	chr->surface_up[0] = 0.0f;
	chr->surface_up[1] = 1.0f;
	chr->surface_up[2] = 0.0f;
	chr->surface_up_prev[0] = 0.0f;
	chr->surface_up_prev[1] = 1.0f;
	chr->surface_up_prev[2] = 0.0f;
	chr->surface_blend_frames = 0;
	chr->surface_loco_flags = 0;
}

bool chrSurfaceLocoIsEnabled(struct chrdata *chr)
{
	if (!chr) {
		return false;
	}

	const u8 flags = chr->surface_loco_flags;

	if (flags & SURFACE_LOCO_FLAG_PER_CHR_DISABLE) {
		return false;
	}
	if (flags & SURFACE_LOCO_FLAG_PER_CHR_ENABLE) {
		return true;
	}

	return chr->race == RACE_SKEDAR;
}

void chrSurfaceLocoForceEnabled(struct chrdata *chr)
{
	if (!chr) {
		return;
	}
	chr->surface_loco_flags &= ~SURFACE_LOCO_FLAG_PER_CHR_DISABLE;
	chr->surface_loco_flags |= SURFACE_LOCO_FLAG_PER_CHR_ENABLE;
}

void chrSurfaceLocoForceDisabled(struct chrdata *chr)
{
	if (!chr) {
		return;
	}
	chr->surface_loco_flags &= ~SURFACE_LOCO_FLAG_PER_CHR_ENABLE;
	chr->surface_loco_flags |= SURFACE_LOCO_FLAG_PER_CHR_DISABLE;
}

void chrSurfaceLocoClearOverride(struct chrdata *chr)
{
	if (!chr) {
		return;
	}
	chr->surface_loco_flags &= ~(SURFACE_LOCO_FLAG_PER_CHR_ENABLE | SURFACE_LOCO_FLAG_PER_CHR_DISABLE);
}

/* Cosine threshold for kicking a blend. ~0.99 corresponds to ~8 deg.
 * Below this we ignore the new sample (snap or do nothing); above this
 * we save prev_up = current_up and start the 8-frame interpolation. */
#define SURFACE_LOCO_BLEND_KICK_COS 0.99f

void chrSurfaceLocoTick(struct chrdata *chr)
{
	if (!chr || !chr->prop) {
		return;
	}

	if (!chrSurfaceLocoIsEnabled(chr)) {
		return;
	}

	/* Slice 5 follow-up (c3738): wall-transition climb trigger.
	 *
	 * Before the canonical floor sample, look FORWARD along the bot's
	 * locomotion heading for an approaching wall. The floor sampler
	 * raycasts along -surface_up (straight down for a floor-walking bot)
	 * and never sees walls the bot is walking into. Without this check
	 * the bot bunches up against the wall and never climbs.
	 *
	 * On wall hit: the wall normal becomes the blend TARGET. The
	 * existing 8-frame blend below rotates surface_up onto the wall, and
	 * subsequent ticks' floor sampler (which now points perpendicular to
	 * the wall, i.e. into it) will keep the bot oriented against the
	 * wall until it walks back off.
	 *
	 * On miss: fall through to the canonical floor sampler.
	 */
	f32 wall_up[3];
	if (chrSurfaceLocoSampleWallAhead(chr, NULL, wall_up)) {
		const f32 dot = chr->surface_up[0] * wall_up[0]
				+ chr->surface_up[1] * wall_up[1]
				+ chr->surface_up[2] * wall_up[2];

		if (dot < SURFACE_LOCO_BLEND_KICK_COS) {
			/* Wall is genuinely different from current up: kick the
			 * 8-frame blend. Mirror the canonical edge-blend block
			 * below (save prev, set target, set countdown). */
			chr->surface_loco_flags &= ~SURFACE_LOCO_FLAG_AIRBORNE;
			chr->surface_up_prev[0] = chr->surface_up[0];
			chr->surface_up_prev[1] = chr->surface_up[1];
			chr->surface_up_prev[2] = chr->surface_up[2];
			chr->surface_up[0] = wall_up[0];
			chr->surface_up[1] = wall_up[1];
			chr->surface_up[2] = wall_up[2];
			chr->surface_blend_frames = SURFACE_LOCO_BLEND_FRAMES;
			chr->surface_loco_flags |= SURFACE_LOCO_FLAG_BLENDING;
			sysLogPrintf(LOG_NOTE,
				"SURFACE_LOCO.WALL_BLEND: chrnum=%d wall_normal=(%.3f,%.3f,%.3f)",
				(s32)chr->chrnum, wall_up[0], wall_up[1], wall_up[2]);
			return;
		}
		/* Wall normal effectively matches current up (we're already
		 * aligned with this wall, or the geometry was a near-floor):
		 * fall through to the canonical floor sample. */
	}

	f32 sampled[3];
	const bool ok = chrSurfaceLocoSampleFloorNormal(chr, sampled);

	if (!ok) {
		/* Slice 5 drop heuristic: raycast along -surface_up missed,
		 * meaning there is no surface within reach in the chr's current
		 * local-down direction. Mark airborne and gracefully blend the
		 * surface_up back to world-up so standard gravity reclaims the
		 * chr. Hold steady (don't restart the blend) once we're already
		 * pointed at world-up. */
		chr->surface_loco_flags |= SURFACE_LOCO_FLAG_AIRBORNE;

		const f32 world_dot = chr->surface_up[1];
		if (world_dot >= SURFACE_LOCO_BLEND_KICK_COS) {
			/* Already (effectively) world-up: snap and clear the blend. */
			chr->surface_up[0] = 0.0f;
			chr->surface_up[1] = 1.0f;
			chr->surface_up[2] = 0.0f;
			if (chr->surface_blend_frames <= 0) {
				chr->surface_loco_flags &= ~SURFACE_LOCO_FLAG_BLENDING;
			}
			return;
		}

		/* Not yet world-up: kick a blend toward (0,1,0). */
		chr->surface_up_prev[0] = chr->surface_up[0];
		chr->surface_up_prev[1] = chr->surface_up[1];
		chr->surface_up_prev[2] = chr->surface_up[2];
		chr->surface_up[0] = 0.0f;
		chr->surface_up[1] = 1.0f;
		chr->surface_up[2] = 0.0f;
		chr->surface_blend_frames = SURFACE_LOCO_BLEND_FRAMES;
		chr->surface_loco_flags |= SURFACE_LOCO_FLAG_BLENDING;
		return;
	}

	/* Surface re-acquired: clear airborne. */
	chr->surface_loco_flags &= ~SURFACE_LOCO_FLAG_AIRBORNE;

	const f32 dot = chr->surface_up[0] * sampled[0]
			+ chr->surface_up[1] * sampled[1]
			+ chr->surface_up[2] * sampled[2];

	if (dot >= SURFACE_LOCO_BLEND_KICK_COS) {
		/* Within blend-kick threshold: snap directly. Cheap, also
		 * prevents constant blends from sub-degree normal jitter. */
		chr->surface_up[0] = sampled[0];
		chr->surface_up[1] = sampled[1];
		chr->surface_up[2] = sampled[2];
		if (chr->surface_blend_frames <= 0) {
			chr->surface_loco_flags &= ~SURFACE_LOCO_FLAG_BLENDING;
		}
		return;
	}

	/* Slice 5 edge / corner blend: a larger delta means a real surface
	 * transition (floor -> wall, wall -> ceiling, slope -> slope at
	 * angle). Save current as prev, target as new; the render path
	 * lerps prev -> target over SURFACE_LOCO_BLEND_FRAMES so the visual
	 * transition is smooth instead of snapping. */
	chr->surface_up_prev[0] = chr->surface_up[0];
	chr->surface_up_prev[1] = chr->surface_up[1];
	chr->surface_up_prev[2] = chr->surface_up[2];
	chr->surface_up[0] = sampled[0];
	chr->surface_up[1] = sampled[1];
	chr->surface_up[2] = sampled[2];
	chr->surface_blend_frames = SURFACE_LOCO_BLEND_FRAMES;
	chr->surface_loco_flags |= SURFACE_LOCO_FLAG_BLENDING;
}

void chrSurfaceLocoGetRenderUp(struct chrdata *chr, f32 *out_up)
{
	if (!out_up) {
		return;
	}

	if (!chr) {
		out_up[0] = 0.0f;
		out_up[1] = 1.0f;
		out_up[2] = 0.0f;
		return;
	}

	if (chr->surface_blend_frames <= 0
			|| (chr->surface_loco_flags & SURFACE_LOCO_FLAG_BLENDING) == 0) {
		out_up[0] = chr->surface_up[0];
		out_up[1] = chr->surface_up[1];
		out_up[2] = chr->surface_up[2];
		return;
	}

	/* Linear interpolation prev -> current, normalize at end so the
	 * lerp stays unit-length. The interpolation parameter goes from 0
	 * (just kicked) to 1 (blend complete) as surface_blend_frames
	 * decrements from BLEND_FRAMES to 0. */
	const f32 t = 1.0f - ((f32)chr->surface_blend_frames / (f32)SURFACE_LOCO_BLEND_FRAMES);
	const f32 lx = chr->surface_up_prev[0] * (1.0f - t) + chr->surface_up[0] * t;
	const f32 ly = chr->surface_up_prev[1] * (1.0f - t) + chr->surface_up[1] * t;
	const f32 lz = chr->surface_up_prev[2] * (1.0f - t) + chr->surface_up[2] * t;

	const f32 len_sq = lx * lx + ly * ly + lz * lz;
	if (len_sq < 1.0e-6f) {
		out_up[0] = 0.0f;
		out_up[1] = 1.0f;
		out_up[2] = 0.0f;
		return;
	}
	const f32 inv = 1.0f / sqrtf(len_sq);
	out_up[0] = lx * inv;
	out_up[1] = ly * inv;
	out_up[2] = lz * inv;
}

/* Sample the surface normal "below" the chr along its current local-up.
 *
 * BEHAVIOUR BY CHR CLASS
 *
 *   Non-surface-loco chrs (humans, etc.):
 *     Use the legacy world-down sampler. cdFindFloorRoomYColourNormalPropAtPos
 *     collects the floor geometry the chr is standing on, finds the closest
 *     tile under prop->pos, and writes the tile's normal. On a flat floor
 *     the normal is (0, 1, 0); on a slope the Y component shrinks
 *     proportionally. This preserves all human/civilian behaviour bit-exact.
 *
 *   Surface-loco chrs (Skedars by default, scenario opt-in for others):
 *     Cast a ray from chr->prop->pos along -chr->surface_up out to a
 *     length of ~1.5 * chr->height + safety pad. If the ray hits BG or
 *     prop collision, the hit geo's normal becomes the new target up.
 *     On a flat floor with surface_up == world-up this collapses to the
 *     same answer as the legacy sampler. On a wall (surface_up horizontal)
 *     the ray points INTO the wall and returns the wall's normal -- this
 *     is the wallrun-enabling change.
 *
 *     The hit normal is flipped to face the chr (i.e. point opposite the
 *     ray direction) so the matrix builder always has a consistent
 *     orientation regardless of which side of the geometry was hit.
 *
 *     Edge / corner handling and per-frame blending live in
 *     chrSurfaceLocoTick (see below). The sampler just returns the best
 *     instantaneous target normal for THIS frame.
 *
 *   Drop heuristic (Slice 5):
 *     A raycast miss means there is no surface within reach along the
 *     current surface_up -- the chr has stepped off an edge or has nothing
 *     to "stand on" along its current local-up. The sampler returns false
 *     with out_up biased to world-up so the tick layer (or downstream
 *     consumers like swarm_gpu) gracefully reorient the chr back to
 *     standard gravity. The tick layer is responsible for kicking the
 *     blend back to world-up; the sampler stays stateless.
 *
 * Cost budget: legacy path uses one cdFindFloorRoomYColourNormalPropAtPos.
 * Surface-loco path uses one cdExamLos08 (BG + props, GEOFLAG_WALL |
 * GEOFLAG_FLOOR1 | GEOFLAG_FLOOR2). Comparable cost; both are O(rooms +
 * props) and run once per chr per tick.
 */
bool chrSurfaceLocoSampleFloorNormal(struct chrdata *chr, f32 *out_up)
{
	if (!out_up) {
		return false;
	}

	out_up[0] = 0.0f;
	out_up[1] = 1.0f;
	out_up[2] = 0.0f;

	if (!chr || !chr->prop) {
		return false;
	}

	if (!chrSurfaceLocoIsEnabled(chr)) {
		/* Legacy world-down sampler for humans / other races. Preserve
		 * bit-exact behaviour for every non-surface-loco caller. */
		struct coord normal = { { 0.0f, 1.0f, 0.0f } };
		f32 floor_y = -1.0e30f;
		struct prop *floor_prop = NULL;
		const RoomNum room = cdFindFloorRoomYColourNormalPropAtPos(
				&chr->prop->pos,
				chr->prop->rooms,
				&floor_y,
				NULL,
				&normal,
				&floor_prop);

		(void)room;
		(void)floor_prop;

		if (floor_y <= -1.0e29f) {
			return false;
		}

		if (normal.y < 0.0f) {
			normal.x = -normal.x;
			normal.y = -normal.y;
			normal.z = -normal.z;
		}

		const f32 len_sq = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
		if (len_sq < 1.0e-6f) {
			return false;
		}

		const f32 inv_len = 1.0f / sqrtf(len_sq);
		out_up[0] = normal.x * inv_len;
		out_up[1] = normal.y * inv_len;
		out_up[2] = normal.z * inv_len;
		return true;
	}

	/* Surface-loco branch: raycast along -chr->surface_up. */

	f32 sux = chr->surface_up[0];
	f32 suy = chr->surface_up[1];
	f32 suz = chr->surface_up[2];
	const f32 su_len_sq = sux * sux + suy * suy + suz * suz;
	if (su_len_sq < 1.0e-6f) {
		/* Degenerate stored up. Fall back to world-up so we don't
		 * shoot a zero-length ray. */
		sux = 0.0f;
		suy = 1.0f;
		suz = 0.0f;
	} else if (su_len_sq < 0.999f || su_len_sq > 1.001f) {
		const f32 inv = 1.0f / sqrtf(su_len_sq);
		sux *= inv;
		suy *= inv;
		suz *= inv;
	}

	/* Ray distance: 1.5x chr->height gives a comfortable margin past
	 * the chr's own capsule + room to detect surfaces a bit further
	 * away (wall the bot is "near" but not yet touching). 200 unit
	 * minimum so very-small chrs still sample reliably. */
	f32 ray_len = chr->height * 1.5f;
	if (ray_len < 200.0f) {
		ray_len = 200.0f;
	}

	struct coord from = chr->prop->pos;
	struct coord to;
	to.x = from.x - sux * ray_len;
	to.y = from.y - suy * ray_len;
	to.z = from.z - suz * ray_len;

	/* cdExamLos08 returns CDRESULT_COLLISION on hit. We want walls
	 * (climbing geometry), floors (standing on slopes / floors), and
	 * sight blockers (catch-all for solid block geometry). */
	const u16 geoflags = GEOFLAG_WALL | GEOFLAG_FLOOR1 | GEOFLAG_FLOOR2 | GEOFLAG_BLOCK_SIGHT;
	const s32 los = cdExamLos08(&from, chr->prop->rooms, &to, CDTYPE_ALL, geoflags);

	if (los != CDRESULT_COLLISION) {
		/* No surface within reach along surface_up. Drop heuristic:
		 * orient back to world-up so the chr falls naturally under
		 * standard gravity. Caller (chrSurfaceLocoTick) sees the
		 * false return and kicks the blend. */
		return false;
	}

	struct coord normal = { { 0.0f, 1.0f, 0.0f } };
	cdGetObstacleNormal(&normal);

	/* Flip the hit normal so it points back toward the chr (i.e.
	 * opposite the ray direction = aligned with the chr's surface_up).
	 * Without this we'd sometimes get the normal pointing INTO the
	 * geometry on double-sided hits. */
	const f32 ndotu = normal.x * sux + normal.y * suy + normal.z * suz;
	if (ndotu < 0.0f) {
		normal.x = -normal.x;
		normal.y = -normal.y;
		normal.z = -normal.z;
	}

	const f32 len_sq = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
	if (len_sq < 1.0e-6f) {
		return false;
	}

	const f32 inv_len = 1.0f / sqrtf(len_sq);
	out_up[0] = normal.x * inv_len;
	out_up[1] = normal.y * inv_len;
	out_up[2] = normal.z * inv_len;
	return true;
}

/* Build a rotation matrix that maps world-up (0,1,0) -> surface_up.
 *
 * Rodrigues form. Axis = world_up x surface_up = (sz, 0, -sx). For
 * surface_up close to world-up (dot ~ 1) the rotation is identity.
 * For surface_up close to -world-up (dot ~ -1) the cross-product axis
 * collapses; pick the X axis explicitly and apply a 180deg flip.
 *
 * Output matrix is row-major in this engine's convention (translation
 * stored in m[3][0..2]); the translation column is identity here.
 */
void chrSurfaceLocoBuildTiltMtx(const f32 *surface_up, Mtxf *out)
{
	mtx4LoadIdentity(out);

	if (!surface_up) {
		return;
	}

	const f32 sx = surface_up[0];
	const f32 sy = surface_up[1];
	const f32 sz = surface_up[2];

	const f32 len_sq = sx * sx + sy * sy + sz * sz;
	if (len_sq < 1.0e-6f) {
		return;
	}
	const f32 inv_len = 1.0f / sqrtf(len_sq);
	const f32 ux = sx * inv_len;
	const f32 uy = sy * inv_len;
	const f32 uz = sz * inv_len;

	/* Within ~1.6deg of world-up: identity. The cosine threshold also
	 * doubles as the blend short-circuit Q2 specified, so the renderer
	 * never pays the matrix-build cost on near-flat ground. */
	if (uy > 0.99961923064f) {
		return;
	}

	if (uy < -0.99961923064f) {
		/* Upside-down: rotate 180deg around X to flip Y and Z. */
		out->m[1][1] = -1.0f;
		out->m[2][2] = -1.0f;
		return;
	}

	const f32 ax = uz;
	const f32 az = -ux;
	const f32 ax_len_sq = ax * ax + az * az;
	if (ax_len_sq < 1.0e-6f) {
		return;
	}
	const f32 ax_inv = 1.0f / sqrtf(ax_len_sq);
	const f32 kx = ax * ax_inv;
	const f32 kz = az * ax_inv;

	const f32 c = uy;
	const f32 s = sqrtf(ax_len_sq);
	const f32 t = 1.0f - c;

	/* Rodrigues' rotation formula with axis (kx, 0, kz):
	 * R = I + sin*K + (1-cos)*K^2
	 * K = [[ 0,  -kz, 0 ],
	 *      [ kz,  0, -kx],
	 *      [ 0,   kx, 0 ]]
	 * K^2 = [[ -kz^2, 0,    kx*kz ],
	 *        [ 0,    -1,    0     ],
	 *        [ kx*kz, 0,   -kx^2 ]]
	 *
	 * Engine matrix convention is row-major (mtx4LoadYRotation places
	 * translation in m[3][0..2] and uses v*M for transformation), so
	 * the column-major Rodrigues result is transposed component-wise
	 * when assigning to m[i][j]. Verified: surface_up=(1,0,0) maps
	 * world-up -> (1,0,0) for v*M with v=(0,1,0).
	 */
	out->m[0][0] = 1.0f + t * (-kz * kz);
	out->m[0][1] = s * kz;
	out->m[0][2] = t * (kx * kz);

	out->m[1][0] = -s * kz;
	out->m[1][1] = 1.0f + t * (-1.0f);
	out->m[1][2] = s * kx;

	out->m[2][0] = t * (kx * kz);
	out->m[2][1] = -s * kx;
	out->m[2][2] = 1.0f + t * (-kx * kx);
}

/* Slice 5 follow-up (c3738): forward-projected wall sampler.
 *
 * Casts a ray FORWARD along the bot's locomotion heading, projected
 * onto the current surface plane (perpendicular to surface_up). If the
 * ray hits geometry whose normal differs sufficiently from the current
 * up, the wall normal is returned as the new transition target.
 *
 * Velocity source:
 *   - vel_hint != NULL: use the caller-supplied 3-vector. Intended for
 *     the GPU swarm path that holds per-bot velocity from the shader
 *     readback (s_BoidScratch[i].vx/vy/vz). Plumbing optional; if the
 *     caller doesn't have a hint it can pass NULL.
 *   - vel_hint == NULL: derive heading from (prop->pos - prevpos). The
 *     standard CPU pattern; chr->prevpos is updated each tick from the
 *     model's root position (chr.c::chr0f022214, chraction.c::chraTick).
 *
 * The raycast direction is the velocity projected onto the plane
 * perpendicular to chr->surface_up. This keeps the cast strictly
 * lateral when the bot is on a floor / wall / ceiling: a floor-walker
 * casting forward (horizontal) projects onto the floor plane unchanged;
 * a wall-walker projects gravity-driven downward drift out so the cast
 * stays aligned with the wall surface.
 *
 * Cost: 1 cdExamLos08 with GEOFLAG_WALL | GEOFLAG_BLOCK_SIGHT. Skipped
 * entirely when |projected vel| < SURFACE_LOCO_WALL_MIN_VEL.
 */
bool chrSurfaceLocoSampleWallAhead(struct chrdata *chr, const f32 *vel_hint, f32 *out_up)
{
	if (!out_up) {
		return false;
	}

	out_up[0] = 0.0f;
	out_up[1] = 1.0f;
	out_up[2] = 0.0f;

	if (!chr || !chr->prop) {
		return false;
	}

	if (!chrSurfaceLocoIsEnabled(chr)) {
		return false;
	}

	/* Resolve velocity. */
	f32 vx, vy, vz;
	if (vel_hint) {
		vx = vel_hint[0];
		vy = vel_hint[1];
		vz = vel_hint[2];
	} else {
		vx = chr->prop->pos.x - chr->prevpos.x;
		vy = chr->prop->pos.y - chr->prevpos.y;
		vz = chr->prop->pos.z - chr->prevpos.z;
	}

	/* Normalize current surface_up so the projection step doesn't
	 * scale the velocity component spuriously. */
	f32 sux = chr->surface_up[0];
	f32 suy = chr->surface_up[1];
	f32 suz = chr->surface_up[2];
	const f32 su_len_sq = sux * sux + suy * suy + suz * suz;
	if (su_len_sq < 1.0e-6f) {
		sux = 0.0f;
		suy = 1.0f;
		suz = 0.0f;
	} else if (su_len_sq < 0.999f || su_len_sq > 1.001f) {
		const f32 inv = 1.0f / sqrtf(su_len_sq);
		sux *= inv;
		suy *= inv;
		suz *= inv;
	}

	/* Project velocity onto the surface plane: v_planar = v - (v.u)*u */
	const f32 vdotu = vx * sux + vy * suy + vz * suz;
	f32 px = vx - vdotu * sux;
	f32 py = vy - vdotu * suy;
	f32 pz = vz - vdotu * suz;

	const f32 p_len_sq = px * px + py * py + pz * pz;
	const f32 min_vel_sq = SURFACE_LOCO_WALL_MIN_VEL * SURFACE_LOCO_WALL_MIN_VEL;
	if (p_len_sq < min_vel_sq) {
		/* Bot is stationary (or moving purely along surface_up, e.g.
		 * falling). No climb intent; skip the raycast. */
		return false;
	}

	const f32 inv_p_len = 1.0f / sqrtf(p_len_sq);
	px *= inv_p_len;
	py *= inv_p_len;
	pz *= inv_p_len;

	/* Ray length: radius slack + height slice. For default Skedar
	 * (radius~30, height~200) this is ~145 units -- about 2 strides
	 * forward. The radius term ensures we always see anything actually
	 * inside the capsule; the height term gives a stand-off so we kick
	 * the blend before the bot mashes into the wall. */
	f32 ray_len = chr->radius * SURFACE_LOCO_WALL_LOOKAHEAD_MULT_RADIUS
			+ chr->height * SURFACE_LOCO_WALL_LOOKAHEAD_MULT_HEIGHT;
	if (ray_len < 100.0f) {
		ray_len = 100.0f;
	}

	struct coord from = chr->prop->pos;
	struct coord to;
	to.x = from.x + px * ray_len;
	to.y = from.y + py * ray_len;
	to.z = from.z + pz * ray_len;

	/* Walls + sight blockers. We deliberately omit FLOOR1/FLOOR2 here:
	 * the floor sampler already covers walkable surfaces directly under
	 * the bot, and including floors in a forward cast would fire on
	 * every adjoining floor tile and spuriously re-orient the bot. */
	const u16 geoflags = GEOFLAG_WALL | GEOFLAG_BLOCK_SIGHT;
	const s32 los = cdExamLos08(&from, chr->prop->rooms, &to, CDTYPE_ALL, geoflags);

	if (los != CDRESULT_COLLISION) {
		return false;
	}

	struct coord normal = { { 0.0f, 1.0f, 0.0f } };
	cdGetObstacleNormal(&normal);

	/* Flip the wall normal to face the chr (opposite the ray
	 * direction). Without this, a hit on the back face would give a
	 * normal pointing away from the chr and the bot would try to align
	 * up with the wrong side of the wall. */
	const f32 ndotp = normal.x * px + normal.y * py + normal.z * pz;
	if (ndotp > 0.0f) {
		normal.x = -normal.x;
		normal.y = -normal.y;
		normal.z = -normal.z;
	}

	const f32 n_len_sq = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
	if (n_len_sq < 1.0e-6f) {
		return false;
	}
	const f32 inv_n = 1.0f / sqrtf(n_len_sq);
	const f32 nx = normal.x * inv_n;
	const f32 ny = normal.y * inv_n;
	const f32 nz = normal.z * inv_n;

	/* Transition gate: only treat this wall as a NEW surface if its
	 * normal is far enough off the current surface_up. dot >= threshold
	 * means the geometry is too close to the current "up" to be a
	 * meaningful transition (e.g. a slope blending into the floor we're
	 * already on); the canonical floor sampler will track it more
	 * smoothly. */
	const f32 wall_dot_up = nx * sux + ny * suy + nz * suz;
	if (wall_dot_up >= SURFACE_LOCO_WALL_NORMAL_THRESHOLD) {
		return false;
	}

	out_up[0] = nx;
	out_up[1] = ny;
	out_up[2] = nz;
	return true;
}

#include <ultra64.h>
#include <math.h>
#include "constants.h"
#include "data.h"
#include "types.h"
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

	f32 sampled[3];
	const bool ok = chrSurfaceLocoSampleFloorNormal(chr, sampled);

	if (!ok) {
		/* No surface under chr (vent over a pit, raycast missed). For
		 * Slice 3 we hold the current surface_up; Slice 5 will add the
		 * seam-safety hold-then-airborne logic with a frame counter. */
		return;
	}

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

	/* Larger delta: kick a blend. Save current as prev, target as new. */
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

/* Sample the floor surface normal under the chr.
 *
 * cdFindFloorRoomYColourNormalPropAtPos collects the floor geometry the
 * chr is standing on, finds the closest tile under prop->pos, and
 * writes the tile's normal via cdGetGeoNormal. One collision sweep, no
 * triangulation. On a flat floor the normal is (0, 1, 0); on a slope
 * the Y component shrinks proportionally.
 *
 * Slice 2 visual budget: this runs once per surface-loco chr per render
 * frame, sharing the same collision-collect cost as the chr's existing
 * ground-find. Negligible at 256 Skedars.
 *
 * Slice 3 will replace the world-up sample direction with a ray cast
 * along -surface_up so the chr can detect wall and ceiling surfaces.
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
		/* No floor under chr: nothing to align to. Slice 3 will treat
		 * this case as airborne; for Slice 2 we fall back to world-up
		 * so the chr renders as if on level ground. */
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

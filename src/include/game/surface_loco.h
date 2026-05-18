#ifndef _IN_GAME_SURFACE_LOCO_H
#define _IN_GAME_SURFACE_LOCO_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

/* Surface-normal locomotion (S594h-B).
 * Skedar-class chrs walk on walls and ceilings with their model rotated
 * to align local-up with the surface normal under them.
 *
 * Slice 1: data plumbing on chrdata, opt-in helpers, init.
 * Slice 2: per-frame floor-normal sample + render tilt.
 * Slice 3: surface-plane gravity / velocity integration.
 * Slice 4: aim path projection.
 * Slice 5: edge/corner blend, drop heuristic, scary-jump.
 *
 * The default for an enabled chr is the world-up vector, so until later
 * slices populate chr->surface_up the rendered transform is identity.
 */

/* Initialize chr->surface_up* and chr->surface_loco_flags to identity.
 * Called from chrInit. Idempotent: safe to call repeatedly. */
void chrSurfaceLocoInit(struct chrdata *chr);

/* Returns 1 if surface-normal locomotion is active for this chr.
 * Logic: PER_CHR_DISABLE wins, then PER_CHR_ENABLE, else race default
 * (currently RACE_SKEDAR == on, all others == off). */
bool chrSurfaceLocoIsEnabled(struct chrdata *chr);

/* Force per-chr opt-in (PER_CHR_ENABLE). Clears PER_CHR_DISABLE. Call
 * site: spawn-volume / mod scenarios that want a non-Skedar to walk on
 * walls, or that want a Skedar to behave normally for one chr. */
void chrSurfaceLocoForceEnabled(struct chrdata *chr);

/* Force per-chr opt-out (PER_CHR_DISABLE). Clears PER_CHR_ENABLE. */
void chrSurfaceLocoForceDisabled(struct chrdata *chr);

/* Clear both override bits. The chr falls back to its race default. */
void chrSurfaceLocoClearOverride(struct chrdata *chr);

/* Sample the surface normal "below" the chr along its current local up.
 *
 * Behaviour depends on whether the chr is surface-loco enabled
 * (chrSurfaceLocoIsEnabled):
 *
 *   - Disabled (humans, etc.): legacy world-down sample via
 *     cdFindFloorRoomYColourNormalPropAtPos. Bit-exact behaviour for
 *     every non-surface-loco caller.
 *
 *   - Enabled (Skedars etc.): Slice 5 upgrade -- raycast along
 *     -chr->surface_up via cdExamLos08 with FLOOR + WALL + BLOCK_SIGHT
 *     geometry. Returns the hit geo's normal, flipped to point back
 *     toward the chr. On flat ground with surface_up == world-up this
 *     collapses to the legacy behaviour; on walls / ceilings it returns
 *     the wall / ceiling normal so the chr aligns to climb.
 *
 * Result written into out_up (length 3, normalized).
 * Returns 1 on success.
 * Returns 0 on raycast miss -- the caller is responsible for triggering
 * the airborne / drop-back-to-world-up blend (chrSurfaceLocoTick already
 * does this). out_up is set to world-up on failure.
 */
bool chrSurfaceLocoSampleFloorNormal(struct chrdata *chr, f32 *out_up);

/* Slice 2: build a 4x4 rotation matrix that maps world-up (0,1,0) onto
 * the supplied surface_up vector. The translation column is identity
 * (caller composes with the chr's world position separately).
 *
 * No-op (loads identity) when surface_up is within ~2.5deg of world-up.
 * Handles the upside-down (-world-up) edge case via a fixed X-axis flip
 * to avoid the singularity in the cross-product axis derivation.
 */
void chrSurfaceLocoBuildTiltMtx(const f32 *surface_up, Mtxf *out);

/* Slice 2: render-path hand-off. chrRender sets the active chr around
 * the modelRender call so the chrinfo matrix builder can read its
 * surface_up. NULL when no surface-loco chr is being rendered. */
extern struct chrdata *g_SurfaceLocoActiveChr;

/* Slice 3: per-tick surface_up update. Called from chrTick once per
 * frame for every active chr. Samples the floor surface normal under
 * the chr (via chrSurfaceLocoSampleFloorNormal), compares to the chr's
 * current surface_up, and kicks an 8-frame blend if the delta exceeds
 * the cosine threshold (~5deg).
 *
 * Idempotent for chrs with surface-loco disabled (returns immediately).
 * For surface-loco chrs the function maintains:
 *   - chr->surface_up (current authoritative value)
 *   - chr->surface_up_prev (start of blend, copied at blend start)
 *   - chr->surface_blend_frames (countdown; render uses lerp prev->up)
 *   - chr->surface_loco_flags BLENDING / AIRBORNE bits
 */
void chrSurfaceLocoTick(struct chrdata *chr);

/* Slice 3: render-path getter. Returns the interpolated surface-up
 * vector that the renderer should use for this frame. Lerps between
 * surface_up_prev and surface_up based on surface_blend_frames; drops
 * to chr->surface_up when no blend is active. Caller-allocated 3-float
 * output. */
void chrSurfaceLocoGetRenderUp(struct chrdata *chr, f32 *out_up);

/* Slice 3: blend window length. 8 frames at 60Hz = ~133ms. Tuned from
 * the design doc; revisit during playtest if transitions feel too
 * snappy (raise) or too rubbery (lower). */
#define SURFACE_LOCO_BLEND_FRAMES 8

/* Slice 5 follow-up (c3738): wall-transition climb trigger.
 *
 * The floor sampler (chrSurfaceLocoSampleFloorNormal) raycasts ALONG
 * -chr->surface_up. For a Skedar walking on a floor, that ray points
 * straight down and never sees a wall the bot is approaching laterally.
 * The bot bunches against the wall and never transitions.
 *
 * chrSurfaceLocoSampleWallAhead solves that: it casts a short ray FORWARD
 * along the bot's locomotion heading, projected onto the current surface
 * plane (perpendicular to surface_up). If the ray hits a wall whose
 * normal is far enough off the current "up", the wall normal is returned
 * as the new surface_up TARGET -- the existing 8-frame blend in
 * chrSurfaceLocoTick smoothly rotates surface_up onto the wall, and the
 * floor sampler takes over for subsequent ticks once the chr is aligned.
 *
 * Velocity hint:
 *   - If vel_hint is non-NULL it is used directly (GPU swarm path can
 *     pass per-bot velocity from the shader readback).
 *   - If vel_hint is NULL the helper falls back to (prop->pos - prevpos)
 *     as the locomotion heading -- the standard CPU pattern.
 *
 * Returns true if a wall was detected and out_up has been populated with
 * the new (normalized) target up. Returns false on miss / low velocity /
 * disabled chr; out_up is set to world-up in that case.
 *
 * Cost: 1 cdExamLos08 raycast per Skedar per tick when |vel| >= MIN_VEL.
 * Comparable to the existing floor sampler.
 */
bool chrSurfaceLocoSampleWallAhead(struct chrdata *chr, const f32 *vel_hint, f32 *out_up);

/* Request a wall-ahead transition using the same logic as
 * chrSurfaceLocoTick, but with an optional velocity hint. GPU swarm
 * callers pass shader velocity here so wall detection follows the
 * actual GPU-driven movement instead of stale prevpos deltas.
 *
 * Returns true when a new wall surface blend was started. */
bool chrSurfaceLocoRequestWallAhead(struct chrdata *chr, const f32 *vel_hint);

/* Apply a surface-contact position without recomputing world-down ground.
 *
 * Standard chrSetPos/chrSetPosWithCachedGround are floor-oriented: they
 * resolve ground as a world-Y value and feed that into manground/ground.
 * Wall and ceiling contact correction must instead keep the chr anchored
 * to the supplied surface contact. This helper performs the normal room,
 * model-root, look-angle, and chrinfo sync while treating pos->y as the
 * contact reference for legacy fall guards.
 */
bool chrSurfaceLocoApplyContactPos(struct chrdata *chr, struct coord *pos,
	RoomNum *rooms, f32 theta);

/* Wall-ahead raycast tuning.
 *
 * Ray length = chr->radius * LOOKAHEAD_MULT_RADIUS + chr->height * LOOKAHEAD_MULT_HEIGHT.
 * For a default Skedar (radius ~30, height ~200) that's ~145 units forward,
 * enough to see a wall two strides out but not so far it triggers on a
 * wall behind the next room.
 *
 * NORMAL_THRESHOLD: dot(wall_normal, current surface_up). If the wall
 * normal is within this cosine of the current up, we treat it as the
 * same surface (no transition needed). 0.3 ~= 72.5deg, so anything
 * tilted more than that off vertical relative to the current floor
 * fires the transition. May need 0.5 (~60deg) if playtest shows missed
 * walls; 0.3 is the design memo's initial value.
 *
 * MIN_VEL: bots with |horizontal vel| below this skip the wall-ahead
 * raycast (no locomotion = no climb intent, and the raycast direction
 * would be degenerate). 2.0 units/frame ~= a slow walk.
 */
#define SURFACE_LOCO_WALL_LOOKAHEAD_MULT_RADIUS 1.5f
#define SURFACE_LOCO_WALL_LOOKAHEAD_MULT_HEIGHT 0.5f
#define SURFACE_LOCO_WALL_NORMAL_THRESHOLD       0.3f
#define SURFACE_LOCO_WALL_MIN_VEL                2.0f

#endif

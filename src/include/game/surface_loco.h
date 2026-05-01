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

/* Slice 2: sample the floor surface normal under the chr by raycasting
 * three offset positions and triangulating. Result written into out_up
 * (length 3, normalized, world-up bias enforced).
 *
 * Cheap (3x cdFindGroundAtCyl); call once per chr per render frame.
 * Returns 1 if the sample succeeded (out_up is the surface normal),
 * 0 if the surface could not be resolved (out_up = world-up fallback).
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

#endif

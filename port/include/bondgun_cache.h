#ifndef PD_BONDGUN_CACHE_H
#define PD_BONDGUN_CACHE_H

#include <PR/ultratypes.h>
#include <stdbool.h>

/*
 * bondgun_cache.h -- B-246 round-10 Phase A: pure predicate for the
 * gun-matrix cache staleness check.
 *
 * Background. The FP gun-render path in bgun0f0a5550 maintains a small
 * cache of identity-space bone matrices (`unk0dd8`) that is populated
 * once per weapon load and read from on every IDLE-anim render frame.
 * The fire / reload paths bypass the cache and run a fresh
 * modelSetMatricesWithAnim each frame.
 *
 * Round-10 playtest log evidence (Build/pd-client.log @ 2026-04-26 23:05)
 * showed the cache was filled with the gun's INITIAL anim state at the
 * moment the gun model was first rendered after master-load CARTS
 * completion -- typically anim 0 frame 0 (T-pose / default-skeleton).
 * By the time idle hold is reached after the equip animation plays out,
 * the gun's current animation is some other track at a non-zero frame
 * (anim 236 frame 17 was observed for FALCON2). The cache is then
 * representing a different pose than the live anim state.
 *
 * Idle frames render the cached pose (anim 0 frame 0 bone arrangement
 * oriented by the per-frame view transform). Fire / reload frames
 * render the actual current anim track. This produces the observed
 * "weapon in proper place during fire / reload, wrong place during
 * idle" asymmetry across multiple weapons.
 *
 * This header defines the pure predicate that captures the invariant
 * the FIX will enforce. Phase A (this commit) lands the predicate plus
 * tests that lock its specification. Phase B will wire the predicate
 * into bgun0f0a5550's a0 decision and capture the cache anim state at
 * fill time.
 *
 * The predicate is intentionally stateless. All inputs are explicit so
 * the test binary can exercise it without dragging in g_Vars / the
 * gun struct / the anim system / GBI globals.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Returns true if the cached matrix buffer should be considered stale
 * relative to the gun model's current animation state.
 *
 * Inputs:
 *   cache_animnum    -- the anim track number the cache was populated under
 *   cache_animframe  -- the anim frame at cache fill time
 *   cur_animnum      -- the gun model's current anim track number
 *   cur_animframe    -- the gun model's current anim frame
 *
 * Returns:
 *   true if the cache is stale and should not be used (caller should
 *      either refill the cache or fall back to fresh modelSetMatricesWithAnim)
 *   false if the cache is still consistent with the current anim state
 *      and is safe to use
 *
 * Decision:
 *   1. If cache_animnum != cur_animnum, the cache encodes a different
 *      animation track entirely. Stale.
 *   2. If same track but the frame has drifted past a half-frame
 *      tolerance, the cache is from an earlier point in the same
 *      animation. Stale.
 *   3. Otherwise the cache reflects the same anim state as live. Fresh.
 *
 * The half-frame tolerance accommodates floating-point drift at the
 * fill-time-vs-read-time boundary; an idle anim that holds at one frame
 * indefinitely will see cache_animframe equal to cur_animframe to
 * within a hair of zero, so the half-frame window is conservative
 * enough not to false-positive while strict enough to catch real
 * progression.
 */
bool bgunMatrixCacheIsStale(s32 cache_animnum, f32 cache_animframe,
                            s32 cur_animnum,   f32 cur_animframe);

#ifdef __cplusplus
}
#endif

#endif /* PD_BONDGUN_CACHE_H */

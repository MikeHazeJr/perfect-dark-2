#ifndef PD_SCENARIO_PAD_FALLBACK_H
#define PD_SCENARIO_PAD_FALLBACK_H

#include <PR/ultratypes.h>
/* No <stdbool.h> and no bool in these declarations: C game code and the C++
 * test runner both see them (SP-75, context/constraints.md). */

/*
 * scenario_pad_fallback.h -- B-1125, B-1126: placement pads for multiplayer
 * scenarios on arenas that carry too few of their own.
 *
 * The objective scenarios take their places from the arena's intro commands:
 * Capture the Case bases and respawn pads (intro CASE and CASERESPAWN), King
 * of the Hill hills (intro HILL), and the "bank" that Hacker Central and Hold
 * the Briefcase pick from (the case pads plus any multiplayer ammo crates).
 * Arenas extracted today carry none of those commands, and mod arenas or solo
 * maps used as arenas may not either. Without them Hacker Central divided by
 * zero (B-1125), every Capture the Case case was created off-world, the King
 * of the Hill hill stayed in the room of pad 0, and the Hold the Briefcase
 * briefcase always started on pad 0 (B-1126).
 *
 * The game feeds this module walkable candidate pads (navigation waypoints,
 * the source the spawn fallback in playerreset.c also uses) and uses the
 * picks only where the arena's own pads are missing. The module has no game
 * state, so pd-tests covers it directly (tests/test_scenario_pad_fallback.cpp).
 */

struct scenariopadpick {
	s16 padnum;
	f32 x;
	f32 z;
	f32 y;    /* height; only the nearest-pad pick measures it */
	s16 room; /* room the pad is in; only the per-room reduction reads it */
};

/* Random source: the game passes rngRandom, so every peer that runs the same
 * setup with the same seed picks the same pads. Tests pass a fixed sequence. */
typedef u32 (*scenariopadrng)(void);

/* Candidates past this count are ignored (no stage comes close). */
#define SCENARIO_PAD_FALLBACK_MAX_CANDIDATES 1024

/* Spacing tiers, widest first. A pick must be at least the current tier's
 * distance (horizontal, x/z) from every pad already chosen; the next, smaller
 * tier is tried only when the current one cannot reach the target. The last
 * tier is 0, so any remaining distinct candidate can still be used. */
#define SCENARIO_PAD_FALLBACK_NUM_SPACINGS 5
#define SCENARIO_PAD_FALLBACK_WIDEST_SPACING 1000.0f

/*
 * Appends picks from cands[] to chosen[] until chosen holds `target` pads
 * (or `capacity`, or the candidates run out). Returns the new count.
 *
 * chosen[0..count) are the pads the scenario already has; they keep their
 * slots and new picks are spaced from them. A candidate is never picked when
 * its padnum is <= 0 (scenario banks use those values for "empty") or when
 * its padnum is already in chosen[], so a duplicated candidate is picked at
 * most once. The candidate order is shuffled once with `rng` and reused for
 * every spacing tier, so the result depends only on the inputs and the rng
 * sequence. Returns `count` unchanged when it is already at the target, when
 * there are no candidates, or when `rng` is NULL.
 */
s32 scenarioPadPickSpaced(struct scenariopadpick *chosen, s32 count,
		s32 capacity, s32 target,
		const struct scenariopadpick *cands, s32 numcands,
		scenariopadrng rng);

/*
 * B-1126: appends picks that are as far apart as possible (Capture the Case
 * bases). Same contract as scenarioPadPickSpaced for chosen/count/capacity/
 * target and for the candidates it refuses.
 *
 * Each new pick is the candidate whose horizontal distance to the nearest pad
 * already chosen is largest (farthest-point order); a tie goes to the earlier
 * candidate. With no pad chosen yet, the first pick is a random candidate
 * (one rng call), so arenas get varied bases; with pads already chosen, no
 * rng call is made at all. Returns `count` unchanged when it is already at
 * the target, when there are no usable candidates, or when the first pick
 * needs `rng` and it is NULL.
 */
s32 scenarioPadPickFarthest(struct scenariopadpick *chosen, s32 count,
		s32 capacity, s32 target,
		const struct scenariopadpick *cands, s32 numcands,
		scenariopadrng rng);

/*
 * B-1126: the candidates nearest to anchors[owner] among those that belong
 * to it (Capture the Case respawn pads around a base).
 *
 * A candidate belongs to the anchor it is nearest to, in three dimensions
 * (so a pad on another floor counts as farther); a tie goes to the lower
 * anchor index. Candidates with padnum <= 0, candidates that are an anchor's
 * own pad, and repeats of a pad already written are skipped. Writes up to
 * `maxout` pad numbers to out[], nearest first (a tie keeps candidate order),
 * and returns how many it wrote. Makes no rng call.
 */
s32 scenarioPadPickNearest(s16 *out, s32 maxout, s32 owner,
		const struct scenariopadpick *anchors, s32 numanchors,
		const struct scenariopadpick *cands, s32 numcands);

/*
 * B-1126: reduces cands[] in place to one candidate per room (King of the
 * Hill: the hill is a whole room, so two hill pads in one room would be the
 * same hill).
 *
 * A room's candidate is the one nearest (horizontally) to the mean position
 * of that room's candidates; a tie goes to the earlier candidate. Rooms with
 * fewer than `minperroom` candidates are dropped (a room crossed by a single
 * waypoint is usually a corridor or a stair), as are candidates with
 * padnum <= 0 or room <= 0. Rooms keep the order of their first candidate.
 * Returns the new count; numcands past SCENARIO_PAD_FALLBACK_MAX_CANDIDATES
 * are ignored.
 */
s32 scenarioPadReduceToRooms(struct scenariopadpick *cands, s32 numcands,
		s32 minperroom);

#endif

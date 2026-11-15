#ifndef PD_SCENARIO_PAD_FALLBACK_H
#define PD_SCENARIO_PAD_FALLBACK_H

#include <PR/ultratypes.h>
/* No <stdbool.h> and no bool in these declarations: C game code and the C++
 * test runner both see them (SP-75, context/constraints.md). */

/*
 * scenario_pad_fallback.h -- B-1125: extra placement pads for multiplayer
 * scenarios on arenas that carry too few of their own.
 *
 * Hacker Central puts its terminal and its data uplink on pads from a "bank":
 * the arena's Capture the Case pads (intro CASE and CASERESPAWN commands)
 * plus any multiplayer ammo crates that exist when the scenario props are
 * set up. An arena without enough bank pads divided by zero in htmInitProps
 * (no pads) or looped forever in its selection loop (fewer pads than it
 * needs). Arenas extracted today carry no intro case pads at all, and mod
 * arenas or solo maps used as arenas may not either.
 *
 * The game feeds this module walkable candidate pads (navigation waypoints,
 * the source the spawn fallback in playerreset.c also uses) and appends the
 * picks to the scenario's bank. The module has no game state, so pd-tests
 * covers it directly (tests/test_scenario_pad_fallback.cpp).
 */

struct scenariopadpick {
	s16 padnum;
	f32 x;
	f32 z;
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

#endif

/*
 * tests/test_bondgun_cache.cpp -- bgunMatrixCacheIsStale specification.
 *
 * B-246 round-10 Phase A. The predicate captures the invariant that
 * Phase B's fix will wire into bgun0f0a5550's a0 decision: the cached
 * gun-matrix buffer (`unk0dd8`) must be considered stale and not used
 * when its captured animation state differs from the gun model's
 * current animation state.
 *
 * Source under test:
 *   port/src/bondgun_cache.c::bgunMatrixCacheIsStale
 *
 * The bug shape, restated for the regression record. The cache fill
 * code at bondgun.c bgun0f0a5550 runs once per weapon load (gated by
 * unk0dd4 == -1) and captures the gun model's bone matrices via
 * modelSetMatricesWithAnim with an identity transform. The captured
 * matrices encode the gun's CURRENT animation state at fill time,
 * which is whatever animInit + the equip-anim warmup has produced
 * by the moment the gun first becomes hand->visible. Empirically
 * (Build/pd-client.log @ 2026-04-26 23:05) this is anim 0 frame 0,
 * the model's default-skeleton T-pose.
 *
 * The IDLE-anim render path then reads the cache every frame, so
 * idle frames render the T-pose oriented by the per-frame view
 * transform. The FIRE / RELOAD render path bypasses the cache and
 * runs a fresh modelSetMatricesWithAnim each frame, producing the
 * actual current anim's bone matrices. The user-visible result is
 * "weapon in proper place during fire / reload, wrong place during
 * idle" -- observed across stock PD weapons (FARSIGHT, DY357MAGNUM,
 * FALCON2) and AllInOne imports alike.
 *
 * Phase A (these tests + the predicate) lock the spec. Phase B will
 * capture cache_animnum / cache_animframe at fill time and call this
 * predicate from the a0 decision in bgun0f0a5550.
 */

#include "catch.hpp"

extern "C" {
#include "bondgun_cache.h"
}

TEST_CASE("bgunMatrixCacheIsStale: identity case (cache matches live)",
          "[bondgun][matrix-cache][regression]") {
	/* Cache filled at the same anim state the gun is currently in. */
	REQUIRE(bgunMatrixCacheIsStale(0, 0.0f, 0, 0.0f) == false);
	REQUIRE(bgunMatrixCacheIsStale(236, 17.0f, 236, 17.0f) == false);
	REQUIRE(bgunMatrixCacheIsStale(45, 3.5f, 45, 3.5f) == false);
}

TEST_CASE("bgunMatrixCacheIsStale: different animnum is always stale",
          "[bondgun][matrix-cache][regression]") {
	/* The H-MTX-A1 bug repro: cache at anim 0 frame 0 (T-pose), gun
	 * currently at anim 236. Cache must be considered stale. This is
	 * exactly what the round-10 playtest log showed. */
	REQUIRE(bgunMatrixCacheIsStale(0, 0.0f, 236, 17.0f) == true);
	REQUIRE(bgunMatrixCacheIsStale(0, 0.0f, 236, 1.0f) == true);

	/* Symmetric direction: live anim is the smaller number. Still stale. */
	REQUIRE(bgunMatrixCacheIsStale(236, 0.0f, 0, 0.0f) == true);

	/* Adjacent anim numbers, still different. */
	REQUIRE(bgunMatrixCacheIsStale(45, 0.0f, 46, 0.0f) == true);
}

TEST_CASE("bgunMatrixCacheIsStale: same animnum but frame drift",
          "[bondgun][matrix-cache][regression]") {
	/* Same anim track but frame has progressed past the half-frame
	 * tolerance. Stale. */
	REQUIRE(bgunMatrixCacheIsStale(236, 0.0f, 236, 17.0f) == true);
	REQUIRE(bgunMatrixCacheIsStale(236, 0.0f, 236, 1.0f) == true);
	REQUIRE(bgunMatrixCacheIsStale(236, 17.0f, 236, 0.0f) == true);

	/* Sub-half-frame drift is tolerated. */
	REQUIRE(bgunMatrixCacheIsStale(236, 17.0f, 236, 17.4f) == false);
	REQUIRE(bgunMatrixCacheIsStale(236, 17.0f, 236, 16.6f) == false);

	/* Exactly half-frame is the boundary; counts as stale (>= epsilon). */
	REQUIRE(bgunMatrixCacheIsStale(236, 17.0f, 236, 17.5f) == true);
	REQUIRE(bgunMatrixCacheIsStale(236, 17.0f, 236, 16.5f) == true);
}

TEST_CASE("bgunMatrixCacheIsStale: animnum mismatch beats frame match",
          "[bondgun][matrix-cache][regression]") {
	/* Even if frames happen to be equal, different animnum is stale.
	 * Pinned because a future change might reorder the conditions
	 * and accidentally let frame-equality short-circuit animnum
	 * mismatch. */
	REQUIRE(bgunMatrixCacheIsStale(0, 17.0f, 236, 17.0f) == true);
	REQUIRE(bgunMatrixCacheIsStale(45, 0.0f, 67, 0.0f) == true);
}

TEST_CASE("bgunMatrixCacheIsStale: floating-point edge cases",
          "[bondgun][matrix-cache][regression]") {
	/* Exact-zero frames. Common case for cache filled at anim 0
	 * frame 0 (T-pose) and gun still at anim 0 frame 0 (e.g. weapon
	 * just loaded, bgunInitHandAnims state). */
	REQUIRE(bgunMatrixCacheIsStale(0, 0.0f, 0, 0.0f) == false);

	/* Tiny positive drift. Float precision must not flip this to
	 * true. */
	REQUIRE(bgunMatrixCacheIsStale(0, 0.0f, 0, 0.001f) == false);

	/* Tiny negative drift (as if cache was filled slightly later
	 * than the captured animframe value, which can happen with
	 * sub-frame interpolation). */
	REQUIRE(bgunMatrixCacheIsStale(0, 17.0f, 0, 16.999f) == false);
}

TEST_CASE("bgunMatrixCacheIsStale: full anim-progress sweep",
          "[bondgun][matrix-cache][regression]") {
	/* Cache stays at frame 0 while live anim sweeps 0 -> 60. The
	 * predicate should flip from "fresh" to "stale" at the half-frame
	 * mark and stay stale all the way through. */
	const s32 anim = 100;
	bool prev = bgunMatrixCacheIsStale(anim, 0.0f, anim, 0.0f);
	REQUIRE(prev == false);

	bool flipped = false;
	for (f32 f = 0.0f; f <= 60.0f; f += 0.1f) {
		bool stale = bgunMatrixCacheIsStale(anim, 0.0f, anim, f);
		if (!flipped && stale) {
			flipped = true;
			/* Flip happens at or before frame 0.6 (half-frame epsilon
			 * with a small float-tolerance band). */
			REQUIRE(f >= 0.4f);
			REQUIRE(f <= 0.6f);
		}
		if (flipped) {
			/* Once flipped, stays stale through the rest of the sweep. */
			REQUIRE(stale == true);
		}
	}
	REQUIRE(flipped == true);
}

/**
 * effect_graph_runtime.c -- OG-executor bridges for .pdeffect graphs.
 *
 * c3849 Unit 1b: bridge stubs only (see the header doc block). The full
 * effect runtime lands in Unit 8. Behavior contract pinned by tests:
 * fallback verbatim except the explosion case, which resolves canonical
 * base explosion tokens through weaponGraphResolveExplosionRef when the
 * Debug.WeaponGraphRuntime toggle is on.
 */

#include "effect_graph_runtime.h"
#include "weapon_graph_runtime.h"

s32 effectGraphResolveExplosionType(const char *effect_ref, s32 fallback_exptype)
{
	s32 resolved;

	if (!effect_ref || !effect_ref[0]) {
		return fallback_exptype;
	}
	/* Toggle gate: with Debug.WeaponGraphRuntime off every consumer must be
	 * bit-identical to OG, so the fallback wins unconditionally. */
	if (!weaponGraphRuntimeEnabled()) {
		return fallback_exptype;
	}

	resolved = weaponGraphResolveExplosionRef(effect_ref);
	if (resolved >= 0) {
		return resolved;
	}

	/* Unit 8: non-base refs resolve through the registered effect graph
	 * table here. Until then an unresolved ref keeps the OG fallback. */
	return fallback_exptype;
}

s32 effectGraphResolveSparkType(const char *effect_ref, s32 fallback_sparktype)
{
	/* Unit 8: spark refs resolve through the runtime spark-row registry.
	 * No base spark vocabulary exists (decided at Unit 1b), so until then
	 * the fallback is returned verbatim. */
	(void)effect_ref;
	return fallback_sparktype;
}

s32 effectGraphResolveSmokeType(const char *effect_ref, s32 fallback_smoketype)
{
	/* Unit 8: smoke refs map to the nearest SMOKETYPE_* row. */
	(void)effect_ref;
	return fallback_smoketype;
}

s32 effectGraphResolveSound(const char *effect_ref, s32 fallback_soundnum)
{
	/* Unit 8: effect sounds resolve through catalogResolveAudio. */
	(void)effect_ref;
	return fallback_soundnum;
}

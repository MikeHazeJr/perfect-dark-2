/**
 * effect_graph_runtime.h -- OG-executor bridges for .pdeffect graphs.
 *
 * c3849 Unit 1b: this header intentionally contains ONLY the four bridge
 * functions. The full effect runtime (compiler, registration, spark-row
 * registry) lands in Unit 8; freezing these signatures now lets every Wave 5
 * detonation/visual consumer ship the call today and light up later without
 * another touch (binding spec B2).
 *
 * Contract: each bridge returns the OG fallback verbatim unless the ref
 * resolves. Until Unit 8, only the explosion bridge can resolve (through the
 * weapon-graph base explosion vocabulary, gated on the runtime toggle);
 * spark/smoke/sound return the fallback unconditionally.
 */
#ifndef _IN_EFFECT_GRAPH_RUNTIME_H
#define _IN_EFFECT_GRAPH_RUNTIME_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the EXPLOSIONTYPE_* for effect_ref when it resolves and the
 * runtime toggle is on; fallback_exptype otherwise. */
s32 effectGraphResolveExplosionType(const char *effect_ref, s32 fallback_exptype);

/* Returns the SPARKTYPE_* for effect_ref once the Unit 8 runtime lands;
 * fallback_sparktype verbatim until then. */
s32 effectGraphResolveSparkType(const char *effect_ref, s32 fallback_sparktype);

/* Returns the SMOKETYPE_* for effect_ref once the Unit 8 runtime lands;
 * fallback_smoketype verbatim until then. */
s32 effectGraphResolveSmokeType(const char *effect_ref, s32 fallback_smoketype);

/* Returns the soundnum for effect_ref once the Unit 8 runtime lands;
 * fallback_soundnum verbatim until then. */
s32 effectGraphResolveSound(const char *effect_ref, s32 fallback_soundnum);

#ifdef __cplusplus
}
#endif

#endif /* _IN_EFFECT_GRAPH_RUNTIME_H */

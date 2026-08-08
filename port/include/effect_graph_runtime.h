/**
 * effect_graph_runtime.h -- .pdeffect compiler/runtime + OG-executor bridges.
 *
 * c3849 Unit 8: the full effect runtime behind the Unit 1b bridge seams.
 * Graphs compile through weaponGraphCompileJson(ASSET_EFFECT) into compact
 * per-asset records keyed by asset_id string (no slot allocator, no owner
 * bits). T-ASSETS-018 retains the complete executable graph/timeline/profile
 * program in growable PC storage; the scalar fields remain compatibility
 * projections for existing consumers until T-ASSETS-019.
 *
 * Bridge contract (frozen at Unit 1b, signatures unchanged): each bridge
 * returns the OG fallback verbatim unless the shared weapon graph runtime
 * gate is enabled AND the ref resolves to a registered record carrying the
 * requested field. Product builds keep the gate enabled after Wave 7; tests
 * can still disable it to prove OG fallback parity.
 */
#ifndef _IN_EFFECT_GRAPH_RUNTIME_H
#define _IN_EFFECT_GRAPH_RUNTIME_H

#include <stddef.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "pdeffect_source.h"
#include "sha256.h"
#include "weapon_graph_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EFFECT_GRAPH_EXPLOSION_CLASS_LEN 16

typedef enum effect_graph_program_kind {
	EFFECT_GRAPH_PROGRAM_GRAPH = 1,
	EFFECT_GRAPH_PROGRAM_TIMELINE = 2,
	EFFECT_GRAPH_PROGRAM_GRAPH_TIMELINE = 3,
	EFFECT_GRAPH_PROGRAM_PROFILE_LIBRARY = 4
} effect_graph_program_kind_t;

typedef struct effect_graph_program {
	effect_graph_program_kind_t kind;
	weapon_graph_ir_context_t *contexts;
	s32 context_count;
	weapon_graph_ir_node_t *nodes;
	s32 node_count;
	weapon_graph_ir_edge_t *edges;
	s32 edge_count;
	weapon_graph_ir_export_t *exports;
	s32 export_count;
	weapon_graph_ir_subgraph_t *subgraphs;
	s32 subgraph_count;
	weapon_graph_ir_param_t *params;
	s32 param_count;
	/* Stable topological execution order. Equal-priority ready nodes retain
	 * their authored node order. */
	s32 *execution_order;
	s32 execution_count;
	pd_effect_timeline_t timeline;
	pd_effect_profile_library_t profiles;
} effect_graph_program_t;

typedef struct effect_graph_runtime {
	s32 valid;
	char asset_id[CATALOG_ID_LEN];
	char source_sha256[SHA256_HEX_SIZE];
	char ir_sha256[SHA256_HEX_SIZE];

	/* effect.explosion: class word -> existing EXPLOSIONTYPE_* index
	 * (tiny->6, small->2, medium->11, large->13, huge->17, massive->25;
	 * binding spec B2 - class words are legal ONLY inside graph bodies).
	 * explosion_type is -1 when the class is absent/unknown. The tint is
	 * recorded presentation data (OG fireballs render hard-white; renderer
	 * tinting is a deferred slice). */
	s32 has_explosion;
	char explosion_class[EFFECT_GRAPH_EXPLOSION_CLASS_LEN];
	s16 explosion_type;
	f32 explosion_tint[4];

	/* effect.spark: tint params compile to the sparktype unk1c/unk20 RGBA
	 * byte layout (0xRRGGBBAA, sparks.c color words). A tinted spark gets a
	 * custom registry row at registration; spark_type is then the registered
	 * row index (>= SPARKTYPE_BASE_COUNT) or -1 when untinted/unregistered
	 * (bridges fall back). */
	s32 has_spark;
	s32 spark_type;
	u32 spark_color1;
	u32 spark_color2;

	/* effect.smoke: class/type -> NEAREST existing SMOKETYPE_* row
	 * (mapping documented at effectSmokeClassToType); -1 unresolved. */
	s32 has_smoke;
	s32 smoke_type;

	/* Optional "sound" param on any effect node (registration-resolved
	 * soundnum; > 0 is the presence convention shared with
	 * impact_hit_sound). */
	s32 has_sound;
	s32 soundnum;

	/* Base presentation nodes author intensity; recorded for the renderer
	 * slice, gameplay-inert. */
	s32 has_intensity;
	f32 intensity;

	/* Complete executable source program. Scalar bridge fields above remain
	 * compatibility projections until T-ASSETS-019 migrates consumers. */
	effect_graph_program_t program;
} effect_graph_runtime_t;

typedef s32 (*effect_graph_program_visit_fn)(
	const effect_graph_program_t *program,
	const weapon_graph_ir_node_t *node,
	s32 execution_index, f32 time, void *user);

/* Registration. GraphJson takes the loose effect.graph.json source;
 * Archive/ArchiveBytes read effect.ini (effect_file member) and compile the
 * named graph member. All three loud-fail (non-zero + err text) on compile
 * or table errors; records re-register in place keyed by asset_id. */
s32 effectGraphRuntimeRegisterGraphJson(const char *asset_id,
                                        const char *json,
                                        u32 json_size,
                                        char *err, size_t err_cap);
s32 effectGraphRuntimeRegisterArchive(const char *archive_path,
                                      char *err, size_t err_cap);
s32 effectGraphRuntimeRegisterArchiveBytes(const void *archive_bytes,
                                           u32 archive_size,
                                           char *err, size_t err_cap);

void effectGraphRuntimeClearAsset(const char *asset_id);
/* ClearAll also resets the custom spark-row registry
 * (sparksResetCustomTypes), so a full mod reload cannot leak rows. */
void effectGraphRuntimeClearAll(void);

/* Get is ungated (tests/tools); GetForGameplay follows the shared weapon
 * graph runtime gate. */
const effect_graph_runtime_t *effectGraphRuntimeGet(const char *asset_id);
const effect_graph_runtime_t *effectGraphRuntimeGetForGameplay(
	const char *asset_id);

size_t effectGraphRuntimeCount(void);
s32 effectGraphProgramSample(const effect_graph_program_t *program,
	const char *property, f32 time, f32 *out_value);
s32 effectGraphProgramExecute(const effect_graph_program_t *program, f32 time,
	effect_graph_program_visit_fn visit, void *user);

/* Returns the EXPLOSIONTYPE_* for effect_ref when it resolves and the
 * runtime gate is enabled; fallback_exptype otherwise. */
s32 effectGraphResolveExplosionType(const char *effect_ref, s32 fallback_exptype);

/* Returns the SPARKTYPE_* (a custom registry row index for tinted sparks)
 * for effect_ref; fallback_sparktype otherwise. */
s32 effectGraphResolveSparkType(const char *effect_ref, s32 fallback_sparktype);

/* Returns the SMOKETYPE_* for effect_ref; fallback_smoketype otherwise. */
s32 effectGraphResolveSmokeType(const char *effect_ref, s32 fallback_smoketype);

/* Returns the soundnum for effect_ref; fallback_soundnum otherwise. */
s32 effectGraphResolveSound(const char *effect_ref, s32 fallback_soundnum);

#ifdef __cplusplus
}
#endif

#endif /* _IN_EFFECT_GRAPH_RUNTIME_H */

/**
 * effect_graph_runtime.h -- .pdeffect compiler/runtime + OG-executor bridges.
 *
 * c3849 Unit 8: the full effect runtime behind the Unit 1b bridge seams.
 * Graphs compile through weaponGraphCompileJson(ASSET_EFFECT) into compact
 * per-asset records keyed by asset_id string (no slot allocator). T-ASSETS-018 retains the complete executable graph/timeline/profile
 * program in growable PC storage. T-ASSETS-019 connects the v2 native-profile
 * libraries; T-ASSETS-032 provides the typed v1 scheduler foundation, with
 * concrete v1 consumers split across T-ASSETS-034/035/036.
 *
 * T-ASSETS-020 adds owner-scoped activation. Standalone catalog rows own
 * themselves; effects embedded by a weapon/projectile/entity archive are
 * owned by the parent weapon. A selected non-empty reference that cannot
 * resolve returns EFFECT_GRAPH_RESOLVE_FAILED instead of silently selecting
 * an OG literal. Empty references remain unauthored and may use the explicit
 * callsite default.
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
#define EFFECT_GRAPH_RESOLVE_FAILED (-1)

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
	/* Normalized v1 descriptor semantics remain public authored metadata.
	 * effect_key is catalog classification: scanner/network ingestion maps it
	 * to ext.effect.effect_type and asset_runtime exposes it as binding.kind.
	 * Executable topology remains graph-authored. target_key, shader_id, and
	 * intensity are production defaults consumed by the instance lanes. */
	char effect_key[64];
	char target_key[64];
	char shader_id[128];
	f32 descriptor_intensity;

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
	 * compatibility projections for legacy selected-channel callsites. */
	effect_graph_program_t program;

	/* Runtime activation owners. Multiple parents may share byte-identical
	 * source; conflicting source for the same catalog ID is rejected. */
	char (*owners)[CATALOG_ID_LEN];
	size_t owner_count;
	size_t owner_capacity;
} effect_graph_runtime_t;

/* T-ASSETS-032: every accepted public v1 node has one permanent dispatch
 * slot. Keep this enum and weaponGraphOpcodeForKind(ASSET_EFFECT) in exact
 * lockstep. A graph whose opcode has no slot is rejected during activation. */
typedef enum effect_graph_dispatch_slot {
	EFFECT_GRAPH_DISPATCH_TINT = 0,
	EFFECT_GRAPH_DISPATCH_GLOW,
	EFFECT_GRAPH_DISPATCH_SHIMMER,
	EFFECT_GRAPH_DISPATCH_DARKEN,
	EFFECT_GRAPH_DISPATCH_SCREEN,
	EFFECT_GRAPH_DISPATCH_PARTICLE,
	EFFECT_GRAPH_DISPATCH_EXPLOSION,
	EFFECT_GRAPH_DISPATCH_SPARK,
	EFFECT_GRAPH_DISPATCH_SMOKE,
	EFFECT_GRAPH_DISPATCH_SLOT_COUNT
} effect_graph_dispatch_slot_t;

typedef struct effect_graph_dispatch_context {
	const effect_graph_runtime_t *runtime;
	f32 time;
	void *user;
} effect_graph_dispatch_context_t;

typedef s32 (*effect_graph_dispatch_node_fn)(
	const effect_graph_dispatch_context_t *context,
	const weapon_graph_ir_node_t *node, s32 execution_index);
typedef s32 (*effect_graph_dispatch_phase_fn)(
	const effect_graph_dispatch_context_t *context);
typedef void (*effect_graph_dispatch_rollback_fn)(
	const effect_graph_dispatch_context_t *context, s32 attempted_count);

typedef struct effect_graph_dispatch_table {
	/* Begin/commit/rollback are mandatory. Rollback receives the number of
	 * callbacks attempted, including a callback that returned failure, so a
	 * consumer can undo partially applied work. */
	effect_graph_dispatch_phase_fn begin;
	effect_graph_dispatch_phase_fn commit;
	effect_graph_dispatch_rollback_fn rollback;
	effect_graph_dispatch_node_fn nodes[EFFECT_GRAPH_DISPATCH_SLOT_COUNT];
} effect_graph_dispatch_table_t;

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

/* Owner-scoped variants used by nested archive transactions. They reject a
 * conflicting same-ID source and never replace another owner's live record. */
s32 effectGraphRuntimeRegisterArchiveOwned(const char *archive_path,
	const char *owner_id, char *err, size_t err_cap);
s32 effectGraphRuntimeRegisterArchiveBytesOwned(const void *archive_bytes,
	u32 archive_size, const char *owner_id, char *err, size_t err_cap);

/* Release every effect edge owned by owner_id. Shared effects remain active
 * until their final owner releases. */
void effectGraphRuntimeReleaseOwner(const char *owner_id);

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

/* Returns the permanent dispatch slot for an accepted effect opcode, or -1.
 * RuntimeDispatch preflights the complete graph and handler table before
 * begin, then executes in stable dependency order. Missing handlers,
 * corrupted order/edges, phase failure, or node failure return non-zero.
 * No node callback runs when preflight fails. */
s32 effectGraphDispatchSlotForOpcode(weapon_graph_opcode_e opcode);
s32 effectGraphRuntimeDispatch(const char *asset_id, f32 time,
	const effect_graph_dispatch_table_t *dispatch, void *user,
	char *err, size_t err_cap);

/* Empty/NULL means unauthored and returns the explicit fallback. A non-empty
 * selected ref returns EFFECT_GRAPH_RESOLVE_FAILED unless the requested
 * channel resolves from active public source. */
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

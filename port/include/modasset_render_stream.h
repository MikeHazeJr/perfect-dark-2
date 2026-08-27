/**
 * modasset_render_stream.h -- typed public render-stream command contract.
 *
 * model.render.json is editable source. Keep its command vocabulary and schema
 * policy in this globals-free layer so extraction, compilation, and focused
 * tests share one definition instead of duplicating stringly-typed decisions.
 */

#ifndef _IN_MODASSET_RENDER_STREAM_H
#define _IN_MODASSET_RENDER_STREAM_H

#include <PR/gbi.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODASSET_RENDER_STREAM_SCHEMA_LEGACY 1
#define MODASSET_RENDER_STREAM_SCHEMA_GEOMETRY 2
#define MODASSET_RENDER_STREAM_SCHEMA_CURRENT 3
#define MODASSET_RENDER_VERTEX_CACHE_SLOTS 64
#define MODASSET_RENDER_MATRIX_INDEX_MAX 32766

/* fast3d consumes these bits while G_VTX transforms a vertex. Schema v2+
 * stores the known/value snapshot for every triangle corner so a compiler that
 * emits fresh vertex loads cannot accidentally reinterpret normals as RGBA (or
 * lose generated texture coordinates/fog) after flattening the source cache.
 * Schema v3 adds explicit vertex-load/cache-slot provenance plus typed
 * opaque/translucent cache boundaries. */
#define MODASSET_RENDER_VERTEX_LOAD_GEOMETRY_MASK \
	(G_FOG | G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR)

typedef enum modasset_render_stream_op {
	MODASSET_RENDER_OP_INVALID = 0,
	MODASSET_RENDER_OP_MTX,
	MODASSET_RENDER_OP_POP,
	MODASSET_RENDER_OP_MATERIAL,
	MODASSET_RENDER_OP_TRI,
	MODASSET_RENDER_OP_GEOMETRY_SET,
	MODASSET_RENDER_OP_GEOMETRY_CLEAR,
	MODASSET_RENDER_OP_VERTEX_LOAD,
	MODASSET_RENDER_OP_VERTEX_SCOPE,
	MODASSET_RENDER_OP_VERTEX_CACHE_RESET,
} modasset_render_stream_op_t;

/* Top-level model nodes are allowed to carry display-list or vertex references
 * that the native preprocessor did not resolve into this model's readable
 * address domain. They still produce a valid hierarchy, part table, and
 * zero-triangle public source archive. This admission decision is intentionally
 * separate from recursive G_DL validation: once both top-level address domains
 * are readable, every nested command stream remains strict/fail-closed. */
typedef enum modasset_render_top_level_walk_disposition {
	MODASSET_RENDER_TOP_LEVEL_WALK_SKIP = 0,
	MODASSET_RENDER_TOP_LEVEL_WALK_EXECUTE = 1,
} modasset_render_top_level_walk_disposition_t;

s32 modAssetRenderStreamSchemaSupported(s32 schema_version);
s32 modAssetRenderStreamResolveSchema(s32 has_numeric_schema,
	s32 numeric_schema, const char *legacy_schema);
modasset_render_stream_op_t modAssetRenderStreamOpFromText(
	s32 schema_version, const char *text);
s32 modAssetRenderStreamOpIsGeometry(modasset_render_stream_op_t op);
s32 modAssetRenderStreamPreservesNodeCombine(s32 schema_version);
s32 modAssetRenderStreamVertexStateValid(u32 mode, u32 known);
s32 modAssetRenderStreamVertexKnownMatches(u32 established, u32 known);
s32 modAssetRenderStreamVertexSnapshotMatches(u32 established_mode,
	u32 established_known, u32 snapshot_mode, u32 snapshot_known);
s32 modAssetRenderStreamVertexRelocationValid(u32 load_known,
	u32 draw_known, u32 compiler_baseline_known);
s32 modAssetRenderStreamVertexLoadRangeValid(s32 slot_first,
	s32 slot_count);
s32 modAssetRenderStreamMatrixIndexValid(s32 matrix_index);
s32 modAssetRenderStreamOptionalMatrixIndexValid(s32 matrix_index);
s32 modAssetRenderStreamCompilerVertexBaseline(s32 render_mode_type,
	u32 *out_mode, u32 *out_known);
s32 modAssetRenderStreamVertexScopeAllowed(s32 schema_version,
	s32 render_mode_type);
s32 modAssetRenderStreamVertexCacheResetAllowed(s32 schema_version,
	s32 render_mode_type);
s32 modAssetRenderStreamTopLevelPairSharesVertexCache(s32 render_mode_type);
modasset_render_top_level_walk_disposition_t
modAssetRenderStreamTopLevelWalkDisposition(s32 has_display_list,
	s32 has_vertex_buffer, s32 vertex_count,
	s32 has_resolved_display_list, s32 has_readable_vertex_domain);

#ifdef __cplusplus
}
#endif

#endif

#include <string.h>

#include "modasset_render_stream.h"

s32 modAssetRenderStreamSchemaSupported(s32 schema_version)
{
	return schema_version == MODASSET_RENDER_STREAM_SCHEMA_LEGACY ||
		schema_version == MODASSET_RENDER_STREAM_SCHEMA_GEOMETRY ||
		schema_version == MODASSET_RENDER_STREAM_SCHEMA_CURRENT;
}

s32 modAssetRenderStreamResolveSchema(s32 has_numeric_schema,
	s32 numeric_schema, const char *legacy_schema)
{
	if (has_numeric_schema) {
		return modAssetRenderStreamSchemaSupported(numeric_schema) ?
			numeric_schema : 0;
	}
	/* The pre-B-1086 public contract already named v1 through its standard
	 * semantic schema string. Preserve installed/user-authored sources while
	 * all current producers add the explicit numeric discriminator. */
	return legacy_schema && strcmp(legacy_schema, "pd2.mesh.render.v1") == 0 ?
		MODASSET_RENDER_STREAM_SCHEMA_LEGACY : 0;
}

modasset_render_stream_op_t modAssetRenderStreamOpFromText(
	s32 schema_version, const char *text)
{
	if (!modAssetRenderStreamSchemaSupported(schema_version) || !text) {
		return MODASSET_RENDER_OP_INVALID;
	}
	if (strcmp(text, "mtx") == 0) return MODASSET_RENDER_OP_MTX;
	if (strcmp(text, "pop") == 0) return MODASSET_RENDER_OP_POP;
	if (strcmp(text, "material") == 0) return MODASSET_RENDER_OP_MATERIAL;
	if (strcmp(text, "tri") == 0) return MODASSET_RENDER_OP_TRI;

	if (schema_version >= MODASSET_RENDER_STREAM_SCHEMA_GEOMETRY) {
		if (strcmp(text, "geometry_set") == 0) {
			return MODASSET_RENDER_OP_GEOMETRY_SET;
		}
		if (strcmp(text, "geometry_clear") == 0) {
			return MODASSET_RENDER_OP_GEOMETRY_CLEAR;
		}
	}
	if (schema_version >= MODASSET_RENDER_STREAM_SCHEMA_CURRENT &&
			strcmp(text, "vertex_load") == 0) {
		return MODASSET_RENDER_OP_VERTEX_LOAD;
	}
	if (schema_version >= MODASSET_RENDER_STREAM_SCHEMA_CURRENT &&
			strcmp(text, "vertex_scope") == 0) {
		return MODASSET_RENDER_OP_VERTEX_SCOPE;
	}
	if (schema_version >= MODASSET_RENDER_STREAM_SCHEMA_CURRENT &&
			strcmp(text, "vertex_cache_reset") == 0) {
		return MODASSET_RENDER_OP_VERTEX_CACHE_RESET;
	}

	return MODASSET_RENDER_OP_INVALID;
}

s32 modAssetRenderStreamOpIsGeometry(modasset_render_stream_op_t op)
{
	return op == MODASSET_RENDER_OP_GEOMETRY_SET ||
		op == MODASSET_RENDER_OP_GEOMETRY_CLEAR;
}

s32 modAssetRenderStreamPreservesNodeCombine(s32 schema_version)
{
	return schema_version >= MODASSET_RENDER_STREAM_SCHEMA_GEOMETRY &&
		modAssetRenderStreamSchemaSupported(schema_version);
}

s32 modAssetRenderStreamVertexStateValid(u32 mode, u32 known)
{
	return (mode & ~known) == 0;
}

s32 modAssetRenderStreamVertexKnownMatches(u32 established, u32 known)
{
	return (established & MODASSET_RENDER_VERTEX_LOAD_GEOMETRY_MASK) ==
		(known & MODASSET_RENDER_VERTEX_LOAD_GEOMETRY_MASK);
}

s32 modAssetRenderStreamVertexSnapshotMatches(u32 established_mode,
		u32 established_known, u32 snapshot_mode, u32 snapshot_known)
{
	u32 known = snapshot_known & MODASSET_RENDER_VERTEX_LOAD_GEOMETRY_MASK;
	return modAssetRenderStreamVertexKnownMatches(established_known,
			snapshot_known) &&
		(((established_mode ^ snapshot_mode) & known) == 0);
}

s32 modAssetRenderStreamVertexRelocationValid(u32 load_known,
		u32 draw_known, u32 compiler_baseline_known)
{
	/* Flattening emits a fresh G_VTX immediately before its triangle. Every
	 * load-time bit must therefore have a deterministic triangle-time value to
	 * restore, and every triangle-time bit must have a deterministic load-time
	 * value to establish. Type-3's compiler baseline fills only the inherited
	 * lighting/texgen domain it actually owns; caller-owned fog remains unknown
	 * and cannot be moved across a source mutation. */
	u32 load_effective = (load_known | compiler_baseline_known) &
		MODASSET_RENDER_VERTEX_LOAD_GEOMETRY_MASK;
	u32 draw_effective = (draw_known | compiler_baseline_known) &
		MODASSET_RENDER_VERTEX_LOAD_GEOMETRY_MASK;
	return load_effective == draw_effective;
}

s32 modAssetRenderStreamVertexLoadRangeValid(s32 slot_first,
		s32 slot_count)
{
	return slot_first >= 0 && slot_count > 0 &&
		slot_first < MODASSET_RENDER_VERTEX_CACHE_SLOTS &&
		slot_count <= MODASSET_RENDER_VERTEX_CACHE_SLOTS - slot_first;
}

s32 modAssetRenderStreamMatrixIndexValid(s32 matrix_index)
{
	return matrix_index >= 0 &&
		matrix_index <= MODASSET_RENDER_MATRIX_INDEX_MAX;
}

s32 modAssetRenderStreamOptionalMatrixIndexValid(s32 matrix_index)
{
	return matrix_index == -1 ||
		modAssetRenderStreamMatrixIndexValid(matrix_index);
}

s32 modAssetRenderStreamCompilerVertexBaseline(s32 render_mode_type,
	u32 *out_mode, u32 *out_known)
{
	u32 mode = 0;
	u32 known = 0;

	/* The generated Type-3 body prologue deliberately establishes the vertex
	 * interpretation needed by its normal/colour carrier: lighting on and both
	 * texture-generation modes off. Fog remains caller-owned, so it is not safe
	 * to use this baseline to relocate a load across a source G_FOG mutation. */
	if (render_mode_type == 3) {
		mode = G_LIGHTING;
		known = G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR;
	}

	if (out_mode) {
		*out_mode = mode;
	}
	if (out_known) {
		*out_known = known;
	}
	return known != 0;
}

s32 modAssetRenderStreamVertexScopeAllowed(s32 schema_version,
		s32 render_mode_type)
{
	return schema_version == MODASSET_RENDER_STREAM_SCHEMA_CURRENT &&
		modAssetRenderStreamCompilerVertexBaseline(render_mode_type,
			NULL, NULL);
}

s32 modAssetRenderStreamVertexCacheResetAllowed(s32 schema_version,
		s32 render_mode_type)
{
	return schema_version == MODASSET_RENDER_STREAM_SCHEMA_CURRENT &&
		render_mode_type != 3;
}

s32 modAssetRenderStreamTopLevelPairSharesVertexCache(s32 render_mode_type)
{
	/* modelRenderNodeDl/modelRenderNodeGundl submit type-3 opaque and
	 * translucent lists consecutively in one RSP stream. The second list may
	 * consume slots loaded by the first. Type 4 submits them in separate render
	 * passes, so carrying slots there would admit a dependency the renderer does
	 * not provide. */
	return render_mode_type == 3;
}

modasset_render_top_level_walk_disposition_t
modAssetRenderStreamTopLevelWalkDisposition(s32 has_display_list,
	s32 has_vertex_buffer, s32 vertex_count,
	s32 has_resolved_display_list, s32 has_readable_vertex_domain)
{
	/* Native top-level nodes may have no extractor-readable GDL/vertex domain
	 * after preprocessing. This is not malformed recursive input: there is no
	 * admitted address domain in which a GDL can be interpreted. */
	if (!has_display_list || !has_vertex_buffer || vertex_count <= 0 ||
			!has_resolved_display_list || !has_readable_vertex_domain) {
		return MODASSET_RENDER_TOP_LEVEL_WALK_SKIP;
	}
	return MODASSET_RENDER_TOP_LEVEL_WALK_EXECUTE;
}

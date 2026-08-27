#include "catch.hpp"

extern "C" {
#include "modasset_render_stream.h"
}

TEST_CASE("render-stream schema gates typed geometry commands",
	"[modding][model][render-stream][B-1086][B-1106]")
{
	REQUIRE(modAssetRenderStreamSchemaSupported(1) == 1);
	REQUIRE(modAssetRenderStreamSchemaSupported(2) == 1);
	REQUIRE(modAssetRenderStreamSchemaSupported(3) == 1);
	REQUIRE(modAssetRenderStreamSchemaSupported(0) == 0);
	REQUIRE(modAssetRenderStreamSchemaSupported(4) == 0);
	REQUIRE(modAssetRenderStreamResolveSchema(0, 0,
		"pd2.mesh.render.v1") == MODASSET_RENDER_STREAM_SCHEMA_LEGACY);
	REQUIRE(modAssetRenderStreamResolveSchema(0, 0, NULL) == 0);
	REQUIRE(modAssetRenderStreamResolveSchema(0, 0,
		"pd2.mesh.render.v2") == 0);
	REQUIRE(modAssetRenderStreamResolveSchema(1, 2,
		"pd2.mesh.render.v1") == MODASSET_RENDER_STREAM_SCHEMA_GEOMETRY);
	REQUIRE(modAssetRenderStreamResolveSchema(1, 3,
		"pd2.mesh.render.v1") == MODASSET_RENDER_STREAM_SCHEMA_CURRENT);
	REQUIRE(modAssetRenderStreamResolveSchema(1, 4,
		"pd2.mesh.render.v1") == 0);

	REQUIRE(modAssetRenderStreamOpFromText(1, "mtx") ==
		MODASSET_RENDER_OP_MTX);
	REQUIRE(modAssetRenderStreamOpFromText(1, "tri") ==
		MODASSET_RENDER_OP_TRI);
	REQUIRE(modAssetRenderStreamOpFromText(1, "geometry_set") ==
		MODASSET_RENDER_OP_INVALID);
	REQUIRE(modAssetRenderStreamOpFromText(1, "geometry_clear") ==
		MODASSET_RENDER_OP_INVALID);

	REQUIRE(modAssetRenderStreamOpFromText(2, "geometry_set") ==
		MODASSET_RENDER_OP_GEOMETRY_SET);
	REQUIRE(modAssetRenderStreamOpFromText(2, "geometry_clear") ==
		MODASSET_RENDER_OP_GEOMETRY_CLEAR);
	REQUIRE(modAssetRenderStreamOpFromText(2, "vertex_scope") ==
		MODASSET_RENDER_OP_INVALID);
	REQUIRE(modAssetRenderStreamOpFromText(3, "vertex_scope") ==
		MODASSET_RENDER_OP_VERTEX_SCOPE);
	REQUIRE(modAssetRenderStreamOpFromText(3, "vertex_load") ==
		MODASSET_RENDER_OP_VERTEX_LOAD);
	REQUIRE(modAssetRenderStreamOpFromText(3, "vertex_cache_reset") ==
		MODASSET_RENDER_OP_VERTEX_CACHE_RESET);
	REQUIRE(modAssetRenderStreamOpFromText(2, "vertex_load") ==
		MODASSET_RENDER_OP_INVALID);
	REQUIRE(modAssetRenderStreamOpFromText(2, "vertex_cache_reset") ==
		MODASSET_RENDER_OP_INVALID);
	REQUIRE(modAssetRenderStreamOpIsGeometry(
		MODASSET_RENDER_OP_GEOMETRY_SET) == 1);
	REQUIRE(modAssetRenderStreamOpIsGeometry(MODASSET_RENDER_OP_TRI) == 0);
	REQUIRE(modAssetRenderStreamOpIsGeometry(
		MODASSET_RENDER_OP_VERTEX_SCOPE) == 0);
}

TEST_CASE("authoritative geometry source preserves the node combine",
	"[modding][model][render-stream][B-1086]")
{
	REQUIRE(MODASSET_RENDER_VERTEX_LOAD_GEOMETRY_MASK == 0x000f0000u);
	REQUIRE(modAssetRenderStreamPreservesNodeCombine(3) == 1);
	REQUIRE(modAssetRenderStreamPreservesNodeCombine(2) == 1);
	REQUIRE(modAssetRenderStreamPreservesNodeCombine(1) == 0);
	REQUIRE(modAssetRenderStreamPreservesNodeCombine(4) == 0);
}

TEST_CASE("vertex load state is complete without inventing inherited bits",
	"[modding][model][render-stream][B-1086][B-1106]")
{
	const u32 established = G_LIGHTING | G_FOG;
	REQUIRE(modAssetRenderStreamVertexStateValid(G_LIGHTING,
		established) == 1);
	REQUIRE(modAssetRenderStreamVertexStateValid(G_TEXTURE_GEN,
		established) == 0);
	REQUIRE(modAssetRenderStreamVertexKnownMatches(established,
		G_LIGHTING | G_FOG) == 1);
	REQUIRE(modAssetRenderStreamVertexKnownMatches(established,
		G_LIGHTING) == 0);
	REQUIRE(modAssetRenderStreamVertexKnownMatches(0, 0) == 1);

	REQUIRE(modAssetRenderStreamVertexSnapshotMatches(G_LIGHTING,
		established, G_LIGHTING, established) == 1);
	REQUIRE(modAssetRenderStreamVertexSnapshotMatches(G_LIGHTING,
		established, 0, established) == 0);
	REQUIRE(modAssetRenderStreamVertexSnapshotMatches(G_LIGHTING,
		established, G_LIGHTING, G_LIGHTING) == 0);
	REQUIRE(modAssetRenderStreamVertexSnapshotMatches(0, 0,
		0, 0) == 1);

	REQUIRE(modAssetRenderStreamVertexRelocationValid(0, G_LIGHTING,
		G_LIGHTING) == 1);
	REQUIRE(modAssetRenderStreamVertexRelocationValid(G_TEXTURE_GEN, 0,
		G_TEXTURE_GEN) == 1);
	REQUIRE(modAssetRenderStreamVertexRelocationValid(G_FOG, 0,
		G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR) == 0);
	REQUIRE(modAssetRenderStreamVertexRelocationValid(0, G_FOG,
		G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR) == 0);

	REQUIRE(modAssetRenderStreamVertexLoadRangeValid(0, 64) == 1);
	REQUIRE(modAssetRenderStreamVertexLoadRangeValid(63, 1) == 1);
	REQUIRE(modAssetRenderStreamVertexLoadRangeValid(-1, 1) == 0);
	REQUIRE(modAssetRenderStreamVertexLoadRangeValid(0, 0) == 0);
	REQUIRE(modAssetRenderStreamVertexLoadRangeValid(63, 2) == 0);
	REQUIRE(modAssetRenderStreamVertexLoadRangeValid(64, 1) == 0);
	REQUIRE(modAssetRenderStreamMatrixIndexValid(0) == 1);
	REQUIRE(modAssetRenderStreamMatrixIndexValid(
		MODASSET_RENDER_MATRIX_INDEX_MAX) == 1);
	REQUIRE(modAssetRenderStreamMatrixIndexValid(-1) == 0);
	REQUIRE(modAssetRenderStreamMatrixIndexValid(
		MODASSET_RENDER_MATRIX_INDEX_MAX + 1) == 0);
	REQUIRE(modAssetRenderStreamOptionalMatrixIndexValid(-1) == 1);
	REQUIRE(modAssetRenderStreamOptionalMatrixIndexValid(0) == 1);
	REQUIRE(modAssetRenderStreamOptionalMatrixIndexValid(-2) == 0);
	REQUIRE(modAssetRenderStreamOptionalMatrixIndexValid(
		MODASSET_RENDER_MATRIX_INDEX_MAX + 1) == 0);
}

TEST_CASE("Type-3 vertex scope owns one explicit compiler baseline",
	"[modding][model][render-stream][B-1086][B-1106]")
{
	u32 baseline_mode = 0;
	u32 baseline_known = 0;

	REQUIRE(modAssetRenderStreamCompilerVertexBaseline(3, &baseline_mode,
		&baseline_known) == 1);
	REQUIRE(baseline_mode == G_LIGHTING);
	REQUIRE(baseline_known ==
		(G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR));

	/* Other node render modes do not own a replacement vertex baseline. Their
	 * schema-v2 streams therefore retain the original strict global check. */
	REQUIRE(modAssetRenderStreamCompilerVertexBaseline(4, &baseline_mode,
		&baseline_known) == 0);
	REQUIRE(baseline_mode == 0);
	REQUIRE(baseline_known == 0);

	REQUIRE(modAssetRenderStreamVertexScopeAllowed(3, 3) == 1);
	REQUIRE(modAssetRenderStreamVertexScopeAllowed(2, 3) == 0);
	REQUIRE(modAssetRenderStreamVertexScopeAllowed(3, 4) == 0);
	REQUIRE(modAssetRenderStreamVertexScopeAllowed(4, 3) == 0);
	REQUIRE(modAssetRenderStreamVertexCacheResetAllowed(3, 4) == 1);
	REQUIRE(modAssetRenderStreamVertexCacheResetAllowed(3, 1) == 1);
	REQUIRE(modAssetRenderStreamVertexCacheResetAllowed(3, 3) == 0);
	REQUIRE(modAssetRenderStreamVertexCacheResetAllowed(2, 4) == 0);
}

TEST_CASE("G_VTX count comes from the semantic parameter byte",
	"[modding][model][render-stream][B-1086]")
{
	/* These are native ROM commands. Their low-16 DMA lengths use the N64
	 * 16-byte vertex stride, not sizeof(PC Vtx), which is 12 bytes. */
	REQUIRE(sizeof(Vtx) == 12);
	REQUIRE(GBI_VTX_COUNT_FROM_W0(0x04e000f0u) == 15u);
	REQUIRE(GBI_VTX_DEST_FROM_W0(0x04e000f0u) == 0u);
	REQUIRE(GBI_VTX_COUNT_FROM_W0(0x04e000b4u) == 15u);
	REQUIRE(GBI_VTX_DEST_FROM_W0(0x04e000b4u) == 0u);
	REQUIRE(GBI_VTX_COUNT_FROM_W0(0x04370040u) == 4u);
	REQUIRE(GBI_VTX_DEST_FROM_W0(0x04370040u) == 7u);
	REQUIRE(GBI_VTX_COUNT_FROM_W0(0x04f00100u) == 16u);
	REQUIRE((0x04e000f0u & 0xffffu) != (0x04e000b4u & 0xffffu));
}

TEST_CASE("only type-3 paired lists share the live RSP vertex cache",
	"[modding][model][render-stream][B-1086]")
{
	REQUIRE(modAssetRenderStreamTopLevelPairSharesVertexCache(3) == 1);
	REQUIRE(modAssetRenderStreamTopLevelPairSharesVertexCache(1) == 0);
	REQUIRE(modAssetRenderStreamTopLevelPairSharesVertexCache(2) == 0);
	REQUIRE(modAssetRenderStreamTopLevelPairSharesVertexCache(4) == 0);
}

TEST_CASE("top-level zero-triangle admission does not weaken recursive streams",
	"[modding][model][render-stream][B-798][B-1086]")
{
	REQUIRE(modAssetRenderStreamTopLevelWalkDisposition(0, 0, 0, 0, 0) ==
		MODASSET_RENDER_TOP_LEVEL_WALK_SKIP);
	REQUIRE(modAssetRenderStreamTopLevelWalkDisposition(1, 0, 0, 0, 0) ==
		MODASSET_RENDER_TOP_LEVEL_WALK_SKIP);
	REQUIRE(modAssetRenderStreamTopLevelWalkDisposition(1, 0, 12, 1, 0) ==
		MODASSET_RENDER_TOP_LEVEL_WALK_SKIP);
	REQUIRE(modAssetRenderStreamTopLevelWalkDisposition(1, 1, 0, 1, 1) ==
		MODASSET_RENDER_TOP_LEVEL_WALK_SKIP);
	REQUIRE(modAssetRenderStreamTopLevelWalkDisposition(1, 1, -1, 1, 1) ==
		MODASSET_RENDER_TOP_LEVEL_WALK_SKIP);
	REQUIRE(modAssetRenderStreamTopLevelWalkDisposition(0, 0, -1, 0, 0) ==
		MODASSET_RENDER_TOP_LEVEL_WALK_SKIP);
	REQUIRE(modAssetRenderStreamTopLevelWalkDisposition(1, 1, -32768, 1, 1) ==
		MODASSET_RENDER_TOP_LEVEL_WALK_SKIP);
	REQUIRE(modAssetRenderStreamTopLevelWalkDisposition(1, 1, 12, 0, 1) ==
		MODASSET_RENDER_TOP_LEVEL_WALK_SKIP);
	REQUIRE(modAssetRenderStreamTopLevelWalkDisposition(1, 1, 12, 1, 0) ==
		MODASSET_RENDER_TOP_LEVEL_WALK_SKIP);
	REQUIRE(modAssetRenderStreamTopLevelWalkDisposition(1, 1, 12, 1, 1) ==
		MODASSET_RENDER_TOP_LEVEL_WALK_EXECUTE);
}

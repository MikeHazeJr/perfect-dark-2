#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "types.h"
#include "asset_source_harness.h"
#include "assetcatalog.h"
#include "fs.h"
#include "lib/meshcollision.h"
#include "modasset_compiler.h"
#include "scenario_scene_renderer.h"
#include "sha256.h"
#include "smoke_harness.h"
#include "system.h"

/* Expected points below are authored independently of the scene planner. Every
 * case reads actual standard files through the same provider/compiler/renderer
 * calls used by catalog loading. Files stay in the isolated smoke save root. */
static int sourceWrite(const char *path, const void *data, size_t size)
{
	FILE *stream = fsFileOpenWrite(path);
	int ok;
	if (!stream) return 0;
	ok = fwrite(data, 1, size, stream) == size && fflush(stream) == 0;
	if (fclose(stream) != 0) ok = 0;
	return ok;
}

static void sourceU32(unsigned char *dst, unsigned int value)
{
	for (int i = 0; i < 4; i++) dst[i] = (unsigned char)(value >> (8 * i));
}

static int sourceWriteScene(const char *path, const char *scene, int glb,
	float edge, int escaped_keys)
{
	const float positions[9] = {0, 0, 0, edge, 0, 0, 0, 2, 0};
	unsigned char binary[36];
	char json[4096];
	int length;
	for (int i = 0; i < 9; i++) {
		unsigned int bits;
		memcpy(&bits, &positions[i], sizeof(bits));
		sourceU32(binary + i * 4, bits);
	}
	length = snprintf(json, sizeof(json),
		"{\"asset\":{\"version\":\"2.0\"},"
		"\"buffers\":[{\"byteLength\":36%s}],"
		"\"bufferViews\":[{\"buffer\":0,\"byteLength\":36}],"
		"\"accessors\":[{\"bufferView\":0,\"componentType\":5126,"
		"\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[%.9g,2,0]}]%s,"
		"\"%s\":[{\"primitives\":[{\"attributes\":{\"%s\":0}}]},"
		"{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}]}",
		glb ? "" : ",\"uri\":\"geometry.bin\"", (double)edge, scene,
		escaped_keys ? "mes\\u0068es" : "meshes",
		escaped_keys ? "POS\\u0049TION" : "POSITION");
	if (length < 0 || (size_t)length >= sizeof(json)) return 0;
	if (!glb) {
		return sourceWrite("$S/asset-source-smoke/geometry.bin", binary, sizeof(binary))
			&& sourceWrite(path, json, (size_t)length);
	}
	unsigned int padded = ((unsigned int)length + 3u) & ~3u;
	unsigned int total = 12u + 8u + padded + 8u + sizeof(binary);
	unsigned char *bytes = calloc(1, total);
	int ok;
	if (!bytes) return 0;
	sourceU32(bytes, 0x46546c67u);
	sourceU32(bytes + 4, 2);
	sourceU32(bytes + 8, total);
	sourceU32(bytes + 12, padded);
	sourceU32(bytes + 16, 0x4e4f534au);
	memcpy(bytes + 20, json, (size_t)length);
	memset(bytes + 20 + length, ' ', padded - (unsigned int)length);
	sourceU32(bytes + 20 + padded, sizeof(binary));
	sourceU32(bytes + 24 + padded, 0x004e4942u);
	memcpy(bytes + 28 + padded, binary, sizeof(binary));
	ok = sourceWrite(path, bytes, total);
	free(bytes);
	return ok;
}

static int sourceNear(float value, float expected)
{
	return value >= expected - 0.0001f && value <= expected + 0.0001f;
}

static int sourceCheckModel(const struct modeldef *model, const float *expected,
	int vertices)
{
	if (!model || !model->rootnode) return 0;
	/* These static sources produce one render node under an identity root. */
	const struct modelnode *node = model->rootnode->child;
	for (int i = 0; node && i < 16; i++, node = node->next) {
		if ((node->type & 0xff) != MODELNODETYPE_DL) continue;
		const struct modelrodata_dl *dl = &node->rodata->dl;
		if (dl->numvertices != vertices || !dl->vertices) return 0;
		for (int v = 0; v < vertices; v++) {
			if (!sourceNear(dl->vertices[v].x, expected[v * 3])
					|| !sourceNear(dl->vertices[v].y, expected[v * 3 + 1])
					|| !sourceNear(dl->vertices[v].z, expected[v * 3 + 2])) return 0;
		}
		return 1;
	}
	return 0;
}

static int sourceCheckCase(const char *name, const char *scene, int glb,
	float edge, const float *expected, int vertices, int native_model,
	int selected_scene, int instances)
{
	const char *path = glb ? "$S/asset-source-smoke/scene.glb"
		: "$S/asset-source-smoke/scene.gltf";
	struct colmesh collision = {0};
	struct modeldef *model = NULL;
	asset_entry_t entry = {0};
	scenario_scene_renderer_probe_t renderer = {0};
	int collision_ok = 0, model_ok = 0, renderer_ok = 0;
	if (!sourceWriteScene(path, scene, glb, edge, strcmp(name, "escaped_json_keys") == 0)) {
		sysLogPrintf(LOG_ERROR, "ASSET.SOURCE.SCENE: case=%s result=FAIL reason=write", name);
		return 0;
	}
	strcpy(entry.id, "smoke:scene_geometry");
	entry.type = ASSET_PROP;
	int col_result = modAssetCompilerBuildColmesh(path, &collision);
	int model_result = modAssetCompilerBuildModeldef(&entry, path, &model);
	int render_result = scenarioSceneRendererProbeSource(entry.id, path, &renderer);
	if (!expected) {
		collision_ok = col_result == -1 && !collision.tris;
		model_ok = model_result == -1 && !model;
		renderer_ok = render_result == 0;
	} else {
		collision_ok = col_result == 1 && collision.numtris == vertices / 3;
		for (int t = 0; collision_ok && t < collision.numtris; t++) {
			const struct coord *points[3] = {&collision.tris[t].v0,
				&collision.tris[t].v1, &collision.tris[t].v2};
			for (int p = 0; p < 3; p++) {
				int index = (t * 3 + p) * 3;
				collision_ok = collision_ok && sourceNear(points[p]->x, expected[index])
					&& sourceNear(points[p]->y, expected[index + 1])
					&& sourceNear(points[p]->z, expected[index + 2]);
			}
		}
		model_ok = native_model ? model_result == 1
			&& sourceCheckModel(model, expected, vertices) : model_result == -1 && !model;
		float bounds_min[3], bounds_max[3];
		for (int axis = 0; axis < 3; axis++) {
			bounds_min[axis] = bounds_max[axis] = expected[axis];
			for (int v = 1; v < vertices; v++) {
				float coordinate = expected[v * 3 + axis];
				if (coordinate < bounds_min[axis]) bounds_min[axis] = coordinate;
				if (coordinate > bounds_max[axis]) bounds_max[axis] = coordinate;
			}
		}
		renderer_ok = render_result && renderer.vertices == (size_t)vertices
			&& renderer.instances == (size_t)instances && renderer.selected_scene == selected_scene;
		for (int axis = 0; axis < 3; axis++) {
			renderer_ok = renderer_ok && sourceNear(renderer.bounds_min[axis], bounds_min[axis])
				&& sourceNear(renderer.bounds_max[axis], bounds_max[axis]);
		}
		/* Exact byte witness where the authored matrices have exact binary
		 * coordinates; TRS uses tolerance above because quaternion math rounds. */
		if (strcmp(name, "parent_trs") != 0) {
			u8 digest[32];
			char hex[65];
			sha256Hash(expected, (size_t)vertices * 3 * sizeof(float), digest);
			sha256ToHex(digest, hex);
			renderer_ok = renderer_ok && strcmp(renderer.geometry_sha256, hex) == 0;
		}
	}
	meshFree(&collision);
	modAssetCompilerFreeModeldef(model);
	int ok = collision_ok && model_ok && renderer_ok;
	sysLogPrintf(LOG_NOTE,
		"ASSET.SOURCE.SCENE: case=%s result=%s collision=%d model=%d renderer=%d geometry=%s",
		name, ok ? "PASS" : "FAIL", collision_ok, model_ok, renderer_ok,
		renderer.geometry_sha256);
	return ok;
}

static int sourceContains(const void *data, u32 size, const char *text)
{
	size_t length = strlen(text);
	if (!data || length > size) return 0;
	for (size_t i = 0; i <= size - length; i++)
		if (memcmp((const char *)data + i, text, length) == 0) return 1;
	return 0;
}

static int sourceCacheCheck(const char *scene)
{
	const char *path = "$S/asset-source-smoke/scene.gltf";
	const float reversed[9] = {0,0,0, 0,2,0, 2,0,0};
	unsigned char binary[36];
	asset_entry_t entry = {0};
	modasset_compiled_result_t first = {0}, second = {0};
	u8 before_digest[32] = {0}, after_digest[32] = {0};
	u32 before_size = 0, after_size = 0, cache_size = 0;
	void *before = NULL, *after = NULL, *cache = NULL;
	int ok = sourceWriteScene(path, scene, 0, 2, 0);
	strcpy(entry.id, "smoke:scene_cache_source");
	entry.type = ASSET_PROP;
	if (ok) {
		before = fsFileLoad(path, &before_size);
		ok = before && modAssetCompilerCompileReadable(&entry, "collision", path, &first) == 1;
	}
	for (int i = 0; i < 9; i++) {
		unsigned int bits;
		memcpy(&bits, &reversed[i], sizeof(bits));
		sourceU32(binary + i * 4, bits);
	}
	/* Reverse the triangle while retaining exact accessor min/max. Only the
	 * external standard buffer changes; the JSON file is not rewritten. */
	if (ok) ok = sourceWrite("$S/asset-source-smoke/geometry.bin", binary, sizeof(binary));
	if (ok) {
		after = fsFileLoad(path, &after_size);
		ok = after && before_size == after_size && memcmp(before, after, before_size) == 0
			&& modAssetCompilerCompileReadable(&entry, "collision", path, &second) == 1
			&& strcmp(first.source_sha256, second.source_sha256) != 0
			&& strcmp(first.normalized_path, second.normalized_path) != 0;
	}
	if (ok) {
		cache = fsFileLoad(second.normalized_path, &cache_size);
		ok = sourceContains(cache, cache_size, "[10, 20, 30],\n    [10, 22, 30],\n    [12, 20, 30]");
	}
	if (before) sha256Hash(before, before_size, before_digest);
	if (after) sha256Hash(after, after_size, after_digest);
	char json_digest[65];
	sha256ToHex(before_digest, json_digest);
	sysLogPrintf(LOG_NOTE,
		"ASSET.SOURCE.SCENE: case=buffer_only_cache_refresh result=%s json_unchanged=%d json_sha256=%s before=%s after=%s",
		ok ? "PASS" : "FAIL", memcmp(before_digest, after_digest, sizeof(before_digest)) == 0,
		json_digest, first.source_sha256, second.source_sha256);
	free(before); free(after); free(cache);
	return ok;
}

int assetSourceSceneHarnessRun(void)
{
	if (!smokeHarnessIsActive()) return -1;
	if (!fsCreateDir("$S/asset-source-smoke")) return -1;
	const float identity[18] = {0,0,0, 2,0,0, 0,2,0, 0,0,0, 2,0,0, 0,2,0};
	const float translated[9] = {10,20,30, 12,20,30, 10,22,30};
	const float trs[9] = {11,22,33, 11,26,33, 5,22,33};
	const float mirrored[18] = {5,0,0, 7,0,0, 5,2,0, -5,0,0, -5,2,0, -7,0,0};
	const float edited[9] = {10,20,30, 14,20,30, 10,22,30};
	const float too_large[9] = {40000,20,30, 40002,20,30, 40000,22,30};
	const char *selected = ",\"nodes\":[{\"mesh\":0,\"translation\":[1000,0,0]},"
		"{\"mesh\":1,\"translation\":[10,20,30]}],\"scenes\":[{\"nodes\":[0]},{\"nodes\":[1]}],\"scene\":1";
	int passed = 0;
	passed += sourceCheckCase("geometry_only", "", 0, 2, identity, 6, 1, -1, 2);
	passed += sourceCheckCase("selected_scene", selected, 0, 2, translated, 3, 1, 1, 1);
	passed += sourceCheckCase("extras_cannot_shadow_meshes", ",\"extras\":{\"meshes\":[]},"
		"\"nodes\":[{\"mesh\":0,\"translation\":[10,20,30]}]", 0, 2, translated, 3, 1, -1, 1);
	passed += sourceCheckCase("escaped_json_keys", ",\"n\\u006fdes\":[{\"me\\u0073h\":0,"
		"\"transl\\u0061tion\":[10,20,30]}]", 0, 2, translated, 3, 1, -1, 1);
	passed += sourceCheckCase("first_scene", ",\"nodes\":[{\"mesh\":0,\"translation\":[10,20,30]},"
		"{\"mesh\":1}],\"scenes\":[{\"nodes\":[0]},{\"nodes\":[1]}]", 0, 2, translated, 3, 1, 0, 1);
	passed += sourceCheckCase("parent_trs", ",\"nodes\":[{\"children\":[1],\"translation\":[10,20,30]},"
		"{\"mesh\":0,\"translation\":[1,2,3],\"rotation\":[0,0,0.7071067811865476,0.7071067811865476],"
		"\"scale\":[2,3,1]}],\"scenes\":[{\"nodes\":[0]}]", 0, 2, trs, 3, 1, 0, 1);
	passed += sourceCheckCase("matrix_instances_mirrored", ",\"nodes\":[{\"mesh\":0,"
		"\"matrix\":[1,0,0,0,0,1,0,0,0,0,1,0,5,0,0,1]},"
		"{\"mesh\":0,\"translation\":[-5,0,0],\"scale\":[-1,1,1]}]", 0, 2, mirrored, 6, 1, -1, 2);
	passed += sourceCheckCase("glb_scene", selected, 1, 2, translated, 3, 1, 1, 1);
	passed += sourceCheckCase("edited_buffer", selected, 0, 4, edited, 3, 1, 1, 1);
	passed += sourceCheckCase("cycle_rejected", ",\"nodes\":[{\"children\":[1]},{\"mesh\":0,\"children\":[0]}]",
		0, 2, NULL, 0, 0, -1, 0);
	passed += sourceCheckCase("native_position_boundary", ",\"nodes\":[{\"mesh\":0,\"translation\":[40000,20,30]}]",
		0, 2, too_large, 3, 0, -1, 1);
	passed += sourceCacheCheck(selected);
	sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.SCENE: passed=%d cases=12 result=%s",
		passed, passed == 12 ? "PASS" : "FAIL");
	return passed == 12 ? 0 : -1;
}

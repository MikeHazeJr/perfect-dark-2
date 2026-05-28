/**
 * modasset_compiler.c -- private runtime cache adapter for external mod assets.
 *
 * This layer owns cache keys and validation for standard authoring sources such
 * as GLTF/OBJ. It intentionally writes cache descriptors as .pdmc metadata, not
 * author-facing .bin payloads.
 */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "constants.h"
#include "types.h"
#include "data.h"
#include "gbiex.h"
#include "lib/meshcollision.h"
#include "lib/model.h"
#include "modasset_compiler.h"
#include "sha256.h"
#include "system.h"

extern double sqrt(double);
extern double atan2(double, double);
extern double asin(double);

static s32 endsWithNoCase(const char *path, const char *ext)
{
	size_t path_len;
	size_t ext_len;

	if (!path || !ext) {
		return 0;
	}

	path_len = strlen(path);
	ext_len = strlen(ext);
	if (path_len < ext_len) {
		return 0;
	}

	path += path_len - ext_len;
	for (size_t i = 0; i < ext_len; i++) {
		if (tolower((u8)path[i]) != tolower((u8)ext[i])) {
			return 0;
		}
	}
	return 1;
}

static const char *sourceKindForPath(const char *path)
{
	if (endsWithNoCase(path, ".gltf")) {
		return "gltf";
	}
	if (endsWithNoCase(path, ".glb")) {
		return "glb";
	}
	if (endsWithNoCase(path, ".obj")) {
		return "obj";
	}
	return "";
}

static u32 readLe32(const u8 *data);

typedef struct skeleton_symbol_binding {
	const char *symbol;
	struct skeleton *skeleton;
	s16 skeleton_id;
} skeleton_symbol_binding_t;

static const skeleton_symbol_binding_t kSkeletonSymbols[] = {
	{ "SKEL_CHR", &g_SkelChr, 0 },
	{ "SKEL_CLASSICGUN", &g_SkelClassicGun, 0 },
	{ "SKEL_06", &g_Skel06, 0 },
	{ "SKEL_UZI", &g_SkelUzi, 0 },
	{ "SKEL_BASIC", &g_SkelBasic, 0 },
	{ "SKEL_HEAD", NULL, SKEL_HEAD },
	{ "SKEL_CCTV", &g_SkelCctv, 0 },
	{ "SKEL_WINDOWEDDOOR", &g_SkelWindowedDoor, 0 },
	{ "SKEL_11", &g_Skel11, 0 },
	{ "SKEL_12", &g_Skel12, 0 },
	{ "SKEL_13", &g_Skel13, 0 },
	{ "SKEL_TERMINAL", &g_SkelTerminal, 0 },
	{ "SKEL_CIHUB", &g_SkelCiHub, 0 },
	{ "SKEL_AUTOGUN", &g_SkelAutogun, 0 },
	{ "SKEL_17", &g_Skel17, 0 },
	{ "SKEL_18", &g_Skel18, 0 },
	{ "SKEL_19", &g_Skel19, 0 },
	{ "SKEL_0A", &g_Skel0A, 0 },
	{ "SKEL_0B", &g_Skel0B, 0 },
	{ "SKEL_CASING", &g_SkelCasing, 0 },
	{ "SKEL_CHRGUN", &g_SkelChrGun, 0 },
	{ "SKEL_0C", &g_Skel0C, 0 },
	{ "SKEL_JOYPAD", &g_SkelJoypad, 0 },
	{ "SKEL_21", &g_Skel21, 0 },
	{ "SKEL_LIFT", &g_SkelLift, 0 },
	{ "SKEL_SKEDAR", &g_SkelSkedar, 0 },
	{ "SKEL_LOGO", &g_SkelLogo, 0 },
	{ "SKEL_PDLOGO", &g_SkelPdLogo, 0 },
	{ "SKEL_HOVERBIKE", &g_SkelHoverbike, 0 },
	{ "SKEL_JUMPSHIP", &g_SkelJumpship, 0 },
	{ "SKEL_20", &g_Skel20, 0 },
	{ "SKEL_22", &g_Skel22, 0 },
	{ "SKEL_LAPTOPGUN", &g_SkelLaptopGun, 0 },
	{ "SKEL_K7AVENGER", &g_SkelK7Avenger, 0 },
	{ "SKEL_CHOPPER", &g_SkelChopper, 0 },
	{ "SKEL_FALCON2", &g_SkelFalcon2, 0 },
	{ "SKEL_KNIFE", &g_SkelKnife, 0 },
	{ "SKEL_DRCAROLL", &g_SkelDrCaroll, 0 },
	{ "SKEL_ROPE", &g_SkelRope, 0 },
	{ "SKEL_CMP150", &g_SkelCmp150, 0 },
	{ "SKEL_BANNER", &g_SkelBanner, 0 },
	{ "SKEL_DRAGON", &g_SkelDragon, 0 },
	{ "SKEL_SUPERDRAGON", &g_SkelSuperDragon, 0 },
	{ "SKEL_ROCKET", &g_SkelRocket, 0 },
	{ "SKEL_4A", &g_Skel4A, 0 },
	{ "SKEL_SHOTGUN", &g_SkelShotgun, 0 },
	{ "SKEL_FARSIGHT", &g_SkelFarsight, 0 },
	{ "SKEL_4D", &g_Skel4D, 0 },
	{ "SKEL_REAPER", &g_SkelReaper, 0 },
	{ "SKEL_DROPSHIP", &g_SkelDropship, 0 },
	{ "SKEL_MAULER", &g_SkelMauler, 0 },
	{ "SKEL_DEVASTATOR", &g_SkelDevastator, 0 },
	{ "SKEL_ROBOT", &g_SkelRobot, 0 },
	{ "SKEL_PISTOL", &g_SkelPistol, 0 },
	{ "SKEL_AR34", &g_SkelAr34, 0 },
	{ "SKEL_MAGNUM", &g_SkelMagnum, 0 },
	{ "SKEL_SLAYERROCKET", &g_SkelSlayerRocket, 0 },
	{ "SKEL_CYCLONE", &g_SkelCyclone, 0 },
	{ "SKEL_SNIPERRIFLE", &g_SkelSniperRifle, 0 },
	{ "SKEL_TRANQUILIZER", &g_SkelTranquilizer, 0 },
	{ "SKEL_CROSSBOW", &g_SkelCrossbow, 0 },
	{ "SKEL_HUDPIECE", &g_SkelHudPiece, 0 },
	{ "SKEL_TIMEDPROXYMINE", &g_SkelTimedProxyMine, 0 },
	{ "SKEL_PHOENIX", &g_SkelPhoenix, 0 },
	{ "SKEL_CALLISTO", &g_SkelCallisto, 0 },
	{ "SKEL_HAND", &g_SkelHand, 0 },
	{ "SKEL_RCP120", &g_SkelRcp120, 0 },
	{ "SKEL_SKSHUTTLE", &g_SkelSkShuttle, 0 },
	{ "SKEL_LASER", &g_SkelLaser, 0 },
	{ "SKEL_MAIANUFO", &g_SkelMaianUfo, 0 },
	{ "SKEL_GRENADE", &g_SkelGrenade, 0 },
	{ "SKEL_CABLECAR", &g_SkelCableCar, 0 },
	{ "SKEL_SUBMARINE", &g_SkelSubmarine, 0 },
	{ "SKEL_TARGET", &g_SkelTarget, 0 },
	{ "SKEL_ECMMINE", &g_SkelEcmMine, 0 },
	{ "SKEL_UPLINK", &g_SkelUplink, 0 },
	{ "SKEL_RARELOGO", &g_SkelRareLogo, 0 },
	{ "SKEL_WIREFENCE", &g_SkelWireFence, 0 },
	{ "SKEL_REMOTEMINE", &g_SkelRemoteMine, 0 },
	{ "SKEL_BB", &g_SkelBB, 0 },
};

static const char *modAssetCompilerSkeletonSymbolForId(s16 skeleton_id)
{
	for (s32 i = 0; i < (s32)(sizeof(kSkeletonSymbols) / sizeof(kSkeletonSymbols[0])); i++) {
		if ((kSkeletonSymbols[i].skeleton
					&& kSkeletonSymbols[i].skeleton->skel == skeleton_id)
				|| (!kSkeletonSymbols[i].skeleton
					&& kSkeletonSymbols[i].skeleton_id == skeleton_id)) {
			return kSkeletonSymbols[i].symbol;
		}
	}

	return NULL;
}

const char *modAssetCompilerSkeletonSymbolForPointer(
	const struct skeleton *skeleton)
{
	uintptr_t token;

	if (!skeleton) {
		return NULL;
	}

	token = (uintptr_t)skeleton;
	if (token < 0x10000) {
		return modAssetCompilerSkeletonSymbolForId((s16)token);
	}

	for (s32 i = 0; i < (s32)(sizeof(kSkeletonSymbols) / sizeof(kSkeletonSymbols[0])); i++) {
		if (kSkeletonSymbols[i].skeleton == skeleton
				|| (kSkeletonSymbols[i].skeleton
					&& kSkeletonSymbols[i].skeleton->skel == skeleton->skel)) {
			return kSkeletonSymbols[i].symbol;
		}
	}

	return NULL;
}

struct skeleton *modAssetCompilerSkeletonForSymbol(const char *symbol)
{
	if (!symbol || !symbol[0]) {
		return NULL;
	}

	for (s32 i = 0; i < (s32)(sizeof(kSkeletonSymbols) / sizeof(kSkeletonSymbols[0])); i++) {
		if (strcmp(kSkeletonSymbols[i].symbol, symbol) == 0) {
			if (kSkeletonSymbols[i].skeleton) {
				return kSkeletonSymbols[i].skeleton;
			}
			if (kSkeletonSymbols[i].skeleton_id) {
				return (struct skeleton *)(uintptr_t)kSkeletonSymbols[i].skeleton_id;
			}
			return NULL;
		}
	}

	return NULL;
}

static s32 modAssetFloatIsFinite(f32 value)
{
	return value == value
		&& value <= 3.402823466e+38F
		&& value >= -3.402823466e+38F;
}

static s32 assetKindUsesGeneratedModeldef(const char *asset_kind)
{
	if (!asset_kind || !asset_kind[0]) {
		return 0;
	}

	return strcmp(asset_kind, "model") == 0
		|| strcmp(asset_kind, "head") == 0
		|| strcmp(asset_kind, "body") == 0
		|| strcmp(asset_kind, "weapon") == 0
		|| strcmp(asset_kind, "prop") == 0;
}

static s32 assetKindUsesGeneratedAnimationClip(const char *asset_kind)
{
	return asset_kind && strcmp(asset_kind, "animation") == 0;
}

typedef struct obj_vertex {
	f32 x;
	f32 y;
	f32 z;
	s32 roomnum;
} obj_vertex_t;

typedef struct obj_triangle {
	s32 a;
	s32 b;
	s32 c;
	s32 roomnum;
} obj_triangle_t;

typedef struct obj_mesh {
	obj_vertex_t *vertices;
	s32 vertex_count;
	s32 vertex_capacity;
	obj_triangle_t *triangles;
	s32 triangle_count;
	s32 triangle_capacity;
	char error[128];
} obj_mesh_t;

static void objMeshFree(obj_mesh_t *mesh)
{
	if (!mesh) {
		return;
	}
	free(mesh->vertices);
	free(mesh->triangles);
	memset(mesh, 0, sizeof(*mesh));
}

static void objMeshSetError(obj_mesh_t *mesh, s32 line_no, const char *reason)
{
	if (!mesh || mesh->error[0]) {
		return;
	}
	snprintf(mesh->error, sizeof(mesh->error), "line=%d %s",
		line_no, reason ? reason : "invalid_obj");
}

static s32 objMeshGrowVertices(obj_mesh_t *mesh, s32 needed)
{
	s32 newcap;
	obj_vertex_t *newptr;

	if (mesh->vertex_count + needed <= mesh->vertex_capacity) {
		return 1;
	}

	newcap = mesh->vertex_capacity * 2;
	if (newcap < mesh->vertex_count + needed) {
		newcap = mesh->vertex_count + needed;
	}
	if (newcap < 64) {
		newcap = 64;
	}

	newptr = realloc(mesh->vertices, (size_t)newcap * sizeof(*mesh->vertices));
	if (!newptr) {
		objMeshSetError(mesh, 0, "vertex_alloc_failed");
		return 0;
	}

	mesh->vertices = newptr;
	mesh->vertex_capacity = newcap;
	return 1;
}

static s32 objMeshGrowTriangles(obj_mesh_t *mesh, s32 needed)
{
	s32 newcap;
	obj_triangle_t *newptr;

	if (mesh->triangle_count + needed <= mesh->triangle_capacity) {
		return 1;
	}

	newcap = mesh->triangle_capacity * 2;
	if (newcap < mesh->triangle_count + needed) {
		newcap = mesh->triangle_count + needed;
	}
	if (newcap < 64) {
		newcap = 64;
	}

	newptr = realloc(mesh->triangles, (size_t)newcap * sizeof(*mesh->triangles));
	if (!newptr) {
		objMeshSetError(mesh, 0, "triangle_alloc_failed");
		return 0;
	}

	mesh->triangles = newptr;
	mesh->triangle_capacity = newcap;
	return 1;
}

static s32 objMeshAddVertex(obj_mesh_t *mesh, f32 x, f32 y, f32 z)
{
	obj_vertex_t *v;

	if (!objMeshGrowVertices(mesh, 1)) {
		return 0;
	}

	v = &mesh->vertices[mesh->vertex_count++];
	v->x = x;
	v->y = y;
	v->z = z;
	v->roomnum = 0;
	return 1;
}

static s32 objMeshAddTriangle(obj_mesh_t *mesh, s32 a, s32 b, s32 c)
{
	obj_triangle_t *tri;

	if (!objMeshGrowTriangles(mesh, 1)) {
		return 0;
	}

	tri = &mesh->triangles[mesh->triangle_count++];
	tri->a = a;
	tri->b = b;
	tri->c = c;
	tri->roomnum = 0;
	if (a >= 0 && b >= 0 && c >= 0
			&& a < mesh->vertex_count
			&& b < mesh->vertex_count
			&& c < mesh->vertex_count) {
		s32 ar = mesh->vertices[a].roomnum;
		s32 br = mesh->vertices[b].roomnum;
		s32 cr = mesh->vertices[c].roomnum;
		if (ar == br && br == cr) {
			tri->roomnum = ar;
		}
	}
	return 1;
}

static char *skipSpaces(char *p)
{
	while (p && *p && isspace((u8)*p)) {
		p++;
	}
	return p;
}

static s32 parseObjFloat(char **cursor, f32 *out)
{
	char *p;
	char *end;
	double value;

	if (!cursor || !*cursor || !out) {
		return 0;
	}

	p = skipSpaces(*cursor);
	value = strtod(p, &end);
	if (end == p) {
		return 0;
	}

	*out = (f32)value;
	*cursor = end;
	return 1;
}

static s32 parseObjIndexToken(char **cursor, s32 vertex_count, s32 *out_index)
{
	char *p;
	char *end;
	long raw;
	s32 index;

	if (!cursor || !*cursor || !out_index) {
		return 0;
	}

	p = skipSpaces(*cursor);
	raw = strtol(p, &end, 10);
	if (end == p || raw == 0) {
		return 0;
	}

	if (raw > 0) {
		index = (s32)raw - 1;
	} else {
		index = vertex_count + (s32)raw;
	}

	if (index < 0 || index >= vertex_count) {
		return 0;
	}

	while (*end && !isspace((u8)*end)) {
		end++;
	}

	*out_index = index;
	*cursor = end;
	return 1;
}

static s32 parseObjVertexLine(char *line, obj_mesh_t *mesh, s32 line_no)
{
	f32 x;
	f32 y;
	f32 z;
	char *p = line + 1;

	if (!parseObjFloat(&p, &x)
			|| !parseObjFloat(&p, &y)
			|| !parseObjFloat(&p, &z)) {
		objMeshSetError(mesh, line_no, "invalid_vertex");
		return 0;
	}

	return objMeshAddVertex(mesh, x, y, z);
}

static s32 parseObjFaceLine(char *line, obj_mesh_t *mesh, s32 line_no)
{
	char *p = line + 1;
	s32 indices[128];
	s32 count = 0;

	while (1) {
		s32 index;

		p = skipSpaces(p);
		if (!p || *p == '\0' || *p == '#') {
			break;
		}

		if (count >= (s32)(sizeof(indices) / sizeof(indices[0]))) {
			objMeshSetError(mesh, line_no, "face_has_too_many_vertices");
			return 0;
		}

		if (!parseObjIndexToken(&p, mesh->vertex_count, &index)) {
			objMeshSetError(mesh, line_no, "invalid_face_index");
			return 0;
		}

		indices[count++] = index;
	}

	if (count < 3) {
		objMeshSetError(mesh, line_no, "face_needs_three_vertices");
		return 0;
	}

	for (s32 i = 2; i < count; i++) {
		if (!objMeshAddTriangle(mesh, indices[0], indices[i - 1], indices[i])) {
			return 0;
		}
	}

	return 1;
}

static s32 parseObjSource(const u8 *data, u32 size, obj_mesh_t *mesh)
{
	char *text;
	char *line;
	s32 line_no = 1;
	s32 ok = 1;

	if (!data || size == 0 || !mesh) {
		return 0;
	}

	memset(mesh, 0, sizeof(*mesh));
	text = malloc((size_t)size + 1);
	if (!text) {
		objMeshSetError(mesh, 0, "source_alloc_failed");
		return 0;
	}

	memcpy(text, data, size);
	text[size] = '\0';

	line = text;
	while (line && *line) {
		char *end = line;
		char *next = NULL;
		char *p;

		while (*end && *end != '\n' && *end != '\r') {
			end++;
		}
		if (*end) {
			char sep = *end;
			*end = '\0';
			next = end + 1;
			if (sep == '\r' && *next == '\n') {
				next++;
			}
		}

		p = skipSpaces(line);
		if (*p == 'v' && isspace((u8)p[1])) {
			ok = parseObjVertexLine(p, mesh, line_no);
		} else if (*p == 'f' && isspace((u8)p[1])) {
			ok = parseObjFaceLine(p, mesh, line_no);
		}

		if (!ok) {
			break;
		}

		line = next;
		line_no++;
	}

	free(text);

	if (ok && mesh->vertex_count == 0) {
		objMeshSetError(mesh, 0, "no_vertices");
		ok = 0;
	}
	if (ok && mesh->triangle_count == 0) {
		objMeshSetError(mesh, 0, "no_triangles");
		ok = 0;
	}

	return ok;
}

typedef struct json_span {
	const char *start;
	const char *end;
} json_span_t;

typedef struct gltf_buffer_view {
	s32 buffer;
	u32 byte_offset;
	u32 byte_length;
	u32 byte_stride;
} gltf_buffer_view_t;

typedef struct gltf_accessor {
	s32 buffer_view;
	u32 byte_offset;
	s32 component_type;
	s32 count;
	char type[12];
} gltf_accessor_t;

typedef struct gltf_animation_info {
	s32 animation_count;
	s32 channel_count;
	s32 sampler_count;
	s32 frame_count;
	char name[64];
	char error[128];
} gltf_animation_info_t;

typedef enum gltf_anim_path {
	GLTF_ANIM_PATH_TRANSLATION = 0,
	GLTF_ANIM_PATH_ROTATION,
	GLTF_ANIM_PATH_SCALE,
} gltf_anim_path_e;

typedef struct gltf_anim_sampler {
	s32 input;
	s32 output;
	char interpolation[16];
} gltf_anim_sampler_t;

typedef struct gltf_anim_channel {
	s32 target_node;
	gltf_anim_path_e path;
	const u8 *input_data;
	const u8 *output_data;
	u32 input_stride;
	u32 output_stride;
	s32 sample_count;
	s32 translation_base[3];
} gltf_anim_channel_t;

typedef struct gltf_animation_clip {
	gltf_animation_info_t info;
	gltf_anim_channel_t *channels;
	u8 *owned_bin;
	s32 channel_count;
	s32 part_count;
	s32 frame_count;
} gltf_animation_clip_t;

static const char *jsonSkipWs(const char *p, const char *end)
{
	while (p < end && isspace((u8)*p)) {
		p++;
	}
	return p;
}

static const char *jsonSkipString(const char *p, const char *end)
{
	if (p >= end || *p != '"') {
		return p;
	}

	p++;
	while (p < end) {
		if (*p == '\\') {
			p += 2;
			continue;
		}
		if (*p == '"') {
			return p + 1;
		}
		p++;
	}
	return end;
}

static const char *jsonFindMatching(const char *p, const char *end,
                                    char open_ch, char close_ch)
{
	s32 depth = 0;

	while (p < end) {
		if (*p == '"') {
			p = jsonSkipString(p, end);
			continue;
		}
		if (*p == open_ch) {
			depth++;
		} else if (*p == close_ch) {
			depth--;
			if (depth == 0) {
				return p;
			}
		}
		p++;
	}
	return NULL;
}

static const char *jsonValueEnd(const char *p, const char *end)
{
	p = jsonSkipWs(p, end);
	if (p >= end) {
		return end;
	}
	if (*p == '"') {
		return jsonSkipString(p, end);
	}
	if (*p == '{') {
		const char *m = jsonFindMatching(p, end, '{', '}');
		return m ? m + 1 : end;
	}
	if (*p == '[') {
		const char *m = jsonFindMatching(p, end, '[', ']');
		return m ? m + 1 : end;
	}
	while (p < end && *p != ',' && *p != '}' && *p != ']') {
		p++;
	}
	return p;
}

static const char *jsonFindKeyInSpan(json_span_t span, const char *key)
{
	size_t key_len;
	const char *p;

	if (!key) {
		return NULL;
	}

	key_len = strlen(key);
	p = span.start;
	while (p < span.end) {
		if (*p == '"') {
			const char *str_start = p + 1;
			const char *str_end = jsonSkipString(p, span.end);
			const char *q = str_end;

			if (str_end > str_start
					&& (size_t)(str_end - str_start - 1) == key_len
					&& memcmp(str_start, key, key_len) == 0) {
				q = jsonSkipWs(q, span.end);
				if (q < span.end && *q == ':') {
					return q + 1;
				}
			}
			p = str_end;
			continue;
		}
		p++;
	}
	return NULL;
}

static s32 jsonReadArrayValue(const char *value, const char *end,
                              json_span_t *out)
{
	const char *p;
	const char *m;

	if (!value || !out) {
		return 0;
	}

	p = jsonSkipWs(value, end);
	if (p >= end || *p != '[') {
		return 0;
	}

	m = jsonFindMatching(p, end, '[', ']');
	if (!m) {
		return 0;
	}

	out->start = p + 1;
	out->end = m;
	return 1;
}

static s32 jsonReadObjectValue(const char *value, const char *end,
                               json_span_t *out)
{
	const char *p;
	const char *m;

	if (!value || !out) {
		return 0;
	}

	p = jsonSkipWs(value, end);
	if (p >= end || *p != '{') {
		return 0;
	}

	m = jsonFindMatching(p, end, '{', '}');
	if (!m) {
		return 0;
	}

	out->start = p + 1;
	out->end = m;
	return 1;
}

static s32 jsonObjectArray(json_span_t object, const char *key,
                           json_span_t *out)
{
	const char *value = jsonFindKeyInSpan(object, key);
	return jsonReadArrayValue(value, object.end, out);
}

static s32 jsonObjectObject(json_span_t object, const char *key,
                            json_span_t *out)
{
	const char *value = jsonFindKeyInSpan(object, key);
	return jsonReadObjectValue(value, object.end, out);
}

static s32 jsonObjectInt(json_span_t object, const char *key, s32 *out)
{
	const char *value = jsonFindKeyInSpan(object, key);
	char *endptr;
	long parsed;

	if (!value || !out) {
		return 0;
	}

	value = jsonSkipWs(value, object.end);
	parsed = strtol(value, &endptr, 10);
	if (endptr == value) {
		return 0;
	}

	*out = (s32)parsed;
	return 1;
}

static s32 jsonObjectString(json_span_t object, const char *key,
                            char *out, size_t out_len)
{
	const char *value = jsonFindKeyInSpan(object, key);
	const char *p;
	size_t written = 0;

	if (!value || !out || out_len == 0) {
		return 0;
	}

	p = jsonSkipWs(value, object.end);
	if (p >= object.end || *p != '"') {
		return 0;
	}

	p++;
	while (p < object.end && *p != '"') {
		char c = *p++;
		if (c == '\\' && p < object.end) {
			c = *p++;
		}
		if (written + 1 < out_len) {
			out[written++] = c;
		}
	}
	out[written] = '\0';
	return p < object.end && *p == '"';
}

static s32 jsonArrayNextObject(json_span_t array, const char **cursor,
                               json_span_t *out)
{
	const char *p;

	if (!cursor || !out) {
		return 0;
	}

	p = *cursor ? *cursor : array.start;
	while (p < array.end) {
		p = jsonSkipWs(p, array.end);
		if (p < array.end && *p == ',') {
			p++;
			continue;
		}
		if (p >= array.end) {
			break;
		}
		if (*p == '{') {
			const char *m = jsonFindMatching(p, array.end, '{', '}');
			if (!m) {
				return 0;
			}
			out->start = p + 1;
			out->end = m;
			*cursor = m + 1;
			return 1;
		}
		p = jsonValueEnd(p, array.end);
	}

	*cursor = array.end;
	return 0;
}

static s32 jsonArrayObjectCount(json_span_t array)
{
	const char *cursor = NULL;
	json_span_t object;
	s32 count = 0;

	while (jsonArrayNextObject(array, &cursor, &object)) {
		count++;
	}

	return count;
}

static s32 base64Value(char c)
{
	if (c >= 'A' && c <= 'Z') {
		return c - 'A';
	}
	if (c >= 'a' && c <= 'z') {
		return c - 'a' + 26;
	}
	if (c >= '0' && c <= '9') {
		return c - '0' + 52;
	}
	if (c == '+') {
		return 62;
	}
	if (c == '/') {
		return 63;
	}
	return -1;
}

static u8 *base64DecodeAlloc(const char *src, size_t len, u32 *out_size)
{
	u8 *out;
	size_t out_cap;
	size_t out_len = 0;
	s32 val = 0;
	s32 valb = -8;

	if (out_size) {
		*out_size = 0;
	}
	if (!src || len == 0) {
		return NULL;
	}

	out_cap = (len / 4 + 1) * 3;
	out = malloc(out_cap);
	if (!out) {
		return NULL;
	}

	for (size_t i = 0; i < len; i++) {
		s32 decoded;
		char c = src[i];

		if (c == '=') {
			break;
		}
		if (isspace((u8)c)) {
			continue;
		}

		decoded = base64Value(c);
		if (decoded < 0) {
			free(out);
			return NULL;
		}

		val = (val << 6) | decoded;
		valb += 6;
		if (valb >= 0) {
			if (out_len >= out_cap) {
				free(out);
				return NULL;
			}
			out[out_len++] = (u8)((val >> valb) & 0xff);
			valb -= 8;
		}
	}

	if (out_size) {
		*out_size = (u32)out_len;
	}
	return out;
}

static u8 *decodeGltfDataUri(const char *uri, u32 *out_size)
{
	const char *comma;
	const char *base64_tag;

	if (out_size) {
		*out_size = 0;
	}
	if (!uri || strncmp(uri, "data:", 5) != 0) {
		return NULL;
	}

	comma = strchr(uri, ',');
	if (!comma) {
		return NULL;
	}

	base64_tag = strstr(uri, ";base64");
	if (!base64_tag || base64_tag > comma) {
		return NULL;
	}

	return base64DecodeAlloc(comma + 1, strlen(comma + 1), out_size);
}

static void gltfFreeParsed(gltf_buffer_view_t *views,
                           gltf_accessor_t *accessors)
{
	free(views);
	free(accessors);
}

static s32 gltfAppendBufferView(gltf_buffer_view_t **views, s32 *count,
                                const gltf_buffer_view_t *view)
{
	gltf_buffer_view_t *new_views;

	new_views = realloc(*views, (size_t)(*count + 1) * sizeof(**views));
	if (!new_views) {
		return 0;
	}

	*views = new_views;
	(*views)[*count] = *view;
	(*count)++;
	return 1;
}

static s32 gltfAppendAccessor(gltf_accessor_t **accessors, s32 *count,
                              const gltf_accessor_t *accessor)
{
	gltf_accessor_t *new_accessors;

	new_accessors = realloc(*accessors,
		(size_t)(*count + 1) * sizeof(**accessors));
	if (!new_accessors) {
		return 0;
	}

	*accessors = new_accessors;
	(*accessors)[*count] = *accessor;
	(*count)++;
	return 1;
}

static s32 parseGltfBufferViews(json_span_t root,
                                gltf_buffer_view_t **out_views,
                                s32 *out_count)
{
	json_span_t views_array;
	json_span_t object;
	const char *cursor = NULL;

	*out_views = NULL;
	*out_count = 0;

	if (!jsonObjectArray(root, "bufferViews", &views_array)) {
		return 0;
	}

	while (jsonArrayNextObject(views_array, &cursor, &object)) {
		gltf_buffer_view_t view;
		s32 value;

		memset(&view, 0, sizeof(view));
		view.buffer = 0;

		if (jsonObjectInt(object, "buffer", &value)) {
			view.buffer = value;
		}
		if (jsonObjectInt(object, "byteOffset", &value) && value > 0) {
			view.byte_offset = (u32)value;
		}
		if (!jsonObjectInt(object, "byteLength", &value) || value <= 0) {
			gltfFreeParsed(*out_views, NULL);
			*out_views = NULL;
			*out_count = 0;
			return 0;
		}
		view.byte_length = (u32)value;
		if (jsonObjectInt(object, "byteStride", &value) && value > 0) {
			view.byte_stride = (u32)value;
		}

		if (!gltfAppendBufferView(out_views, out_count, &view)) {
			gltfFreeParsed(*out_views, NULL);
			*out_views = NULL;
			*out_count = 0;
			return 0;
		}
	}

	return *out_count > 0;
}

static s32 parseGltfAccessors(json_span_t root,
                              gltf_accessor_t **out_accessors,
                              s32 *out_count)
{
	json_span_t accessors_array;
	json_span_t object;
	const char *cursor = NULL;

	*out_accessors = NULL;
	*out_count = 0;

	if (!jsonObjectArray(root, "accessors", &accessors_array)) {
		return 0;
	}

	while (jsonArrayNextObject(accessors_array, &cursor, &object)) {
		gltf_accessor_t accessor;
		s32 value;

		memset(&accessor, 0, sizeof(accessor));
		accessor.buffer_view = -1;

		if (!jsonObjectInt(object, "bufferView", &value)) {
			gltfFreeParsed(NULL, *out_accessors);
			*out_accessors = NULL;
			*out_count = 0;
			return 0;
		}
		accessor.buffer_view = value;
		if (jsonObjectInt(object, "byteOffset", &value) && value > 0) {
			accessor.byte_offset = (u32)value;
		}
		if (!jsonObjectInt(object, "componentType", &value)) {
			gltfFreeParsed(NULL, *out_accessors);
			*out_accessors = NULL;
			*out_count = 0;
			return 0;
		}
		accessor.component_type = value;
		if (!jsonObjectInt(object, "count", &value) || value <= 0) {
			gltfFreeParsed(NULL, *out_accessors);
			*out_accessors = NULL;
			*out_count = 0;
			return 0;
		}
		accessor.count = value;
		if (!jsonObjectString(object, "type", accessor.type,
				sizeof(accessor.type))) {
			gltfFreeParsed(NULL, *out_accessors);
			*out_accessors = NULL;
			*out_count = 0;
			return 0;
		}

		if (!gltfAppendAccessor(out_accessors, out_count, &accessor)) {
			gltfFreeParsed(NULL, *out_accessors);
			*out_accessors = NULL;
			*out_count = 0;
			return 0;
		}
	}

	return *out_count > 0;
}

static u32 gltfComponentSize(s32 component_type)
{
	switch (component_type) {
	case 5120:
	case 5121:
		return 1;
	case 5122:
	case 5123:
		return 2;
	case 5125:
	case 5126:
		return 4;
	default:
		return 0;
	}
}

static u32 gltfTypeComponentCount(const char *type)
{
	if (strcmp(type, "SCALAR") == 0) {
		return 1;
	}
	if (strcmp(type, "VEC2") == 0) {
		return 2;
	}
	if (strcmp(type, "VEC3") == 0) {
		return 3;
	}
	if (strcmp(type, "VEC4") == 0) {
		return 4;
	}
	return 0;
}

static f32 readLeFloat(const u8 *data)
{
	u32 raw = readLe32(data);
	f32 value;
	memcpy(&value, &raw, sizeof(value));
	return value;
}

static s32 gltfAccessorData(const gltf_accessor_t *accessor,
                            const gltf_buffer_view_t *views,
                            s32 view_count,
                            const u8 *bin,
                            u32 bin_size,
                            const u8 **out_data,
                            u32 *out_stride,
                            u32 *out_element_size)
{
	const gltf_buffer_view_t *view;
	u32 component_size;
	u32 component_count;
	u32 element_size;
	u32 stride;
	u32 start;
	u32 last_offset;

	if (!accessor || !views || !bin || !out_data || !out_stride
			|| !out_element_size) {
		return 0;
	}
	if (accessor->buffer_view < 0 || accessor->buffer_view >= view_count) {
		return 0;
	}

	view = &views[accessor->buffer_view];
	if (view->buffer != 0) {
		return 0;
	}

	component_size = gltfComponentSize(accessor->component_type);
	component_count = gltfTypeComponentCount(accessor->type);
	if (component_size == 0 || component_count == 0) {
		return 0;
	}

	element_size = component_size * component_count;
	stride = view->byte_stride ? view->byte_stride : element_size;
	if (stride < element_size || accessor->count <= 0) {
		return 0;
	}

	if (view->byte_offset > bin_size || view->byte_length > bin_size
			|| view->byte_offset + view->byte_length > bin_size) {
		return 0;
	}
	if (accessor->byte_offset > view->byte_length) {
		return 0;
	}

	start = view->byte_offset + accessor->byte_offset;
	last_offset = accessor->byte_offset
		+ (u32)(accessor->count - 1) * stride + element_size;
	if (last_offset > view->byte_length || start + element_size > bin_size) {
		return 0;
	}

	*out_data = bin + start;
	*out_stride = stride;
	*out_element_size = element_size;
	return 1;
}

static f32 clampfLocal(f32 value, f32 lo, f32 hi)
{
	if (value < lo) {
		return lo;
	}
	if (value > hi) {
		return hi;
	}
	return value;
}

static s32 floatToMilliS32(f32 value)
{
	f32 scaled = value * 1000.0f;

	if (scaled >= 2147483000.0f) {
		return 2147483000;
	}
	if (scaled <= -2147483000.0f) {
		return -2147483000;
	}
	return (s32)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

static void writeBe32(u8 *dst, u32 value)
{
	dst[0] = (u8)((value >> 24) & 0xff);
	dst[1] = (u8)((value >> 16) & 0xff);
	dst[2] = (u8)((value >> 8) & 0xff);
	dst[3] = (u8)(value & 0xff);
}

static void writeBeFloat(u8 *dst, f32 value)
{
	u32 raw;
	memcpy(&raw, &value, sizeof(raw));
	writeBe32(dst, raw);
}

static f32 gltfChannelReadFloat(const gltf_anim_channel_t *channel,
                                s32 sample_index,
                                s32 component)
{
	const u8 *p;

	if (!channel || !channel->output_data || sample_index < 0
			|| sample_index >= channel->sample_count || component < 0) {
		return 0.0f;
	}

	p = channel->output_data + (u32)sample_index * channel->output_stride
		+ (u32)component * 4u;
	return readLeFloat(p);
}

static void quatToEuler(f32 x, f32 y, f32 z, f32 w,
                        f32 *out_x, f32 *out_y, f32 *out_z)
{
	f32 norm = x * x + y * y + z * z + w * w;
	f32 sinr_cosp;
	f32 cosr_cosp;
	f32 sinp;
	f32 siny_cosp;
	f32 cosy_cosp;

	if (norm > 0.000001f) {
		f32 inv = (f32)(1.0 / sqrt((double)norm));
		x *= inv;
		y *= inv;
		z *= inv;
		w *= inv;
	} else {
		x = y = z = 0.0f;
		w = 1.0f;
	}

	sinr_cosp = 2.0f * (w * x + y * z);
	cosr_cosp = 1.0f - 2.0f * (x * x + y * y);
	sinp = 2.0f * (w * y - z * x);
	siny_cosp = 2.0f * (w * z + x * y);
	cosy_cosp = 1.0f - 2.0f * (y * y + z * z);

	*out_x = (f32)atan2((double)sinr_cosp, (double)cosr_cosp);
	*out_y = (f32)asin((double)clampfLocal(sinp, -1.0f, 1.0f));
	*out_z = (f32)atan2((double)siny_cosp, (double)cosy_cosp);
}

static s32 gltfAppendPositions(const gltf_accessor_t *accessor,
                               const gltf_buffer_view_t *views,
                               s32 view_count,
                               const u8 *bin,
                               u32 bin_size,
                               obj_mesh_t *mesh,
                               s32 *out_base,
                               s32 *out_count)
{
	const u8 *data;
	u32 stride;
	u32 element_size;
	s32 base;

	if (!accessor || !mesh || !out_base || !out_count) {
		return 0;
	}
	if (accessor->component_type != 5126
			|| strcmp(accessor->type, "VEC3") != 0) {
		objMeshSetError(mesh, 0, "gltf_position_accessor_must_be_float_vec3");
		return 0;
	}
	if (!gltfAccessorData(accessor, views, view_count, bin, bin_size,
			&data, &stride, &element_size)) {
		objMeshSetError(mesh, 0, "gltf_position_accessor_out_of_bounds");
		return 0;
	}

	base = mesh->vertex_count;
	for (s32 i = 0; i < accessor->count; i++) {
		const u8 *p = data + (u32)i * stride;
		if (!objMeshAddVertex(mesh,
				readLeFloat(p + 0),
				readLeFloat(p + 4),
				readLeFloat(p + 8))) {
			return 0;
		}
	}

	(void)element_size;
	*out_base = base;
	*out_count = accessor->count;
	return 1;
}

static s32 gltfReadIndex(const u8 *p, s32 component_type, u32 *out);

static s32 gltfApplyRoomTags(const gltf_accessor_t *accessor,
                             const gltf_buffer_view_t *views,
                             s32 view_count,
                             const u8 *bin,
                             u32 bin_size,
                             obj_mesh_t *mesh,
                             s32 vertex_base,
                             s32 vertex_count)
{
	const u8 *data;
	u32 stride;
	u32 element_size;

	if (!accessor || !mesh || vertex_base < 0 || vertex_count <= 0) {
		return 0;
	}
	if (strcmp(accessor->type, "SCALAR") != 0
			|| (accessor->component_type != 5121
				&& accessor->component_type != 5123
				&& accessor->component_type != 5125
				&& accessor->component_type != 5126)) {
		objMeshSetError(mesh, 0, "gltf_room_accessor_must_be_scalar");
		return 0;
	}
	if (accessor->count != vertex_count) {
		objMeshSetError(mesh, 0, "gltf_room_accessor_count_mismatch");
		return 0;
	}
	if (!gltfAccessorData(accessor, views, view_count, bin, bin_size,
			&data, &stride, &element_size)) {
		objMeshSetError(mesh, 0, "gltf_room_accessor_out_of_bounds");
		return 0;
	}

	for (s32 i = 0; i < accessor->count; i++) {
		const u8 *p = data + (u32)i * stride;
		u32 room = 0;
		if (accessor->component_type == 5126) {
			f32 value = readLeFloat(p);
			if (value < 0.0f || value > 32767.0f) {
				objMeshSetError(mesh, 0, "gltf_room_value_out_of_range");
				return 0;
			}
			room = (u32)(value + 0.5f);
		} else if (!gltfReadIndex(p, accessor->component_type, &room)) {
			objMeshSetError(mesh, 0, "gltf_room_decode_failed");
			return 0;
		}
		if (room > 32767u || vertex_base + i >= mesh->vertex_count) {
			objMeshSetError(mesh, 0, "gltf_room_value_out_of_range");
			return 0;
		}
		mesh->vertices[vertex_base + i].roomnum = (s32)room;
	}

	(void)element_size;
	return 1;
}

static s32 gltfReadIndex(const u8 *p, s32 component_type, u32 *out)
{
	if (!p || !out) {
		return 0;
	}

	switch (component_type) {
	case 5121:
		*out = p[0];
		return 1;
	case 5123:
		*out = (u32)p[0] | ((u32)p[1] << 8);
		return 1;
	case 5125:
		*out = readLe32(p);
		return 1;
	default:
		return 0;
	}
}

static s32 gltfAppendIndexedTriangles(const gltf_accessor_t *accessor,
                                      const gltf_buffer_view_t *views,
                                      s32 view_count,
                                      const u8 *bin,
                                      u32 bin_size,
                                      obj_mesh_t *mesh,
                                      s32 vertex_base,
                                      s32 vertex_count)
{
	const u8 *data;
	u32 stride;
	u32 element_size;

	if (!accessor || !mesh) {
		return 0;
	}
	if (strcmp(accessor->type, "SCALAR") != 0
			|| (accessor->component_type != 5121
				&& accessor->component_type != 5123
				&& accessor->component_type != 5125)) {
		objMeshSetError(mesh, 0, "gltf_indices_must_be_unsigned_scalar");
		return 0;
	}
	if ((accessor->count % 3) != 0) {
		objMeshSetError(mesh, 0, "gltf_triangle_indices_not_multiple_of_three");
		return 0;
	}
	if (!gltfAccessorData(accessor, views, view_count, bin, bin_size,
			&data, &stride, &element_size)) {
		objMeshSetError(mesh, 0, "gltf_index_accessor_out_of_bounds");
		return 0;
	}

	for (s32 i = 0; i < accessor->count; i += 3) {
		u32 ia;
		u32 ib;
		u32 ic;
		if (!gltfReadIndex(data + (u32)(i + 0) * stride,
				accessor->component_type, &ia)
				|| !gltfReadIndex(data + (u32)(i + 1) * stride,
					accessor->component_type, &ib)
				|| !gltfReadIndex(data + (u32)(i + 2) * stride,
					accessor->component_type, &ic)) {
			objMeshSetError(mesh, 0, "gltf_index_decode_failed");
			return 0;
		}
		if (ia >= (u32)vertex_count || ib >= (u32)vertex_count
				|| ic >= (u32)vertex_count) {
			objMeshSetError(mesh, 0, "gltf_index_out_of_range");
			return 0;
		}
		if (!objMeshAddTriangle(mesh,
				vertex_base + (s32)ia,
				vertex_base + (s32)ib,
				vertex_base + (s32)ic)) {
			return 0;
		}
	}

	(void)element_size;
	return 1;
}

static s32 gltfAppendSequentialTriangles(obj_mesh_t *mesh,
                                         s32 vertex_base,
                                         s32 vertex_count)
{
	if ((vertex_count % 3) != 0) {
		objMeshSetError(mesh, 0, "gltf_unindexed_triangles_not_multiple_of_three");
		return 0;
	}

	for (s32 i = 0; i < vertex_count; i += 3) {
		if (!objMeshAddTriangle(mesh,
				vertex_base + i,
				vertex_base + i + 1,
				vertex_base + i + 2)) {
			return 0;
		}
	}
	return 1;
}

static s32 parseGltfMeshFromJson(const char *json,
                                 u32 json_size,
                                 const u8 *bin,
                                 u32 bin_size,
                                 obj_mesh_t *mesh)
{
	json_span_t root;
	json_span_t meshes_array;
	json_span_t mesh_object;
	const char *mesh_cursor = NULL;
	gltf_buffer_view_t *views = NULL;
	gltf_accessor_t *accessors = NULL;
	s32 view_count = 0;
	s32 accessor_count = 0;
	s32 ok = 1;

	if (!json || json_size == 0 || !bin || bin_size == 0 || !mesh) {
		return 0;
	}

	memset(mesh, 0, sizeof(*mesh));
	root.start = json;
	root.end = json + json_size;

	if (!parseGltfBufferViews(root, &views, &view_count)
			|| !parseGltfAccessors(root, &accessors, &accessor_count)
			|| !jsonObjectArray(root, "meshes", &meshes_array)) {
		objMeshSetError(mesh, 0, "gltf_missing_mesh_buffers_or_accessors");
		gltfFreeParsed(views, accessors);
		return 0;
	}

	while (ok && jsonArrayNextObject(meshes_array, &mesh_cursor, &mesh_object)) {
		json_span_t primitives_array;
		json_span_t primitive;
		const char *primitive_cursor = NULL;

		if (!jsonObjectArray(mesh_object, "primitives", &primitives_array)) {
			continue;
		}

		while (ok && jsonArrayNextObject(primitives_array,
				&primitive_cursor, &primitive)) {
			json_span_t attributes;
			s32 mode = 4;
			s32 position_accessor_index;
			s32 room_accessor_index = -1;
			s32 indices_accessor_index = -1;
			s32 vertex_base = 0;
			s32 vertex_count = 0;

			jsonObjectInt(primitive, "mode", &mode);
			if (mode != 4) {
				objMeshSetError(mesh, 0, "gltf_only_triangle_primitives_supported");
				ok = 0;
				break;
			}

			if (!jsonObjectObject(primitive, "attributes", &attributes)
					|| !jsonObjectInt(attributes, "POSITION",
						&position_accessor_index)
					|| position_accessor_index < 0
					|| position_accessor_index >= accessor_count) {
				objMeshSetError(mesh, 0, "gltf_primitive_missing_position");
				ok = 0;
				break;
			}

			if (!gltfAppendPositions(&accessors[position_accessor_index],
					views, view_count, bin, bin_size, mesh,
					&vertex_base, &vertex_count)) {
				ok = 0;
				break;
			}

			if (jsonObjectInt(attributes, "_PD_ROOM", &room_accessor_index)) {
				if (room_accessor_index < 0
						|| room_accessor_index >= accessor_count) {
					objMeshSetError(mesh, 0, "gltf_room_accessor_out_of_range");
					ok = 0;
					break;
				}
				if (!gltfApplyRoomTags(&accessors[room_accessor_index],
						views, view_count, bin, bin_size, mesh,
						vertex_base, vertex_count)) {
					ok = 0;
					break;
				}
			}

			if (jsonObjectInt(primitive, "indices", &indices_accessor_index)) {
				if (indices_accessor_index < 0
						|| indices_accessor_index >= accessor_count) {
					objMeshSetError(mesh, 0, "gltf_indices_accessor_out_of_range");
					ok = 0;
					break;
				}
				ok = gltfAppendIndexedTriangles(&accessors[indices_accessor_index],
					views, view_count, bin, bin_size, mesh,
					vertex_base, vertex_count);
			} else {
				ok = gltfAppendSequentialTriangles(mesh, vertex_base, vertex_count);
			}
		}
	}

	if (ok && mesh->triangle_count == 0) {
		objMeshSetError(mesh, 0, "gltf_no_triangle_primitives");
		ok = 0;
	}

	gltfFreeParsed(views, accessors);
	if (!ok) {
		objMeshFree(mesh);
	}
	return ok;
}

static s32 parseGltfTextMesh(const u8 *data, u32 size, obj_mesh_t *mesh)
{
	char *json;
	json_span_t root;
	json_span_t buffers_array;
	json_span_t buffer_object;
	const char *cursor = NULL;
	char uri[4096];
	u8 *bin;
	u32 bin_size = 0;
	s32 ok;

	if (!data || size == 0 || !mesh) {
		return 0;
	}

	json = malloc((size_t)size + 1);
	if (!json) {
		objMeshSetError(mesh, 0, "gltf_json_alloc_failed");
		return 0;
	}
	memcpy(json, data, size);
	json[size] = '\0';

	root.start = json;
	root.end = json + size;
	if (!jsonObjectArray(root, "buffers", &buffers_array)
			|| !jsonArrayNextObject(buffers_array, &cursor, &buffer_object)
			|| !jsonObjectString(buffer_object, "uri", uri, sizeof(uri))) {
		free(json);
		objMeshSetError(mesh, 0, "gltf_missing_embedded_buffer_uri");
		return 0;
	}

	bin = decodeGltfDataUri(uri, &bin_size);
	if (!bin || bin_size == 0) {
		free(json);
		if (bin) {
			free(bin);
		}
		objMeshSetError(mesh, 0, "gltf_external_binary_buffers_are_not_allowed");
		return 0;
	}

	ok = parseGltfMeshFromJson(json, size, bin, bin_size, mesh);
	free(bin);
	free(json);
	return ok;
}

static s32 parseGlbMesh(const u8 *data, u32 size, obj_mesh_t *mesh)
{
	u32 declared_len;
	u32 offset = 12;
	char *json = NULL;
	u32 json_size = 0;
	const u8 *bin = NULL;
	u32 bin_size = 0;
	s32 ok;

	if (!data || size < 20 || !mesh) {
		return 0;
	}
	if (memcmp(data, "glTF", 4) != 0 || readLe32(data + 4) < 2) {
		objMeshSetError(mesh, 0, "glb_invalid_header");
		return 0;
	}

	declared_len = readLe32(data + 8);
	if (declared_len > size) {
		objMeshSetError(mesh, 0, "glb_declared_size_exceeds_source");
		return 0;
	}

	while (offset + 8 <= declared_len) {
		u32 chunk_len = readLe32(data + offset);
		u32 chunk_type = readLe32(data + offset + 4);
		const u8 *chunk_data = data + offset + 8;

		offset += 8;
		if (chunk_len > declared_len || offset + chunk_len > declared_len) {
			objMeshSetError(mesh, 0, "glb_chunk_out_of_bounds");
			free(json);
			return 0;
		}

		if (chunk_type == 0x4e4f534a) {
			free(json);
			json = malloc((size_t)chunk_len + 1);
			if (!json) {
				objMeshSetError(mesh, 0, "glb_json_alloc_failed");
				return 0;
			}
			memcpy(json, chunk_data, chunk_len);
			json[chunk_len] = '\0';
			json_size = chunk_len;
		} else if (chunk_type == 0x004e4942) {
			bin = chunk_data;
			bin_size = chunk_len;
		}

		offset += (chunk_len + 3u) & ~3u;
	}

	if (!json || !bin || bin_size == 0) {
		objMeshSetError(mesh, 0, "glb_missing_json_or_binary_chunk");
		free(json);
		return 0;
	}

	ok = parseGltfMeshFromJson(json, json_size, bin, bin_size, mesh);
	free(json);
	return ok;
}

static void gltfAnimationSetError(gltf_animation_info_t *info, const char *error)
{
	if (!info || info->error[0]) {
		return;
	}
	snprintf(info->error, sizeof(info->error), "%s", error ? error : "parse_failed");
}

static s32 gltfValidateTextBuffersAreEmbedded(json_span_t root,
                                              gltf_animation_info_t *info)
{
	json_span_t buffers_array;
	json_span_t buffer_object;
	const char *cursor = NULL;

	if (!jsonObjectArray(root, "buffers", &buffers_array)) {
		return 1;
	}

	while (jsonArrayNextObject(buffers_array, &cursor, &buffer_object)) {
		char uri[192] = {0};

		if (!jsonObjectString(buffer_object, "uri", uri, sizeof(uri))) {
			gltfAnimationSetError(info, "gltf_text_buffer_missing_data_uri");
			return 0;
		}
		if (strncmp(uri, "data:", 5) != 0) {
			gltfAnimationSetError(info, "gltf_external_binary_buffers_are_not_allowed");
			return 0;
		}
	}

	return 1;
}

static s32 parseGltfAnimationInfoFromJson(const char *json, u32 json_size,
                                          s32 text_gltf,
                                          gltf_animation_info_t *info)
{
	json_span_t root;
	json_span_t asset;
	json_span_t animations_array;
	json_span_t animation_object;
	const char *cursor = NULL;
	gltf_accessor_t *accessors = NULL;
	s32 accessor_count = 0;

	if (!json || json_size == 0 || !info) {
		return 0;
	}

	memset(info, 0, sizeof(*info));
	root.start = json;
	root.end = json + json_size;

	if (!jsonObjectObject(root, "asset", &asset)) {
		gltfAnimationSetError(info, "gltf_missing_asset");
		return 0;
	}
	if (!jsonObjectArray(root, "animations", &animations_array)) {
		gltfAnimationSetError(info, "gltf_missing_animations");
		return 0;
	}
	if (text_gltf && !gltfValidateTextBuffersAreEmbedded(root, info)) {
		return 0;
	}

	(void)parseGltfAccessors(root, &accessors, &accessor_count);

	while (jsonArrayNextObject(animations_array, &cursor, &animation_object)) {
		json_span_t channels_array;
		json_span_t samplers_array;
		json_span_t sampler_object;
		const char *sampler_cursor = NULL;
		s32 channels = 0;

		info->animation_count++;
		if (!info->name[0]) {
			(void)jsonObjectString(animation_object, "name",
				info->name, sizeof(info->name));
		}

		if (jsonObjectArray(animation_object, "channels", &channels_array)) {
			channels = jsonArrayObjectCount(channels_array);
			info->channel_count += channels;
		}

		if (jsonObjectArray(animation_object, "samplers", &samplers_array)) {
			while (jsonArrayNextObject(samplers_array, &sampler_cursor,
					&sampler_object)) {
				s32 input = -1;
				info->sampler_count++;
				if (jsonObjectInt(sampler_object, "input", &input)
						&& input >= 0 && input < accessor_count
						&& accessors[input].count > info->frame_count) {
					info->frame_count = accessors[input].count;
				}
			}
		}
	}

	gltfFreeParsed(NULL, accessors);

	if (info->animation_count <= 0) {
		gltfAnimationSetError(info, "gltf_no_animation_clips");
		return 0;
	}
	if (info->frame_count <= 0) {
		info->frame_count = 1;
	}
	if (!info->name[0]) {
		snprintf(info->name, sizeof(info->name), "clip_0");
	}

	return 1;
}

static s32 parseGltfTextAnimationInfo(const u8 *data, u32 size,
                                      gltf_animation_info_t *info)
{
	char *json;
	s32 ok;

	if (!data || size == 0 || !info) {
		return 0;
	}

	json = malloc((size_t)size + 1);
	if (!json) {
		memset(info, 0, sizeof(*info));
		gltfAnimationSetError(info, "gltf_json_alloc_failed");
		return 0;
	}

	memcpy(json, data, size);
	json[size] = '\0';
	ok = parseGltfAnimationInfoFromJson(json, size, 1, info);
	free(json);
	return ok;
}

static s32 parseGlbAnimationInfo(const u8 *data, u32 size,
                                 gltf_animation_info_t *info)
{
	u32 declared_len;
	u32 offset = 12;
	char *json = NULL;
	u32 json_size = 0;
	s32 ok;

	if (!data || size < 20 || !info) {
		if (info) {
			memset(info, 0, sizeof(*info));
			gltfAnimationSetError(info, "glb_invalid_header");
		}
		return 0;
	}
	if (memcmp(data, "glTF", 4) != 0 || readLe32(data + 4) < 2) {
		memset(info, 0, sizeof(*info));
		gltfAnimationSetError(info, "glb_invalid_header");
		return 0;
	}

	declared_len = readLe32(data + 8);
	if (declared_len > size) {
		memset(info, 0, sizeof(*info));
		gltfAnimationSetError(info, "glb_declared_size_exceeds_source");
		return 0;
	}

	while (offset + 8 <= declared_len) {
		u32 chunk_len = readLe32(data + offset);
		u32 chunk_type = readLe32(data + offset + 4);
		const u8 *chunk_data = data + offset + 8;

		offset += 8;
		if (chunk_len > declared_len || offset + chunk_len > declared_len) {
			memset(info, 0, sizeof(*info));
			gltfAnimationSetError(info, "glb_chunk_out_of_bounds");
			free(json);
			return 0;
		}
		if (chunk_type == 0x4e4f534a) {
			free(json);
			json = malloc((size_t)chunk_len + 1);
			if (!json) {
				memset(info, 0, sizeof(*info));
				gltfAnimationSetError(info, "glb_json_alloc_failed");
				return 0;
			}
			memcpy(json, chunk_data, chunk_len);
			json[chunk_len] = '\0';
			json_size = chunk_len;
		}
		offset += (chunk_len + 3u) & ~3u;
	}

	if (!json || json_size == 0) {
		memset(info, 0, sizeof(*info));
		gltfAnimationSetError(info, "glb_missing_json_chunk");
		free(json);
		return 0;
	}

	ok = parseGltfAnimationInfoFromJson(json, json_size, 0, info);
	free(json);
	return ok;
}

static s32 parseGltfLikeAnimationInfo(const char *path,
                                      const u8 *data,
                                      u32 size,
                                      gltf_animation_info_t *info)
{
	if (endsWithNoCase(path, ".gltf")) {
		return parseGltfTextAnimationInfo(data, size, info);
	}
	if (endsWithNoCase(path, ".glb")) {
		return parseGlbAnimationInfo(data, size, info);
	}
	if (info) {
		memset(info, 0, sizeof(*info));
		gltfAnimationSetError(info, "unsupported_animation_source");
	}
	return 0;
}

static void gltfAnimationClipFree(gltf_animation_clip_t *clip)
{
	if (!clip) {
		return;
	}
	free(clip->channels);
	free(clip->owned_bin);
	memset(clip, 0, sizeof(*clip));
}

static s32 gltfAnimPathFromString(const char *path, gltf_anim_path_e *out)
{
	if (!path || !out) {
		return 0;
	}
	if (strcmp(path, "translation") == 0) {
		*out = GLTF_ANIM_PATH_TRANSLATION;
		return 1;
	}
	if (strcmp(path, "rotation") == 0) {
		*out = GLTF_ANIM_PATH_ROTATION;
		return 1;
	}
	if (strcmp(path, "scale") == 0) {
		*out = GLTF_ANIM_PATH_SCALE;
		return 1;
	}
	return 0;
}

static s32 gltfAnimationChannelDuplicate(const gltf_animation_clip_t *clip,
                                         s32 channel_count,
                                         s32 target_node,
                                         gltf_anim_path_e path)
{
	for (s32 i = 0; i < channel_count; i++) {
		if (clip->channels[i].target_node == target_node
				&& clip->channels[i].path == path) {
			return 1;
		}
	}
	return 0;
}

static s32 parseGltfAnimationClipFromJson(const char *json,
                                          u32 json_size,
                                          const u8 *bin,
                                          u32 bin_size,
                                          s32 text_gltf,
                                          gltf_animation_clip_t *clip)
{
	json_span_t root;
	json_span_t asset;
	json_span_t animations_array;
	json_span_t animation_object;
	json_span_t first_animation = {0};
	json_span_t samplers_array;
	json_span_t channels_array;
	const char *cursor = NULL;
	gltf_buffer_view_t *views = NULL;
	gltf_accessor_t *accessors = NULL;
	gltf_anim_sampler_t *samplers = NULL;
	s32 view_count = 0;
	s32 accessor_count = 0;
	s32 sampler_count = 0;
	s32 channel_count = 0;
	s32 ok = 0;

	if (!json || json_size == 0 || !clip) {
		return 0;
	}

	memset(clip, 0, sizeof(*clip));
	root.start = json;
	root.end = json + json_size;

	if (!jsonObjectObject(root, "asset", &asset)) {
		gltfAnimationSetError(&clip->info, "gltf_missing_asset");
		return 0;
	}
	if (!jsonObjectArray(root, "animations", &animations_array)) {
		gltfAnimationSetError(&clip->info, "gltf_missing_animations");
		return 0;
	}
	if (text_gltf && !gltfValidateTextBuffersAreEmbedded(root, &clip->info)) {
		return 0;
	}

	while (jsonArrayNextObject(animations_array, &cursor, &animation_object)) {
		json_span_t local_channels;
		json_span_t local_samplers;

		clip->info.animation_count++;
		if (!first_animation.start) {
			first_animation = animation_object;
			(void)jsonObjectString(animation_object, "name",
				clip->info.name, sizeof(clip->info.name));
		}
		if (jsonObjectArray(animation_object, "channels", &local_channels)) {
			clip->info.channel_count += jsonArrayObjectCount(local_channels);
		}
		if (jsonObjectArray(animation_object, "samplers", &local_samplers)) {
			clip->info.sampler_count += jsonArrayObjectCount(local_samplers);
		}
	}

	if (clip->info.animation_count <= 0 || !first_animation.start) {
		gltfAnimationSetError(&clip->info, "gltf_no_animation_clips");
		return 0;
	}
	if (!clip->info.name[0]) {
		snprintf(clip->info.name, sizeof(clip->info.name), "clip_0");
	}

	if (!jsonObjectArray(first_animation, "channels", &channels_array)) {
		clip->info.frame_count = 1;
		clip->frame_count = 1;
		clip->part_count = 1;
		return 1;
	}

	channel_count = jsonArrayObjectCount(channels_array);
	if (channel_count <= 0) {
		clip->info.frame_count = 1;
		clip->frame_count = 1;
		clip->part_count = 1;
		return 1;
	}
	if (!bin || bin_size == 0) {
		gltfAnimationSetError(&clip->info, "gltf_animation_buffer_missing");
		return 0;
	}
	if (!parseGltfBufferViews(root, &views, &view_count)
			|| !parseGltfAccessors(root, &accessors, &accessor_count)) {
		gltfAnimationSetError(&clip->info,
			"gltf_animation_missing_buffers_or_accessors");
		goto cleanup;
	}
	if (!jsonObjectArray(first_animation, "samplers", &samplers_array)) {
		gltfAnimationSetError(&clip->info, "gltf_animation_missing_samplers");
		goto cleanup;
	}

	sampler_count = jsonArrayObjectCount(samplers_array);
	if (sampler_count <= 0) {
		gltfAnimationSetError(&clip->info, "gltf_animation_missing_samplers");
		goto cleanup;
	}
	samplers = calloc((size_t)sampler_count, sizeof(*samplers));
	clip->channels = calloc((size_t)channel_count, sizeof(*clip->channels));
	if (!samplers || !clip->channels) {
		gltfAnimationSetError(&clip->info, "gltf_animation_alloc_failed");
		goto cleanup;
	}

	cursor = NULL;
	for (s32 i = 0; i < sampler_count; i++) {
		json_span_t sampler_object;

		if (!jsonArrayNextObject(samplers_array, &cursor, &sampler_object)) {
			gltfAnimationSetError(&clip->info, "gltf_sampler_parse_failed");
			goto cleanup;
		}

		samplers[i].input = -1;
		samplers[i].output = -1;
		snprintf(samplers[i].interpolation,
			sizeof(samplers[i].interpolation), "LINEAR");
		(void)jsonObjectString(sampler_object, "interpolation",
			samplers[i].interpolation, sizeof(samplers[i].interpolation));

		if (!jsonObjectInt(sampler_object, "input", &samplers[i].input)
				|| !jsonObjectInt(sampler_object, "output",
					&samplers[i].output)) {
			gltfAnimationSetError(&clip->info,
				"gltf_sampler_missing_input_or_output");
			goto cleanup;
		}
		if (strcmp(samplers[i].interpolation, "LINEAR") != 0
				&& strcmp(samplers[i].interpolation, "STEP") != 0) {
			gltfAnimationSetError(&clip->info,
				"gltf_animation_interpolation_not_supported");
			goto cleanup;
		}
	}

	cursor = NULL;
	for (s32 i = 0; i < channel_count; i++) {
		json_span_t channel_object;
		json_span_t target_object;
		gltf_anim_channel_t *channel = &clip->channels[i];
		gltf_anim_sampler_t *sampler;
		gltf_accessor_t *input;
		gltf_accessor_t *output;
		const u8 *input_data;
		const u8 *output_data;
		u32 input_stride;
		u32 output_stride;
		u32 element_size;
		char path[32] = {0};
		s32 sampler_index = -1;

		if (!jsonArrayNextObject(channels_array, &cursor, &channel_object)) {
			gltfAnimationSetError(&clip->info, "gltf_channel_parse_failed");
			goto cleanup;
		}
		if (!jsonObjectInt(channel_object, "sampler", &sampler_index)
				|| sampler_index < 0 || sampler_index >= sampler_count) {
			gltfAnimationSetError(&clip->info, "gltf_channel_bad_sampler");
			goto cleanup;
		}
		if (!jsonObjectObject(channel_object, "target", &target_object)
				|| !jsonObjectInt(target_object, "node",
					&channel->target_node)
				|| !jsonObjectString(target_object, "path", path,
					sizeof(path))) {
			gltfAnimationSetError(&clip->info, "gltf_channel_bad_target");
			goto cleanup;
		}
		if (strcmp(path, "weights") == 0) {
			gltfAnimationSetError(&clip->info,
				"gltf_animation_weights_not_supported");
			goto cleanup;
		}
		if (!gltfAnimPathFromString(path, &channel->path)) {
			gltfAnimationSetError(&clip->info,
				"gltf_animation_channel_path_not_supported");
			goto cleanup;
		}
		if (channel->target_node < 0 || channel->target_node > 511) {
			gltfAnimationSetError(&clip->info, "gltf_channel_node_out_of_range");
			goto cleanup;
		}
		if (gltfAnimationChannelDuplicate(clip, i,
				channel->target_node, channel->path)) {
			gltfAnimationSetError(&clip->info,
				"gltf_duplicate_animation_channel");
			goto cleanup;
		}

		sampler = &samplers[sampler_index];
		if (sampler->input < 0 || sampler->input >= accessor_count
				|| sampler->output < 0
				|| sampler->output >= accessor_count) {
			gltfAnimationSetError(&clip->info, "gltf_sampler_accessor_out_of_range");
			goto cleanup;
		}

		input = &accessors[sampler->input];
		output = &accessors[sampler->output];
		if (input->component_type != 5126
				|| strcmp(input->type, "SCALAR") != 0) {
			gltfAnimationSetError(&clip->info,
				"gltf_animation_input_must_be_float_scalar");
			goto cleanup;
		}
		if (output->component_type != 5126
				|| ((channel->path == GLTF_ANIM_PATH_ROTATION
						&& strcmp(output->type, "VEC4") != 0)
					|| (channel->path != GLTF_ANIM_PATH_ROTATION
						&& strcmp(output->type, "VEC3") != 0))) {
			gltfAnimationSetError(&clip->info,
				"gltf_animation_output_type_mismatch");
			goto cleanup;
		}
		if (output->count < input->count) {
			gltfAnimationSetError(&clip->info,
				"gltf_animation_output_shorter_than_input");
			goto cleanup;
		}
		if (!gltfAccessorData(input, views, view_count, bin, bin_size,
				&input_data, &input_stride, &element_size)
				|| !gltfAccessorData(output, views, view_count,
					bin, bin_size, &output_data, &output_stride,
					&element_size)) {
			gltfAnimationSetError(&clip->info,
				"gltf_animation_accessor_out_of_bounds");
			goto cleanup;
		}

		channel->input_data = input_data;
		channel->output_data = output_data;
		channel->input_stride = input_stride;
		channel->output_stride = output_stride;
		channel->sample_count = input->count;

		if (input->count > clip->frame_count) {
			clip->frame_count = input->count;
		}
		if (channel->target_node + 1 > clip->part_count) {
			clip->part_count = channel->target_node + 1;
		}
	}

	if (clip->frame_count <= 0) {
		clip->frame_count = 1;
	}
	if (clip->part_count <= 0) {
		clip->part_count = 1;
	}
	clip->channel_count = channel_count;
	clip->info.frame_count = clip->frame_count;
	clip->info.channel_count = channel_count;
	ok = 1;

cleanup:
	free(samplers);
	gltfFreeParsed(views, accessors);
	if (!ok) {
		free(clip->channels);
		clip->channels = NULL;
		clip->channel_count = 0;
	}
	return ok;
}

static s32 parseGltfTextAnimationClip(const u8 *data,
                                      u32 size,
                                      gltf_animation_clip_t *clip)
{
	char *json;
	json_span_t root;
	json_span_t buffers_array;
	json_span_t buffer_object;
	u8 *bin = NULL;
	u32 bin_size = 0;
	s32 ok;

	if (!data || size == 0 || !clip) {
		return 0;
	}

	json = malloc((size_t)size + 1);
	if (!json) {
		memset(clip, 0, sizeof(*clip));
		gltfAnimationSetError(&clip->info, "gltf_json_alloc_failed");
		return 0;
	}

	memcpy(json, data, size);
	json[size] = '\0';

	root.start = json;
	root.end = json + size;
	if (jsonObjectArray(root, "buffers", &buffers_array)) {
		const char *buffer_cursor = NULL;
		if (jsonArrayNextObject(buffers_array, &buffer_cursor,
				&buffer_object)) {
			char uri[8192] = {0};
			if (jsonObjectString(buffer_object, "uri", uri, sizeof(uri))) {
				bin = decodeGltfDataUri(uri, &bin_size);
			}
		}
	}

	ok = parseGltfAnimationClipFromJson(json, size, bin, bin_size, 1, clip);
	if (ok) {
		clip->owned_bin = bin;
		bin = NULL;
	}
	free(bin);
	free(json);
	return ok;
}

static s32 parseGlbAnimationClip(const u8 *data,
                                 u32 size,
                                 gltf_animation_clip_t *clip)
{
	u32 declared_len;
	u32 offset = 12;
	char *json = NULL;
	u32 json_size = 0;
	const u8 *bin = NULL;
	u32 bin_size = 0;
	s32 ok;

	if (!data || size < 20 || !clip) {
		if (clip) {
			memset(clip, 0, sizeof(*clip));
			gltfAnimationSetError(&clip->info, "glb_invalid_header");
		}
		return 0;
	}
	if (memcmp(data, "glTF", 4) != 0 || readLe32(data + 4) < 2) {
		memset(clip, 0, sizeof(*clip));
		gltfAnimationSetError(&clip->info, "glb_invalid_header");
		return 0;
	}

	declared_len = readLe32(data + 8);
	if (declared_len > size) {
		memset(clip, 0, sizeof(*clip));
		gltfAnimationSetError(&clip->info, "glb_declared_size_exceeds_source");
		return 0;
	}

	while (offset + 8 <= declared_len) {
		u32 chunk_len = readLe32(data + offset);
		u32 chunk_type = readLe32(data + offset + 4);
		const u8 *chunk_data = data + offset + 8;

		offset += 8;
		if (chunk_len > declared_len || offset + chunk_len > declared_len) {
			memset(clip, 0, sizeof(*clip));
			gltfAnimationSetError(&clip->info, "glb_chunk_out_of_bounds");
			free(json);
			return 0;
		}
		if (chunk_type == 0x4e4f534a) {
			free(json);
			json = malloc((size_t)chunk_len + 1);
			if (!json) {
				memset(clip, 0, sizeof(*clip));
				gltfAnimationSetError(&clip->info,
					"glb_json_alloc_failed");
				return 0;
			}
			memcpy(json, chunk_data, chunk_len);
			json[chunk_len] = '\0';
			json_size = chunk_len;
		} else if (chunk_type == 0x004e4942) {
			bin = chunk_data;
			bin_size = chunk_len;
		}
		offset += (chunk_len + 3u) & ~3u;
	}

	if (!json || json_size == 0) {
		memset(clip, 0, sizeof(*clip));
		gltfAnimationSetError(&clip->info, "glb_missing_json_chunk");
		free(json);
		return 0;
	}

	ok = parseGltfAnimationClipFromJson(json, json_size, bin, bin_size, 0, clip);
	free(json);
	return ok;
}

static s32 parseGltfLikeAnimationClip(const char *path,
                                      const u8 *data,
                                      u32 size,
                                      gltf_animation_clip_t *clip)
{
	if (endsWithNoCase(path, ".gltf")) {
		return parseGltfTextAnimationClip(data, size, clip);
	}
	if (endsWithNoCase(path, ".glb")) {
		return parseGlbAnimationClip(data, size, clip);
	}
	if (clip) {
		memset(clip, 0, sizeof(*clip));
		gltfAnimationSetError(&clip->info, "unsupported_animation_source");
	}
	return 0;
}

static s32 parseGltfLikeMeshSource(const char *path,
                                   const u8 *data,
                                   u32 size,
                                   obj_mesh_t *mesh)
{
	if (endsWithNoCase(path, ".gltf")) {
		return parseGltfTextMesh(data, size, mesh);
	}
	if (endsWithNoCase(path, ".glb")) {
		return parseGlbMesh(data, size, mesh);
	}
	return 0;
}

s32 modAssetCompilerIsExternalSource(const char *path)
{
	return endsWithNoCase(path, ".gltf")
		|| endsWithNoCase(path, ".glb")
		|| endsWithNoCase(path, ".obj");
}

static s32 bufferContains(const u8 *data, u32 size, const char *needle)
{
	size_t needle_len;

	if (!data || !needle) {
		return 0;
	}

	needle_len = strlen(needle);
	if (needle_len == 0 || size < needle_len) {
		return 0;
	}

	for (u32 i = 0; i + needle_len <= size; i++) {
		if (memcmp(data + i, needle, needle_len) == 0) {
			return 1;
		}
	}

	return 0;
}

static u32 readLe32(const u8 *data)
{
	return ((u32)data[0])
		| ((u32)data[1] << 8)
		| ((u32)data[2] << 16)
		| ((u32)data[3] << 24);
}

static s32 validateObjSource(const u8 *data, u32 size,
                             char *summary, size_t summary_len)
{
	obj_mesh_t mesh;
	s32 ok = parseObjSource(data, size, &mesh);

	if (summary && summary_len > 0) {
		if (ok) {
			snprintf(summary, summary_len, "format=obj vertices=%d triangles=%d",
				mesh.vertex_count, mesh.triangle_count);
		} else {
			snprintf(summary, summary_len, "format=obj invalid %s",
				mesh.error[0] ? mesh.error : "parse_failed");
		}
	}

	objMeshFree(&mesh);
	return ok;
}

static s32 validateGltfSource(const u8 *data, u32 size,
                              char *summary, size_t summary_len)
{
	s32 has_asset = bufferContains(data, size, "\"asset\"");
	s32 has_version = bufferContains(data, size, "\"version\"");
	s32 has_payload = bufferContains(data, size, "\"nodes\"")
		|| bufferContains(data, size, "\"meshes\"")
		|| bufferContains(data, size, "\"animations\"");

	if (summary && summary_len > 0) {
		snprintf(summary, summary_len, "format=gltf asset=%d version=%d payload=%d",
			has_asset, has_version, has_payload);
	}

	return has_asset && has_version && has_payload;
}

static s32 validateGlbSource(const u8 *data, u32 size,
                             char *summary, size_t summary_len)
{
	u32 version;
	u32 declared_len;

	if (!data || size < 12) {
		if (summary && summary_len > 0) {
			snprintf(summary, summary_len, "format=glb invalid_header");
		}
		return 0;
	}

	if (memcmp(data, "glTF", 4) != 0) {
		if (summary && summary_len > 0) {
			snprintf(summary, summary_len, "format=glb bad_magic");
		}
		return 0;
	}

	version = readLe32(data + 4);
	declared_len = readLe32(data + 8);
	if (summary && summary_len > 0) {
		snprintf(summary, summary_len, "format=glb version=%u declared_size=%u",
			version, declared_len);
	}

	return version >= 2 && declared_len <= size;
}

static s32 validateExternalSource(const char *path, const u8 *data, u32 size,
                                  char *summary, size_t summary_len)
{
	const char *kind = sourceKindForPath(path);

	if (strcmp(kind, "obj") == 0) {
		return validateObjSource(data, size, summary, summary_len);
	}
	if (strcmp(kind, "gltf") == 0) {
		return validateGltfSource(data, size, summary, summary_len);
	}
	if (strcmp(kind, "glb") == 0) {
		return validateGlbSource(data, size, summary, summary_len);
	}

	if (summary && summary_len > 0) {
		snprintf(summary, summary_len, "format=unknown");
	}
	return 0;
}

static void sanitizePathPart(const char *src, char *dst, size_t dst_len)
{
	size_t j = 0;

	if (!dst || dst_len == 0) {
		return;
	}

	if (!src || !src[0]) {
		src = "unknown";
	}

	for (size_t i = 0; src[i] && j + 1 < dst_len; i++) {
		unsigned char c = (unsigned char)src[i];
		if (isalnum(c) || c == '-' || c == '_') {
			dst[j++] = (char)c;
		} else {
			dst[j++] = '_';
		}
	}

	dst[j] = '\0';
}

static void jsonWriteEscaped(FILE *f, const char *text)
{
	if (!text) {
		return;
	}

	for (const char *p = text; *p; p++) {
		switch (*p) {
		case '\\': fputs("\\\\", f); break;
		case '"':  fputs("\\\"", f); break;
		case '\n': fputs("\\n", f); break;
		case '\r': fputs("\\r", f); break;
		case '\t': fputs("\\t", f); break;
		default:
			fputc(*p, f);
			break;
		}
	}
}

static s32 ensureCacheDirs(const char *mod_part, const char *asset_part)
{
	char path[FS_MAXPATH];

	if (!fsCreateDir("$S/mod-cache")) {
		return 0;
	}

	snprintf(path, sizeof(path), "$S/mod-cache/%s", mod_part);
	if (!fsCreateDir(path)) {
		return 0;
	}

	snprintf(path, sizeof(path), "$S/mod-cache/%s/%s", mod_part, asset_part);
	if (!fsCreateDir(path)) {
		return 0;
	}

	return 1;
}

static s32 cachePathExists(const char *path)
{
	return path && path[0] && fsFileSize(path) >= 0;
}

static s32 writeObjMeshJson(const char *path, const asset_entry_t *entry,
                            const char *asset_kind, const char *source_path,
                            const char *source_sha256, const obj_mesh_t *mesh)
{
	FILE *f;

	if (!path || !path[0] || !mesh) {
		return 0;
	}

	f = fsFileOpenWrite(path);
	if (!f) {
		return 0;
	}

	fprintf(f, "{\n");
	fprintf(f, "  \"schema\": \"pd2.modasset.mesh.v1\",\n");
	fprintf(f, "  \"compiler_version\": %d,\n", MODASSET_COMPILER_VERSION);
	fprintf(f, "  \"asset_id\": \"");
	jsonWriteEscaped(f, entry ? entry->id : "");
	fprintf(f, "\",\n");
	fprintf(f, "  \"kind\": \"");
	jsonWriteEscaped(f, asset_kind && asset_kind[0] ? asset_kind : "asset");
	fprintf(f, "\",\n");
	fprintf(f, "  \"source\": \"");
	jsonWriteEscaped(f, source_path);
	fprintf(f, "\",\n");
	fprintf(f, "  \"source_sha256\": \"%s\",\n", source_sha256 ? source_sha256 : "");
	fprintf(f, "  \"vertex_count\": %d,\n", mesh->vertex_count);
	fprintf(f, "  \"triangle_count\": %d,\n", mesh->triangle_count);
	fprintf(f, "  \"vertices\": [\n");
	for (s32 i = 0; i < mesh->vertex_count; i++) {
		const obj_vertex_t *v = &mesh->vertices[i];
		fprintf(f, "    [%.9g, %.9g, %.9g]%s\n",
			v->x, v->y, v->z, i + 1 == mesh->vertex_count ? "" : ",");
	}
	fprintf(f, "  ],\n");
	fprintf(f, "  \"triangles\": [\n");
	for (s32 i = 0; i < mesh->triangle_count; i++) {
		const obj_triangle_t *tri = &mesh->triangles[i];
		fprintf(f, "    [%d, %d, %d]%s\n",
			tri->a, tri->b, tri->c, i + 1 == mesh->triangle_count ? "" : ",");
	}
	fprintf(f, "  ]\n");
	fprintf(f, "}\n");
	fclose(f);
	return 1;
}

static s32 writeModelMeshJson(const char *path, const asset_entry_t *entry,
                              const char *asset_kind, const char *source_path,
                              const char *source_sha256, const obj_mesh_t *mesh)
{
	FILE *f;

	if (!path || !path[0] || !mesh) {
		return 0;
	}

	f = fsFileOpenWrite(path);
	if (!f) {
		return 0;
	}

	fprintf(f, "{\n");
	fprintf(f, "  \"schema\": \"pd2.modasset.model.v1\",\n");
	fprintf(f, "  \"compiler_version\": %d,\n", MODASSET_COMPILER_VERSION);
	fprintf(f, "  \"asset_id\": \"");
	jsonWriteEscaped(f, entry ? entry->id : "");
	fprintf(f, "\",\n");
	fprintf(f, "  \"kind\": \"");
	jsonWriteEscaped(f, asset_kind && asset_kind[0] ? asset_kind : "model");
	fprintf(f, "\",\n");
	fprintf(f, "  \"source\": \"");
	jsonWriteEscaped(f, source_path);
	fprintf(f, "\",\n");
	fprintf(f, "  \"source_sha256\": \"%s\",\n", source_sha256 ? source_sha256 : "");
	fprintf(f, "  \"runtime_boundary\": \"modeldef\",\n");
	fprintf(f, "  \"vertex_count\": %d,\n", mesh->vertex_count);
	fprintf(f, "  \"triangle_count\": %d,\n", mesh->triangle_count);
	fprintf(f, "  \"vertices\": [\n");
	for (s32 i = 0; i < mesh->vertex_count; i++) {
		const obj_vertex_t *v = &mesh->vertices[i];
		fprintf(f, "    [%.9g, %.9g, %.9g]%s\n",
			v->x, v->y, v->z, i + 1 == mesh->vertex_count ? "" : ",");
	}
	fprintf(f, "  ],\n");
	fprintf(f, "  \"triangles\": [\n");
	for (s32 i = 0; i < mesh->triangle_count; i++) {
		const obj_triangle_t *tri = &mesh->triangles[i];
		fprintf(f, "    [%d, %d, %d]%s\n",
			tri->a, tri->b, tri->c, i + 1 == mesh->triangle_count ? "" : ",");
	}
	fprintf(f, "  ]\n");
	fprintf(f, "}\n");
	fclose(f);
	return 1;
}

static s32 writeAnimationClipJson(const char *path, const asset_entry_t *entry,
                                  const char *asset_kind, const char *source_path,
                                  const char *source_sha256,
                                  const gltf_animation_info_t *info,
                                  s32 frame_count)
{
	FILE *f;

	if (!path || !path[0] || !info) {
		return 0;
	}

	f = fsFileOpenWrite(path);
	if (!f) {
		return 0;
	}

	fprintf(f, "{\n");
	fprintf(f, "  \"schema\": \"pd2.modasset.animation.v1\",\n");
	fprintf(f, "  \"compiler_version\": %d,\n", MODASSET_COMPILER_VERSION);
	fprintf(f, "  \"asset_id\": \"");
	jsonWriteEscaped(f, entry ? entry->id : "");
	fprintf(f, "\",\n");
	fprintf(f, "  \"kind\": \"");
	jsonWriteEscaped(f, asset_kind && asset_kind[0] ? asset_kind : "animation");
	fprintf(f, "\",\n");
	fprintf(f, "  \"source\": \"");
	jsonWriteEscaped(f, source_path);
	fprintf(f, "\",\n");
	fprintf(f, "  \"source_sha256\": \"%s\",\n", source_sha256 ? source_sha256 : "");
	fprintf(f, "  \"runtime_boundary\": \"animtableentry\",\n");
	fprintf(f, "  \"clip_name\": \"");
	jsonWriteEscaped(f, info->name);
	fprintf(f, "\",\n");
	fprintf(f, "  \"animation_count\": %d,\n", info->animation_count);
	fprintf(f, "  \"channel_count\": %d,\n", info->channel_count);
	fprintf(f, "  \"sampler_count\": %d,\n", info->sampler_count);
	fprintf(f, "  \"frame_count\": %d,\n", frame_count);
	fprintf(f, "  \"header_length\": %d,\n",
		info->channel_count > 0 ? -1 : 1);
	fprintf(f, "  \"bytes_per_frame\": %d,\n",
		info->channel_count > 0 ? -1 : 0);
	fprintf(f, "  \"transform_policy\": \"%s\"",
		info->channel_count > 0
			? "gltf_skeletal_channel_pack"
			: "identity_static_clip");
	if (info->channel_count > 0) {
		fprintf(f, ",\n");
		fprintf(f, "  \"packed_channels\": [\"translation\", \"rotation\", \"scale\"]\n");
	} else {
		fprintf(f, "\n");
	}
	fprintf(f, "}\n");
	fclose(f);
	return 1;
}

static s32 writeCacheDescriptor(const char *path,
                                const asset_entry_t *entry,
                                const char *asset_kind,
                                const char *source_path,
                                u32 source_size,
                                const char *source_sha256,
                                const char *validation,
                                const char *normalized_path,
                                const obj_mesh_t *mesh)
{
	FILE *f;

	f = fsFileOpenWrite(path);
	if (!f) {
		return 0;
	}

	fprintf(f, "{\n");
	fprintf(f, "  \"schema\": \"pd2.modasset.cache.v1\",\n");
	fprintf(f, "  \"compiler_version\": %d,\n", MODASSET_COMPILER_VERSION);
	fprintf(f, "  \"asset_id\": \"");
	jsonWriteEscaped(f, entry ? entry->id : "");
	fprintf(f, "\",\n");
	fprintf(f, "  \"mod_id\": \"");
	jsonWriteEscaped(f, entry && entry->category[0] ? entry->category : "mod");
	fprintf(f, "\",\n");
	fprintf(f, "  \"kind\": \"");
	jsonWriteEscaped(f, asset_kind && asset_kind[0] ? asset_kind : "asset");
	fprintf(f, "\",\n");
	fprintf(f, "  \"source\": \"");
	jsonWriteEscaped(f, source_path);
	fprintf(f, "\",\n");
	fprintf(f, "  \"source_size\": %u,\n", source_size);
	fprintf(f, "  \"source_sha256\": \"%s\",\n", source_sha256 ? source_sha256 : "");
	fprintf(f, "  \"validation\": \"");
	jsonWriteEscaped(f, validation);
	fprintf(f, "\",\n");
	if (normalized_path && normalized_path[0]) {
		fprintf(f, "  \"normalized\": \"");
		jsonWriteEscaped(f, normalized_path);
		fprintf(f, "\",\n");
	}
	if (mesh) {
		fprintf(f, "  \"vertex_count\": %d,\n", mesh->vertex_count);
		fprintf(f, "  \"triangle_count\": %d,\n", mesh->triangle_count);
		fprintf(f, "  \"runtime_payload\": \"readable-generated-cache\",\n");
	} else if (normalized_path && normalized_path[0]) {
		fprintf(f, "  \"runtime_payload\": \"readable-generated-cache\",\n");
	} else {
		fprintf(f, "  \"runtime_payload\": null,\n");
	}
	fprintf(f, "  \"note\": \"External source verified; generated cache is readable, rebuildable, and never authored archive content.\"\n");
	fprintf(f, "}\n");
	fclose(f);
	return 1;
}

static void fillCompileResult(modasset_compiled_result_t *out,
                              const char *descriptor_path,
                              const char *normalized_path,
                              const char *source_sha256,
                              u32 source_size,
                              s32 vertex_count,
                              s32 triangle_count,
                              const char *validation)
{
	if (!out) {
		return;
	}

	memset(out, 0, sizeof(*out));
	out->ready = 1;
	if (descriptor_path) {
		strncpy(out->descriptor_path, descriptor_path, sizeof(out->descriptor_path) - 1);
	}
	if (normalized_path) {
		strncpy(out->normalized_path, normalized_path, sizeof(out->normalized_path) - 1);
	}
	if (source_sha256) {
		strncpy(out->source_sha256, source_sha256, sizeof(out->source_sha256) - 1);
	}
	out->source_size = source_size;
	out->vertex_count = vertex_count;
	out->triangle_count = triangle_count;
	if (validation) {
		strncpy(out->validation, validation, sizeof(out->validation) - 1);
	}
}

s32 modAssetCompilerCompileReadable(const asset_entry_t *entry,
                                    const char *asset_kind,
                                    const char *source_path,
                                    modasset_compiled_result_t *out)
{
	u32 source_size = 0;
	void *source_bytes;
	u8 digest[SHA256_DIGEST_SIZE];
	char digest_hex[SHA256_HEX_SIZE];
	char mod_part[96];
	char asset_part[96];
	char kind_part[48];
	char cache_rel[FS_MAXPATH];
	char normalized_rel[FS_MAXPATH];
	char validation[128];
	const char *source_kind;
	obj_mesh_t obj_mesh;
	obj_mesh_t model_mesh;
	gltf_animation_info_t animation_info;
	s32 has_obj_mesh = 0;
	s32 has_model_mesh = 0;
	s32 has_animation_clip = 0;
	s32 animation_frame_count = 0;

	if (out) {
		memset(out, 0, sizeof(*out));
	}

	if (!modAssetCompilerIsExternalSource(source_path)) {
		return 0;
	}

	if (!entry || !entry->id[0] || !source_path || !source_path[0]) {
		return -1;
	}

	source_bytes = fsFileLoad(source_path, &source_size);
	if (!source_bytes || source_size == 0) {
		sysLogPrintf(LOG_WARNING,
			"MODASSET.COMPILER: source missing for '%s': %s",
			entry->id, source_path ? source_path : "(null)");
		if (source_bytes) {
			free(source_bytes);
		}
		return -1;
	}

	sha256Hash(source_bytes, (size_t)source_size, digest);
	sha256ToHex(digest, digest_hex);

	sanitizePathPart(entry->category[0] ? entry->category : "mod", mod_part, sizeof(mod_part));
	sanitizePathPart(entry->id, asset_part, sizeof(asset_part));
	sanitizePathPart(asset_kind && asset_kind[0] ? asset_kind : "asset",
		kind_part, sizeof(kind_part));

	if (!ensureCacheDirs(mod_part, asset_part)) {
		sysLogPrintf(LOG_WARNING,
			"MODASSET.COMPILER: could not create private cache dirs for '%s'",
			entry->id);
		free(source_bytes);
		return -1;
	}

	snprintf(cache_rel, sizeof(cache_rel),
		"$S/mod-cache/%s/%s/%s-v%d-%s.pdmc",
		mod_part, asset_part, kind_part, MODASSET_COMPILER_VERSION, digest_hex);

	normalized_rel[0] = '\0';
	source_kind = sourceKindForPath(source_path);
	if (strcmp(source_kind, "obj") == 0 && assetKindUsesGeneratedModeldef(asset_kind)) {
		snprintf(normalized_rel, sizeof(normalized_rel),
			"$S/mod-cache/%s/%s/%s-v%d-%s.pdmodel.json",
			mod_part, asset_part, kind_part, MODASSET_COMPILER_VERSION, digest_hex);
	} else if (strcmp(source_kind, "obj") == 0) {
		snprintf(normalized_rel, sizeof(normalized_rel),
			"$S/mod-cache/%s/%s/%s-v%d-%s.pdmesh.json",
			mod_part, asset_part, kind_part, MODASSET_COMPILER_VERSION, digest_hex);
	} else if ((strcmp(source_kind, "gltf") == 0 || strcmp(source_kind, "glb") == 0)
			&& assetKindUsesGeneratedModeldef(asset_kind)) {
		snprintf(normalized_rel, sizeof(normalized_rel),
			"$S/mod-cache/%s/%s/%s-v%d-%s.pdmodel.json",
			mod_part, asset_part, kind_part, MODASSET_COMPILER_VERSION, digest_hex);
	} else if ((strcmp(source_kind, "gltf") == 0 || strcmp(source_kind, "glb") == 0)
			&& assetKindUsesGeneratedAnimationClip(asset_kind)) {
		snprintf(normalized_rel, sizeof(normalized_rel),
			"$S/mod-cache/%s/%s/%s-v%d-%s.pdanimation.json",
			mod_part, asset_part, kind_part, MODASSET_COMPILER_VERSION, digest_hex);
	} else if (strcmp(source_kind, "gltf") == 0 || strcmp(source_kind, "glb") == 0) {
		snprintf(normalized_rel, sizeof(normalized_rel),
			"$S/mod-cache/%s/%s/%s-v%d-%s.pdmesh.json",
			mod_part, asset_part, kind_part, MODASSET_COMPILER_VERSION, digest_hex);
	}

	if (cachePathExists(cache_rel)
			&& (normalized_rel[0] == '\0' || cachePathExists(normalized_rel))) {
		free(source_bytes);
		fillCompileResult(out, cache_rel, normalized_rel, digest_hex,
			source_size, 0, 0, "cache_hit");
		sysLogPrintf(LOG_NOTE,
			"MODASSET.COMPILER: cache hit for external %s '%s' source=%s cache=%s",
			asset_kind && asset_kind[0] ? asset_kind : "asset",
			entry->id, source_path, cache_rel);
		return 1;
	}

	memset(&obj_mesh, 0, sizeof(obj_mesh));
	memset(&model_mesh, 0, sizeof(model_mesh));
	memset(&animation_info, 0, sizeof(animation_info));
	if (strcmp(source_kind, "obj") == 0) {
		if (!parseObjSource((const u8 *)source_bytes, source_size, &obj_mesh)) {
			snprintf(validation, sizeof(validation), "format=obj invalid %s",
				obj_mesh.error[0] ? obj_mesh.error : "parse_failed");
			sysLogPrintf(LOG_WARNING,
				"MODASSET.COMPILER: invalid external source for '%s': %s (%s)",
				entry->id, source_path, validation);
			objMeshFree(&obj_mesh);
			free(source_bytes);
			return -1;
		}
		snprintf(validation, sizeof(validation), "format=obj vertices=%d triangles=%d",
			obj_mesh.vertex_count, obj_mesh.triangle_count);
		has_obj_mesh = 1;
		if (assetKindUsesGeneratedModeldef(asset_kind)) {
			model_mesh = obj_mesh;
			memset(&obj_mesh, 0, sizeof(obj_mesh));
			has_model_mesh = 1;
			has_obj_mesh = 0;
		}
	} else if ((strcmp(source_kind, "gltf") == 0 || strcmp(source_kind, "glb") == 0)
			&& assetKindUsesGeneratedModeldef(asset_kind)) {
		if (!parseGltfLikeMeshSource(source_path, (const u8 *)source_bytes,
				source_size, &model_mesh)) {
			snprintf(validation, sizeof(validation), "format=%s invalid %s",
				source_kind, model_mesh.error[0] ? model_mesh.error : "parse_failed");
			sysLogPrintf(LOG_WARNING,
				"MODASSET.COMPILER: invalid external model source for '%s': %s (%s)",
				entry->id, source_path, validation);
			objMeshFree(&model_mesh);
			free(source_bytes);
			return -1;
		}
		snprintf(validation, sizeof(validation),
			"format=%s modeldef vertices=%d triangles=%d",
			source_kind, model_mesh.vertex_count, model_mesh.triangle_count);
		has_model_mesh = 1;
	} else if ((strcmp(source_kind, "gltf") == 0 || strcmp(source_kind, "glb") == 0)
			&& assetKindUsesGeneratedAnimationClip(asset_kind)) {
		if (!parseGltfLikeAnimationInfo(source_path, (const u8 *)source_bytes,
				source_size, &animation_info)) {
			snprintf(validation, sizeof(validation), "format=%s invalid %s",
				source_kind, animation_info.error[0] ? animation_info.error : "parse_failed");
			sysLogPrintf(LOG_WARNING,
				"MODASSET.COMPILER: invalid external animation source for '%s': %s (%s)",
				entry->id, source_path, validation);
			free(source_bytes);
			return -1;
		}
		animation_frame_count = entry->ext.anim.frame_count > 0
			? entry->ext.anim.frame_count : animation_info.frame_count;
		if (animation_frame_count <= 0) {
			animation_frame_count = 1;
		}
		snprintf(validation, sizeof(validation),
			"format=%s animtableentry clips=%d frames=%d channels=%d",
			source_kind, animation_info.animation_count,
			animation_frame_count, animation_info.channel_count);
		has_animation_clip = 1;
	} else if (strcmp(source_kind, "gltf") == 0 || strcmp(source_kind, "glb") == 0) {
		if (!parseGltfLikeMeshSource(source_path, (const u8 *)source_bytes,
				source_size, &obj_mesh)) {
			snprintf(validation, sizeof(validation), "format=%s invalid %s",
				source_kind, obj_mesh.error[0] ? obj_mesh.error : "parse_failed");
			sysLogPrintf(LOG_WARNING,
				"MODASSET.COMPILER: invalid external mesh source for '%s': %s (%s)",
				entry->id, source_path, validation);
			objMeshFree(&obj_mesh);
			free(source_bytes);
			return -1;
		}
		snprintf(validation, sizeof(validation), "format=%s vertices=%d triangles=%d",
			source_kind, obj_mesh.vertex_count, obj_mesh.triangle_count);
		has_obj_mesh = 1;
	} else if (!validateExternalSource(source_path, (const u8 *)source_bytes,
			source_size, validation, sizeof(validation))) {
		sysLogPrintf(LOG_WARNING,
			"MODASSET.COMPILER: invalid external source for '%s': %s (%s)",
			entry->id, source_path, validation);
		free(source_bytes);
		return -1;
	}

	free(source_bytes);

	if (has_obj_mesh) {
		if (!writeObjMeshJson(normalized_rel, entry, asset_kind, source_path,
				digest_hex, &obj_mesh)) {
			sysLogPrintf(LOG_WARNING,
				"MODASSET.COMPILER: could not write normalized mesh cache '%s'",
				normalized_rel);
			objMeshFree(&obj_mesh);
			return -1;
		}
	}
	if (has_model_mesh) {
		if (!writeModelMeshJson(normalized_rel, entry, asset_kind, source_path,
				digest_hex, &model_mesh)) {
			sysLogPrintf(LOG_WARNING,
				"MODASSET.COMPILER: could not write normalized model cache '%s'",
				normalized_rel);
			objMeshFree(&model_mesh);
			return -1;
		}
	}
	if (has_animation_clip) {
		if (!writeAnimationClipJson(normalized_rel, entry, asset_kind,
				source_path, digest_hex, &animation_info,
				animation_frame_count)) {
			sysLogPrintf(LOG_WARNING,
				"MODASSET.COMPILER: could not write normalized animation cache '%s'",
				normalized_rel);
			objMeshFree(&obj_mesh);
			objMeshFree(&model_mesh);
			return -1;
		}
	}

	if (!writeCacheDescriptor(cache_rel, entry, asset_kind, source_path,
			source_size, digest_hex, validation,
			normalized_rel[0] ? normalized_rel : NULL,
			has_obj_mesh ? &obj_mesh : (has_model_mesh ? &model_mesh : NULL))) {
		sysLogPrintf(LOG_WARNING,
			"MODASSET.COMPILER: could not write cache descriptor '%s'",
			cache_rel);
		objMeshFree(&obj_mesh);
		objMeshFree(&model_mesh);
		return -1;
	}

	fillCompileResult(out, cache_rel, normalized_rel, digest_hex, source_size,
		has_obj_mesh ? obj_mesh.vertex_count
			: (has_model_mesh ? model_mesh.vertex_count : 0),
		has_obj_mesh ? obj_mesh.triangle_count
			: (has_model_mesh ? model_mesh.triangle_count : 0),
		validation);

	sysLogPrintf(LOG_NOTE,
		"MODASSET.COMPILER: cached external %s '%s' source=%s cache=%s",
		asset_kind && asset_kind[0] ? asset_kind : "asset",
		entry->id, source_path, cache_rel);
	objMeshFree(&obj_mesh);
	objMeshFree(&model_mesh);
	return 1;
}

s32 modAssetCompilerEnsureCache(const asset_entry_t *entry,
                                const char *asset_kind,
                                const char *source_path,
                                char *out_cache_path,
                                s32 out_cache_path_len)
{
	modasset_compiled_result_t result;
	s32 rc;

	if (out_cache_path && out_cache_path_len > 0) {
		out_cache_path[0] = '\0';
	}

	rc = modAssetCompilerCompileReadable(entry, asset_kind, source_path, &result);
	if (rc > 0 && out_cache_path && out_cache_path_len > 0) {
		strncpy(out_cache_path, result.descriptor_path, (size_t)out_cache_path_len - 1);
		out_cache_path[out_cache_path_len - 1] = '\0';
	}

	return rc;
}

typedef struct generated_modeldef {
	struct modeldef def;
	struct modelnode root_node;
	struct modelnode bbox_node;
	struct modelnode toggle_node;
	struct modelnode dl_node;
	union modelrodata root_rodata;
	union modelrodata bbox_rodata;
	union modelrodata toggle_rodata;
	union modelrodata dl_rodata;
	struct {
		struct modelnode *nodes[4];
		s16 partnums[5];
	} part_table;
	Vtx *vertices;
	Col *colours;
	Gfx *gdl;
	s32 triangle_count;
} generated_modeldef_t;

static s16 clampToS16(f32 value)
{
	s32 rounded;

	if (value > 32767.0f) {
		return 32767;
	}
	if (value < -32768.0f) {
		return -32768;
	}

	rounded = value >= 0.0f ? (s32)(value + 0.5f) : (s32)(value - 0.5f);
	return (s16)rounded;
}

static void fillGeneratedVertex(Vtx *dst, const obj_vertex_t *src)
{
	memset(dst, 0, sizeof(*dst));
	dst->x = clampToS16(src->x);
	dst->y = clampToS16(src->y);
	dst->z = clampToS16(src->z);
	dst->colour = 0;
}

static s32 generatedModeldefNeedsChrRoot(const asset_entry_t *entry)
{
	return entry != NULL && entry->type == ASSET_BODY;
}

static void generatedModeldefMeshBounds(const obj_mesh_t *mesh,
                                        struct modelrodata_bbox *bbox)
{
	f32 xmin = 0.0f;
	f32 xmax = 0.0f;
	f32 ymin = 0.0f;
	f32 ymax = 0.0f;
	f32 zmin = 0.0f;
	f32 zmax = 0.0f;

	if (mesh && mesh->vertex_count > 0) {
		xmin = xmax = mesh->vertices[0].x;
		ymin = ymax = mesh->vertices[0].y;
		zmin = zmax = mesh->vertices[0].z;

		for (s32 i = 1; i < mesh->vertex_count; i++) {
			const obj_vertex_t *v = &mesh->vertices[i];
			if (v->x < xmin) xmin = v->x;
			if (v->x > xmax) xmax = v->x;
			if (v->y < ymin) ymin = v->y;
			if (v->y > ymax) ymax = v->y;
			if (v->z < zmin) zmin = v->z;
			if (v->z > zmax) zmax = v->z;
		}
	}

	bbox->hitpart = 0;
	bbox->xmin = xmin;
	bbox->xmax = xmax;
	bbox->ymin = ymin;
	bbox->ymax = ymax;
	bbox->zmin = zmin;
	bbox->zmax = zmax;
}

static void generatedModeldefConfigureCctvParts(generated_modeldef_t *owner,
                                                const obj_mesh_t *mesh)
{
	if (!owner || owner->def.skel != &g_SkelCctv) {
		return;
	}

	owner->bbox_node.type = MODELNODETYPE_BBOX;
	owner->bbox_node.rodata = &owner->bbox_rodata;
	owner->bbox_node.parent = &owner->root_node;
	owner->bbox_node.next = &owner->toggle_node;
	generatedModeldefMeshBounds(mesh, &owner->bbox_rodata.bbox);

	owner->toggle_node.type = MODELNODETYPE_TOGGLE;
	owner->toggle_node.rodata = &owner->toggle_rodata;
	owner->toggle_node.parent = &owner->root_node;
	owner->toggle_node.prev = &owner->bbox_node;
	owner->toggle_node.child = &owner->dl_node;
	owner->toggle_rodata.toggle.target = &owner->dl_node;

	owner->dl_node.parent = &owner->toggle_node;
	owner->root_node.child = &owner->bbox_node;

	owner->part_table.nodes[0] = &owner->root_node;
	owner->part_table.nodes[1] = &owner->dl_node;
	owner->part_table.nodes[2] = &owner->bbox_node;
	owner->part_table.nodes[3] = &owner->toggle_node;
	owner->part_table.partnums[0] = MODELPART_CCTV_CASING;
	owner->part_table.partnums[1] = MODELPART_CCTV_LENS;
	owner->part_table.partnums[2] = MODELPART_CCTV_0002;
	owner->part_table.partnums[3] = MODELPART_CCTV_0003;
	owner->part_table.partnums[4] = 0x7fff;

	owner->def.parts = owner->part_table.nodes;
	owner->def.numparts = 4;
	owner->def.nummatrices = 2;
}

static s32 generatedModeldefMetadataPath(const char *source_path,
                                         const char *member,
                                         char *out,
                                         size_t out_n)
{
	const char *sep;
	const char *next;
	int prefix_len;

	if (!source_path || !member || !out || out_n == 0) {
		return 0;
	}

	sep = strstr(source_path, "::");
	if (!sep) {
		return 0;
	}
	while ((next = strstr(sep + 2, "::")) != NULL) {
		sep = next;
	}

	prefix_len = (int)(sep - source_path);
	if (prefix_len <= 0) {
		return 0;
	}

	return snprintf(out, out_n, "%.*s::%s", prefix_len, source_path,
		member) > 0;
}

static s32 generatedModeldefReadStringValue(const char *text,
                                            const char *key,
                                            char *out,
                                            size_t out_n)
{
	const char *p;
	const char *value;
	size_t len = 0;

	if (!text || !key || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	p = strstr(text, key);
	if (!p) {
		return 0;
	}

	p += strlen(key);
	while (*p && *p != ':' && *p != '=') {
		p++;
	}
	if (!*p) {
		return 0;
	}
	p++;
	while (*p == ' ' || *p == '\t' || *p == '"') {
		p++;
	}

	value = p;
	while (value[len]
			&& value[len] != '"'
			&& value[len] != ','
			&& value[len] != '\r'
			&& value[len] != '\n') {
		len++;
	}
	while (len > 0 && (value[len - 1] == ' ' || value[len - 1] == '\t')) {
		len--;
	}
	if (len == 0) {
		return 0;
	}
	if (len >= out_n) {
		len = out_n - 1;
	}

	memcpy(out, value, len);
	out[len] = '\0';
	return 1;
}

static struct skeleton *generatedModeldefSkeletonFromMetadata(
	const char *source_path)
{
	static const char *members[] = {
		"_meta/manifest.json",
		"mesh.ini",
	};
	char metadata_path[FS_MAXPATH + 1];
	char skeleton_symbol[64];

	for (s32 i = 0; i < (s32)(sizeof(members) / sizeof(members[0])); i++) {
		u32 size = 0;
		char *text;

		if (!generatedModeldefMetadataPath(source_path, members[i],
				metadata_path, sizeof(metadata_path))) {
			continue;
		}

		text = (char *)fsFileLoad(metadata_path, &size);
		if (!text || size == 0) {
			if (text) {
				free(text);
			}
			continue;
		}

		skeleton_symbol[0] = '\0';
		if (generatedModeldefReadStringValue(text, "skeleton_symbol",
				skeleton_symbol, sizeof(skeleton_symbol))) {
			struct skeleton *skeleton =
				modAssetCompilerSkeletonForSymbol(skeleton_symbol);
			free(text);
			if (skeleton) {
				return skeleton;
			}
			continue;
		}

		free(text);
	}

	return NULL;
}

static s32 buildGeneratedModeldefFromMesh(const asset_entry_t *entry,
                                          const char *source_path,
                                          const obj_mesh_t *mesh,
                                          struct modeldef **out_modeldef)
{
	generated_modeldef_t *owner;
	Gfx *gdl;
	s32 vtx_count;
	s32 gdl_count;
	size_t vertex_bytes;
	size_t vertex_colour_bytes;
	s32 chr_root;
	struct skeleton *skeleton;

	if (out_modeldef) {
		*out_modeldef = NULL;
	}
	if (!mesh || !out_modeldef || mesh->triangle_count <= 0) {
		return -1;
	}

	vtx_count = mesh->triangle_count * 3;
	if (vtx_count <= 0 || vtx_count > 32767) {
		sysLogPrintf(LOG_WARNING,
			"MODASSET.COMPILER: modeldef conversion rejected %s; vertex count %d exceeds model node limit",
			source_path ? source_path : "(null)", vtx_count);
		return -1;
	}

	gdl_count = mesh->triangle_count * 2 + 3;
	owner = calloc(1, sizeof(*owner));
	if (!owner) {
		return -1;
	}

	vertex_bytes = (size_t)vtx_count * sizeof(*owner->vertices);
	vertex_colour_bytes = (size_t)ALIGN8(vertex_bytes) + sizeof(*owner->colours);
	owner->vertices = calloc(1, vertex_colour_bytes);
	owner->colours = (Col *)((u8 *)owner->vertices + ALIGN8(vertex_bytes));
	owner->gdl = calloc((size_t)gdl_count, sizeof(*owner->gdl));
	if (!owner->vertices || !owner->colours || !owner->gdl) {
		modAssetCompilerFreeModeldef(&owner->def);
		return -1;
	}

	owner->triangle_count = mesh->triangle_count;
	owner->colours[0].r = 0xff;
	owner->colours[0].g = 0xff;
	owner->colours[0].b = 0xff;
	owner->colours[0].a = 0xff;

	for (s32 i = 0; i < mesh->triangle_count; i++) {
		const obj_triangle_t *tri = &mesh->triangles[i];
		fillGeneratedVertex(&owner->vertices[i * 3 + 0], &mesh->vertices[tri->a]);
		fillGeneratedVertex(&owner->vertices[i * 3 + 1], &mesh->vertices[tri->b]);
		fillGeneratedVertex(&owner->vertices[i * 3 + 2], &mesh->vertices[tri->c]);
	}

	gdl = owner->gdl;
	gSPMatrix(gdl++, SEGADDR(SPSEGMENT_MODEL_MTX << 24),
		G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
	gSPColor(gdl++, SEGADDR(SPSEGMENT_MODEL_COL2 << 24), 1);
	for (s32 i = 0; i < mesh->triangle_count; i++) {
		uintptr_t offset = (uintptr_t)(i * 3 * (s32)sizeof(Vtx));
		gSPVertex(gdl++, SEGADDR((SPSEGMENT_MODEL_VTX << 24) | offset), 3, 0);
		gSP1Triangle(gdl++, 0, 1, 2, 0);
	}
	gSPEndDisplayList(gdl++);

	chr_root = generatedModeldefNeedsChrRoot(entry);
	skeleton = chr_root ? &g_SkelChr :
		generatedModeldefSkeletonFromMetadata(source_path);
	owner->root_node.type = chr_root ? MODELNODETYPE_CHRINFO : MODELNODETYPE_POSITION;
	owner->root_node.rodata = &owner->root_rodata;
	owner->root_node.child = &owner->dl_node;
	if (chr_root) {
		owner->root_rodata.chrinfo.animpart = 0;
		owner->root_rodata.chrinfo.mtxindex = 0;
		owner->root_rodata.chrinfo.unk04 = 0.0f;
		owner->root_rodata.chrinfo.rwdataindex = 0;
	} else {
		owner->root_rodata.position.pos.x = 0.0f;
		owner->root_rodata.position.pos.y = 0.0f;
		owner->root_rodata.position.pos.z = 0.0f;
		owner->root_rodata.position.part = skeleton ? 0 : 0xffff;
		owner->root_rodata.position.mtxindex0 = 0;
		owner->root_rodata.position.mtxindex1 = -1;
		owner->root_rodata.position.mtxindex2 = -1;
		owner->root_rodata.position.drawdist = 0.0f;
	}

	owner->dl_node.type = MODELNODETYPE_DL;
	owner->dl_node.rodata = &owner->dl_rodata;
	owner->dl_node.parent = &owner->root_node;
	owner->dl_rodata.dl.opagdl = owner->gdl;
	owner->dl_rodata.dl.xlugdl = NULL;
	owner->dl_rodata.dl.colours = owner->colours;
	owner->dl_rodata.dl.vertices = owner->vertices;
	owner->dl_rodata.dl.numvertices = (s16)vtx_count;
	owner->dl_rodata.dl.mcount = 1;
	owner->dl_rodata.dl.numcolours = 1;

	owner->def.rootnode = &owner->root_node;
	owner->def.skel = skeleton;
	owner->def.parts = NULL;
	owner->def.numparts = 0;
	owner->def.nummatrices = 1;
	owner->def.scale = entry && entry->model_scale > 0.0f ? entry->model_scale : 1.0f;
	owner->def.numtexconfigs = 0;
	owner->def.texconfigs = NULL;
	owner->def.rwdatalen = modelCalculateRwDataIndexes(owner->def.rootnode);
	generatedModeldefConfigureCctvParts(owner, mesh);
	owner->def.rwdatalen = modelCalculateRwDataIndexes(owner->def.rootnode);

	*out_modeldef = &owner->def;
	sysLogPrintf(LOG_NOTE,
		"MODASSET.COMPILER: built generated modeldef '%s' source=%s vertices=%d tris=%d rwdatalen=%d skeleton=%s",
		entry ? entry->id : "(unknown)",
		source_path ? source_path : "(null)",
		vtx_count, mesh->triangle_count, owner->def.rwdatalen,
		modAssetCompilerSkeletonSymbolForPointer(owner->def.skel) ?
			modAssetCompilerSkeletonSymbolForPointer(owner->def.skel) : "(none)");
	return 1;
}

s32 modAssetCompilerBuildModeldef(const asset_entry_t *entry,
                                  const char *source_path,
                                  struct modeldef **out_modeldef)
{
	u32 source_size = 0;
	void *source_bytes;
	obj_mesh_t mesh;
	s32 rc = 0;

	if (out_modeldef) {
		*out_modeldef = NULL;
	}
	if (!modAssetCompilerIsExternalSource(source_path)) {
		return 0;
	}
	if (!entry || !source_path || !source_path[0] || !out_modeldef) {
		return -1;
	}

	source_bytes = fsFileLoad(source_path, &source_size);
	if (!source_bytes || source_size == 0) {
		if (source_bytes) {
			free(source_bytes);
		}
		return -1;
	}

	memset(&mesh, 0, sizeof(mesh));
	if (endsWithNoCase(source_path, ".obj")) {
		rc = parseObjSource((const u8 *)source_bytes, source_size, &mesh);
	} else {
		rc = parseGltfLikeMeshSource(source_path, (const u8 *)source_bytes,
			source_size, &mesh);
	}

	free(source_bytes);
	if (!rc) {
		sysLogPrintf(LOG_WARNING,
			"MODASSET.COMPILER: modeldef parse failed for '%s': %s (%s)",
			entry->id, source_path, mesh.error[0] ? mesh.error : "parse_failed");
		objMeshFree(&mesh);
		return -1;
	}

	rc = buildGeneratedModeldefFromMesh(entry, source_path, &mesh, out_modeldef);
	objMeshFree(&mesh);
	return rc;
}

void modAssetCompilerFreeModeldef(struct modeldef *modeldef)
{
	generated_modeldef_t *owner;

	if (!modeldef) {
		return;
	}

	owner = (generated_modeldef_t *)modeldef;
	free(owner->vertices);
	free(owner->gdl);
	free(owner);
}

s32 modAssetCompilerBuildColmesh(const char *source_path,
                                 struct colmesh *out_mesh)
{
	u32 source_size = 0;
	void *source_bytes;
	obj_mesh_t obj_mesh;
	const char *source_kind;
	s32 parsed = 0;
	s32 skipped_degenerate = 0;

	if (!modAssetCompilerIsExternalSource(source_path)) {
		return 0;
	}

	if (!out_mesh || !source_path || !source_path[0]) {
		return -1;
	}

	source_bytes = fsFileLoad(source_path, &source_size);
	if (!source_bytes || source_size == 0) {
		if (source_bytes) {
			free(source_bytes);
		}
		return -1;
	}

	memset(&obj_mesh, 0, sizeof(obj_mesh));
	source_kind = sourceKindForPath(source_path);
	if (strcmp(source_kind, "obj") == 0) {
		parsed = parseObjSource((const u8 *)source_bytes, source_size, &obj_mesh);
	} else if (strcmp(source_kind, "gltf") == 0 || strcmp(source_kind, "glb") == 0) {
		parsed = parseGltfLikeMeshSource(source_path,
			(const u8 *)source_bytes, source_size, &obj_mesh);
	}

	if (!parsed) {
		sysLogPrintf(LOG_WARNING,
			"MODASSET.COMPILER: colmesh parse failed for %s (%s)",
			source_path, obj_mesh.error[0] ? obj_mesh.error : "parse_failed");
		objMeshFree(&obj_mesh);
		free(source_bytes);
		return -1;
	}

	free(source_bytes);
	meshInit(out_mesh);

	for (s32 i = 0; i < obj_mesh.triangle_count; i++) {
		const obj_triangle_t *tri = &obj_mesh.triangles[i];
		const obj_vertex_t *a = &obj_mesh.vertices[tri->a];
		const obj_vertex_t *b = &obj_mesh.vertices[tri->b];
		const obj_vertex_t *c = &obj_mesh.vertices[tri->c];
		struct coord v0 = { a->x, a->y, a->z };
		struct coord v1 = { b->x, b->y, b->z };
		struct coord v2 = { c->x, c->y, c->z };
		f32 e1x = v1.x - v0.x;
		f32 e1y = v1.y - v0.y;
		f32 e1z = v1.z - v0.z;
		f32 e2x = v2.x - v0.x;
		f32 e2y = v2.y - v0.y;
		f32 e2z = v2.z - v0.z;
		f32 nx = e1y * e2z - e1z * e2y;
		f32 ny = e1z * e2x - e1x * e2z;
		f32 nz = e1x * e2y - e1y * e2x;
		f32 len2 = nx * nx + ny * ny + nz * nz;

		if (!modAssetFloatIsFinite(v0.x)
				|| !modAssetFloatIsFinite(v0.y)
				|| !modAssetFloatIsFinite(v0.z)
				|| !modAssetFloatIsFinite(v1.x)
				|| !modAssetFloatIsFinite(v1.y)
				|| !modAssetFloatIsFinite(v1.z)
				|| !modAssetFloatIsFinite(v2.x)
				|| !modAssetFloatIsFinite(v2.y)
				|| !modAssetFloatIsFinite(v2.z)
				|| !modAssetFloatIsFinite(len2)
				|| len2 <= 0.000001f) {
			skipped_degenerate++;
			continue;
		}

		if (!meshAddTriangle(out_mesh, &v0, &v1, &v2)) {
			sysLogPrintf(LOG_WARNING,
				"MODASSET.COMPILER: colmesh triangle add failed for %s tri=%d/%d built=%d capacity=%d skipped_degenerate=%d len2=%g",
				source_path, i, obj_mesh.triangle_count, out_mesh->numtris,
				out_mesh->capacity, skipped_degenerate, len2);
			meshFree(out_mesh);
			objMeshFree(&obj_mesh);
			return -1;
		}
		out_mesh->tris[out_mesh->numtris - 1].roomnum =
			(RoomNum)tri->roomnum;
	}

	sysLogPrintf(LOG_NOTE,
		"MODASSET.COMPILER: built colmesh source=%s format=%s vertices=%d tris=%d skipped_degenerate=%d",
		source_path, source_kind, obj_mesh.vertex_count, out_mesh->numtris,
		skipped_degenerate);
	objMeshFree(&obj_mesh);
	return out_mesh->numtris > 0 ? 1 : -1;
}

s32 modAssetCompilerBuildObjColmesh(const char *source_path,
                                    struct colmesh *out_mesh)
{
	return modAssetCompilerBuildColmesh(source_path, out_mesh);
}

static s32 gltfAnimationSampleForFrame(const gltf_anim_channel_t *channel,
                                       s32 frame,
                                       s32 frame_count)
{
	if (!channel || channel->sample_count <= 1 || frame_count <= 1) {
		return 0;
	}
	return (s32)(((s64)frame * (s64)(channel->sample_count - 1))
		/ (s64)(frame_count - 1));
}

static s32 gltfBuildAnimationClipPayload(gltf_animation_clip_t *clip,
                                         s32 requested_frame_count,
                                         struct animtableentry *out_entry,
                                         u8 **out_data,
                                         u32 *out_data_size)
{
	s32 frame_count;
	s32 part_count;
	s32 header_len = 0;
	s32 bits_per_frame = 0;
	s32 bytes_per_frame;
	s32 *translation_for_part = NULL;
	s32 *rotation_for_part = NULL;
	s32 *scale_for_part = NULL;
	u8 *data = NULL;
	u32 total_size;

	if (!clip || !out_entry || !out_data || !out_data_size) {
		return 0;
	}

	frame_count = requested_frame_count > 0
		? requested_frame_count : clip->frame_count;
	if (frame_count <= 0) {
		frame_count = 1;
	}
	if (frame_count > 0xffff) {
		frame_count = 0xffff;
	}

	if (clip->channel_count <= 0) {
		data = malloc(1);
		if (!data) {
			return 0;
		}
		data[0] = 0;
		memset(out_entry, 0, sizeof(*out_entry));
		out_entry->numframes = (u16)frame_count;
		out_entry->bytesperframe = 0;
		out_entry->data = 0xffffffff;
		out_entry->headerlen = 1;
		out_entry->framelen = 16;
		out_entry->flags = 0;
		*out_data = data;
		*out_data_size = 1;
		return 1;
	}

	part_count = clip->part_count > 0 ? clip->part_count : 1;
	translation_for_part = malloc((size_t)part_count * sizeof(s32));
	rotation_for_part = malloc((size_t)part_count * sizeof(s32));
	scale_for_part = malloc((size_t)part_count * sizeof(s32));
	if (!translation_for_part || !rotation_for_part || !scale_for_part) {
		goto fail;
	}

	for (s32 i = 0; i < part_count; i++) {
		translation_for_part[i] = -1;
		rotation_for_part[i] = -1;
		scale_for_part[i] = -1;
	}

	for (s32 i = 0; i < clip->channel_count; i++) {
		gltf_anim_channel_t *channel = &clip->channels[i];
		if (channel->target_node < 0 || channel->target_node >= part_count) {
			goto fail;
		}
		if (channel->path == GLTF_ANIM_PATH_TRANSLATION) {
			translation_for_part[channel->target_node] = i;
			for (s32 axis = 0; axis < 3; axis++) {
				s32 min_value = floatToMilliS32(
					gltfChannelReadFloat(channel, 0, axis));
				for (s32 s = 1; s < channel->sample_count; s++) {
					s32 value = floatToMilliS32(
						gltfChannelReadFloat(channel, s, axis));
					if (value < min_value) {
						min_value = value;
					}
				}
				channel->translation_base[axis] = min_value;
			}
		} else if (channel->path == GLTF_ANIM_PATH_ROTATION) {
			rotation_for_part[channel->target_node] = i;
		} else if (channel->path == GLTF_ANIM_PATH_SCALE) {
			scale_for_part[channel->target_node] = i;
		}
	}

	for (s32 part = 0; part < part_count; part++) {
		header_len += 1;
		if (translation_for_part[part] >= 0) {
			header_len += 15;
			bits_per_frame += 96;
		}
		if (rotation_for_part[part] >= 0) {
			bits_per_frame += 96;
		}
		if (scale_for_part[part] >= 0) {
			bits_per_frame += 96;
		}
	}

	bytes_per_frame = (bits_per_frame + 7) / 8;
	if (header_len <= 0 || bytes_per_frame <= 0) {
		goto fail;
	}

	total_size = (u32)header_len + (u32)frame_count * (u32)bytes_per_frame;
	data = calloc(1, total_size);
	if (!data) {
		goto fail;
	}

	{
		u8 *p = data;
		for (s32 part = 0; part < part_count; part++) {
			u8 flags = 0;
			s32 trans_index = translation_for_part[part];
			if (trans_index >= 0) {
				flags |= ANIMFIELD_S32_TRANSLATE;
			}
			if (rotation_for_part[part] >= 0) {
				flags |= ANIMFIELD_F32_ROTATE;
			}
			if (scale_for_part[part] >= 0) {
				flags |= ANIMFIELD_F32_SCALE;
			}

			*p++ = flags;
			if (trans_index >= 0) {
				gltf_anim_channel_t *channel = &clip->channels[trans_index];
				for (s32 axis = 0; axis < 3; axis++) {
					*p++ = 32;
					writeBe32(p, (u32)channel->translation_base[axis]);
					p += 4;
				}
			}
		}
	}

	for (s32 frame = 0; frame < frame_count; frame++) {
		u8 *p = data + header_len + (u32)frame * (u32)bytes_per_frame;
		for (s32 part = 0; part < part_count; part++) {
			s32 trans_index = translation_for_part[part];
			s32 rot_index = rotation_for_part[part];
			s32 scale_index = scale_for_part[part];

			if (trans_index >= 0) {
				gltf_anim_channel_t *channel = &clip->channels[trans_index];
				s32 sample = gltfAnimationSampleForFrame(channel,
					frame, frame_count);
				for (s32 axis = 0; axis < 3; axis++) {
					s32 value = floatToMilliS32(
						gltfChannelReadFloat(channel, sample, axis));
					writeBe32(p, (u32)(value - channel->translation_base[axis]));
					p += 4;
				}
			}

			if (rot_index >= 0) {
				gltf_anim_channel_t *channel = &clip->channels[rot_index];
				s32 sample = gltfAnimationSampleForFrame(channel,
					frame, frame_count);
				f32 rx;
				f32 ry;
				f32 rz;
				quatToEuler(
					gltfChannelReadFloat(channel, sample, 0),
					gltfChannelReadFloat(channel, sample, 1),
					gltfChannelReadFloat(channel, sample, 2),
					gltfChannelReadFloat(channel, sample, 3),
					&rx, &ry, &rz);
				writeBeFloat(p + 0, rx);
				writeBeFloat(p + 4, ry);
				writeBeFloat(p + 8, rz);
				p += 12;
			}

			if (scale_index >= 0) {
				gltf_anim_channel_t *channel = &clip->channels[scale_index];
				s32 sample = gltfAnimationSampleForFrame(channel,
					frame, frame_count);
				writeBeFloat(p + 0, gltfChannelReadFloat(channel, sample, 0));
				writeBeFloat(p + 4, gltfChannelReadFloat(channel, sample, 1));
				writeBeFloat(p + 8, gltfChannelReadFloat(channel, sample, 2));
				p += 12;
			}
		}
	}

	memset(out_entry, 0, sizeof(*out_entry));
	out_entry->numframes = (u16)frame_count;
	out_entry->bytesperframe = (u16)bytes_per_frame;
	out_entry->data = 0xffffffff;
	out_entry->headerlen = (u16)header_len;
	out_entry->framelen = 16;
	out_entry->flags = 0;

	*out_data = data;
	*out_data_size = total_size;
	free(translation_for_part);
	free(rotation_for_part);
	free(scale_for_part);
	return 1;

fail:
	free(data);
	free(translation_for_part);
	free(rotation_for_part);
	free(scale_for_part);
	return 0;
}

s32 modAssetCompilerBuildAnimationClip(const asset_entry_t *entry,
                                       const char *source_path,
                                       struct animtableentry *out_entry,
                                       u8 **out_data,
                                       u32 *out_data_size)
{
	u32 source_size = 0;
	void *source_bytes;
	gltf_animation_clip_t clip;
	s32 frame_count;
	s32 ok;

	if (!source_path || !source_path[0]
			|| (!endsWithNoCase(source_path, ".gltf")
				&& !endsWithNoCase(source_path, ".glb"))) {
		return 0;
	}
	if (!out_entry || !out_data || !out_data_size) {
		return -1;
	}

	*out_data = NULL;
	*out_data_size = 0;
	memset(out_entry, 0, sizeof(*out_entry));
	memset(&clip, 0, sizeof(clip));

	source_bytes = fsFileLoad(source_path, &source_size);
	if (!source_bytes || source_size == 0) {
		sysLogPrintf(LOG_WARNING,
			"MODASSET.COMPILER: animation source missing for '%s': %s",
			entry ? entry->id : "(unknown)", source_path);
		if (source_bytes) {
			free(source_bytes);
		}
		return -1;
	}

	if (!parseGltfLikeAnimationClip(source_path, (const u8 *)source_bytes,
			source_size, &clip)) {
		sysLogPrintf(LOG_WARNING,
			"MODASSET.COMPILER: animation source invalid for '%s': %s (%s)",
			entry ? entry->id : "(unknown)", source_path,
			clip.info.error[0] ? clip.info.error : "parse_failed");
		free(source_bytes);
		return -1;
	}

	frame_count = entry && entry->ext.anim.frame_count > 0
		? entry->ext.anim.frame_count : clip.frame_count;
	if (frame_count <= 0) {
		frame_count = 1;
	}
	if (frame_count > 0xffff) {
		frame_count = 0xffff;
	}

	ok = gltfBuildAnimationClipPayload(&clip, frame_count, out_entry,
		out_data, out_data_size);
	if (!ok) {
		sysLogPrintf(LOG_WARNING,
			"MODASSET.COMPILER: animation clip allocation failed for '%s'",
			entry ? entry->id : "(unknown)");
		gltfAnimationClipFree(&clip);
		free(source_bytes);
		return -1;
	}

	sysLogPrintf(LOG_NOTE,
		"MODASSET.COMPILER: built animation clip '%s' source=%s clips=%d channels=%d frames=%d boundary=animtableentry",
		entry ? entry->id : "(unknown)", source_path,
		clip.info.animation_count, clip.channel_count, frame_count);
	gltfAnimationClipFree(&clip);
	free(source_bytes);
	return 1;
}

void modAssetCompilerFreeAnimationClip(void *data)
{
	free(data);
}

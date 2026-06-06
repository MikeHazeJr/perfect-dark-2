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
#include "assetcatalog.h"
#include "data.h"
#include "game/tex.h"
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

typedef struct obj_texcoord {
	f32 u;
	f32 v;
} obj_texcoord_t;

typedef struct obj_material {
	char name[64];
	char texture_catalog_id[CATALOG_ID_LEN];
	char secondary_texture_catalog_id[CATALOG_ID_LEN];
	s32 texture_num;
	s32 secondary_texture_num;
	s32 subcmd;
	s32 smode;
	s32 tmode;
	s32 offset;
	s32 shifts;
	s32 shiftt;
	s32 min;
	s32 flag;
} obj_material_t;

typedef struct obj_group {
	char name[64];
} obj_group_t;

typedef struct obj_triangle {
	s32 a;
	s32 b;
	s32 c;
	s32 ta;
	s32 tb;
	s32 tc;
	s32 roomnum;
	s32 material_index;
	s32 group_index;
	s32 matrix_index;
} obj_triangle_t;

typedef struct obj_mesh {
	obj_vertex_t *vertices;
	s32 vertex_count;
	s32 vertex_capacity;
	obj_texcoord_t *texcoords;
	s32 texcoord_count;
	s32 texcoord_capacity;
	obj_material_t *materials;
	s32 material_count;
	s32 material_capacity;
	obj_group_t *groups;
	s32 group_count;
	s32 group_capacity;
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
	free(mesh->texcoords);
	free(mesh->materials);
	free(mesh->groups);
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

static s32 objMeshGrowTexcoords(obj_mesh_t *mesh, s32 needed)
{
	s32 newcap;
	obj_texcoord_t *newptr;

	if (mesh->texcoord_count + needed <= mesh->texcoord_capacity) {
		return 1;
	}

	newcap = mesh->texcoord_capacity * 2;
	if (newcap < mesh->texcoord_count + needed) {
		newcap = mesh->texcoord_count + needed;
	}
	if (newcap < 64) {
		newcap = 64;
	}

	newptr = realloc(mesh->texcoords,
		(size_t)newcap * sizeof(*mesh->texcoords));
	if (!newptr) {
		objMeshSetError(mesh, 0, "texcoord_alloc_failed");
		return 0;
	}

	mesh->texcoords = newptr;
	mesh->texcoord_capacity = newcap;
	return 1;
}

static s32 objMeshGrowMaterials(obj_mesh_t *mesh, s32 needed)
{
	s32 newcap;
	obj_material_t *newptr;

	if (mesh->material_count + needed <= mesh->material_capacity) {
		return 1;
	}

	newcap = mesh->material_capacity * 2;
	if (newcap < mesh->material_count + needed) {
		newcap = mesh->material_count + needed;
	}
	if (newcap < 8) {
		newcap = 8;
	}

	newptr = realloc(mesh->materials,
		(size_t)newcap * sizeof(*mesh->materials));
	if (!newptr) {
		objMeshSetError(mesh, 0, "material_alloc_failed");
		return 0;
	}

	memset(newptr + mesh->material_capacity, 0,
		(size_t)(newcap - mesh->material_capacity) * sizeof(*newptr));
	mesh->materials = newptr;
	mesh->material_capacity = newcap;
	return 1;
}

static s32 objMeshGrowGroups(obj_mesh_t *mesh, s32 needed)
{
	s32 newcap;
	obj_group_t *newptr;

	if (mesh->group_count + needed <= mesh->group_capacity) {
		return 1;
	}

	newcap = mesh->group_capacity * 2;
	if (newcap < mesh->group_count + needed) {
		newcap = mesh->group_count + needed;
	}
	if (newcap < 8) {
		newcap = 8;
	}

	newptr = realloc(mesh->groups, (size_t)newcap * sizeof(*mesh->groups));
	if (!newptr) {
		objMeshSetError(mesh, 0, "group_alloc_failed");
		return 0;
	}

	memset(newptr + mesh->group_capacity, 0,
		(size_t)(newcap - mesh->group_capacity) * sizeof(*newptr));
	mesh->groups = newptr;
	mesh->group_capacity = newcap;
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

static void copyObjToken(char *dst, size_t dst_n, const char *src)
{
	size_t len = 0;

	if (!dst || dst_n == 0) {
		return;
	}
	dst[0] = '\0';
	if (!src) {
		return;
	}
	while (*src && isspace((u8)*src)) {
		src++;
	}
	while (src[len] && !isspace((u8)src[len]) && src[len] != '#') {
		len++;
	}
	while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t')) {
		len--;
	}
	if (len >= dst_n) {
		len = dst_n - 1;
	}
	memcpy(dst, src, len);
	dst[len] = '\0';
}

static s32 objMeshEnsureMaterial(obj_mesh_t *mesh, const char *name)
{
	char material_name[64];

	if (!mesh) {
		return -1;
	}

	copyObjToken(material_name, sizeof(material_name),
		name && name[0] ? name : "pd_default");
	if (!material_name[0]) {
		strncpy(material_name, "pd_default", sizeof(material_name) - 1);
		material_name[sizeof(material_name) - 1] = '\0';
	}

	for (s32 i = 0; i < mesh->material_count; i++) {
		if (strcmp(mesh->materials[i].name, material_name) == 0) {
			return i;
		}
	}

	if (!objMeshGrowMaterials(mesh, 1)) {
		return -1;
	}

	obj_material_t *material = &mesh->materials[mesh->material_count];
	memset(material, 0, sizeof(*material));
	strncpy(material->name, material_name, sizeof(material->name) - 1);
	material->name[sizeof(material->name) - 1] = '\0';
	material->texture_num = -1;
	material->secondary_texture_num = -1;
	material->subcmd = -1;
	mesh->material_count++;
	return mesh->material_count - 1;
}

static s32 objMeshEnsureGroup(obj_mesh_t *mesh, const char *name)
{
	char group_name[64];

	if (!mesh) {
		return -1;
	}

	copyObjToken(group_name, sizeof(group_name),
		name && name[0] ? name : "default");
	if (!group_name[0]) {
		strncpy(group_name, "default", sizeof(group_name) - 1);
		group_name[sizeof(group_name) - 1] = '\0';
	}

	for (s32 i = 0; i < mesh->group_count; i++) {
		if (strcmp(mesh->groups[i].name, group_name) == 0) {
			return i;
		}
	}

	if (!objMeshGrowGroups(mesh, 1)) {
		return -1;
	}

	obj_group_t *group = &mesh->groups[mesh->group_count];
	memset(group, 0, sizeof(*group));
	strncpy(group->name, group_name, sizeof(group->name) - 1);
	group->name[sizeof(group->name) - 1] = '\0';
	mesh->group_count++;
	return mesh->group_count - 1;
}

static s32 objMeshAddVertex(obj_mesh_t *mesh, f32 x, f32 y, f32 z, s32 roomnum)
{
	obj_vertex_t *v;

	if (!objMeshGrowVertices(mesh, 1)) {
		return 0;
	}

	v = &mesh->vertices[mesh->vertex_count++];
	v->x = x;
	v->y = y;
	v->z = z;
	v->roomnum = roomnum > 0 ? roomnum : 0;
	return 1;
}

static s32 objMeshAddTexcoord(obj_mesh_t *mesh, f32 u, f32 v)
{
	obj_texcoord_t *tc;

	if (!objMeshGrowTexcoords(mesh, 1)) {
		return 0;
	}

	tc = &mesh->texcoords[mesh->texcoord_count++];
	tc->u = u;
	tc->v = v;
	return 1;
}

static s32 objMeshAddTriangleWithTexcoords(obj_mesh_t *mesh,
	s32 a, s32 b, s32 c, s32 ta, s32 tb, s32 tc, s32 roomnum,
	s32 material_index, s32 group_index)
{
	obj_triangle_t *tri;

	if (!objMeshGrowTriangles(mesh, 1)) {
		return 0;
	}

	tri = &mesh->triangles[mesh->triangle_count++];
	tri->a = a;
	tri->b = b;
	tri->c = c;
	tri->ta = ta;
	tri->tb = tb;
	tri->tc = tc;
	tri->roomnum = roomnum > 0 ? roomnum : 0;
	tri->material_index = material_index >= 0 ? material_index : -1;
	tri->group_index = group_index >= 0 ? group_index : -1;
	tri->matrix_index = -1;
	if (a >= 0 && b >= 0 && c >= 0
			&& a < mesh->vertex_count
			&& b < mesh->vertex_count
			&& c < mesh->vertex_count) {
		s32 ar = mesh->vertices[a].roomnum;
		s32 br = mesh->vertices[b].roomnum;
		s32 cr = mesh->vertices[c].roomnum;
		if (tri->roomnum <= 0 && ar == br && br == cr) {
			tri->roomnum = ar;
		}
	}
	return 1;
}

static s32 objMeshAddTriangle(obj_mesh_t *mesh, s32 a, s32 b, s32 c, s32 roomnum)
{
	return objMeshAddTriangleWithTexcoords(mesh, a, b, c, -1, -1, -1,
		roomnum, -1, -1);
}

static char *skipSpaces(char *p);

static s32 parseObjRoomName(char *line)
{
	char *p = line;

	if (!p) {
		return 0;
	}

	while (*p && !isspace((u8)*p)) {
		p++;
	}
	p = skipSpaces(p);
	if (strncmp(p, "room_", 5) == 0) {
		p += 5;
		while (*p == '0' && isdigit((u8)p[1])) {
			p++;
		}
		if (isdigit((u8)*p)) {
			return (s32)strtol(p, NULL, 10);
		}
	}
	return 0;
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

static s32 resolveObjIndex(long raw, s32 count, s32 *out_index)
{
	s32 index;

	if (!out_index || raw == 0 || count <= 0) {
		return 0;
	}

	if (raw > 0) {
		index = (s32)raw - 1;
	} else {
		index = count + (s32)raw;
	}

	if (index < 0 || index >= count) {
		return 0;
	}

	*out_index = index;
	return 1;
}

static s32 parseObjIndexToken(char **cursor, s32 vertex_count,
	s32 texcoord_count, s32 *out_index, s32 *out_texcoord)
{
	char *p;
	char *end;
	long raw;
	s32 index = -1;
	s32 texcoord = -1;

	if (!cursor || !*cursor || !out_index || !out_texcoord) {
		return 0;
	}

	p = skipSpaces(*cursor);
	raw = strtol(p, &end, 10);
	if (end == p || !resolveObjIndex(raw, vertex_count, &index)) {
		return 0;
	}

	if (*end == '/') {
		char *tex_start = end + 1;

		if (*tex_start != '/' && *tex_start != '\0'
				&& !isspace((u8)*tex_start)) {
			long raw_texcoord;
			char *tex_end;

			raw_texcoord = strtol(tex_start, &tex_end, 10);
			if (tex_end == tex_start
					|| !resolveObjIndex(raw_texcoord, texcoord_count,
						&texcoord)) {
				return 0;
			}
			end = tex_end;
		} else {
			end = tex_start;
		}
	}

	if (*end == '/') {
		end++;
	}

	while (*end && !isspace((u8)*end)) {
		end++;
	}

	*out_index = index;
	*out_texcoord = texcoord;
	*cursor = end;
	return 1;
}

static s32 parseObjVertexLine(char *line, obj_mesh_t *mesh, s32 line_no,
	s32 current_room)
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

	return objMeshAddVertex(mesh, x, y, z, current_room);
}

static s32 parseObjTexcoordLine(char *line, obj_mesh_t *mesh, s32 line_no)
{
	f32 u;
	f32 v;
	char *p = line + 2;

	if (!parseObjFloat(&p, &u) || !parseObjFloat(&p, &v)) {
		objMeshSetError(mesh, line_no, "invalid_texcoord");
		return 0;
	}

	return objMeshAddTexcoord(mesh, u, v);
}

static s32 parseObjFaceLine(char *line, obj_mesh_t *mesh, s32 line_no,
	s32 current_room, s32 current_material, s32 current_group)
{
	char *p = line + 1;
	s32 indices[128];
	s32 texcoords[128];
	s32 count = 0;

	while (1) {
		s32 index;
		s32 texcoord;

		p = skipSpaces(p);
		if (!p || *p == '\0' || *p == '#') {
			break;
		}

		if (count >= (s32)(sizeof(indices) / sizeof(indices[0]))) {
			objMeshSetError(mesh, line_no, "face_has_too_many_vertices");
			return 0;
		}

		if (!parseObjIndexToken(&p, mesh->vertex_count,
				mesh->texcoord_count, &index, &texcoord)) {
			objMeshSetError(mesh, line_no, "invalid_face_index");
			return 0;
		}

		indices[count++] = index;
		texcoords[count - 1] = texcoord;
	}

	if (count < 3) {
		objMeshSetError(mesh, line_no, "face_needs_three_vertices");
		return 0;
	}

	for (s32 i = 2; i < count; i++) {
		if (!objMeshAddTriangleWithTexcoords(mesh,
				indices[0], indices[i - 1], indices[i],
				texcoords[0], texcoords[i - 1], texcoords[i],
				current_room, current_material, current_group)) {
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
	s32 current_room = 0;
	s32 current_material = -1;
	s32 current_group = -1;

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
	current_material = objMeshEnsureMaterial(mesh, "pd_default");
	current_group = objMeshEnsureGroup(mesh, "default");

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
		if (*p == 'v' && p[1] == 't' && isspace((u8)p[2])) {
			ok = parseObjTexcoordLine(p, mesh, line_no);
		} else if (*p == 'v' && isspace((u8)p[1])) {
			ok = parseObjVertexLine(p, mesh, line_no, current_room);
		} else if (*p == 'f' && isspace((u8)p[1])) {
			ok = parseObjFaceLine(p, mesh, line_no, current_room,
				current_material, current_group);
		} else if ((*p == 'g' || *p == 'o') && isspace((u8)p[1])) {
			current_room = parseObjRoomName(p);
			current_group = objMeshEnsureGroup(mesh, skipSpaces(p + 1));
			if (current_group < 0) {
				ok = 0;
			}
		} else if (strncmp(p, "usemtl", 6) == 0 && isspace((u8)p[6])) {
			char *name = skipSpaces(p + 6);
			current_material = objMeshEnsureMaterial(mesh, name);
			if (current_material < 0) {
				ok = 0;
			}
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
				readLeFloat(p + 8),
				0)) {
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
				vertex_base + (s32)ic,
				0)) {
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
				vertex_base + i + 2,
				0)) {
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
			snprintf(summary, summary_len,
				"format=obj vertices=%d texcoords=%d triangles=%d",
				mesh.vertex_count, mesh.texcoord_count, mesh.triangle_count);
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

static s32 ensureCacheDirs(const char *cache_root,
                           const char *mod_part, const char *asset_part)
{
	char path[FS_MAXPATH];

	if (!cache_root || !cache_root[0] || !fsCreateDir(cache_root)) {
		return 0;
	}

	snprintf(path, sizeof(path), "%s/%s", cache_root, mod_part);
	if (!fsCreateDir(path)) {
		return 0;
	}

	snprintf(path, sizeof(path), "%s/%s/%s", cache_root, mod_part, asset_part);
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
	fprintf(f, "  \"texcoord_count\": %d,\n", mesh->texcoord_count);
	fprintf(f, "  \"triangle_count\": %d,\n", mesh->triangle_count);
	fprintf(f, "  \"vertices\": [\n");
	for (s32 i = 0; i < mesh->vertex_count; i++) {
		const obj_vertex_t *v = &mesh->vertices[i];
		fprintf(f, "    [%.9g, %.9g, %.9g]%s\n",
			v->x, v->y, v->z, i + 1 == mesh->vertex_count ? "" : ",");
	}
	fprintf(f, "  ],\n");
	fprintf(f, "  \"texcoords\": [\n");
	for (s32 i = 0; i < mesh->texcoord_count; i++) {
		const obj_texcoord_t *tc = &mesh->texcoords[i];
		fprintf(f, "    [%.9g, %.9g]%s\n",
			tc->u, tc->v, i + 1 == mesh->texcoord_count ? "" : ",");
	}
	fprintf(f, "  ],\n");
	fprintf(f, "  \"triangles\": [\n");
	for (s32 i = 0; i < mesh->triangle_count; i++) {
		const obj_triangle_t *tri = &mesh->triangles[i];
		fprintf(f, "    [%d, %d, %d]%s\n",
			tri->a, tri->b, tri->c, i + 1 == mesh->triangle_count ? "" : ",");
	}
	fprintf(f, "  ],\n");
	fprintf(f, "  \"triangle_texcoords\": [\n");
	for (s32 i = 0; i < mesh->triangle_count; i++) {
		const obj_triangle_t *tri = &mesh->triangles[i];
		fprintf(f, "    [%d, %d, %d]%s\n",
			tri->ta, tri->tb, tri->tc,
			i + 1 == mesh->triangle_count ? "" : ",");
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
	fprintf(f, "  \"texcoord_count\": %d,\n", mesh->texcoord_count);
	fprintf(f, "  \"triangle_count\": %d,\n", mesh->triangle_count);
	fprintf(f, "  \"vertices\": [\n");
	for (s32 i = 0; i < mesh->vertex_count; i++) {
		const obj_vertex_t *v = &mesh->vertices[i];
		fprintf(f, "    [%.9g, %.9g, %.9g]%s\n",
			v->x, v->y, v->z, i + 1 == mesh->vertex_count ? "" : ",");
	}
	fprintf(f, "  ],\n");
	fprintf(f, "  \"texcoords\": [\n");
	for (s32 i = 0; i < mesh->texcoord_count; i++) {
		const obj_texcoord_t *tc = &mesh->texcoords[i];
		fprintf(f, "    [%.9g, %.9g]%s\n",
			tc->u, tc->v, i + 1 == mesh->texcoord_count ? "" : ",");
	}
	fprintf(f, "  ],\n");
	fprintf(f, "  \"triangles\": [\n");
	for (s32 i = 0; i < mesh->triangle_count; i++) {
		const obj_triangle_t *tri = &mesh->triangles[i];
		fprintf(f, "    [%d, %d, %d]%s\n",
			tri->a, tri->b, tri->c, i + 1 == mesh->triangle_count ? "" : ",");
	}
	fprintf(f, "  ],\n");
	fprintf(f, "  \"triangle_texcoords\": [\n");
	for (s32 i = 0; i < mesh->triangle_count; i++) {
		const obj_triangle_t *tri = &mesh->triangles[i];
		fprintf(f, "    [%d, %d, %d]%s\n",
			tri->ta, tri->tb, tri->tc,
			i + 1 == mesh->triangle_count ? "" : ",");
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
	char cache_root[FS_MAXPATH];
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

	snprintf(cache_root, sizeof(cache_root), "$S/mod-cache");
	if (!ensureCacheDirs(cache_root, mod_part, asset_part)) {
		snprintf(cache_root, sizeof(cache_root), "$B/mod-cache");
	}

	if (!ensureCacheDirs(cache_root, mod_part, asset_part)) {
		sysLogPrintf(LOG_WARNING,
			"MODASSET.COMPILER: could not create private cache dirs for '%s'",
			entry->id);
		free(source_bytes);
		return -1;
	}

	snprintf(cache_rel, sizeof(cache_rel),
		"%s/%s/%s/%s-v%d-%s.pdmc",
		cache_root, mod_part, asset_part, kind_part,
		MODASSET_COMPILER_VERSION, digest_hex);

	normalized_rel[0] = '\0';
	source_kind = sourceKindForPath(source_path);
	if (strcmp(source_kind, "obj") == 0 && assetKindUsesGeneratedModeldef(asset_kind)) {
		snprintf(normalized_rel, sizeof(normalized_rel),
			"%s/%s/%s/%s-v%d-%s.pdmodel.json",
			cache_root, mod_part, asset_part, kind_part,
			MODASSET_COMPILER_VERSION, digest_hex);
	} else if (strcmp(source_kind, "obj") == 0) {
		snprintf(normalized_rel, sizeof(normalized_rel),
			"%s/%s/%s/%s-v%d-%s.pdmesh.json",
			cache_root, mod_part, asset_part, kind_part,
			MODASSET_COMPILER_VERSION, digest_hex);
	} else if ((strcmp(source_kind, "gltf") == 0 || strcmp(source_kind, "glb") == 0)
			&& assetKindUsesGeneratedModeldef(asset_kind)) {
		snprintf(normalized_rel, sizeof(normalized_rel),
			"%s/%s/%s/%s-v%d-%s.pdmodel.json",
			cache_root, mod_part, asset_part, kind_part,
			MODASSET_COMPILER_VERSION, digest_hex);
	} else if ((strcmp(source_kind, "gltf") == 0 || strcmp(source_kind, "glb") == 0)
			&& assetKindUsesGeneratedAnimationClip(asset_kind)) {
		snprintf(normalized_rel, sizeof(normalized_rel),
			"%s/%s/%s/%s-v%d-%s.pdanimation.json",
			cache_root, mod_part, asset_part, kind_part,
			MODASSET_COMPILER_VERSION, digest_hex);
	} else if (strcmp(source_kind, "gltf") == 0 || strcmp(source_kind, "glb") == 0) {
		snprintf(normalized_rel, sizeof(normalized_rel),
			"%s/%s/%s/%s-v%d-%s.pdmesh.json",
			cache_root, mod_part, asset_part, kind_part,
			MODASSET_COMPILER_VERSION, digest_hex);
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
	struct generated_modeldef *next;
	struct modelnode root_node;
	struct modelnode bbox_node;
	struct modelnode toggle_node;
	struct modelnode dl_node;
	struct modelnode logo_toggle_nodes[2];
	struct modelnode logo_dl_nodes[8];
	union modelrodata root_rodata;
	union modelrodata bbox_rodata;
	union modelrodata toggle_rodata;
	union modelrodata dl_rodata;
	union modelrodata logo_toggle_rodatas[2];
	union modelrodata logo_dl_rodatas[8];
	struct {
		struct modelnode *nodes[4];
		s16 partnums[4];
	} cctv_part_table;
	struct {
		struct modelnode *nodes[2];
		s16 partnums[2];
	} autogun_part_table;
	struct {
		struct modelnode *nodes[3];
		s16 partnums[3];
	} windowed_door_part_table;
	struct {
		struct modelnode *nodes[4];
		s16 partnums[4];
	} logo_part_table;
	Vtx *vertices;
	Col *colours;
	Gfx *gdl;
	struct modelnode *dynamic_nodes;
	union modelrodata *dynamic_rodatas;
	void *dynamic_parts;
	struct generated_dl_payload *dynamic_payloads;
	s32 dynamic_payload_count;
	s32 triangle_count;
	s32 vertex_count;
	s32 render_audit_logged;
	char catalog_id[CATALOG_ID_LEN];
	char source_path[FS_MAXPATH + 1];
} generated_modeldef_t;

typedef struct generated_dl_payload {
	Vtx *vertices;
	Col *colours;
	Gfx *gdl;
	void *baseaddr;
	Gfx *seg_gdl;
	size_t gdl_bytes;
	size_t vertex_bytes;
	s32 vertex_count;
	s32 triangle_count;
} generated_dl_payload_t;

typedef struct generated_hierarchy_row {
	s32 id;
	s32 parent;
	s32 type;
	s32 partnum;
	s32 part;
	s32 mtx0;
	s32 mtx1;
	s32 mtx2;
	f32 pos_x;
	f32 pos_y;
	f32 pos_z;
	f32 drawdist;
	s32 target;
	char group[64];
	s32 render_mtx;
	s32 mcount;
	s32 hitpart;
	f32 xmin;
	f32 xmax;
	f32 ymin;
	f32 ymax;
	f32 zmin;
	f32 zmax;
	f32 distance_near;
	f32 distance_far;
	f32 reorder_x;
	f32 reorder_y;
	f32 reorder_z;
	f32 reorder_axis_x;
	f32 reorder_axis_y;
	f32 reorder_axis_z;
	s32 reorder_target_a;
	s32 reorder_target_b;
	s32 reorder_side;
	s32 payload_index;
} generated_hierarchy_row_t;

typedef struct generated_hierarchy {
	generated_hierarchy_row_t *rows;
	s32 row_count;
	s32 row_capacity;
} generated_hierarchy_t;

enum {
	GENERATED_RENDER_OP_MTX = 1,
	GENERATED_RENDER_OP_POP,
	GENERATED_RENDER_OP_MATERIAL,
	GENERATED_RENDER_OP_TRI,
};

typedef struct generated_render_row {
	char group[64];
	u8 op;
	s32 face;
	s32 matrix;
	u8 params;
	s32 material;
} generated_render_row_t;

typedef struct generated_render_stream {
	generated_render_row_t *rows;
	s32 row_count;
	s32 row_capacity;
} generated_render_stream_t;

static generated_modeldef_t *s_GeneratedModeldefs = NULL;
static s32 s_GeneratedModeldefRenderAuditEnabled = 0;

static void generatedModeldefRegister(generated_modeldef_t *owner)
{
	if (!owner) {
		return;
	}
	owner->next = s_GeneratedModeldefs;
	s_GeneratedModeldefs = owner;
}

static void generatedModeldefUnregister(generated_modeldef_t *owner)
{
	generated_modeldef_t **cursor = &s_GeneratedModeldefs;

	while (*cursor) {
		if (*cursor == owner) {
			*cursor = owner->next;
			owner->next = NULL;
			return;
		}
		cursor = &(*cursor)->next;
	}
}

static generated_modeldef_t *generatedModeldefOwner(
	const struct modeldef *modeldef)
{
	generated_modeldef_t *owner = s_GeneratedModeldefs;

	while (owner) {
		if (&owner->def == modeldef) {
			return owner;
		}
		owner = owner->next;
	}

	return NULL;
}

void modAssetCompilerSetGeneratedModeldefRenderAudit(s32 enabled)
{
	s_GeneratedModeldefRenderAuditEnabled = enabled ? 1 : 0;
}

s32 modAssetCompilerGeneratedModeldefRenderAuditEnabled(void)
{
	return s_GeneratedModeldefRenderAuditEnabled;
}

s32 modAssetCompilerModeldefIsGenerated(const struct modeldef *modeldef)
{
	return generatedModeldefOwner(modeldef) != NULL;
}

void modAssetCompilerTraceGeneratedModeldefRender(
	const struct modeldef *modeldef,
	const struct modelnode *node)
{
	generated_modeldef_t *owner;

	if (!s_GeneratedModeldefRenderAuditEnabled) {
		return;
	}

	owner = generatedModeldefOwner(modeldef);
	if (!owner || owner->render_audit_logged) {
		return;
	}

	owner->render_audit_logged = 1;
	sysLogPrintf(LOG_NOTE,
		"MODASSET.RENDER: generated source modeldef rendered id=%s source=%s modeldef=%p node=%p vertices=%d tris=%d skeleton=%s",
		owner->catalog_id[0] ? owner->catalog_id : "(unknown)",
		owner->source_path[0] ? owner->source_path : "(unknown)",
		(void *)&owner->def,
		(void *)node,
		owner->vertex_count,
		owner->triangle_count,
		modAssetCompilerSkeletonSymbolForPointer(owner->def.skel) ?
			modAssetCompilerSkeletonSymbolForPointer(owner->def.skel) : "(none)");
}

void modAssetCompilerTraceGeneratedModeldefRenderStep(
	const struct modeldef *modeldef,
	const struct modelnode *node,
	const char *stage,
	const void *rwdata,
	const void *gdl,
	const void *vertices,
	const void *colours,
	s32 numvertices,
	s32 mcount)
{
	generated_modeldef_t *owner;

	if (!s_GeneratedModeldefRenderAuditEnabled) {
		return;
	}

	owner = generatedModeldefOwner(modeldef);
	if (!owner) {
		return;
	}

	sysLogPrintf(LOG_NOTE,
		"MODASSET.RENDER.STEP: id=%s stage=%s modeldef=%p node=%p rwdata=%p gdl=%p vertices=%p colours=%p numvertices=%d mcount=%d tris=%d",
		owner->catalog_id[0] ? owner->catalog_id : "(unknown)",
		stage ? stage : "(unknown)",
		(void *)&owner->def,
		(void *)node,
		rwdata,
		gdl,
		vertices,
		colours,
		numvertices,
		mcount,
		owner->triangle_count);
}

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

static const obj_texcoord_t *objMeshTexcoord(const obj_mesh_t *mesh,
                                             s32 index)
{
	if (!mesh || index < 0 || index >= mesh->texcoord_count) {
		return NULL;
	}
	return &mesh->texcoords[index];
}

static const obj_material_t *objMeshTriangleMaterial(const obj_mesh_t *mesh,
                                                     const obj_triangle_t *tri)
{
	if (!mesh || !tri || tri->material_index < 0
			|| tri->material_index >= mesh->material_count) {
		return NULL;
	}
	return &mesh->materials[tri->material_index];
}

static s32 objMaterialHasTexture(const obj_material_t *material)
{
	return material && material->texture_num >= 0;
}

static void emitGeneratedTextureMarker(Gfx **gdlptr,
                                       const obj_material_t *material)
{
	Gfx *gdl;
	u32 w0;
	u32 w1;
	s32 subcmd;

	if (!gdlptr || !*gdlptr || !objMaterialHasTexture(material)) {
		return;
	}

	gdl = *gdlptr;
	subcmd = material->subcmd >= 0 ? material->subcmd : 0;
	w0 = ((u32)G_NOOP << 24)
		| (((u32)material->smode & 3u) << 22)
		| (((u32)material->tmode & 3u) << 20)
		| (((u32)material->offset & 3u) << 18)
		| (((u32)material->shifts & 0x0fu) << 14)
		| (((u32)material->shiftt & 0x0fu) << 10)
		| (material->flag ? 0x200u : 0u)
		| ((u32)subcmd & 7u);
	w1 = (((u32)material->min & 0xffu) << 24)
		| (((u32)(material->secondary_texture_num >= 0
				? material->secondary_texture_num : 0) & 0xfffu) << 12)
		| ((u32)material->texture_num & 0xfffu);

	gdl->words.w0 = w0;
	gdl->words.w1 = w1;
	gdl++;
	*gdlptr = gdl;
}

static void emitGeneratedUntexturedState(Gfx **gdlptr)
{
	Gfx *gdl;

	if (!gdlptr || !*gdlptr) {
		return;
	}

	gdl = *gdlptr;
	gSPTexture(gdl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
	gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
	*gdlptr = gdl;
}

static void fillGeneratedVertex(Vtx *dst, const obj_vertex_t *src,
                                const obj_texcoord_t *texcoord)
{
	memset(dst, 0, sizeof(*dst));
	dst->x = clampToS16(src->x);
	dst->y = clampToS16(src->y);
	dst->z = clampToS16(src->z);
	dst->colour = 0;
	if (texcoord) {
		dst->s = clampToS16(texcoord->u * 32.0f);
		dst->t = clampToS16((1.0f - texcoord->v) * 32.0f);
	}
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

static void generatedModeldefConfigureSourceBounds(generated_modeldef_t *owner,
                                                   const obj_mesh_t *mesh)
{
	if (!owner || !mesh || owner->root_node.type == MODELNODETYPE_CHRINFO) {
		return;
	}

	owner->bbox_node.type = MODELNODETYPE_BBOX;
	owner->bbox_node.rodata = &owner->bbox_rodata;
	owner->bbox_node.parent = &owner->root_node;
	owner->bbox_node.next = &owner->dl_node;
	owner->bbox_node.prev = NULL;
	owner->bbox_node.child = NULL;
	generatedModeldefMeshBounds(mesh, &owner->bbox_rodata.bbox);

	owner->dl_node.parent = &owner->root_node;
	owner->dl_node.prev = &owner->bbox_node;
	owner->dl_node.next = NULL;
	owner->dl_node.child = NULL;
	owner->root_node.child = &owner->bbox_node;
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
	owner->bbox_node.prev = NULL;
	owner->bbox_node.child = NULL;
	generatedModeldefMeshBounds(mesh, &owner->bbox_rodata.bbox);

	owner->toggle_node.type = MODELNODETYPE_TOGGLE;
	owner->toggle_node.rodata = &owner->toggle_rodata;
	owner->toggle_node.parent = &owner->root_node;
	owner->toggle_node.prev = &owner->bbox_node;
	owner->toggle_node.next = NULL;
	owner->toggle_node.child = &owner->dl_node;
	owner->toggle_rodata.toggle.target = &owner->dl_node;

	owner->dl_node.parent = &owner->toggle_node;
	owner->dl_node.prev = NULL;
	owner->dl_node.next = NULL;
	owner->dl_node.child = NULL;
	owner->root_node.child = &owner->bbox_node;

	owner->cctv_part_table.nodes[0] = &owner->root_node;
	owner->cctv_part_table.partnums[0] = MODELPART_CCTV_CASING;
	owner->cctv_part_table.nodes[1] = &owner->dl_node;
	owner->cctv_part_table.partnums[1] = MODELPART_CCTV_LENS;
	owner->cctv_part_table.nodes[2] = &owner->bbox_node;
	owner->cctv_part_table.partnums[2] = MODELPART_CCTV_0002;
	owner->cctv_part_table.nodes[3] = &owner->toggle_node;
	owner->cctv_part_table.partnums[3] = MODELPART_CCTV_0003;

	owner->def.parts = owner->cctv_part_table.nodes;
	owner->def.numparts = 4;
	owner->def.nummatrices = 2;
}

static void generatedModeldefConfigureAutogunParts(generated_modeldef_t *owner)
{
	if (!owner || owner->def.skel != &g_SkelAutogun) {
		return;
	}

	owner->bbox_node.next = &owner->toggle_node;

	owner->toggle_node.type = MODELNODETYPE_POSITION;
	owner->toggle_node.rodata = &owner->toggle_rodata;
	owner->toggle_node.parent = &owner->root_node;
	owner->toggle_node.prev = &owner->bbox_node;
	owner->toggle_node.next = NULL;
	owner->toggle_node.child = &owner->dl_node;
	owner->toggle_rodata.position.pos.x = 0.0f;
	owner->toggle_rodata.position.pos.y = 0.0f;
	owner->toggle_rodata.position.pos.z = 0.0f;
	owner->toggle_rodata.position.part = 1;
	owner->toggle_rodata.position.mtxindex0 = 2;
	owner->toggle_rodata.position.mtxindex1 = -1;
	owner->toggle_rodata.position.mtxindex2 = -1;
	owner->toggle_rodata.position.drawdist = 0.0f;

	owner->dl_node.parent = &owner->toggle_node;
	owner->dl_node.prev = NULL;

	owner->autogun_part_table.nodes[0] = &owner->root_node;
	owner->autogun_part_table.partnums[0] = MODELPART_AUTOGUN_0001;
	owner->autogun_part_table.nodes[1] = &owner->toggle_node;
	owner->autogun_part_table.partnums[1] = MODELPART_AUTOGUN_0002;

	owner->def.parts = owner->autogun_part_table.nodes;
	owner->def.numparts = 2;
	owner->def.nummatrices = 3;
}

static void generatedModeldefConfigureWindowedDoorParts(
	generated_modeldef_t *owner)
{
	if (!owner || owner->def.skel != &g_SkelWindowedDoor) {
		return;
	}

	owner->bbox_node.type = MODELNODETYPE_BBOX;
	owner->bbox_node.rodata = &owner->bbox_rodata;
	owner->bbox_node.parent = &owner->root_node;
	owner->bbox_node.prev = NULL;
	owner->bbox_node.next = &owner->toggle_node;
	owner->bbox_node.child = NULL;

	owner->toggle_node.type = MODELNODETYPE_TOGGLE;
	owner->toggle_node.rodata = &owner->toggle_rodata;
	owner->toggle_node.parent = &owner->root_node;
	owner->toggle_node.prev = &owner->bbox_node;
	owner->toggle_node.next = &owner->dl_node;
	owner->toggle_node.child = NULL;
	owner->toggle_rodata.toggle.target = NULL;

	owner->dl_node.parent = &owner->root_node;
	owner->dl_node.prev = &owner->toggle_node;
	owner->dl_node.next = NULL;
	owner->dl_node.child = NULL;
	owner->root_node.child = &owner->bbox_node;

	owner->windowed_door_part_table.nodes[0] = &owner->bbox_node;
	owner->windowed_door_part_table.partnums[0] = MODELPART_WINDOWEDDOOR_0000;
	owner->windowed_door_part_table.nodes[1] = &owner->toggle_node;
	owner->windowed_door_part_table.partnums[1] = MODELPART_WINDOWEDDOOR_0001;
	owner->windowed_door_part_table.nodes[2] = &owner->bbox_node;
	owner->windowed_door_part_table.partnums[2] = MODELPART_WINDOWEDDOOR_0002;

	owner->def.parts = owner->windowed_door_part_table.nodes;
	owner->def.numparts = 3;
}

static void generatedModeldefConfigureLogoParts(generated_modeldef_t *owner)
{
	if (!owner || (owner->def.skel != &g_SkelLogo &&
			owner->def.skel != &g_SkelPdLogo)) {
		return;
	}

	owner->root_node.child = &owner->logo_toggle_nodes[0];

	for (s32 i = 0; i < 2; i++) {
		struct modelnode *toggle = &owner->logo_toggle_nodes[i];

		toggle->type = MODELNODETYPE_TOGGLE;
		toggle->rodata = &owner->logo_toggle_rodatas[i];
		toggle->parent = &owner->root_node;
		toggle->child = &owner->logo_dl_nodes[i];
		toggle->prev = i == 0 ? NULL : &owner->logo_toggle_nodes[i - 1];
		toggle->next = i == 0 ? &owner->logo_toggle_nodes[1] : NULL;
		owner->logo_toggle_rodatas[i].toggle.target = &owner->logo_dl_nodes[i];
	}

	for (s32 i = 0; i < 2; i++) {
		struct modelnode *node = &owner->logo_dl_nodes[i];

		node->type = MODELNODETYPE_DL;
		node->rodata = &owner->logo_dl_rodatas[i];
		node->parent = &owner->logo_toggle_nodes[i];
		node->prev = NULL;
		node->next = NULL;
		owner->logo_dl_rodatas[i].dl = owner->dl_rodata.dl;
	}

	owner->logo_part_table.nodes[0] = &owner->logo_toggle_nodes[0];
	owner->logo_part_table.partnums[0] = MODELPART_LOGO_0000;
	owner->logo_part_table.nodes[1] = &owner->logo_toggle_nodes[1];
	owner->logo_part_table.partnums[1] = MODELPART_LOGO_0001;

	owner->logo_part_table.nodes[2] = &owner->logo_dl_nodes[0];
	owner->logo_part_table.partnums[2] = MODELPART_LOGO_FRONTSIDE;
	owner->logo_part_table.nodes[3] = &owner->logo_dl_nodes[1];
	owner->logo_part_table.partnums[3] = MODELPART_LOGO_0003;

	owner->def.parts = owner->logo_part_table.nodes;
	owner->def.numparts = 4;
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

static obj_material_t *objMeshFindMaterial(obj_mesh_t *mesh, const char *name)
{
	char material_name[64];

	if (!mesh || !name || !name[0]) {
		return NULL;
	}

	copyObjToken(material_name, sizeof(material_name), name);
	if (!material_name[0]) {
		return NULL;
	}

	for (s32 i = 0; i < mesh->material_count; i++) {
		if (strcmp(mesh->materials[i].name, material_name) == 0) {
			return &mesh->materials[i];
		}
	}

	return NULL;
}

static s32 parseObjMaterialInt(const char *text, s32 *out)
{
	char *end;
	long value;

	if (!text || !out) {
		return 0;
	}

	while (*text == ' ' || *text == '\t' || *text == '=') {
		text++;
	}

	value = strtol(text, &end, 0);
	if (end == text) {
		return 0;
	}

	*out = (s32)value;
	return 1;
}

static void objMaterialSetTextureCatalog(obj_material_t *material,
                                         const char *catalog_id,
                                         s32 secondary)
{
	const asset_entry_t *entry;
	s32 texture_num;
	char id[CATALOG_ID_LEN];

	if (!material || !catalog_id) {
		return;
	}

	copyObjToken(id, sizeof(id), catalog_id);
	if (!id[0]) {
		return;
	}

	entry = assetCatalogResolve(id);
	if (!entry || entry->type != ASSET_TEXTURE) {
		sysLogPrintf(LOG_WARNING,
			"MODASSET.COMPILER: material '%s' references unknown texture catalog id '%s'",
			material->name, id);
		return;
	}
	texture_num = entry->source_texnum >= 0 ?
		entry->source_texnum : entry->ext.texture.texture_id;
	if (texture_num < 0) {
		sysLogPrintf(LOG_WARNING,
			"MODASSET.COMPILER: material '%s' references texture catalog id '%s' without a texture table id",
			material->name, id);
		return;
	}

	if (secondary) {
		strncpy(material->secondary_texture_catalog_id, id,
			sizeof(material->secondary_texture_catalog_id) - 1);
		material->secondary_texture_catalog_id[
			sizeof(material->secondary_texture_catalog_id) - 1] = '\0';
		material->secondary_texture_num = texture_num;
	} else {
		strncpy(material->texture_catalog_id, id,
			sizeof(material->texture_catalog_id) - 1);
		material->texture_catalog_id[
			sizeof(material->texture_catalog_id) - 1] = '\0';
		material->texture_num = texture_num;
	}
}

static void generatedModeldefLoadMaterialMetadata(const char *source_path,
                                                  obj_mesh_t *mesh)
{
	char mtl_path[FS_MAXPATH + 1];
	u32 size = 0;
	char *text;
	char *line;
	obj_material_t *current = NULL;

	if (!mesh || !source_path || !source_path[0]) {
		return;
	}
	if (!generatedModeldefMetadataPath(source_path, "model.mtl",
			mtl_path, sizeof(mtl_path))) {
		return;
	}

	text = (char *)fsFileLoad(mtl_path, &size);
	if (!text || size == 0) {
		if (text) {
			free(text);
		}
		return;
	}

	char *copy = malloc((size_t)size + 1);
	if (!copy) {
		free(text);
		return;
	}
	memcpy(copy, text, size);
	copy[size] = '\0';
	free(text);

	line = copy;
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
		if (strncmp(p, "newmtl", 6) == 0 && isspace((u8)p[6])) {
			current = objMeshFindMaterial(mesh, skipSpaces(p + 6));
		} else if (current && strncmp(p, "pd_texture_catalog", 18) == 0) {
			char *value = strchr(p, '=');
			if (value) {
				objMaterialSetTextureCatalog(current, value + 1, 0);
			}
		} else if (current &&
				strncmp(p, "pd_secondary_texture_catalog", 28) == 0) {
			char *value = strchr(p, '=');
			if (value) {
				objMaterialSetTextureCatalog(current, value + 1, 1);
			}
		} else if (current && strncmp(p, "pd_texture_subcmd", 17) == 0) {
			char *value = strchr(p, '=');
			if (value) parseObjMaterialInt(value + 1, &current->subcmd);
		} else if (current && strncmp(p, "pd_texture_smode", 16) == 0) {
			char *value = strchr(p, '=');
			if (value) parseObjMaterialInt(value + 1, &current->smode);
		} else if (current && strncmp(p, "pd_texture_tmode", 16) == 0) {
			char *value = strchr(p, '=');
			if (value) parseObjMaterialInt(value + 1, &current->tmode);
		} else if (current && strncmp(p, "pd_texture_offset", 17) == 0) {
			char *value = strchr(p, '=');
			if (value) parseObjMaterialInt(value + 1, &current->offset);
		} else if (current && strncmp(p, "pd_texture_shifts", 17) == 0) {
			char *value = strchr(p, '=');
			if (value) parseObjMaterialInt(value + 1, &current->shifts);
		} else if (current && strncmp(p, "pd_texture_shiftt", 17) == 0) {
			char *value = strchr(p, '=');
			if (value) parseObjMaterialInt(value + 1, &current->shiftt);
		} else if (current && strncmp(p, "pd_texture_min", 14) == 0) {
			char *value = strchr(p, '=');
			if (value) parseObjMaterialInt(value + 1, &current->min);
		} else if (current && strncmp(p, "pd_texture_flag", 15) == 0) {
			char *value = strchr(p, '=');
			if (value) parseObjMaterialInt(value + 1, &current->flag);
		}

		line = next;
	}

	free(copy);
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

static f32 generatedModeldefScaleFromMetadata(const char *source_path)
{
	static const char *members[] = {
		"mesh.ini",
		"_meta/manifest.json",
	};
	char metadata_path[FS_MAXPATH + 1];
	char scale_text[64];

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

		scale_text[0] = '\0';
		if (generatedModeldefReadStringValue(text, "model_scale",
				scale_text, sizeof(scale_text))) {
			char *end = NULL;
			double scale = strtod(scale_text, &end);
			free(text);
			if (end != scale_text && scale > 0.0 &&
					scale < 1000000000.0) {
				return (f32)scale;
			}
			continue;
		}

		free(text);
	}

	return 1.0f;
}

static s32 objMeshGroupIndexByName(const obj_mesh_t *mesh, const char *name)
{
	if (!mesh || !name || !name[0] || strcmp(name, "-") == 0) {
		return -1;
	}
	for (s32 i = 0; i < mesh->group_count; i++) {
		if (strcmp(mesh->groups[i].name, name) == 0) {
			return i;
		}
	}
	return -1;
}

static void generatedHierarchyFree(generated_hierarchy_t *hierarchy)
{
	if (!hierarchy) {
		return;
	}
	free(hierarchy->rows);
	memset(hierarchy, 0, sizeof(*hierarchy));
}

static s32 generatedHierarchyGrow(generated_hierarchy_t *hierarchy, s32 needed)
{
	s32 newcap;
	generated_hierarchy_row_t *newptr;

	if (hierarchy->row_count + needed <= hierarchy->row_capacity) {
		return 1;
	}

	newcap = hierarchy->row_capacity * 2;
	if (newcap < hierarchy->row_count + needed) {
		newcap = hierarchy->row_count + needed;
	}
	if (newcap < 64) {
		newcap = 64;
	}

	newptr = realloc(hierarchy->rows,
		(size_t)newcap * sizeof(*hierarchy->rows));
	if (!newptr) {
		return 0;
	}
	memset(newptr + hierarchy->row_capacity, 0,
		(size_t)(newcap - hierarchy->row_capacity) * sizeof(*newptr));
	hierarchy->rows = newptr;
	hierarchy->row_capacity = newcap;
	return 1;
}

static void generatedRenderStreamFree(generated_render_stream_t *stream)
{
	if (!stream) {
		return;
	}
	free(stream->rows);
	memset(stream, 0, sizeof(*stream));
}

static s32 generatedRenderStreamGrow(generated_render_stream_t *stream,
                                     s32 needed)
{
	s32 newcap;
	generated_render_row_t *newptr;

	if (stream->row_count + needed <= stream->row_capacity) {
		return 1;
	}

	newcap = stream->row_capacity * 2;
	if (newcap < stream->row_count + needed) {
		newcap = stream->row_count + needed;
	}
	if (newcap < 128) {
		newcap = 128;
	}

	newptr = realloc(stream->rows, (size_t)newcap * sizeof(*stream->rows));
	if (!newptr) {
		return 0;
	}
	memset(newptr + stream->row_capacity, 0,
		(size_t)(newcap - stream->row_capacity) * sizeof(*newptr));
	stream->rows = newptr;
	stream->row_capacity = newcap;
	return 1;
}

static s32 generatedSplitTabs(char *line, char **cols, s32 max_cols)
{
	s32 count = 0;
	char *p = line;

	if (!line || !cols || max_cols <= 0) {
		return 0;
	}

	while (count < max_cols) {
		char *tab;
		cols[count++] = p;
		tab = strchr(p, '\t');
		if (!tab) {
			break;
		}
		*tab = '\0';
		p = tab + 1;
	}

	return count;
}

static s32 generatedRenderOpFromText(const char *op)
{
	if (!op) {
		return 0;
	}
	if (strcmp(op, "mtx") == 0) return GENERATED_RENDER_OP_MTX;
	if (strcmp(op, "pop") == 0) return GENERATED_RENDER_OP_POP;
	if (strcmp(op, "material") == 0) return GENERATED_RENDER_OP_MATERIAL;
	if (strcmp(op, "tri") == 0) return GENERATED_RENDER_OP_TRI;
	return 0;
}

static s32 generatedModeldefReadRenderStream(const char *source_path,
                                             const obj_mesh_t *mesh,
                                             generated_render_stream_t *stream)
{
	char render_path[FS_MAXPATH + 1];
	u32 size = 0;
	char *text;
	char *copy;
	char *line;
	s32 parsed_rows = 0;

	if (!source_path || !mesh || !stream) {
		return 0;
	}
	memset(stream, 0, sizeof(*stream));
	if (!generatedModeldefMetadataPath(source_path, "model.render.tsv",
			render_path, sizeof(render_path))) {
		return 0;
	}

	text = (char *)fsFileLoad(render_path, &size);
	if (!text || size == 0) {
		if (text) {
			free(text);
		}
		return 0;
	}

	copy = malloc((size_t)size + 1);
	if (!copy) {
		free(text);
		return -1;
	}
	memcpy(copy, text, size);
	copy[size] = '\0';
	free(text);

	line = copy;
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
		if (*p && *p != '#' && strncmp(p, "group\t", 6) != 0) {
			char *cols[6];
			generated_render_row_t row;
			char *endptr;
			long face;
			long matrix;
			long params;
			long material;
			s32 op;

			if (generatedSplitTabs(p, cols, 6) != 6) {
				free(copy);
				generatedRenderStreamFree(stream);
				return -1;
			}
			op = generatedRenderOpFromText(cols[1]);
			if (!op) {
				free(copy);
				generatedRenderStreamFree(stream);
				return -1;
			}
			face = strtol(cols[2], &endptr, 10);
			if (endptr == cols[2]) {
				free(copy);
				generatedRenderStreamFree(stream);
				return -1;
			}
			matrix = strtol(cols[3], &endptr, 10);
			if (endptr == cols[3]) {
				free(copy);
				generatedRenderStreamFree(stream);
				return -1;
			}
			params = strtol(cols[4], &endptr, 0);
			if (endptr == cols[4]) {
				free(copy);
				generatedRenderStreamFree(stream);
				return -1;
			}
			material = strtol(cols[5], &endptr, 10);
			if (endptr == cols[5]) {
				free(copy);
				generatedRenderStreamFree(stream);
				return -1;
			}
			if (op == GENERATED_RENDER_OP_TRI &&
					(face < 0 || face >= mesh->triangle_count)) {
				free(copy);
				generatedRenderStreamFree(stream);
				return -1;
			}
			if (!generatedRenderStreamGrow(stream, 1)) {
				free(copy);
				generatedRenderStreamFree(stream);
				return -1;
			}
			memset(&row, 0, sizeof(row));
			strncpy(row.group, cols[0], sizeof(row.group) - 1);
			row.group[sizeof(row.group) - 1] = '\0';
			row.op = (u8)op;
			row.face = (s32)face;
			row.matrix = (s32)matrix;
			row.params = (u8)params;
			row.material = (s32)material;
			stream->rows[stream->row_count++] = row;
			parsed_rows++;
		}

		line = next;
	}

	free(copy);
	return parsed_rows > 0 ? 1 : 0;
}

static s32 generatedModeldefReadHierarchy(const char *source_path,
                                          generated_hierarchy_t *hierarchy)
{
	char hierarchy_path[FS_MAXPATH + 1];
	u32 size = 0;
	char *text;
	char *line;
	s32 parsed_rows = 0;

	if (!source_path || !hierarchy) {
		return 0;
	}
	memset(hierarchy, 0, sizeof(*hierarchy));
	if (!generatedModeldefMetadataPath(source_path, "model.nodes.tsv",
			hierarchy_path, sizeof(hierarchy_path))) {
		return 0;
	}

	text = (char *)fsFileLoad(hierarchy_path, &size);
	if (!text || size == 0) {
		if (text) {
			free(text);
		}
		return 0;
	}

	char *copy = malloc((size_t)size + 1);
	if (!copy) {
		free(text);
		return -1;
	}
	memcpy(copy, text, size);
	copy[size] = '\0';
	free(text);

	line = copy;
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
		if (*p && *p != '#' && strncmp(p, "id\t", 3) != 0) {
			generated_hierarchy_row_t row;
			memset(&row, 0, sizeof(row));
			row.payload_index = -1;
			if (sscanf(p,
					"%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%f\t%f\t%f\t%f\t%d\t%63s\t%d\t%d\t%d\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%d\t%d\t%d",
					&row.id, &row.parent, &row.type,
					&row.partnum, &row.part, &row.mtx0,
					&row.mtx1, &row.mtx2, &row.pos_x,
					&row.pos_y, &row.pos_z, &row.drawdist,
					&row.target, row.group, &row.render_mtx,
					&row.mcount, &row.hitpart, &row.xmin,
					&row.xmax, &row.ymin, &row.ymax,
					&row.zmin, &row.zmax, &row.distance_near,
					&row.distance_far, &row.reorder_x,
					&row.reorder_y, &row.reorder_z,
					&row.reorder_axis_x, &row.reorder_axis_y,
					&row.reorder_axis_z, &row.reorder_target_a,
					&row.reorder_target_b, &row.reorder_side) != 34) {
				free(copy);
				generatedHierarchyFree(hierarchy);
				return -1;
			}
			if (!generatedHierarchyGrow(hierarchy, 1)) {
				free(copy);
				generatedHierarchyFree(hierarchy);
				return -1;
			}
			hierarchy->rows[hierarchy->row_count++] = row;
			parsed_rows++;
		}

		line = next;
	}

	free(copy);
	return parsed_rows > 0 ? 1 : 0;
}

static s32 generatedModeldefTriangleUsesGroup(const obj_triangle_t *tri,
                                              s32 group_index)
{
	return group_index >= 0 && tri && tri->group_index == group_index;
}

static s32 generatedRenderRowUsesGroup(const generated_render_row_t *row,
                                       const char *group_name)
{
	return row && group_name && group_name[0] &&
		strcmp(row->group, group_name) == 0;
}

static s32 generatedRenderStreamTriCount(
	const generated_render_stream_t *stream,
	const obj_mesh_t *mesh,
	const char *group_name,
	s32 group_index)
{
	s32 count = 0;

	if (!stream || !mesh || !group_name || !group_name[0]) {
		return 0;
	}

	for (s32 i = 0; i < stream->row_count; i++) {
		const generated_render_row_t *row = &stream->rows[i];
		if (row->op != GENERATED_RENDER_OP_TRI ||
				!generatedRenderRowUsesGroup(row, group_name) ||
				row->face < 0 ||
				row->face >= mesh->triangle_count ||
				!generatedModeldefTriangleUsesGroup(
					&mesh->triangles[row->face], group_index)) {
			continue;
		}
		count++;
	}

	return count;
}

static s32 generatedModeldefBuildPayload(const obj_mesh_t *mesh,
                                         s32 group_index,
                                         const char *group_name,
                                         s32 matrix_index,
                                         s32 model_segment,
                                         const generated_render_stream_t *stream,
                                         generated_dl_payload_t *payload)
{
	s32 tri_count = 0;
	s32 material_switch_count = 0;
	s32 textured_material_count = 0;
	s32 vtx_count;
	s32 source_gdl_count;
	s32 output_gdl_count;
	size_t vertex_bytes;
	size_t vertex_colour_bytes;
	u32 vertex_segment;
	u32 colour_segment;
	Gfx *source_gdl = NULL;
	Gfx *gdl;
	s32 emitted = 0;
	s32 use_render_stream = 0;
	s32 render_stream_cmd_count = 0;

	if (!mesh || !payload) {
		return 0;
	}
	memset(payload, 0, sizeof(*payload));

	tri_count = generatedRenderStreamTriCount(stream, mesh, group_name,
		group_index);
	use_render_stream = tri_count > 0;

	if (use_render_stream) {
		for (s32 i = 0, last_material = -2; i < stream->row_count; i++) {
			const generated_render_row_t *row = &stream->rows[i];
			const obj_triangle_t *tri;
			if (!generatedRenderRowUsesGroup(row, group_name)) {
				continue;
			}
			render_stream_cmd_count++;
			if (row->op != GENERATED_RENDER_OP_TRI ||
					row->face < 0 ||
					row->face >= mesh->triangle_count) {
				continue;
			}
			tri = &mesh->triangles[row->face];
			if (!generatedModeldefTriangleUsesGroup(tri, group_index)) {
				continue;
			}
			if (tri->material_index != last_material) {
				material_switch_count++;
				last_material = tri->material_index;
			}
		}
	} else {
		for (s32 i = 0, last_material = -2; i < mesh->triangle_count; i++) {
			const obj_triangle_t *tri = &mesh->triangles[i];
			if (!generatedModeldefTriangleUsesGroup(tri, group_index)) {
				continue;
			}
			tri_count++;
			if (tri->material_index != last_material) {
				material_switch_count++;
				last_material = tri->material_index;
			}
		}
	}

	for (s32 m = 0; m < mesh->material_count; m++) {
		if (!objMaterialHasTexture(&mesh->materials[m])) {
			continue;
		}
		if (use_render_stream) {
			for (s32 i = 0; i < stream->row_count; i++) {
				const generated_render_row_t *row = &stream->rows[i];
				const obj_triangle_t *tri;
				if (row->op != GENERATED_RENDER_OP_TRI ||
						!generatedRenderRowUsesGroup(row, group_name) ||
						row->face < 0 ||
						row->face >= mesh->triangle_count) {
					continue;
				}
				tri = &mesh->triangles[row->face];
				if (generatedModeldefTriangleUsesGroup(tri, group_index) &&
						tri->material_index == m) {
					textured_material_count++;
					break;
				}
			}
		} else {
			for (s32 i = 0; i < mesh->triangle_count; i++) {
				const obj_triangle_t *tri = &mesh->triangles[i];
				if (generatedModeldefTriangleUsesGroup(tri, group_index) &&
						tri->material_index == m) {
					textured_material_count++;
					break;
				}
			}
		}
	}

	vtx_count = tri_count * 3;
	vertex_bytes = (size_t)(vtx_count > 0 ? vtx_count : 1) *
		sizeof(*payload->vertices);
	vertex_colour_bytes = (size_t)ALIGN8(vertex_bytes) +
		sizeof(*payload->colours);
	payload->vertices = calloc(1, vertex_colour_bytes);
	if (!payload->vertices) {
		return 0;
	}
	payload->colours = (Col *)((u8 *)payload->vertices + ALIGN8(vertex_bytes));
	payload->colours[0].r = 0xff;
	payload->colours[0].g = 0xff;
	payload->colours[0].b = 0xff;
	payload->colours[0].a = 0xff;
	payload->vertex_bytes = vertex_bytes;
	payload->vertex_count = vtx_count;
	payload->triangle_count = tri_count;
	if (model_segment != SPSEGMENT_MODEL_COL1) {
		model_segment = SPSEGMENT_MODEL_VTX;
	}
	vertex_segment = ((u32)model_segment << 24);
	colour_segment = model_segment == SPSEGMENT_MODEL_COL1 ?
		(((u32)SPSEGMENT_MODEL_COL1 << 24) | (u32)ALIGN8(vertex_bytes)) :
		((u32)SPSEGMENT_MODEL_COL2 << 24);

	if (tri_count == 0) {
		return 1;
	}

	source_gdl_count = tri_count * 3 + 7 + material_switch_count * 3 +
		render_stream_cmd_count * 2;
	output_gdl_count = source_gdl_count
		+ material_switch_count * 512
		+ textured_material_count * 256
		+ 4096;
	source_gdl = calloc((size_t)source_gdl_count, sizeof(*source_gdl));
	payload->gdl = calloc((size_t)output_gdl_count, sizeof(*payload->gdl));
	if (!source_gdl || !payload->gdl) {
		free(source_gdl);
		free(payload->vertices);
		memset(payload, 0, sizeof(*payload));
		return 0;
	}

	if (use_render_stream) {
		for (s32 i = 0; i < stream->row_count; i++) {
			const generated_render_row_t *row = &stream->rows[i];
			const obj_triangle_t *tri;
			const obj_texcoord_t *ta;
			const obj_texcoord_t *tb;
			const obj_texcoord_t *tc;
			if (row->op != GENERATED_RENDER_OP_TRI ||
					!generatedRenderRowUsesGroup(row, group_name) ||
					row->face < 0 ||
					row->face >= mesh->triangle_count) {
				continue;
			}
			tri = &mesh->triangles[row->face];
			if (!generatedModeldefTriangleUsesGroup(tri, group_index)) {
				continue;
			}
			ta = objMeshTexcoord(mesh, tri->ta);
			tb = objMeshTexcoord(mesh, tri->tb);
			tc = objMeshTexcoord(mesh, tri->tc);
			fillGeneratedVertex(&payload->vertices[emitted * 3 + 0],
				&mesh->vertices[tri->a], ta);
			fillGeneratedVertex(&payload->vertices[emitted * 3 + 1],
				&mesh->vertices[tri->b], tb);
			fillGeneratedVertex(&payload->vertices[emitted * 3 + 2],
				&mesh->vertices[tri->c], tc);
			emitted++;
		}
	} else {
		for (s32 i = 0; i < mesh->triangle_count; i++) {
			const obj_triangle_t *tri = &mesh->triangles[i];
			const obj_texcoord_t *ta;
			const obj_texcoord_t *tb;
			const obj_texcoord_t *tc;
			if (!generatedModeldefTriangleUsesGroup(tri, group_index)) {
				continue;
			}
			ta = objMeshTexcoord(mesh, tri->ta);
			tb = objMeshTexcoord(mesh, tri->tb);
			tc = objMeshTexcoord(mesh, tri->tc);
			fillGeneratedVertex(&payload->vertices[emitted * 3 + 0],
				&mesh->vertices[tri->a], ta);
			fillGeneratedVertex(&payload->vertices[emitted * 3 + 1],
				&mesh->vertices[tri->b], tb);
			fillGeneratedVertex(&payload->vertices[emitted * 3 + 2],
				&mesh->vertices[tri->c], tc);
			emitted++;
		}
	}

	gdl = source_gdl;
	if (matrix_index < 0) {
		matrix_index = 0;
	}
	if (!use_render_stream) {
		gSPMatrix(gdl++,
			SEGADDR((SPSEGMENT_MODEL_MTX << 24) |
				((u32)matrix_index * (u32)sizeof(Mtxf))),
			G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
	}
	gSPColor(gdl++, SEGADDR(colour_segment), 1);
	gSPTexture(gdl++, 0, 0, 0, 0, 0);
	gSPClearGeometryMode(gdl++,
		G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_CULL_BOTH);
	gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
	gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);

	emitted = 0;
	if (use_render_stream) {
		for (s32 i = 0, last_material = -2, last_textured = 0,
				last_matrix = -0x40000000; i < stream->row_count; i++) {
			const generated_render_row_t *row = &stream->rows[i];
			const obj_triangle_t *tri;
			const obj_material_t *material;
			uintptr_t offset;
			s32 tri_matrix;

			if (!generatedRenderRowUsesGroup(row, group_name)) {
				continue;
			}

			if (row->op == GENERATED_RENDER_OP_MTX) {
				if (row->matrix >= 0 &&
						!(row->params & G_MTX_PROJECTION)) {
					gSPMatrix(gdl++,
						SEGADDR((SPSEGMENT_MODEL_MTX << 24) |
							((u32)row->matrix * (u32)sizeof(Mtxf))),
						row->params);
					last_matrix = row->matrix;
				}
				continue;
			}

			if (row->op == GENERATED_RENDER_OP_POP) {
				gSPPopMatrix(gdl++, G_MTX_MODELVIEW);
				last_matrix = -0x40000000;
				continue;
			}

			if (row->op == GENERATED_RENDER_OP_MATERIAL) {
				if (row->material >= 0 &&
						row->material < mesh->material_count) {
					material = &mesh->materials[row->material];
					if (objMaterialHasTexture(material)) {
						emitGeneratedTextureMarker(&gdl, material);
						last_textured = 1;
					} else if (last_textured) {
						emitGeneratedUntexturedState(&gdl);
						last_textured = 0;
					}
					last_material = row->material;
				}
				continue;
			}

			if (row->op != GENERATED_RENDER_OP_TRI ||
					row->face < 0 ||
					row->face >= mesh->triangle_count) {
				continue;
			}

			tri = &mesh->triangles[row->face];
			if (!generatedModeldefTriangleUsesGroup(tri, group_index)) {
				continue;
			}

			material = objMeshTriangleMaterial(mesh, tri);
			offset = (uintptr_t)(emitted * 3 * (s32)sizeof(Vtx));
			tri_matrix = row->matrix >= 0 ? row->matrix :
				(tri->matrix_index >= 0 ? tri->matrix_index :
					matrix_index);
			if (tri_matrix != last_matrix) {
				gSPMatrix(gdl++,
					SEGADDR((SPSEGMENT_MODEL_MTX << 24) |
						((u32)tri_matrix * (u32)sizeof(Mtxf))),
					G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
				last_matrix = tri_matrix;
			}
			if (tri->material_index != last_material) {
				if (objMaterialHasTexture(material)) {
					emitGeneratedTextureMarker(&gdl, material);
					last_textured = 1;
				} else if (last_textured) {
					emitGeneratedUntexturedState(&gdl);
					last_textured = 0;
				}
				last_material = tri->material_index;
			}
			gSPVertex(gdl++, SEGADDR(vertex_segment | offset), 3, 0);
			gSP1Triangle(gdl++, 0, 1, 2, 0);
			emitted++;
		}
	} else {
		for (s32 i = 0, last_material = -2, last_textured = 0,
				last_matrix = matrix_index;
				i < mesh->triangle_count; i++) {
			const obj_triangle_t *tri = &mesh->triangles[i];
			const obj_material_t *material;
			uintptr_t offset;
			s32 tri_matrix;
			if (!generatedModeldefTriangleUsesGroup(tri, group_index)) {
				continue;
			}
			material = objMeshTriangleMaterial(mesh, tri);
			offset = (uintptr_t)(emitted * 3 * (s32)sizeof(Vtx));
			tri_matrix = tri->matrix_index >= 0 ?
				tri->matrix_index : matrix_index;
			if (tri_matrix != last_matrix) {
				gSPMatrix(gdl++,
					SEGADDR((SPSEGMENT_MODEL_MTX << 24) |
						((u32)tri_matrix * (u32)sizeof(Mtxf))),
					G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
				last_matrix = tri_matrix;
			}
			if (tri->material_index != last_material) {
				if (objMaterialHasTexture(material)) {
					emitGeneratedTextureMarker(&gdl, material);
					last_textured = 1;
				} else if (last_textured) {
					emitGeneratedUntexturedState(&gdl);
					last_textured = 0;
				}
				last_material = tri->material_index;
			}
			gSPVertex(gdl++, SEGADDR(vertex_segment | offset), 3, 0);
			gSP1Triangle(gdl++, 0, 1, 2, 0);
			emitted++;
		}
	}
	gSPEndDisplayList(gdl++);

	{
		s32 source_bytes = (s32)((uintptr_t)gdl - (uintptr_t)source_gdl);
		s32 output_bytes = source_bytes;
		if (textured_material_count > 0) {
			output_bytes = texLoadFromGdl(source_gdl, source_bytes,
				payload->gdl, NULL, (u8 *)payload->vertices);
			if (output_bytes <= 0 ||
					output_bytes > output_gdl_count * (s32)sizeof(Gfx)) {
				free(source_gdl);
				free(payload->vertices);
				free(payload->gdl);
				memset(payload, 0, sizeof(*payload));
				return 0;
			}
		} else {
			memcpy(payload->gdl, source_gdl, (size_t)source_bytes);
		}
		payload->gdl_bytes = (size_t)output_bytes;
		if (model_segment == SPSEGMENT_MODEL_COL1) {
			size_t colour_offset = ALIGN8(vertex_bytes);
			size_t gdl_offset = ALIGN8(colour_offset + sizeof(*payload->colours));
			size_t base_bytes = gdl_offset + payload->gdl_bytes;
			payload->baseaddr = calloc(1, base_bytes);
			if (!payload->baseaddr) {
				free(source_gdl);
				free(payload->vertices);
				free(payload->gdl);
				memset(payload, 0, sizeof(*payload));
				return 0;
			}
			memcpy(payload->baseaddr, payload->vertices, vertex_bytes);
			memcpy((u8 *)payload->baseaddr + colour_offset,
				payload->colours, sizeof(*payload->colours));
			memcpy((u8 *)payload->baseaddr + gdl_offset,
				payload->gdl, payload->gdl_bytes);
			payload->seg_gdl = (Gfx *)SEGADDR(
				((u32)SPSEGMENT_MODEL_COL1 << 24) | (u32)gdl_offset);
		}
	}
	free(source_gdl);
	return 1;
}

typedef struct generated_part_entry {
	s32 partnum;
	struct modelnode *node;
} generated_part_entry_t;

static s32 generatedModeldefPartEntriesGrow(generated_part_entry_t **entries,
                                             s32 *capacity,
                                             s32 needed)
{
	s32 newcap;
	generated_part_entry_t *newptr;

	if (!entries || !capacity) {
		return 0;
	}
	if (needed <= *capacity) {
		return 1;
	}

	newcap = *capacity * 2;
	if (newcap < needed) {
		newcap = needed;
	}
	if (newcap < 32) {
		newcap = 32;
	}

	newptr = realloc(*entries, (size_t)newcap * sizeof(**entries));
	if (!newptr) {
		return 0;
	}
	memset(newptr + *capacity, 0,
		(size_t)(newcap - *capacity) * sizeof(*newptr));
	*entries = newptr;
	*capacity = newcap;
	return 1;
}

static s32 generatedModeldefReadParts(const char *source_path,
                                      struct modelnode *nodes,
                                      s32 node_count,
                                      generated_part_entry_t **out_parts,
                                      s32 *out_part_count)
{
	char parts_path[FS_MAXPATH + 1];
	u32 size = 0;
	char *text;
	char *copy;
	char *line;
	generated_part_entry_t *parts = NULL;
	s32 part_count = 0;
	s32 part_capacity = 0;

	if (out_parts) {
		*out_parts = NULL;
	}
	if (out_part_count) {
		*out_part_count = 0;
	}
	if (!source_path || !nodes || node_count <= 0 ||
			!out_parts || !out_part_count) {
		return 0;
	}
	if (!generatedModeldefMetadataPath(source_path, "model.parts.tsv",
			parts_path, sizeof(parts_path))) {
		return 0;
	}

	text = (char *)fsFileLoad(parts_path, &size);
	if (!text || size == 0) {
		if (text) {
			free(text);
		}
		return 0;
	}

	copy = malloc((size_t)size + 1);
	if (!copy) {
		free(text);
		return -1;
	}
	memcpy(copy, text, size);
	copy[size] = '\0';
	free(text);

	line = copy;
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
		if (*p && *p != '#' && strncmp(p, "partnum\t", 8) != 0) {
			s32 partnum;
			s32 node_id;
			if (sscanf(p, "%d\t%d", &partnum, &node_id) != 2 ||
					node_id < 0 || node_id >= node_count) {
				free(parts);
				free(copy);
				return -1;
			}
			if (!generatedModeldefPartEntriesGrow(&parts, &part_capacity,
					part_count + 1)) {
				free(parts);
				free(copy);
				return -1;
			}
			parts[part_count].partnum = partnum;
			parts[part_count].node = &nodes[node_id];
			part_count++;
		}

		line = next;
	}

	free(copy);
	*out_parts = parts;
	*out_part_count = part_count;
	return 1;
}

static s32 generatedModeldefReadFaces(const char *source_path,
                                      obj_mesh_t *mesh)
{
	char faces_path[FS_MAXPATH + 1];
	u32 size = 0;
	char *text;
	char *copy;
	char *line;
	u8 *seen;
	s32 parsed_rows = 0;

	if (!source_path || !mesh || mesh->triangle_count <= 0) {
		return 0;
	}
	if (!generatedModeldefMetadataPath(source_path, "model.faces.tsv",
			faces_path, sizeof(faces_path))) {
		return 0;
	}

	text = (char *)fsFileLoad(faces_path, &size);
	if (!text || size == 0) {
		if (text) {
			free(text);
		}
		return 0;
	}

	copy = malloc((size_t)size + 1);
	seen = calloc((size_t)mesh->triangle_count, sizeof(*seen));
	if (!copy || !seen) {
		free(text);
		free(copy);
		free(seen);
		return -1;
	}
	memcpy(copy, text, size);
	copy[size] = '\0';
	free(text);

	for (s32 i = 0; i < mesh->triangle_count; i++) {
		mesh->triangles[i].matrix_index = -1;
	}

	line = copy;
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
		if (*p && *p != '#' && strncmp(p, "face\t", 5) != 0) {
			s32 face_index;
			s32 matrix_index;
			if (sscanf(p, "%d\t%d", &face_index, &matrix_index) != 2 ||
					face_index < 0 ||
					face_index >= mesh->triangle_count ||
					matrix_index < 0 ||
					seen[face_index]) {
				free(seen);
				free(copy);
				return -1;
			}
			mesh->triangles[face_index].matrix_index = matrix_index;
			seen[face_index] = 1;
			parsed_rows++;
		}

		line = next;
	}

	free(seen);
	free(copy);
	return parsed_rows == mesh->triangle_count ? 1 : -1;
}

static int generatedPartCompare(const void *a, const void *b)
{
	const generated_part_entry_t *pa = (const generated_part_entry_t *)a;
	const generated_part_entry_t *pb = (const generated_part_entry_t *)b;
	if (pa->partnum < pb->partnum) return -1;
	if (pa->partnum > pb->partnum) return 1;
	return 0;
}

static s32 generatedModeldefTypeIsRender(s32 type)
{
	return type == MODELNODETYPE_DL ||
		type == MODELNODETYPE_GUNDL ||
		type == MODELNODETYPE_STARGUNFIRE;
}

static s32 generatedModeldefPreserveGunDlType(struct skeleton *skeleton)
{
	if (!skeleton) {
		return 0;
	}
	if (skeleton == &g_SkelHand) {
		return 1;
	}
	switch (skeleton->skel) {
	case SKEL_CLASSICGUN:
	case SKEL_UZI:
	case SKEL_LAPTOPGUN:
	case SKEL_K7AVENGER:
	case SKEL_FALCON2:
	case SKEL_KNIFE:
	case SKEL_CMP150:
	case SKEL_DRAGON:
	case SKEL_SUPERDRAGON:
	case SKEL_ROCKET:
	case SKEL_SHOTGUN:
	case SKEL_FARSIGHT:
	case SKEL_REAPER:
	case SKEL_MAULER:
	case SKEL_DEVASTATOR:
	case SKEL_PISTOL:
	case SKEL_AR34:
	case SKEL_MAGNUM:
	case SKEL_SLAYERROCKET:
	case SKEL_CYCLONE:
	case SKEL_SNIPERRIFLE:
	case SKEL_TRANQUILIZER:
	case SKEL_CROSSBOW:
	case SKEL_TIMEDPROXYMINE:
	case SKEL_PHOENIX:
	case SKEL_CALLISTO:
	case SKEL_RCP120:
	case SKEL_LASER:
	case SKEL_GRENADE:
	case SKEL_ECMMINE:
	case SKEL_UPLINK:
	case SKEL_REMOTEMINE:
		return 1;
	default:
		return 0;
	}
}

static s32 generatedModeldefAppendChild(struct modelnode *parent,
                                        struct modelnode *child)
{
	struct modelnode *cursor;

	if (!parent || !child) {
		return 0;
	}
	child->parent = parent;
	if (!parent->child) {
		parent->child = child;
		return 1;
	}
	cursor = parent->child;
	while (cursor->next) {
		cursor = cursor->next;
	}
	cursor->next = child;
	child->prev = cursor;
	return 1;
}

static void generatedModeldefFreePayloads(generated_modeldef_t *owner)
{
	if (!owner || !owner->dynamic_payloads) {
		return;
	}
	for (s32 i = 0; i < owner->dynamic_payload_count; i++) {
		free(owner->dynamic_payloads[i].baseaddr);
		free(owner->dynamic_payloads[i].vertices);
		free(owner->dynamic_payloads[i].gdl);
	}
	free(owner->dynamic_payloads);
	owner->dynamic_payloads = NULL;
	owner->dynamic_payload_count = 0;
}

static s32 buildGeneratedModeldefFromMeshHierarchy(const asset_entry_t *entry,
                                                   const char *source_path,
                                                   obj_mesh_t *mesh,
                                                   struct modeldef **out_modeldef)
{
	generated_hierarchy_t hierarchy;
	generated_modeldef_t *owner = NULL;
	generated_part_entry_t *parts = NULL;
	generated_render_stream_t render_stream;
	s32 part_count = 0;
	s32 max_mtx = -1;
	s32 payload_count = 0;
	struct skeleton *skeleton;
	s32 read_rc;
	s32 render_stream_rc;

	if (out_modeldef) {
		*out_modeldef = NULL;
	}
	read_rc = generatedModeldefReadHierarchy(source_path, &hierarchy);
	if (read_rc <= 0) {
		return read_rc;
	}
	if (!mesh || !out_modeldef || hierarchy.row_count <= 0) {
		generatedHierarchyFree(&hierarchy);
		return -1;
	}
	if (generatedModeldefReadFaces(source_path, mesh) <= 0) {
		generatedHierarchyFree(&hierarchy);
		return -1;
	}
	render_stream_rc = generatedModeldefReadRenderStream(source_path, mesh,
		&render_stream);
	if (render_stream_rc <= 0) {
		generatedHierarchyFree(&hierarchy);
		return -1;
	}

	owner = calloc(1, sizeof(*owner));
	if (!owner) {
		generatedRenderStreamFree(&render_stream);
		generatedHierarchyFree(&hierarchy);
		return -1;
	}
	owner->dynamic_nodes = calloc((size_t)hierarchy.row_count,
		sizeof(*owner->dynamic_nodes));
	owner->dynamic_rodatas = calloc((size_t)hierarchy.row_count,
		sizeof(*owner->dynamic_rodatas));
	owner->dynamic_payloads = calloc((size_t)hierarchy.row_count,
		sizeof(*owner->dynamic_payloads));
	if (!owner->dynamic_nodes || !owner->dynamic_rodatas ||
			!owner->dynamic_payloads) {
		generatedRenderStreamFree(&render_stream);
		generatedHierarchyFree(&hierarchy);
		modAssetCompilerFreeModeldef(&owner->def);
		return -1;
	}
	owner->dynamic_payload_count = hierarchy.row_count;
	skeleton = generatedModeldefSkeletonFromMetadata(source_path);

	if (entry && entry->id[0]) {
		strncpy(owner->catalog_id, entry->id, sizeof(owner->catalog_id) - 1);
		owner->catalog_id[sizeof(owner->catalog_id) - 1] = '\0';
	}
	if (source_path && source_path[0]) {
		strncpy(owner->source_path, source_path,
			sizeof(owner->source_path) - 1);
		owner->source_path[sizeof(owner->source_path) - 1] = '\0';
	}

	for (s32 i = 0; i < hierarchy.row_count; i++) {
		generated_hierarchy_row_t *row = &hierarchy.rows[i];
		struct modelnode *node = &owner->dynamic_nodes[i];
		union modelrodata *rodata = &owner->dynamic_rodatas[i];
		s32 node_type = row->type;

		if (row->id != i) {
			free(parts);
			generatedRenderStreamFree(&render_stream);
			generatedHierarchyFree(&hierarchy);
			modAssetCompilerFreeModeldef(&owner->def);
			return -1;
		}

		if (node_type == MODELNODETYPE_GUNDL &&
				!generatedModeldefPreserveGunDlType(skeleton)) {
			node_type = MODELNODETYPE_DL;
		}

		if (generatedModeldefTypeIsRender(node_type)) {
			s32 group_index = objMeshGroupIndexByName(mesh, row->group);
			s32 payload_segment = node_type == MODELNODETYPE_GUNDL ?
				SPSEGMENT_MODEL_COL1 : SPSEGMENT_MODEL_VTX;
			row->payload_index = payload_count++;
			if (!generatedModeldefBuildPayload(mesh, group_index,
					row->group, row->render_mtx, payload_segment,
					&render_stream,
					&owner->dynamic_payloads[row->payload_index])) {
				free(parts);
				generatedRenderStreamFree(&render_stream);
				generatedHierarchyFree(&hierarchy);
				modAssetCompilerFreeModeldef(&owner->def);
				return -1;
			}
			owner->triangle_count +=
				owner->dynamic_payloads[row->payload_index].triangle_count;
			owner->vertex_count +=
				owner->dynamic_payloads[row->payload_index].vertex_count;
			if (node_type == MODELNODETYPE_STARGUNFIRE) {
				node_type = MODELNODETYPE_DL;
			}
		}

		node->type = (u16)node_type;
		node->rodata = rodata;

		switch (node_type) {
		case MODELNODETYPE_CHRINFO:
			rodata->chrinfo.animpart = row->part >= 0 ? (u16)row->part : 0;
			rodata->chrinfo.mtxindex = (s16)row->mtx0;
			rodata->chrinfo.unk04 = 0.0f;
			break;
		case MODELNODETYPE_POSITION:
			rodata->position.pos.x = row->pos_x;
			rodata->position.pos.y = row->pos_y;
			rodata->position.pos.z = row->pos_z;
			rodata->position.part = row->part >= 0 ? (u16)row->part : 0xffff;
			rodata->position.mtxindex0 = (s16)row->mtx0;
			rodata->position.mtxindex1 = (s16)row->mtx1;
			rodata->position.mtxindex2 = (s16)row->mtx2;
			rodata->position.drawdist = row->drawdist;
			break;
		case MODELNODETYPE_POSITIONHELD:
			rodata->positionheld.pos.x = row->pos_x;
			rodata->positionheld.pos.y = row->pos_y;
			rodata->positionheld.pos.z = row->pos_z;
			rodata->positionheld.mtxindex = (s16)row->mtx0;
			break;
		case MODELNODETYPE_TOGGLE:
			break;
		case MODELNODETYPE_DISTANCE:
			rodata->distance.near = row->distance_near;
			rodata->distance.far = row->distance_far;
			break;
		case MODELNODETYPE_REORDER:
			rodata->reorder.unk00 = row->reorder_x;
			rodata->reorder.unk04 = row->reorder_y;
			rodata->reorder.unk08 = row->reorder_z;
			rodata->reorder.unk0c[0] = row->reorder_axis_x;
			rodata->reorder.unk0c[1] = row->reorder_axis_y;
			rodata->reorder.unk0c[2] = row->reorder_axis_z;
			rodata->reorder.side = (s16)row->reorder_side;
			break;
		case MODELNODETYPE_HEADSPOT:
			break;
		case MODELNODETYPE_BBOX:
			rodata->bbox.hitpart = row->hitpart;
			rodata->bbox.xmin = row->xmin;
			rodata->bbox.xmax = row->xmax;
			rodata->bbox.ymin = row->ymin;
			rodata->bbox.ymax = row->ymax;
			rodata->bbox.zmin = row->zmin;
			rodata->bbox.zmax = row->zmax;
			break;
		case MODELNODETYPE_DL:
			if (row->payload_index >= 0) {
				generated_dl_payload_t *payload =
					&owner->dynamic_payloads[row->payload_index];
				rodata->dl.opagdl = payload->gdl;
				rodata->dl.xlugdl = NULL;
				rodata->dl.colours = payload->colours;
				rodata->dl.vertices = payload->vertices;
				rodata->dl.numvertices = (s16)payload->vertex_count;
				rodata->dl.mcount = row->mcount > 0 ? (s16)row->mcount : 1;
				rodata->dl.numcolours = 1;
			}
			break;
		case MODELNODETYPE_GUNDL:
			if (row->payload_index >= 0) {
				generated_dl_payload_t *payload =
					&owner->dynamic_payloads[row->payload_index];
				rodata->gundl.opagdl = payload->seg_gdl;
				rodata->gundl.xlugdl = NULL;
				rodata->gundl.baseaddr = payload->baseaddr;
				rodata->gundl.vertices = (Vtx *)payload->baseaddr;
				rodata->gundl.numvertices = (s16)payload->vertex_count;
				rodata->gundl.unk12 = row->mcount > 0 ? (s16)row->mcount : 1;
			}
			break;
		default:
			break;
		}

		if (row->mtx0 > max_mtx) max_mtx = row->mtx0;
		if (row->mtx1 > max_mtx) max_mtx = row->mtx1;
		if (row->mtx2 > max_mtx) max_mtx = row->mtx2;
		if (row->render_mtx > max_mtx) max_mtx = row->render_mtx;
	}
	for (s32 i = 0; i < mesh->triangle_count; i++) {
		if (mesh->triangles[i].matrix_index > max_mtx) {
			max_mtx = mesh->triangles[i].matrix_index;
		}
	}
	for (s32 i = 0; i < render_stream.row_count; i++) {
		if (render_stream.rows[i].matrix > max_mtx) {
			max_mtx = render_stream.rows[i].matrix;
		}
	}
	for (s32 i = 0; i < hierarchy.row_count; i++) {
		generated_hierarchy_row_t *row = &hierarchy.rows[i];
		if (row->parent >= 0 && row->parent < hierarchy.row_count) {
			generatedModeldefAppendChild(&owner->dynamic_nodes[row->parent],
				&owner->dynamic_nodes[i]);
		}
	}

	for (s32 i = 0; i < hierarchy.row_count; i++) {
		generated_hierarchy_row_t *row = &hierarchy.rows[i];
		if (row->type == MODELNODETYPE_TOGGLE && row->target >= 0 &&
				row->target < hierarchy.row_count) {
			owner->dynamic_rodatas[i].toggle.target =
				&owner->dynamic_nodes[row->target];
		} else if (row->type == MODELNODETYPE_DISTANCE &&
				row->target >= 0 && row->target < hierarchy.row_count) {
			owner->dynamic_rodatas[i].distance.target =
				&owner->dynamic_nodes[row->target];
		} else if (row->type == MODELNODETYPE_REORDER) {
			if (row->reorder_target_a >= 0 &&
					row->reorder_target_a < hierarchy.row_count) {
				owner->dynamic_rodatas[i].reorder.unk18 =
					&owner->dynamic_nodes[row->reorder_target_a];
			}
			if (row->reorder_target_b >= 0 &&
					row->reorder_target_b < hierarchy.row_count) {
				owner->dynamic_rodatas[i].reorder.unk1c =
					&owner->dynamic_nodes[row->reorder_target_b];
			}
		}
	}

	{
		s32 parts_rc = generatedModeldefReadParts(source_path,
			owner->dynamic_nodes, hierarchy.row_count, &parts, &part_count);
		if (parts_rc <= 0) {
			free(parts);
			generatedRenderStreamFree(&render_stream);
			generatedHierarchyFree(&hierarchy);
			modAssetCompilerFreeModeldef(&owner->def);
			return -1;
		}
	}

	if (part_count > 0) {
		size_t ptr_bytes = (size_t)part_count *
			sizeof(struct modelnode *);
		size_t num_bytes = (size_t)part_count * sizeof(s16);
		struct modelnode **part_nodes;
		s16 *part_nums;
		qsort(parts, (size_t)part_count, sizeof(*parts),
			generatedPartCompare);
		owner->dynamic_parts = calloc(1, ptr_bytes + num_bytes);
		if (!owner->dynamic_parts) {
			free(parts);
			generatedRenderStreamFree(&render_stream);
			generatedHierarchyFree(&hierarchy);
			modAssetCompilerFreeModeldef(&owner->def);
			return -1;
		}
		part_nodes = (struct modelnode **)owner->dynamic_parts;
		part_nums = (s16 *)((u8 *)owner->dynamic_parts + ptr_bytes);
		for (s32 i = 0; i < part_count; i++) {
			part_nodes[i] = parts[i].node;
			part_nums[i] = (s16)parts[i].partnum;
		}
		owner->def.parts = part_nodes;
		owner->def.numparts = (s16)part_count;
	}

	free(parts);
	owner->def.rootnode = &owner->dynamic_nodes[0];
	owner->def.skel = skeleton;
	owner->def.nummatrices = (s16)(max_mtx >= 0 ? max_mtx + 1 : 1);
	owner->def.scale = generatedModeldefScaleFromMetadata(source_path);
	owner->def.numtexconfigs = 0;
	owner->def.texconfigs = NULL;
	owner->def.rwdatalen = modelCalculateRwDataIndexes(owner->def.rootnode);
	generatedModeldefRegister(owner);
	*out_modeldef = &owner->def;

	sysLogPrintf(LOG_NOTE,
		"MODASSET.COMPILER: built hierarchy modeldef '%s' source=%s nodes=%d parts=%d matrices=%d vertices=%d tris=%d rwdatalen=%d skeleton=%s",
		entry ? entry->id : "(unknown)",
		source_path ? source_path : "(null)",
		hierarchy.row_count, part_count, owner->def.nummatrices,
		owner->vertex_count, owner->triangle_count, owner->def.rwdatalen,
		modAssetCompilerSkeletonSymbolForPointer(owner->def.skel) ?
			modAssetCompilerSkeletonSymbolForPointer(owner->def.skel) : "(none)");

	generatedRenderStreamFree(&render_stream);
	generatedHierarchyFree(&hierarchy);
	return 1;
}

static s32 buildGeneratedModeldefFromMesh(const asset_entry_t *entry,
                                          const char *source_path,
                                          const obj_mesh_t *mesh,
                                          struct modeldef **out_modeldef)
{
	generated_modeldef_t *owner;
	Gfx *source_gdl;
	Gfx *gdl;
	s32 vtx_count;
	s32 source_gdl_count;
	s32 output_gdl_count;
	size_t vertex_bytes;
	size_t vertex_colour_bytes;
	s32 chr_root;
	s32 uv_vertex_count = 0;
	s32 textured_material_count = 0;
	s32 material_switch_count = 0;
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

	for (s32 i = 0; i < mesh->material_count; i++) {
		if (objMaterialHasTexture(&mesh->materials[i])) {
			textured_material_count++;
		}
	}

	for (s32 i = 0, last_material = -2; i < mesh->triangle_count; i++) {
		const obj_triangle_t *tri = &mesh->triangles[i];
		if (tri->material_index != last_material) {
			material_switch_count++;
			last_material = tri->material_index;
		}
	}

	source_gdl_count = mesh->triangle_count * 2 + 7
		+ material_switch_count * 3;
	output_gdl_count = source_gdl_count
		+ material_switch_count * 512
		+ textured_material_count * 256
		+ 4096;
	owner = calloc(1, sizeof(*owner));
	if (!owner) {
		return -1;
	}

	vertex_bytes = (size_t)vtx_count * sizeof(*owner->vertices);
	vertex_colour_bytes = (size_t)ALIGN8(vertex_bytes) + sizeof(*owner->colours);
	owner->vertices = calloc(1, vertex_colour_bytes);
	owner->colours = (Col *)((u8 *)owner->vertices + ALIGN8(vertex_bytes));
	source_gdl = calloc((size_t)source_gdl_count, sizeof(*source_gdl));
	owner->gdl = calloc((size_t)output_gdl_count, sizeof(*owner->gdl));
	if (!owner->vertices || !owner->colours || !source_gdl || !owner->gdl) {
		free(source_gdl);
		modAssetCompilerFreeModeldef(&owner->def);
		return -1;
	}

	owner->triangle_count = mesh->triangle_count;
	owner->vertex_count = vtx_count;
	if (entry && entry->id[0]) {
		strncpy(owner->catalog_id, entry->id, sizeof(owner->catalog_id) - 1);
		owner->catalog_id[sizeof(owner->catalog_id) - 1] = '\0';
	}
	if (source_path && source_path[0]) {
		strncpy(owner->source_path, source_path,
			sizeof(owner->source_path) - 1);
		owner->source_path[sizeof(owner->source_path) - 1] = '\0';
	}
	owner->colours[0].r = 0xff;
	owner->colours[0].g = 0xff;
	owner->colours[0].b = 0xff;
	owner->colours[0].a = 0xff;

	for (s32 i = 0; i < mesh->triangle_count; i++) {
		const obj_triangle_t *tri = &mesh->triangles[i];
		const obj_texcoord_t *ta = objMeshTexcoord(mesh, tri->ta);
		const obj_texcoord_t *tb = objMeshTexcoord(mesh, tri->tb);
		const obj_texcoord_t *tc = objMeshTexcoord(mesh, tri->tc);
		uv_vertex_count += ta ? 1 : 0;
		uv_vertex_count += tb ? 1 : 0;
		uv_vertex_count += tc ? 1 : 0;
		fillGeneratedVertex(&owner->vertices[i * 3 + 0],
			&mesh->vertices[tri->a], ta);
		fillGeneratedVertex(&owner->vertices[i * 3 + 1],
			&mesh->vertices[tri->b], tb);
		fillGeneratedVertex(&owner->vertices[i * 3 + 2],
			&mesh->vertices[tri->c], tc);
	}

	gdl = source_gdl;
	gSPMatrix(gdl++, SEGADDR(SPSEGMENT_MODEL_MTX << 24),
		G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
	gSPColor(gdl++, SEGADDR(SPSEGMENT_MODEL_COL2 << 24), 1);
	gSPTexture(gdl++, 0, 0, 0, 0, 0);
	gSPClearGeometryMode(gdl++,
		G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_CULL_BOTH);
	gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
	gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
	for (s32 i = 0, last_material = -2, last_textured = 0;
			i < mesh->triangle_count; i++) {
		const obj_triangle_t *tri = &mesh->triangles[i];
		const obj_material_t *material =
			objMeshTriangleMaterial(mesh, tri);
		uintptr_t offset = (uintptr_t)(i * 3 * (s32)sizeof(Vtx));
		if (tri->material_index != last_material) {
			if (objMaterialHasTexture(material)) {
				emitGeneratedTextureMarker(&gdl, material);
				last_textured = 1;
			} else if (last_textured) {
				emitGeneratedUntexturedState(&gdl);
				last_textured = 0;
			}
			last_material = tri->material_index;
		}
		gSPVertex(gdl++, SEGADDR((SPSEGMENT_MODEL_VTX << 24) | offset), 3, 0);
		gSP1Triangle(gdl++, 0, 1, 2, 0);
	}
	gSPEndDisplayList(gdl++);
	{
		s32 source_bytes = (s32)((uintptr_t)gdl - (uintptr_t)source_gdl);
		if (textured_material_count > 0) {
			s32 output_bytes = texLoadFromGdl(source_gdl, source_bytes,
				owner->gdl, NULL, (u8 *)owner->vertices);
			if (output_bytes <= 0 ||
					output_bytes > output_gdl_count * (s32)sizeof(Gfx)) {
				sysLogPrintf(LOG_WARNING,
					"MODASSET.COMPILER: texture expansion overflow for generated modeldef '%s' source=%s output_bytes=%d capacity=%d",
					entry ? entry->id : "(unknown)",
					source_path ? source_path : "(null)",
					output_bytes,
					output_gdl_count * (s32)sizeof(Gfx));
				free(source_gdl);
				modAssetCompilerFreeModeldef(&owner->def);
				return -1;
			}
		} else {
			memcpy(owner->gdl, source_gdl, (size_t)source_bytes);
		}
		free(source_gdl);
	}

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
	owner->dl_node.prev = NULL;
	owner->dl_node.next = NULL;
	owner->dl_node.child = NULL;
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
	owner->def.scale = generatedModeldefScaleFromMetadata(source_path);
	owner->def.numtexconfigs = 0;
	owner->def.texconfigs = NULL;
	owner->def.rwdatalen = modelCalculateRwDataIndexes(owner->def.rootnode);
	generatedModeldefConfigureSourceBounds(owner, mesh);
	generatedModeldefConfigureAutogunParts(owner);
	generatedModeldefConfigureLogoParts(owner);
	generatedModeldefConfigureCctvParts(owner, mesh);
	generatedModeldefConfigureWindowedDoorParts(owner);
	owner->def.rwdatalen = modelCalculateRwDataIndexes(owner->def.rootnode);
	generatedModeldefRegister(owner);

	*out_modeldef = &owner->def;
	sysLogPrintf(LOG_NOTE,
		"MODASSET.COMPILER: built generated modeldef '%s' source=%s vertices=%d texcoords=%d uv_vertices=%d materials=%d textured_materials=%d material_switches=%d tris=%d rwdatalen=%d skeleton=%s",
		entry ? entry->id : "(unknown)",
		source_path ? source_path : "(null)",
		vtx_count, mesh->texcoord_count, uv_vertex_count,
		mesh->material_count, textured_material_count,
		material_switch_count, mesh->triangle_count, owner->def.rwdatalen,
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

	generatedModeldefLoadMaterialMetadata(source_path, &mesh);
	rc = buildGeneratedModeldefFromMeshHierarchy(entry, source_path, &mesh,
		out_modeldef);
	if (rc == 0) {
		rc = buildGeneratedModeldefFromMesh(entry, source_path, &mesh,
			out_modeldef);
	}
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
	generatedModeldefUnregister(owner);
	generatedModeldefFreePayloads(owner);
	free(owner->dynamic_nodes);
	free(owner->dynamic_rodatas);
	free(owner->dynamic_parts);
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

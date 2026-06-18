/**
 * romextract_pdarena.c -- Catalog universality pivot Step 2 (2026-05-03).
 *
 * Walks the loader_pool arena pool and emits TWO files per registered
 * arena per Mike's Q-1 unified-scenario directive:
 *
 *   1. data/<romid>/arenas/<id>.pdarena
 *      ZIP-openable typed asset archive. The archive root carries
 *      arena.ini for the modder-facing descriptor plus _meta/manifest.json for
 *      the legacy universal walker until the base walker consumes the INI
 *      path directly. Playable arenas also embed their authored scenario
 *      dependency closure under dependencies/assets/scenarios/ as an intact
 *      .pdscenario archive so opening one .pdarena exposes the multiplayer
 *      wrapper and the exact scenario content unit it uses.
 *
 *   2. data/<romid>/scenarios/<scenario_id>.pdscenario
 *      ZIP compound bundling scene.glb, pads.json, spawns.json, volumes.json,
 *      objects.json, setup.fields.json, ai/ailists.json, objectives.json,
 *      navigation/paths.json, navigation.ini, level.graph.json, and
 *      _meta/generated-*.json envelopes. UNIFIED per Q-1 (one file per stage;
 *      modder-friendly atomic distribution).
 *      Schema: universality-pivot-schemas.md Section 2.10.
 *
 * Boot order: must run AFTER stageTableInit (g_Stages populated) AND
 * loaderPoolFinalize (arena pool populated) AND
 * romExtractAllFiles (per-stage .bin files extracted to disk).
 *
 * Cross-reference convention for Step 2: the .pdarena `scenario` field
 * carries the catalog ID `base:scenario_<arena_slug>` matching the
 * .pdscenario this emitter produces. Arenas without a resolvable
 * stagetable entry (Random meta arenas with token stagenums
 * STAGE_MP_RANDOM_MULTI=0x02 / STAGE_MP_RANDOM_SOLO=0x03) get a
 * `scenario: null` and skip the .pdscenario emit.
 *
 * ARENA_LOADMODE_CANVAS invariant: preserved per-arena via the
 * load_mode field in the .pdarena JSON. The 14 Solo Missions arenas
 * still get .pdscenario emits because their stage files exist on disk
 * (they are rendered as static canvases at runtime by the menu UI but
 * the asset bytes are real).
 *
 * Server build: emitter early-returns 0 (arena loader not active
 * server-side; g_RomFile is freed; no stage files extracted).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include <SDL.h>
#include <PR/ultratypes.h>

#include "boot_pool.h"
#include "boot_progress.h"
#include "data.h"
#include "types.h"
#include "constants.h"
#include "fs.h"
#include "loader_enum_reverse.h"
#include "assetcatalog.h"
#include "asset_archive_writer.h"
#include "modarchive.h"
#include "preprocess.h"
#include "romdata.h"
#include "romextract.h"
#include "romextract_pd.h"
#include "sha256.h"
#include "system.h"
#include "memsizes.h"
#include "game/stagetable.h"
#include "game/chrai.h"
#include "game/texdecompress.h"
#include "game/tex.h"
#include "arenadata_authored.h"
#include "weapondata_authored.h"
#include "lib/rzip.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../external/stb_image_write.h"

#define PDSCENARIO_BG_VISUAL_EXPORT_VERSION "bg_visual_scene_glb_v11_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_color0_alphamask_materialextras_dualtex"
#define PDSCENARIO_BG_VISUAL_EXPORT_VERSION_FILE \
	PDSCENARIO_BG_VISUAL_EXPORT_VERSION "\n"
#define ROMEXTRACT_PDARENA_FAST_CACHE_KIND "pdarena_clean_public_v8_pdscenario_v91"
#define ROMEXTRACT_PDSCENARIO_FAST_CACHE_KIND "pdscenario_scene_glb_clean_public_v96_standalone_backfill_collision_obj_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_color0_alphamask_quip_shuffle_graph_portals_json_objects_json_setup_fields_json_ai_lists_json_ai_command_graph_navhashes_objectives_spawns_volumes_pads_paths_json_navtables_json"

/* Convert "base:arena_mp_skedar" -> "base_arena_mp_skedar". */
static void s_idToFilename(const char *id, char *out, size_t n)
{
	if (!id || !out || n == 0) { if (out && n) out[0] = '\0'; return; }
	size_t i;
	for (i = 0; i + 1 < n && id[i]; i++) {
		out[i] = (id[i] == ':') ? '_' : id[i];
	}
	out[i] = '\0';
}

/* Build the scenario catalog ID for an arena slug. The arena's
 * `scenario` field carries this string; the .pdscenario file lives at
 * data/<romid>/scenarios/<filename_slug>.pdscenario. */
static void s_scenarioCatalogIdFromSlug(const char *slug, char *out, size_t n)
{
	if (!slug || !out || n == 0) { if (out && n) out[0] = '\0'; return; }
	snprintf(out, n, "base:scenario_%s", slug);
}

static s32 s_arenaStageIndexForScenario(s16 stagenum)
{
	if (stagenum == STAGE_MP_RANDOM_MULTI ||
			stagenum == STAGE_MP_RANDOM_SOLO) {
		return -1;
	}
	return stageGetIndex(stagenum);
}

/* Map an arena's category to the scenario `kind` string per Section
 * 2.10 of the schema. "Solo Missions" -> "solo"; everything else -> "mp"
 * (Dark / Classic / Bonus / Random). The schema also defines
 * "firingrange" and "coop" but those map to stages outside the arena
 * pool (firing range is the boot training stage; coop is a per-game
 * mode flag, not a per-arena attribute). */
static const char *s_kindFromCategory(const char *category)
{
	if (!category || !category[0]) return "mp";
	if (strcmp(category, "Solo Missions") == 0) return "solo";
	return "mp";
}

typedef struct {
	char scenario_id[96];
	char slug[96];
	char kind[32];
	s32 stagenum;
	s32 stage_idx;
	const char *provenance_source;
	s32 provenance_index;
} pdscenario_emit_spec_t;

typedef struct {
	pdscenario_emit_spec_t specs[128];
	s32 count;
} pdscenario_stage_specs_t;

static s32 s_stagenumHasArenaScenario(s32 stagenum)
{
	for (s32 i = 0; i < g_ArenaDataCount; i++) {
		const arena_authored_record_t *a = &g_ArenaData[i];
		if (a->stagenum == stagenum &&
				s_arenaStageIndexForScenario(a->stagenum) >= 0) {
			return 1;
		}
	}
	return 0;
}

static const char *s_kindFromStageEntry(const asset_entry_t *entry)
{
	if (!entry) {
		return "solo";
	}
	if (strstr(entry->id, ":citraining")) {
		return "firingrange";
	}
	if (entry->ext.map.mode & MAP_MODE_MP) {
		return "mp";
	}
	if (entry->ext.map.mode & MAP_MODE_COOP) {
		return "coop";
	}
	return "solo";
}

static void s_collectStandaloneStageScenario(const asset_entry_t *entry,
	void *userdata)
{
	pdscenario_stage_specs_t *ctx = (pdscenario_stage_specs_t *)userdata;
	const char *colon;
	const char *slug;
	s32 stage_idx;
	pdscenario_emit_spec_t *spec;

	if (!entry || !ctx || entry->type != ASSET_MAP || !entry->bundled) {
		return;
	}
	if (strncmp(entry->category, "base", CATALOG_CATEGORY_LEN) != 0) {
		return;
	}
	if (s_stagenumHasArenaScenario(entry->ext.map.stagenum)) {
		return;
	}

	stage_idx = stageGetIndex(entry->ext.map.stagenum);
	if (stage_idx < 0 || !stageGetEntry(stage_idx)) {
		return;
	}
	if (ctx->count >= (s32)(sizeof(ctx->specs) / sizeof(ctx->specs[0]))) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdscenario: standalone stage scenario spec capacity exceeded at '%s'",
			entry->id);
		return;
	}

	colon = strchr(entry->id, ':');
	slug = (colon && colon[1]) ? colon + 1 : entry->id;
	if (!slug || !slug[0]) {
		return;
	}
	spec = &ctx->specs[ctx->count++];
	memset(spec, 0, sizeof(*spec));
	snprintf(spec->scenario_id, sizeof(spec->scenario_id),
		"base:scenario_%s", slug);
	strncpy(spec->slug, slug, sizeof(spec->slug) - 1);
	strncpy(spec->kind, s_kindFromStageEntry(entry), sizeof(spec->kind) - 1);
	spec->stagenum = entry->ext.map.stagenum;
	spec->stage_idx = stage_idx;
	spec->provenance_source = "stagecatalog";
	spec->provenance_index = stage_idx;
}

typedef struct {
	char *data;
	u32   len;
	u32   cap;
} pdscenario_textbuf_t;

typedef struct {
	u32 tag_id;
	s32 target_index;
} pdscenario_tag_target_t;

typedef struct {
	u8  *data;
	u32  len;
	u32  cap;
} pdscenario_binbuf_t;

typedef struct {
	f32 x, y, z;
	f32 u, v;
	u8 r, g, b, a;
	u16 roomnum;
} pdscenario_visual_vertex_t;

typedef struct {
	pdscenario_visual_vertex_t *vertices;
	u32 vertex_count;
	u32 vertex_cap;
	u32 *indices;
	u32 index_count;
	u32 index_cap;
	f32 min_x, min_y, min_z;
	f32 max_x, max_y, max_z;
	s32 has_bounds;
} pdscenario_visualmesh_t;

typedef struct {
	u16 texnum;
	s32 used_by_geometry;
	s32 decoded;
	u32 width;
	u32 height;
	s32 has_alpha;
	u8 *tga;
	u32 tga_size;
	u8 *png;
	u32 png_size;
	char path[96];
} pdscenario_bgtexture_t;

typedef struct {
	char name[48];
	s32 texnum;
	s32 texnum2;
	s32 subcmd;
	u32 w0;
	u32 w1;
	s32 smode;
	s32 tmode;
	s32 offset;
	s32 shifts;
	s32 shiftt;
	s32 min;
	s32 flag;
	u32 tri_count;
	pdscenario_visual_vertex_t *vertices;
	u32 vertex_count;
	u32 vertex_cap;
} pdscenario_bgmaterial_t;

typedef struct {
	pdscenario_textbuf_t obj;
	pdscenario_textbuf_t mtl;
	u8 *scene_glb;
	u32 scene_glb_size;
	pdscenario_bgtexture_t *textures;
	u32 texture_count;
	u32 texture_cap;
	pdscenario_bgmaterial_t *materials;
	u32 material_count;
	u32 material_cap;
	u32 next_obj_index;
	s32 current_material;
	u32 room_count;
	u32 gdl_count;
	u32 tri_count;
	u32 decoded_texture_count;
	u32 failed_texture_count;
	f32 min_x, min_y, min_z;
	f32 max_x, max_y, max_z;
	s32 has_bounds;
} pdscenario_bgscene_t;

static void s_textbufFree(pdscenario_textbuf_t *b)
{
	if (b->data) free(b->data);
	memset(b, 0, sizeof(*b));
}

static void s_binbufFree(pdscenario_binbuf_t *b)
{
	if (b->data) free(b->data);
	memset(b, 0, sizeof(*b));
}

static s32 s_textbufReserve(pdscenario_textbuf_t *b, u32 extra)
{
	if (extra > 0xffffffffu - b->len) return -1;
	u32 need = b->len + extra + 1;
	if (need <= b->cap) return 0;
	u32 cap = b->cap ? b->cap : 8192;
	while (cap < need) {
		if (cap > 0x80000000u) return -1;
		cap *= 2;
	}
	char *p = (char *)realloc(b->data, cap);
	if (!p) return -1;
	b->data = p;
	b->cap = cap;
	return 0;
}

static s32 s_textbufAppend(pdscenario_textbuf_t *b, const char *s)
{
	u32 n = (u32)strlen(s);
	if (s_textbufReserve(b, n) != 0) return -1;
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
	return 0;
}

static s32 s_textbufAppendf(pdscenario_textbuf_t *b, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	va_list ap2;
	va_copy(ap2, ap);
	int need = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (need < 0) {
		va_end(ap2);
		return -1;
	}
	if (s_textbufReserve(b, (u32)need) != 0) {
		va_end(ap2);
		return -1;
	}
	int wrote = vsnprintf(b->data + b->len, (size_t)need + 1, fmt, ap2);
	va_end(ap2);
	if (wrote != need) return -1;
	b->len += (u32)need;
	return 0;
}

static s32 s_textbufAppendJsonString(pdscenario_textbuf_t *b, const char *s)
{
	if (!s) s = "";
	if (s_textbufAppend(b, "\"") != 0) return -1;
	while (*s) {
		unsigned char c = (unsigned char)*s++;
		switch (c) {
		case '\\':
			if (s_textbufAppend(b, "\\\\") != 0) return -1;
			break;
		case '"':
			if (s_textbufAppend(b, "\\\"") != 0) return -1;
			break;
		case '\b':
			if (s_textbufAppend(b, "\\b") != 0) return -1;
			break;
		case '\f':
			if (s_textbufAppend(b, "\\f") != 0) return -1;
			break;
		case '\n':
			if (s_textbufAppend(b, "\\n") != 0) return -1;
			break;
		case '\r':
			if (s_textbufAppend(b, "\\r") != 0) return -1;
			break;
		case '\t':
			if (s_textbufAppend(b, "\\t") != 0) return -1;
			break;
		default:
			if (c < 0x20) {
				if (s_textbufAppendf(b, "\\u%04x", (unsigned)c) != 0) {
					return -1;
				}
			} else {
				char tmp[2] = { (char)c, '\0' };
				if (s_textbufAppend(b, tmp) != 0) return -1;
			}
			break;
		}
	}
	return s_textbufAppend(b, "\"");
}

static void s_textbufSha256Hex(const pdscenario_textbuf_t *b,
	char out[SHA256_HEX_SIZE])
{
	u8 digest[SHA256_DIGEST_SIZE];

	if (!out) {
		return;
	}
	out[0] = '\0';
	if (!b || !b->data) {
		return;
	}

	sha256Hash(b->data, (size_t)b->len, digest);
	sha256ToHex(digest, out);
}

static void s_bytesSha256Hex(const u8 *data, u32 len,
	char out[SHA256_HEX_SIZE])
{
	u8 digest[SHA256_DIGEST_SIZE];

	if (!out) {
		return;
	}
	out[0] = '\0';
	if (!data || len == 0) {
		return;
	}

	sha256Hash(data, (size_t)len, digest);
	sha256ToHex(digest, out);
}

static s32 s_binbufReserve(pdscenario_binbuf_t *b, u32 extra)
{
	if (!b || extra > 0xffffffffu - b->len) return -1;
	u32 need = b->len + extra;
	if (need <= b->cap) return 0;
	u32 cap = b->cap ? b->cap : 8192;
	while (cap < need) {
		if (cap > 0x80000000u) return -1;
		cap *= 2;
	}
	u8 *p = (u8 *)realloc(b->data, cap);
	if (!p) return -1;
	b->data = p;
	b->cap = cap;
	return 0;
}

static s32 s_binbufAppend(pdscenario_binbuf_t *b, const void *data, u32 len)
{
	if (!b || (!data && len)) return -1;
	if (s_binbufReserve(b, len) != 0) return -1;
	if (len) memcpy(b->data + b->len, data, len);
	b->len += len;
	return 0;
}

static s32 s_binbufAppendZeroes(pdscenario_binbuf_t *b, u32 len)
{
	if (!b) return -1;
	if (s_binbufReserve(b, len) != 0) return -1;
	memset(b->data + b->len, 0, len);
	b->len += len;
	return 0;
}

static s32 s_binbufPad4(pdscenario_binbuf_t *b)
{
	u32 pad = (4u - (b->len & 3u)) & 3u;
	return pad ? s_binbufAppendZeroes(b, pad) : 0;
}

static s32 s_binbufAppendLe32(pdscenario_binbuf_t *b, u32 value)
{
	u8 tmp[4];
	tmp[0] = (u8)(value & 0xffu);
	tmp[1] = (u8)((value >> 8) & 0xffu);
	tmp[2] = (u8)((value >> 16) & 0xffu);
	tmp[3] = (u8)((value >> 24) & 0xffu);
	return s_binbufAppend(b, tmp, sizeof(tmp));
}

static void s_visualMeshFree(pdscenario_visualmesh_t *m)
{
	if (!m) return;
	if (m->vertices) free(m->vertices);
	if (m->indices) free(m->indices);
	memset(m, 0, sizeof(*m));
}

static void s_bgSceneFree(pdscenario_bgscene_t *s)
{
	if (!s) return;
	s_textbufFree(&s->obj);
	s_textbufFree(&s->mtl);
	if (s->scene_glb) free(s->scene_glb);
	if (s->textures) {
		for (u32 i = 0; i < s->texture_count; i++) {
			if (s->textures[i].tga) free(s->textures[i].tga);
			if (s->textures[i].png) free(s->textures[i].png);
		}
		free(s->textures);
	}
	if (s->materials) {
		for (u32 i = 0; i < s->material_count; i++) {
			if (s->materials[i].vertices) free(s->materials[i].vertices);
		}
		free(s->materials);
	}
	memset(s, 0, sizeof(*s));
}

static s32 s_visualMeshReserve(pdscenario_visualmesh_t *m,
                               u32 add_vertices, u32 add_indices)
{
	if (!m) return -1;
	if (add_vertices > 0xffffffffu - m->vertex_count) return -1;
	if (add_indices > 0xffffffffu - m->index_count) return -1;

	u32 need_vertices = m->vertex_count + add_vertices;
	if (need_vertices > m->vertex_cap) {
		u32 cap = m->vertex_cap ? m->vertex_cap : 4096;
		while (cap < need_vertices) {
			if (cap > 0x80000000u) return -1;
			cap *= 2;
		}
		pdscenario_visual_vertex_t *p =
			(pdscenario_visual_vertex_t *)realloc(m->vertices,
				(size_t)cap * sizeof(*m->vertices));
		if (!p) return -1;
		m->vertices = p;
		m->vertex_cap = cap;
	}

	u32 need_indices = m->index_count + add_indices;
	if (need_indices > m->index_cap) {
		u32 cap = m->index_cap ? m->index_cap : 4096;
		while (cap < need_indices) {
			if (cap > 0x80000000u) return -1;
			cap *= 2;
		}
		u32 *p = (u32 *)realloc(m->indices,
			(size_t)cap * sizeof(*m->indices));
		if (!p) return -1;
		m->indices = p;
		m->index_cap = cap;
	}

	return 0;
}

static void s_visualMeshBounds(pdscenario_visualmesh_t *m, f32 x, f32 y, f32 z)
{
	if (!m->has_bounds) {
		m->min_x = m->max_x = x;
		m->min_y = m->max_y = y;
		m->min_z = m->max_z = z;
		m->has_bounds = 1;
		return;
	}
	if (x < m->min_x) m->min_x = x;
	if (y < m->min_y) m->min_y = y;
	if (z < m->min_z) m->min_z = z;
	if (x > m->max_x) m->max_x = x;
	if (y > m->max_y) m->max_y = y;
	if (z > m->max_z) m->max_z = z;
}

static void s_triUvs(f32 x0, f32 y0, f32 z0,
                     f32 x1, f32 y1, f32 z1,
                     f32 x2, f32 y2, f32 z2,
                     f32 *u0, f32 *v0,
                     f32 *u1, f32 *v1,
                     f32 *u2, f32 *v2)
{
	f32 ax = x1 - x0, ay = y1 - y0, az = z1 - z0;
	f32 bx = x2 - x0, by = y2 - y0, bz = z2 - z0;
	f32 nx = ay * bz - az * by;
	f32 ny = az * bx - ax * bz;
	f32 nz = ax * by - ay * bx;
	if (nx < 0.0f) nx = -nx;
	if (ny < 0.0f) ny = -ny;
	if (nz < 0.0f) nz = -nz;

	const f32 scale = 1.0f / 256.0f;
	if (nx >= ny && nx >= nz) {
		*u0 = z0 * scale; *v0 = y0 * scale;
		*u1 = z1 * scale; *v1 = y1 * scale;
		*u2 = z2 * scale; *v2 = y2 * scale;
	} else if (ny >= nx && ny >= nz) {
		*u0 = x0 * scale; *v0 = z0 * scale;
		*u1 = x1 * scale; *v1 = z1 * scale;
		*u2 = x2 * scale; *v2 = z2 * scale;
	} else {
		*u0 = x0 * scale; *v0 = y0 * scale;
		*u1 = x1 * scale; *v1 = y1 * scale;
		*u2 = x2 * scale; *v2 = y2 * scale;
	}
}

static s32 s_visualMeshAddTri(pdscenario_visualmesh_t *m,
                              f32 x0, f32 y0, f32 z0, f32 u0, f32 v0,
                              f32 x1, f32 y1, f32 z1, f32 u1, f32 v1,
                              f32 x2, f32 y2, f32 z2, f32 u2, f32 v2)
{
	if (!m) return 0;
	if (s_visualMeshReserve(m, 3, 3) != 0) return -1;
	u32 base = m->vertex_count;
	m->vertices[m->vertex_count++] =
		(pdscenario_visual_vertex_t){ x0, y0, z0, u0, v0 };
	m->vertices[m->vertex_count++] =
		(pdscenario_visual_vertex_t){ x1, y1, z1, u1, v1 };
	m->vertices[m->vertex_count++] =
		(pdscenario_visual_vertex_t){ x2, y2, z2, u2, v2 };
	m->indices[m->index_count++] = base;
	m->indices[m->index_count++] = base + 1u;
	m->indices[m->index_count++] = base + 2u;
	s_visualMeshBounds(m, x0, y0, z0);
	s_visualMeshBounds(m, x1, y1, z1);
	s_visualMeshBounds(m, x2, y2, z2);
	return 0;
}

static void s_pdscenarioScratchFree(pdscenario_textbuf_t *rooms_obj,
                                    pdscenario_textbuf_t *portals_json,
                                    pdscenario_textbuf_t *pads_json,
                                    pdscenario_textbuf_t *spawns_json,
                                    pdscenario_textbuf_t *volumes_json,
                                    pdscenario_textbuf_t *waypoints_json,
                                    pdscenario_textbuf_t *waygroups_json,
                                    pdscenario_textbuf_t *covers_json,
                                    pdscenario_textbuf_t *paths_json,
                                    pdscenario_textbuf_t *objects_json,
                                    pdscenario_textbuf_t *setup_fields_json,
                                    pdscenario_textbuf_t *ai_lists_json,
                                    pdscenario_textbuf_t *ai_command_nodes_json,
                                    pdscenario_textbuf_t *ai_command_links_json,
                                    pdscenario_textbuf_t *objectives_json,
                                    pdscenario_textbuf_t *navigation_ini,
                                    pdscenario_textbuf_t *level_graph_json,
                                    pdscenario_textbuf_t *collision_meta_json,
                                    pdscenario_textbuf_t *navmesh_meta_json,
                                    pdscenario_visualmesh_t *visual_mesh,
                                    pdscenario_bgscene_t *bg_scene)
{
	s_textbufFree(rooms_obj);
	s_textbufFree(portals_json);
	s_textbufFree(pads_json);
	s_textbufFree(spawns_json);
	s_textbufFree(volumes_json);
	s_textbufFree(waypoints_json);
	s_textbufFree(waygroups_json);
	s_textbufFree(covers_json);
	s_textbufFree(paths_json);
	s_textbufFree(objects_json);
	s_textbufFree(setup_fields_json);
	s_textbufFree(ai_lists_json);
	s_textbufFree(ai_command_nodes_json);
	s_textbufFree(ai_command_links_json);
	s_textbufFree(objectives_json);
	s_textbufFree(navigation_ini);
	s_textbufFree(level_graph_json);
	s_textbufFree(collision_meta_json);
	s_textbufFree(navmesh_meta_json);
	s_visualMeshFree(visual_mesh);
	s_bgSceneFree(bg_scene);
}

static s32 s_loadStageFileRaw(u16 filenum, u8 **out_data, u32 *out_size)
{
	if (out_data) *out_data = NULL;
	if (out_size) *out_size = 0;
	if (filenum == 0) return 0;

	char src_rel[FS_MAXPATH];
	if (romExtractRelPathForFilenum((s32)filenum, src_rel, sizeof(src_rel)) <= 0) {
		return 0;
	}
	if (fsFileSize(src_rel) <= 0) {
		sysLogPrintf(LOG_NOTE,
			"romextract pdarena: stage source missing for filenum=0x%04x (rel=\"%s\")",
			(unsigned)filenum, src_rel);
		return 0;
	}

	u32 raw_size = 0;
	u8 *raw = (u8 *)fsFileLoad(src_rel, &raw_size);
	if (!raw || raw_size == 0) {
		if (raw) sysMemFree(raw);
		return 0;
	}

	*out_data = raw;
	*out_size = raw_size;
	return 1;
}

static s32 s_loadStageFilePreprocessed(u16 filenum, u32 loadtype,
                                       u8 **out_data, u32 *out_size)
{
	if (out_data) *out_data = NULL;
	if (out_size) *out_size = 0;

	u8 *raw = NULL;
	u32 raw_size = 0;
	s32 loaded = s_loadStageFileRaw(filenum, &raw, &raw_size);
	if (loaded <= 0) return loaded;

	u32 inflated_size = raw_size;
	if (raw_size >= 5 && rzipIs1173(raw)) {
		inflated_size = ALIGN16((raw[2] << 16) | (raw[3] << 8) | raw[4]);
	}

	u32 cap = romdataFileGetEstimatedSize(inflated_size, loadtype);
	if (cap < inflated_size) cap = inflated_size;
	u8 *work = sysMemZeroAlloc(cap);
	if (!work) {
		sysMemFree(raw);
		return -1;
	}

	if (raw_size >= 5 && rzipIs1173(raw)) {
		u8 scratch[5 * 1024];
		s32 result = rzipInflate(raw, work, scratch);
		sysMemFree(raw);
		if (result <= 0) {
			sysMemFree(work);
			return -1;
		}
		inflated_size = ALIGN16((u32)result);
	} else {
		memcpy(work, raw, raw_size);
		sysMemFree(raw);
	}

	u32 new_size = inflated_size;
	sysLogPrintf(LOG_NOTE,
		"romextract pdarena: preprocessing filenum=0x%04x loadtype=%u raw_size=%u inflated_size=%u",
		(unsigned)filenum, (unsigned)loadtype, (unsigned)raw_size,
		(unsigned)inflated_size);
	if (loadtype == LOADTYPE_TILES) {
		(void)preprocessTilesFile(work, inflated_size, &new_size);
	} else if (loadtype == LOADTYPE_PADS) {
		(void)preprocessPadsFile(work, inflated_size, &new_size);
	} else if (loadtype == LOADTYPE_SETUP) {
		(void)preprocessSetupFile(work, inflated_size, &new_size);
	}

	if (new_size == 0 || new_size > cap) {
		sysMemFree(work);
		return -1;
	}

	*out_data = work;
	*out_size = new_size;
	return 1;
}

static s32 s_objEmitTri(pdscenario_textbuf_t *obj,
                        pdscenario_visualmesh_t *visual,
                        u32 *next_index,
                        f32 x0, f32 y0, f32 z0,
                        f32 x1, f32 y1, f32 z1,
                        f32 x2, f32 y2, f32 z2)
{
	f32 u0, v0, u1, v1, u2, v2;
	s_triUvs(x0, y0, z0, x1, y1, z1, x2, y2, z2,
		&u0, &v0, &u1, &v1, &u2, &v2);
	if (s_visualMeshAddTri(visual,
			x0, y0, z0, u0, v0,
			x1, y1, z1, u1, v1,
			x2, y2, z2, u2, v2) != 0) {
		return -1;
	}
	u32 i0 = (*next_index)++;
	u32 i1 = (*next_index)++;
	u32 i2 = (*next_index)++;
	return s_textbufAppendf(obj,
		"v %.6f %.6f %.6f\n"
		"v %.6f %.6f %.6f\n"
		"v %.6f %.6f %.6f\n"
		"vt %.6f %.6f\n"
		"vt %.6f %.6f\n"
		"vt %.6f %.6f\n"
		"f %u/%u %u/%u %u/%u\n",
		(double)x0, (double)y0, (double)z0,
		(double)x1, (double)y1, (double)z1,
		(double)x2, (double)y2, (double)z2,
		(double)u0, (double)v0,
		(double)u1, (double)v1,
		(double)u2, (double)v2,
		i0, i0, i1, i1, i2, i2);
}

static s32 s_tilesGeoStride(const struct geo *geo)
{
	if (!geo) return -1;
	s32 n = geo->numvertices;
	if (geo->type == GEOTYPE_TILE_I) {
		if (n < 3 || n > 64) return -1;
		return 0x0e + n * 6;
	}
	if (geo->type == GEOTYPE_TILE_F) {
		if (n < 3 || n > 64) return -1;
		return 0x10 + n * 12;
	}
	if (geo->type == GEOTYPE_BLOCK) {
		if (n < 3 || n > 8) return -1;
		return (s32)sizeof(struct geoblock);
	}
	if (geo->type == GEOTYPE_CYL) {
		return (s32)sizeof(struct geocyl);
	}
	return -1;
}

static s32 s_buildTilesExports(const u8 *data, u32 size,
                               pdscenario_textbuf_t *obj,
                               pdscenario_visualmesh_t *visual,
                               u32 *out_rooms, u32 *out_geos,
                               u32 *out_tris)
{
	if (out_rooms) *out_rooms = 0;
	if (out_geos) *out_geos = 0;
	if (out_tris) *out_tris = 0;
	if (!data || size < 12 || !obj) return -1;

	u32 num_rooms = *(const u32 *)data;
	if (num_rooms == 0 || num_rooms > 4096) return -1;
	if (4u + (num_rooms + 1u) * 4u > size) return -1;
	const u32 *offsets = (const u32 *)(data + 4);
	if (offsets[0] < 4u + (num_rooms + 1u) * 4u || offsets[num_rooms] > size) {
		return -1;
	}
	for (u32 i = 0; i < num_rooms; i++) {
		if (offsets[i] > offsets[i + 1] || offsets[i + 1] > size) return -1;
	}

	if (s_textbufAppend(obj,
			"# Perfect Dark 2 base scenario collision export\n"
			"mtllib scenario.mtl\n"
			"usemtl collision\n") != 0) {
		return -1;
	}

	u32 next_index = 1;
	u32 geos = 0;
	u32 tris = 0;

	static const f32 cylx[8] = { 1.0f, 0.707107f, 0.0f, -0.707107f, -1.0f, -0.707107f, 0.0f, 0.707107f };
	static const f32 cylz[8] = { 0.0f, 0.707107f, 1.0f, 0.707107f, 0.0f, -0.707107f, -1.0f, -0.707107f };

	for (u32 room = 0; room < num_rooms; room++) {
		const u8 *ptr = data + offsets[room];
		const u8 *end = data + offsets[room + 1];
		if (s_textbufAppendf(obj, "g room_%u\n", room) != 0) return -1;

		u32 geo_index = 0;
		while (ptr + sizeof(struct geo) <= end) {
			const struct geo *geo = (const struct geo *)ptr;
			s32 stride = s_tilesGeoStride(geo);
			if (stride <= 0 || ptr + stride > end) break;

			geos++;
			if (geo->type == GEOTYPE_TILE_I) {
				const struct geotilei *tile = (const struct geotilei *)ptr;
				for (s32 i = 1; i < geo->numvertices - 1; i++) {
					if (s_objEmitTri(obj, visual, &next_index,
							(f32)tile->vertices[0][0], (f32)tile->vertices[0][1], (f32)tile->vertices[0][2],
							(f32)tile->vertices[i][0], (f32)tile->vertices[i][1], (f32)tile->vertices[i][2],
							(f32)tile->vertices[i + 1][0], (f32)tile->vertices[i + 1][1], (f32)tile->vertices[i + 1][2]) != 0) return -1;
					tris++;
				}
			} else if (geo->type == GEOTYPE_TILE_F) {
				const struct geotilef *tile = (const struct geotilef *)ptr;
				for (s32 i = 1; i < geo->numvertices - 1; i++) {
					const struct coord *v0 = &tile->vertices[0];
					const struct coord *v1 = &tile->vertices[i];
					const struct coord *v2 = &tile->vertices[i + 1];
					if (s_objEmitTri(obj, visual, &next_index,
							v0->x, v0->y, v0->z,
							v1->x, v1->y, v1->z,
							v2->x, v2->y, v2->z) != 0) return -1;
					tris++;
				}
			} else if (geo->type == GEOTYPE_BLOCK) {
				const struct geoblock *block = (const struct geoblock *)ptr;
				for (s32 i = 0; i < geo->numvertices; i++) {
					s32 j = (i + 1) % geo->numvertices;
					f32 x0 = block->vertices[i][0], z0 = block->vertices[i][1];
					f32 x1 = block->vertices[j][0], z1 = block->vertices[j][1];
					if (s_objEmitTri(obj, visual, &next_index, x0, block->ymin, z0, x1, block->ymin, z1, x1, block->ymax, z1) != 0 ||
					    s_objEmitTri(obj, visual, &next_index, x0, block->ymin, z0, x1, block->ymax, z1, x0, block->ymax, z0) != 0) return -1;
					tris += 2;
				}
				for (s32 i = 1; i < geo->numvertices - 1; i++) {
					if (s_objEmitTri(obj, visual, &next_index,
							block->vertices[0][0], block->ymax, block->vertices[0][1],
							block->vertices[i][0], block->ymax, block->vertices[i][1],
							block->vertices[i + 1][0], block->ymax, block->vertices[i + 1][1]) != 0) return -1;
					tris++;
				}
			} else if (geo->type == GEOTYPE_CYL) {
				const struct geocyl *cyl = (const struct geocyl *)ptr;
				for (s32 i = 0; i < 8; i++) {
					s32 j = (i + 1) & 7;
					f32 x0 = cyl->x + cylx[i] * cyl->radius;
					f32 z0 = cyl->z + cylz[i] * cyl->radius;
					f32 x1 = cyl->x + cylx[j] * cyl->radius;
					f32 z1 = cyl->z + cylz[j] * cyl->radius;
					if (s_objEmitTri(obj, visual, &next_index, x0, cyl->ymin, z0, x1, cyl->ymin, z1, x1, cyl->ymax, z1) != 0 ||
					    s_objEmitTri(obj, visual, &next_index, x0, cyl->ymin, z0, x1, cyl->ymax, z1, x0, cyl->ymax, z0) != 0) return -1;
					tris += 2;
				}
			}

			ptr += stride;
			geo_index++;
		}
	}

	if (out_rooms) *out_rooms = num_rooms;
	if (out_geos) *out_geos = geos;
	if (out_tris) *out_tris = tris;
	return 0;
}

static void s_padAlignedVec(u32 flags, u32 xflag, u32 yflag, u32 zflag,
                            u32 invertflag, f32 *x, f32 *y, f32 *z)
{
	*x = *y = *z = 0.0f;
	f32 sign = (flags & invertflag) ? -1.0f : 1.0f;
	if (flags & xflag) *x = sign;
	else if (flags & yflag) *y = sign;
	else if (flags & zflag) *z = sign;
}

static void s_padRef(s32 pad, char *out, size_t out_size)
{
	if (!out || out_size == 0) return;
	if (pad < 0) {
		out[0] = '\0';
		return;
	}
	snprintf(out, out_size, "pad_%04d", pad);
}

static void s_tagRef(u32 tag_id, char *out, size_t out_size)
{
	if (!out || out_size == 0) return;
	snprintf(out, out_size, "tag_%04u", (unsigned)tag_id);
}

static void s_stageFlagRef(u32 flag_mask, char *out, size_t out_size)
{
	if (!out || out_size == 0) return;
	snprintf(out, out_size, "stage_flag_0x%08x", (unsigned)flag_mask);
}

static void s_roomRef(s32 room, char *out, size_t out_size)
{
	if (!out || out_size == 0) return;
	if (room < 0) {
		out[0] = '\0';
		return;
	}
	snprintf(out, out_size, "room_%04d", room);
}

static void s_waypointRef(s32 waypoint, char *out, size_t out_size)
{
	if (!out || out_size == 0) return;
	if (waypoint < 0) {
		out[0] = '\0';
		return;
	}
	snprintf(out, out_size, "waypoint_%04d", waypoint);
}

static void s_waygroupRef(s32 waygroup, char *out, size_t out_size)
{
	if (!out || out_size == 0) return;
	if (waygroup < 0) {
		out[0] = '\0';
		return;
	}
	snprintf(out, out_size, "waygroup_%04d", waygroup);
}

static void s_pathRef(s32 path, char *out, size_t out_size)
{
	if (!out || out_size == 0) return;
	if (path < 0) {
		out[0] = '\0';
		return;
	}
	snprintf(out, out_size, "path_%04d", path);
}

static s32 s_offsetRangeValid(u32 size, uintptr_t offset, size_t len)
{
	if (offset > (uintptr_t)size) return 0;
	if (len > (size_t)((uintptr_t)size - offset)) return 0;
	return 1;
}

static s32 s_resolveSetupPointer(const u8 *data, u32 size,
                                 const void *rawptr, size_t minsize,
                                 size_t align, uintptr_t *out_ofs)
{
	uintptr_t raw;
	uintptr_t base;
	uintptr_t end;
	uintptr_t addr;

	if (out_ofs) *out_ofs = 0;
	if (!data || !rawptr || !out_ofs || size <= sizeof(struct stagesetup)) {
		return 0;
	}

	raw = (uintptr_t)rawptr;
	base = (uintptr_t)data;
	end = base + (uintptr_t)size;

	if (raw >= base && raw < end) {
		addr = raw;
	} else if (raw < (uintptr_t)size) {
		addr = base + raw;
	} else {
		return 0;
	}

	if (addr < base + sizeof(struct stagesetup) ||
			addr + minsize > end ||
			(align > 0 && (addr & (align - 1)) != 0)) {
		return 0;
	}

	*out_ofs = addr - base;
	return 1;
}

static s32 s_appendSegmentRef(pdscenario_textbuf_t *out, u32 segment,
                              const char *prefix)
{
	u32 id = segment & 0x3fffu;
	u32 flags = segment & ~0x3fffu;

	if (s_textbufAppendf(out, "%s_%04u", prefix, (unsigned)id) != 0) {
		return -1;
	}
	if (flags & 0x4000u) {
		if (s_textbufAppend(out, "|outward") != 0) return -1;
	}
	if (flags & 0x8000u) {
		if (s_textbufAppend(out, "|inward") != 0) return -1;
	}
	return 0;
}

static s32 s_appendSegmentListJson(pdscenario_textbuf_t *json,
                                   const u8 *data, u32 size, uintptr_t offset,
                                   const char *prefix)
{
	const u32 *segments;
	u32 guard;
	const char *sep = "";

	if (!json) return -1;
	if (s_textbufAppend(json, "[") != 0) return -1;
	if (!offset) return s_textbufAppend(json, "]");
	if (!s_offsetRangeValid(size, offset, sizeof(u32))) return -1;

	segments = (const u32 *)(const void *)(data + offset);
	for (guard = 0; guard < 8192; guard++) {
		u32 segment;
		pdscenario_textbuf_t token = { 0 };

		if (!s_offsetRangeValid(size, offset + (uintptr_t)guard * sizeof(u32),
				sizeof(u32))) {
			s_textbufFree(&token);
			return -1;
		}
		segment = segments[guard];
		if (segment == 0xffffffffu) {
			return s_textbufAppend(json, "]");
		}
		if (s_appendSegmentRef(&token, segment, prefix) != 0 ||
				s_textbufAppend(json, sep) != 0 ||
				s_textbufAppendJsonString(json, token.data) != 0) {
			s_textbufFree(&token);
			return -1;
		}
		s_textbufFree(&token);
		sep = ", ";
	}

	return -1;
}

static s32 s_buildNavigationJson(const u8 *data, u32 size,
                                pdscenario_textbuf_t *waypoints_json,
                                pdscenario_textbuf_t *waygroups_json,
                                pdscenario_textbuf_t *covers_json,
                                u32 *out_waypoints,
                                u32 *out_waygroups,
                                u32 *out_covers)
{
	const struct padsfileheader *hdr;
	u32 waypoint_count = 0;
	u32 waygroup_count = 0;
	u32 cover_count = 0;

	if (out_waypoints) *out_waypoints = 0;
	if (out_waygroups) *out_waygroups = 0;
	if (out_covers) *out_covers = 0;
	if (!data || size < sizeof(struct padsfileheader) ||
			!waypoints_json || !waygroups_json || !covers_json) {
		return -1;
	}

	hdr = (const struct padsfileheader *)data;
	if (hdr->numpads < 0 || hdr->numpads > 8192 ||
			hdr->numcovers < 0 || hdr->numcovers > 8192) {
		return -1;
	}

	if (s_textbufAppend(waypoints_json,
			"{\n"
			"  \"schema\": \"pd2.scenario.waypoints.v1\",\n"
			"  \"rows\": [\n") != 0 ||
	    s_textbufAppend(waygroups_json,
			"{\n"
			"  \"schema\": \"pd2.scenario.waygroups.v1\",\n"
			"  \"rows\": [\n") != 0 ||
	    s_textbufAppend(covers_json,
			"{\n"
			"  \"schema\": \"pd2.scenario.covers.v1\",\n"
			"  \"rows\": [\n") != 0) {
		return -1;
	}

	if (hdr->waypointsoffset) {
		const struct waypoint *waypoints;
		u32 i;
		if (!s_offsetRangeValid(size, hdr->waypointsoffset,
				sizeof(struct waypoint))) {
			return -1;
		}
		waypoints = (const struct waypoint *)(const void *)(data + hdr->waypointsoffset);
		for (i = 0; i < 8192; i++) {
			char waypoint_ref[32];
			char pad_ref[32];
			char group_ref[32];
			if (!s_offsetRangeValid(size,
					hdr->waypointsoffset + (uintptr_t)i * sizeof(*waypoints),
					sizeof(*waypoints))) {
				return -1;
			}
			if (waypoints[i].padnum < 0) {
				break;
			}
			s_waypointRef((s32)i, waypoint_ref, sizeof(waypoint_ref));
			s_padRef(waypoints[i].padnum, pad_ref, sizeof(pad_ref));
			s_waygroupRef(waypoints[i].groupnum, group_ref, sizeof(group_ref));
			if (s_textbufAppendf(waypoints_json,
					"%s    { \"waypoint_ref\": \"%s\", \"pad_ref\": \"%s\", \"group_ref\": \"%s\", \"step\": %d, \"neighbours\": ",
					waypoint_count ? ",\n" : "",
					waypoint_ref, pad_ref, group_ref, waypoints[i].step) != 0 ||
			    s_appendSegmentListJson(waypoints_json, data, size,
					(uintptr_t)waypoints[i].neighbours, "waypoint") != 0 ||
			    s_textbufAppend(waypoints_json, " }") != 0) {
				return -1;
			}
			waypoint_count++;
		}
		if (i >= 8192) {
			return -1;
		}
	}

	if (hdr->waygroupsoffset) {
		const struct waygroup *waygroups;
		u32 i;
		if (!s_offsetRangeValid(size, hdr->waygroupsoffset,
				sizeof(struct waygroup))) {
			return -1;
		}
		waygroups = (const struct waygroup *)(const void *)(data + hdr->waygroupsoffset);
		for (i = 0; i < 8192; i++) {
			char waygroup_ref[32];
			if (!s_offsetRangeValid(size,
					hdr->waygroupsoffset + (uintptr_t)i * sizeof(*waygroups),
					sizeof(*waygroups))) {
				return -1;
			}
			if (!waygroups[i].neighbours) {
				break;
			}
			s_waygroupRef((s32)i, waygroup_ref, sizeof(waygroup_ref));
			if (s_textbufAppendf(waygroups_json,
					"%s    { \"waygroup_ref\": \"%s\", \"step\": %d, \"waypoints\": ",
					waygroup_count ? ",\n" : "",
					waygroup_ref, waygroups[i].step) != 0 ||
			    s_appendSegmentListJson(waygroups_json, data, size,
					(uintptr_t)waygroups[i].waypoints, "waypoint") != 0 ||
			    s_textbufAppend(waygroups_json, ", \"neighbours\": ") != 0 ||
			    s_appendSegmentListJson(waygroups_json, data, size,
					(uintptr_t)waygroups[i].neighbours, "waygroup") != 0 ||
			    s_textbufAppend(waygroups_json, " }") != 0) {
				return -1;
			}
			waygroup_count++;
		}
		if (i >= 8192) {
			return -1;
		}
	}

	cover_count = (u32)hdr->numcovers;
	if (cover_count > 0) {
		const struct coverdefinition *covers;
		u32 i;
		if (!hdr->coversoffset ||
				!s_offsetRangeValid(size, hdr->coversoffset,
					(size_t)cover_count * sizeof(*covers))) {
			return -1;
		}
		covers = (const struct coverdefinition *)(const void *)(data + hdr->coversoffset);
		for (i = 0; i < cover_count; i++) {
			if (s_textbufAppendf(covers_json,
					"%s    { \"cover_ref\": \"cover_%04u\", \"flags\": %u, \"position\": [%.6f, %.6f, %.6f], \"look\": [%.6f, %.6f, %.6f] }",
					i ? ",\n" : "",
					(unsigned)i, (unsigned)covers[i].flags,
					(double)covers[i].pos.x,
					(double)covers[i].pos.y,
					(double)covers[i].pos.z,
					(double)covers[i].look.x,
					(double)covers[i].look.y,
					(double)covers[i].look.z) != 0) {
				return -1;
			}
		}
	}

	if (s_textbufAppend(waypoints_json, "\n  ]\n}\n") != 0 ||
	    s_textbufAppend(waygroups_json, "\n  ]\n}\n") != 0 ||
	    s_textbufAppend(covers_json, "\n  ]\n}\n") != 0) {
		return -1;
	}

	if (out_waypoints) *out_waypoints = waypoint_count;
	if (out_waygroups) *out_waygroups = waygroup_count;
	if (out_covers) *out_covers = cover_count;
	return 0;
}

static s32 s_buildPathsJson(const u8 *data, u32 size,
                            pdscenario_textbuf_t *paths_json,
                            u32 *out_paths)
{
	const struct stagesetup *setup;
	const struct path *paths;
	uintptr_t paths_ofs;
	u32 path_count = 0;

	if (out_paths) *out_paths = 0;
	if (!data || size < sizeof(struct stagesetup) || !paths_json) {
		return -1;
	}
	if (s_textbufAppend(paths_json,
			"{\n"
			"  \"schema\": \"pd2.scenario.paths.v1\",\n"
			"  \"rows\": [\n") != 0) {
		return -1;
	}

	setup = (const struct stagesetup *)data;
	paths_ofs = (uintptr_t)setup->paths;
	if (!paths_ofs) {
		if (s_textbufAppend(paths_json, "  ]\n}\n") != 0) {
			return -1;
		}
		return 0;
	}
	if (!s_resolveSetupPointer(data, size, setup->paths, sizeof(void *),
			4, &paths_ofs)) {
		return -1;
	}

	paths = (const struct path *)(const void *)(data + paths_ofs);
	for (u32 i = 0; i < 8192u; i++) {
		uintptr_t pads_ofs;
		const s32 *pads;
		char path_ref[32];
		const char *sep = "";

		if (!s_offsetRangeValid(size,
				paths_ofs + (uintptr_t)i * sizeof(*paths),
				sizeof(paths[i].pads))) {
			return -1;
		}
		if (!paths[i].pads) {
			break;
		}
		if (!s_offsetRangeValid(size,
				paths_ofs + (uintptr_t)i * sizeof(*paths),
				sizeof(*paths))) {
			return -1;
		}
		if (!s_resolveSetupPointer(data, size, paths[i].pads, sizeof(s32),
				4, &pads_ofs)) {
			return -1;
		}
		pads = (const s32 *)(const void *)(data + pads_ofs);
		s_pathRef((s32)paths[i].id, path_ref, sizeof(path_ref));
		if (path_count > 0 && s_textbufAppend(paths_json, ",\n") != 0) {
			return -1;
		}
		if (s_textbufAppend(paths_json, "    { \"path_ref\": ") != 0 ||
		    s_textbufAppendJsonString(paths_json, path_ref) != 0 ||
		    s_textbufAppendf(paths_json,
				", \"flags\": \"0x%02x\", \"pads\": [",
				(unsigned)paths[i].flags) != 0) {
			return -1;
		}
		for (u32 j = 0; j < 8192u; j++) {
			char pad_ref[32];
			if (!s_offsetRangeValid(size,
					pads_ofs + (uintptr_t)j * sizeof(*pads),
					sizeof(*pads))) {
				return -1;
			}
			if (pads[j] < 0) {
				break;
			}
			s_padRef(pads[j], pad_ref, sizeof(pad_ref));
			if (s_textbufAppend(paths_json, sep) != 0 ||
			    s_textbufAppendJsonString(paths_json, pad_ref) != 0) {
				return -1;
			}
			sep = ", ";
			if (j == 8191u) {
				return -1;
			}
		}
		if (s_textbufAppend(paths_json, "] }") != 0) {
			return -1;
		}
		path_count++;
		if (i == 8191u) {
			return -1;
		}
	}

	if (out_paths) *out_paths = path_count;
	if (s_textbufAppend(paths_json, "\n  ]\n}\n") != 0) {
		return -1;
	}
	return 0;
}

static s32 s_buildPadsJson(const u8 *data, u32 size,
                          pdscenario_textbuf_t *pads_json,
                          pdscenario_textbuf_t *spawns_json,
                          pdscenario_textbuf_t *volumes_json,
                          u32 *out_pads)
{
	if (out_pads) *out_pads = 0;
	if (!data || size < sizeof(struct padsfileheader) || !pads_json) return -1;
	const struct padsfileheader *hdr = (const struct padsfileheader *)data;
	if (hdr->numpads < 0 || hdr->numpads > 8192) return -1;
	if ((uintptr_t)&hdr->padoffsets[hdr->numpads] > (uintptr_t)data + size) return -1;

	if (s_textbufAppend(pads_json,
			"{\n"
			"  \"schema\": \"pd2.scenario.pads.v1\",\n"
			"  \"rows\": [\n") != 0) {
		return -1;
	}
	if (spawns_json && s_textbufAppend(spawns_json,
			"{\n"
			"  \"schema\": \"pd2.scenario.spawns.v1\",\n"
			"  \"rows\": [\n") != 0) {
		return -1;
	}
	if (volumes_json && s_textbufAppend(volumes_json,
			"{\n"
			"  \"schema\": \"pd2.scenario.volumes.v1\",\n"
			"  \"rows\": [\n") != 0) {
		return -1;
	}

	for (s32 i = 0; i < hdr->numpads; i++) {
		char pad_ref[32];
		char room_ref[32];
		u32 offset = hdr->padoffsets[i];
		if (offset + 4 > size) return -1;
		const u8 *ptr = data + offset;
		u32 header = *(const u32 *)ptr;
		u32 flags = header >> 14;
		s32 room = ((s32)(header << 18)) >> 22;
		s32 liftnum = (s32)(header & 0x0f);
		ptr += 4;

		f32 pos[3] = { 0.0f, 0.0f, 0.0f };
		f32 up[3] = { 0.0f, 1.0f, 0.0f };
		f32 look[3] = { 0.0f, 0.0f, 1.0f };
		f32 bbox[6] = { -100.0f, 100.0f, -100.0f, 100.0f, -100.0f, 100.0f };

		if (flags & PADFLAG_INTPOS) {
			if (ptr + 8 > data + size) return -1;
			const s16 *s = (const s16 *)ptr;
			pos[0] = (f32)s[0]; pos[1] = (f32)s[1]; pos[2] = (f32)s[2];
			ptr += 8;
		} else {
			if (ptr + 12 > data + size) return -1;
			const f32 *f = (const f32 *)ptr;
			pos[0] = f[0]; pos[1] = f[1]; pos[2] = f[2];
			ptr += 12;
		}

		if (flags & (PADFLAG_UPALIGNTOX | PADFLAG_UPALIGNTOY | PADFLAG_UPALIGNTOZ)) {
			s_padAlignedVec(flags, PADFLAG_UPALIGNTOX, PADFLAG_UPALIGNTOY,
				PADFLAG_UPALIGNTOZ, PADFLAG_UPALIGNINVERT, &up[0], &up[1], &up[2]);
		} else {
			if (ptr + 12 > data + size) return -1;
			const f32 *f = (const f32 *)ptr;
			up[0] = f[0]; up[1] = f[1]; up[2] = f[2];
			ptr += 12;
		}

		if (flags & (PADFLAG_LOOKALIGNTOX | PADFLAG_LOOKALIGNTOY | PADFLAG_LOOKALIGNTOZ)) {
			s_padAlignedVec(flags, PADFLAG_LOOKALIGNTOX, PADFLAG_LOOKALIGNTOY,
				PADFLAG_LOOKALIGNTOZ, PADFLAG_LOOKALIGNINVERT, &look[0], &look[1], &look[2]);
		} else {
			if (ptr + 12 > data + size) return -1;
			const f32 *f = (const f32 *)ptr;
			look[0] = f[0]; look[1] = f[1]; look[2] = f[2];
			ptr += 12;
		}

		if (flags & PADFLAG_HASBBOXDATA) {
			if (ptr + 24 > data + size) return -1;
			const f32 *f = (const f32 *)ptr;
			for (s32 j = 0; j < 6; j++) bbox[j] = f[j];
		}

		s_padRef(i, pad_ref, sizeof(pad_ref));
		s_roomRef(room, room_ref, sizeof(room_ref));

		if (i > 0 && s_textbufAppend(pads_json, ",\n") != 0) {
			return -1;
		}
		if (s_textbufAppend(pads_json,
				"    { \"pad_ref\": ") != 0 ||
			    s_textbufAppendJsonString(pads_json, pad_ref) != 0 ||
			    s_textbufAppend(pads_json, ", \"room_ref\": ") != 0 ||
			    s_textbufAppendJsonString(pads_json, room_ref) != 0 ||
			    s_textbufAppendf(pads_json,
				", \"liftnum\": %d, \"flags\": \"0x%05x\", "
				"\"position\": [%.6f, %.6f, %.6f], "
				"\"up\": [%.6f, %.6f, %.6f], "
				"\"look\": [%.6f, %.6f, %.6f], "
				"\"bbox\": { \"min\": [%.6f, %.6f, %.6f], "
				"\"max\": [%.6f, %.6f, %.6f] } }",
				liftnum, (unsigned)flags,
				(double)pos[0], (double)pos[1], (double)pos[2],
				(double)up[0], (double)up[1], (double)up[2],
				(double)look[0], (double)look[1], (double)look[2],
				(double)bbox[0], (double)bbox[2], (double)bbox[4],
				(double)bbox[1], (double)bbox[3], (double)bbox[5]) != 0) {
			return -1;
		}
		if (spawns_json) {
			if (i > 0 && s_textbufAppend(spawns_json, ",\n") != 0) {
				return -1;
			}
			if (s_textbufAppend(spawns_json,
					"    { \"spawn_id\": ") != 0 ||
			    s_textbufAppendf(spawns_json, "\"spawn_%04d\", \"pad_ref\": ",
					i) != 0 ||
			    s_textbufAppendJsonString(spawns_json, pad_ref) != 0 ||
			    s_textbufAppend(spawns_json, ", \"room_ref\": ") != 0 ||
			    s_textbufAppendJsonString(spawns_json, room_ref) != 0 ||
			    s_textbufAppendf(spawns_json,
					", \"team\": \"any\", \"profile\": \"default\", "
					"\"position\": [%.6f, %.6f, %.6f], "
					"\"look\": [%.6f, %.6f, %.6f] }",
					(double)pos[0], (double)pos[1], (double)pos[2],
					(double)look[0], (double)look[1],
					(double)look[2]) != 0) {
				return -1;
			}
		}
		if (volumes_json) {
			if (i > 0 && s_textbufAppend(volumes_json, ",\n") != 0) {
				return -1;
			}
			if (s_textbufAppendf(volumes_json,
					"    { \"volume_id\": \"volume_pad_%04d\", \"pad_ref\": ",
					i) != 0 ||
			    s_textbufAppendJsonString(volumes_json, pad_ref) != 0 ||
			    s_textbufAppend(volumes_json, ", \"kind\": \"pad_bounds\", \"room_ref\": ") != 0 ||
			    s_textbufAppendJsonString(volumes_json, room_ref) != 0 ||
			    s_textbufAppendf(volumes_json,
					", \"shape\": \"aabb\", \"min\": [%.6f, %.6f, %.6f], "
					"\"max\": [%.6f, %.6f, %.6f] }",
					(double)(pos[0] + bbox[0]), (double)(pos[1] + bbox[2]),
					(double)(pos[2] + bbox[4]),
					(double)(pos[0] + bbox[1]), (double)(pos[1] + bbox[3]),
					(double)(pos[2] + bbox[5])) != 0) {
				return -1;
			}
		}
	}

	if (volumes_json && s_textbufAppend(volumes_json,
			"\n"
			"  ]\n"
			"}\n") != 0) {
		return -1;
	}

	if (out_pads) *out_pads = (u32)hdr->numpads;
	if (s_textbufAppend(pads_json, "\n  ]\n}\n") != 0) {
		return -1;
	}
	if (spawns_json && s_textbufAppend(spawns_json, "\n  ]\n}\n") != 0) {
		return -1;
	}
	return 0;
}

static const char *s_objTypeName(u8 type)
{
	switch (type) {
	case OBJTYPE_DOOR: return "door";
	case OBJTYPE_DOORSCALE: return "door_scale";
	case OBJTYPE_BASIC: return "prop";
	case OBJTYPE_KEY: return "key";
	case OBJTYPE_ALARM: return "alarm";
	case OBJTYPE_CCTV: return "cctv";
	case OBJTYPE_AMMOCRATE: return "ammo_crate";
	case OBJTYPE_WEAPON: return "weapon_pickup";
	case OBJTYPE_CHR: return "character_spawn";
	case OBJTYPE_SINGLEMONITOR: return "single_monitor";
	case OBJTYPE_MULTIMONITOR: return "multi_monitor";
	case OBJTYPE_HANGINGMONITORS: return "hanging_monitors";
	case OBJTYPE_AUTOGUN: return "autogun";
	case OBJTYPE_LINKGUNS: return "linked_guns";
	case OBJTYPE_DEBRIS: return "debris";
	case OBJTYPE_HAT: return "hat";
	case OBJTYPE_GRENADEPROB: return "grenade_probability";
	case OBJTYPE_LINKLIFTDOOR: return "lift_door_link";
	case OBJTYPE_MULTIAMMOCRATE: return "multi_ammo_crate";
	case OBJTYPE_SHIELD: return "shield";
	case OBJTYPE_TAG: return "tag";
	case OBJTYPE_BEGINOBJECTIVE: return "objective_begin";
	case OBJTYPE_ENDOBJECTIVE: return "objective_end";
	case OBJECTIVETYPE_DESTROYOBJ: return "objective_destroy_object";
	case OBJECTIVETYPE_COMPFLAGS: return "objective_complete_flags";
	case OBJECTIVETYPE_FAILFLAGS: return "objective_fail_flags";
	case OBJECTIVETYPE_COLLECTOBJ: return "objective_collect_object";
	case OBJECTIVETYPE_THROWOBJ: return "objective_throw_object";
	case OBJECTIVETYPE_HOLOGRAPH: return "objective_holograph";
	case OBJECTIVETYPE_1F: return "objective_marker";
	case OBJECTIVETYPE_ENTERROOM: return "objective_enter_room";
	case OBJECTIVETYPE_THROWINROOM: return "objective_throw_in_room";
	case OBJTYPE_22: return "objective_marker_22";
	case OBJTYPE_BRIEFING: return "briefing";
	case OBJTYPE_GASBOTTLE: return "gas_bottle";
	case OBJTYPE_RENAMEOBJ: return "rename_object";
	case OBJTYPE_PADLOCKEDDOOR: return "padlocked_door";
	case OBJTYPE_TRUCK: return "truck";
	case OBJTYPE_HELI: return "heli";
	case OBJTYPE_TANK: return "tank";
	case OBJTYPE_CAMERAPOS: return "camera_position";
	case OBJTYPE_GLASS: return "glass";
	case OBJTYPE_SAFE: return "safe";
	case OBJTYPE_SAFEITEM: return "safe_item";
	case OBJTYPE_TINTEDGLASS: return "tinted_glass";
	case OBJTYPE_LIFT: return "lift";
	case OBJTYPE_CONDITIONALSCENERY: return "conditional_scenery";
	case OBJTYPE_BLOCKEDPATH: return "blocked_path";
	case OBJTYPE_HOVERBIKE: return "hoverbike";
	case OBJTYPE_HOVERPROP: return "hover_prop";
	case OBJTYPE_FAN: return "fan";
	case OBJTYPE_HOVERCAR: return "hover_car";
	case OBJTYPE_PADEFFECT: return "pad_effect";
	case OBJTYPE_CHOPPER: return "chopper";
	case OBJTYPE_MINE: return "mine";
	case OBJTYPE_ESCASTEP: return "escalator_step";
	default: return "unknown";
	}
}

static u8 s_setupCommandType(const u8 *ptr)
{
	u32 first = 0;
	if (!ptr) return 0xffu;
	memcpy(&first, ptr, sizeof(first));
	return (u8)PD_BE32(first);
}

static s32 s_setupCommandLengthBytes(const u8 *ptr)
{
	if (!ptr) return 0;
	switch (s_setupCommandType(ptr)) {
	case OBJTYPE_CHR:                return (s32)sizeof(struct packedchr);
	case OBJTYPE_DOOR:               return (s32)sizeof(struct doorobj);
	case OBJTYPE_DOORSCALE:          return (s32)sizeof(struct doorscaleobj);
	case OBJTYPE_BASIC:              return (s32)sizeof(struct defaultobj);
	case OBJTYPE_DEBRIS:             return (s32)sizeof(struct debrisobj);
	case OBJTYPE_GLASS:              return (s32)sizeof(struct glassobj);
	case OBJTYPE_TINTEDGLASS:        return (s32)sizeof(struct tintedglassobj);
	case OBJTYPE_SAFE:               return (s32)sizeof(struct safeobj);
	case OBJTYPE_GASBOTTLE:          return (s32)sizeof(struct gasbottleobj);
	case OBJTYPE_KEY:                return (s32)sizeof(struct keyobj);
	case OBJTYPE_ALARM:              return (s32)sizeof(struct alarmobj);
	case OBJTYPE_CCTV:               return (s32)sizeof(struct cctvobj);
	case OBJTYPE_AMMOCRATE:          return (s32)sizeof(struct ammocrateobj);
	case OBJTYPE_WEAPON:             return (s32)sizeof(struct weaponobj);
	case OBJTYPE_SINGLEMONITOR:      return (s32)sizeof(struct singlemonitorobj);
	case OBJTYPE_MULTIMONITOR:       return (s32)sizeof(struct multimonitorobj);
	case OBJTYPE_HANGINGMONITORS:    return (s32)sizeof(struct hangingmonitorsobj);
	case OBJTYPE_AUTOGUN:            return (s32)sizeof(struct autogunobj);
	case OBJTYPE_LINKGUNS:           return (s32)sizeof(struct linkgunsobj);
	case OBJTYPE_HAT:                return (s32)sizeof(struct hatobj);
	case OBJTYPE_GRENADEPROB:        return (s32)sizeof(struct grenadeprobobj);
	case OBJTYPE_LINKLIFTDOOR:       return (s32)sizeof(struct linkliftdoorobj);
	case OBJTYPE_SAFEITEM:           return (s32)sizeof(struct safeitemobj);
	case OBJTYPE_MULTIAMMOCRATE:     return (s32)sizeof(struct multiammocrateobj);
	case OBJTYPE_SHIELD:             return (s32)sizeof(struct shieldobj);
	case OBJTYPE_TAG:                return (s32)sizeof(struct tag);
	case OBJTYPE_RENAMEOBJ:          return (s32)sizeof(struct textoverride);
	case OBJTYPE_BEGINOBJECTIVE:     return (s32)sizeof(struct objective);
	case OBJTYPE_ENDOBJECTIVE:       return (s32)sizeof(u32);
	case OBJECTIVETYPE_DESTROYOBJ:
	case OBJECTIVETYPE_COMPFLAGS:
	case OBJECTIVETYPE_FAILFLAGS:
	case OBJECTIVETYPE_COLLECTOBJ:
	case OBJECTIVETYPE_THROWOBJ:     return (s32)(sizeof(u32) * 2u);
	case OBJECTIVETYPE_HOLOGRAPH:    return (s32)sizeof(struct criteria_holograph);
	case OBJECTIVETYPE_1F:           return (s32)sizeof(u32);
	case OBJECTIVETYPE_ENTERROOM:    return (s32)sizeof(struct criteria_roomentered);
	case OBJECTIVETYPE_THROWINROOM:  return (s32)sizeof(struct criteria_throwinroom);
	case OBJTYPE_22:                 return (s32)sizeof(u32);
	case OBJTYPE_BRIEFING:           return (s32)sizeof(struct briefingobj);
	case OBJTYPE_PADLOCKEDDOOR:      return (s32)sizeof(struct padlockeddoorobj);
	case OBJTYPE_TRUCK:              return (s32)sizeof(struct truckobj);
	case OBJTYPE_HELI:               return (s32)sizeof(struct heliobj);
	case OBJTYPE_TANK:               return (s32)(32u * sizeof(u32));
	case OBJTYPE_CAMERAPOS:          return (s32)sizeof(struct cameraposobj);
	case OBJTYPE_LIFT:               return (s32)sizeof(struct liftobj);
	case OBJTYPE_CONDITIONALSCENERY: return (s32)sizeof(struct linksceneryobj);
	case OBJTYPE_BLOCKEDPATH:        return (s32)sizeof(struct blockedpathobj);
	case OBJTYPE_HOVERBIKE:          return (s32)sizeof(struct hoverbikeobj);
	case OBJTYPE_HOVERPROP:          return (s32)sizeof(struct hoverpropobj);
	case OBJTYPE_FAN:                return (s32)sizeof(struct fanobj);
	case OBJTYPE_HOVERCAR:           return (s32)sizeof(struct hovercarobj);
	case OBJTYPE_CHOPPER:            return (s32)sizeof(struct chopperobj);
	case OBJTYPE_PADEFFECT:          return (s32)sizeof(struct padeffectobj);
	case OBJTYPE_MINE:               return (s32)sizeof(struct weaponobj);
	case OBJTYPE_ESCASTEP:           return (s32)sizeof(struct escalatorobj);
	case OBJTYPE_END:                return (s32)sizeof(u32);
	default:                         return (s32)sizeof(u32);
	}
}

static s32 s_objTypeHasDefaultBase(u8 type)
{
	switch (type) {
	case OBJTYPE_DOOR:
	case OBJTYPE_BASIC:
	case OBJTYPE_KEY:
	case OBJTYPE_ALARM:
	case OBJTYPE_CCTV:
	case OBJTYPE_AMMOCRATE:
	case OBJTYPE_WEAPON:
	case OBJTYPE_SINGLEMONITOR:
	case OBJTYPE_MULTIMONITOR:
	case OBJTYPE_HANGINGMONITORS:
	case OBJTYPE_AUTOGUN:
	case OBJTYPE_DEBRIS:
	case OBJTYPE_HAT:
	case OBJTYPE_MULTIAMMOCRATE:
	case OBJTYPE_SHIELD:
	case OBJTYPE_GASBOTTLE:
	case OBJTYPE_TRUCK:
	case OBJTYPE_HELI:
	case OBJTYPE_GLASS:
	case OBJTYPE_SAFE:
	case OBJTYPE_TINTEDGLASS:
	case OBJTYPE_LIFT:
	case OBJTYPE_HOVERBIKE:
	case OBJTYPE_HOVERPROP:
	case OBJTYPE_FAN:
	case OBJTYPE_HOVERCAR:
	case OBJTYPE_CHOPPER:
	case OBJTYPE_MINE:
	case OBJTYPE_ESCASTEP:
		return 1;
	default:
		return 0;
	}
}

static const char *s_nonnullCatalogId(const char *id)
{
	return id ? id : "";
}

static s32 s_setupFieldAppend(pdscenario_textbuf_t *fields,
                              const char *record_id,
                              const char *kind,
                              const char *field,
                              const char *type,
                              const char *value,
                              const char *catalog_id,
                              const char *ref_record_id)
{
	size_t i;
	char last = '\0';

	if (!fields) {
		return 0;
	}

	for (i = fields->len; i > 0; i--) {
		char c = fields->data[i - 1];
		if (!isspace((unsigned char)c)) {
			last = c;
			break;
		}
	}

	if (last == '}') {
		if (s_textbufAppend(fields, ",\n") != 0) {
			return -1;
		}
	}

	if (s_textbufAppend(fields, "    {\n      \"record_id\": ") != 0 ||
	    s_textbufAppendJsonString(fields, record_id ? record_id : "") != 0 ||
	    s_textbufAppend(fields, ",\n      \"kind\": ") != 0 ||
	    s_textbufAppendJsonString(fields, kind ? kind : "") != 0 ||
	    s_textbufAppend(fields, ",\n      \"field\": ") != 0 ||
	    s_textbufAppendJsonString(fields, field ? field : "") != 0 ||
	    s_textbufAppend(fields, ",\n      \"type\": ") != 0 ||
	    s_textbufAppendJsonString(fields, type ? type : "") != 0 ||
	    s_textbufAppend(fields, ",\n      \"value\": ") != 0 ||
	    s_textbufAppendJsonString(fields, value ? value : "") != 0 ||
	    s_textbufAppend(fields, ",\n      \"catalog_id\": ") != 0 ||
	    s_textbufAppendJsonString(fields, catalog_id ? catalog_id : "") != 0 ||
	    s_textbufAppend(fields, ",\n      \"ref_record_id\": ") != 0 ||
	    s_textbufAppendJsonString(fields, ref_record_id ? ref_record_id : "") != 0 ||
	    s_textbufAppend(fields, "\n    }") != 0) {
		return -1;
	}

	return 0;
}

static s32 s_setupFieldS32(pdscenario_textbuf_t *fields,
                           const char *record_id,
                           const char *kind,
                           const char *field,
                           s32 value)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%d", value);
	return s_setupFieldAppend(fields, record_id, kind, field, "s32",
		buf, "", "");
}

static s32 s_setupFieldU32Hex(pdscenario_textbuf_t *fields,
                              const char *record_id,
                              const char *kind,
                              const char *field,
                              u32 value)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "0x%08x", (unsigned)value);
	return s_setupFieldAppend(fields, record_id, kind, field, "u32_hex",
		buf, "", "");
}

static s32 s_setupFieldF32(pdscenario_textbuf_t *fields,
                           const char *record_id,
                           const char *kind,
                           const char *field,
                           f32 value)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "%.9g", (double)value);
	return s_setupFieldAppend(fields, record_id, kind, field, "f32",
		buf, "", "");
}

static s32 s_setupFieldPad(pdscenario_textbuf_t *fields,
                           const char *record_id,
                           const char *kind,
                           const char *field,
                           s32 pad)
{
	char buf[32];
	s_padRef(pad, buf, sizeof(buf));
	return s_setupFieldAppend(fields, record_id, kind, field, "pad_ref",
		buf, "", "");
}

static s32 s_setupFieldTag(pdscenario_textbuf_t *fields,
                           const char *record_id,
                           const char *kind,
                           const char *field,
                           u32 tag_id,
                           const char *ref_record_id)
{
	char buf[32];
	s_tagRef(tag_id, buf, sizeof(buf));
	return s_setupFieldAppend(fields, record_id, kind, field, "tag_ref",
		buf, "", ref_record_id ? ref_record_id : "");
}

static s32 s_setupFieldStageFlag(pdscenario_textbuf_t *fields,
                                 const char *record_id,
                                 const char *kind,
                                 const char *field,
                                 u32 flag_mask)
{
	char buf[40];
	s_stageFlagRef(flag_mask, buf, sizeof(buf));
	return s_setupFieldAppend(fields, record_id, kind, field,
		"stage_flag_ref", buf, "", "");
}

static s32 s_setupFieldAilist(pdscenario_textbuf_t *fields,
                              const char *record_id,
                              const char *kind,
                              const char *field,
                              s32 ailist)
{
	char buf[32];
	if (ailist < 0) {
		buf[0] = '\0';
	} else {
		snprintf(buf, sizeof(buf), "ailist_%04d", ailist);
	}
	return s_setupFieldAppend(fields, record_id, kind, field, "ailist_ref",
		buf, "", "");
}

static s32 s_setupFieldCatalog(pdscenario_textbuf_t *fields,
                               const char *record_id,
                               const char *kind,
                               const char *field,
                               const char *type,
                               const char *catalog_id)
{
	return s_setupFieldAppend(fields, record_id, kind, field, type,
		"", s_nonnullCatalogId(catalog_id), "");
}

static s32 s_setupFieldHeadRef(pdscenario_textbuf_t *fields,
                               const char *record_id,
                               const char *kind,
                               const char *field,
                               s32 bodynum,
                               s32 headnum)
{
	const char *catalog_id = catalogHeadIdByHeadnum(headnum);

	if (catalog_id && catalog_id[0]) {
		return s_setupFieldCatalog(fields, record_id, kind, field,
			"head_catalog_id", catalog_id);
	}

	if (headnum == HEAD_RANDOM) {
		return s_setupFieldAppend(fields, record_id, kind, field,
			"head_selector", "random", "", "");
	}

	if (catalogGetBodyIsComplete(bodynum)) {
		return s_setupFieldAppend(fields, record_id, kind, field,
			"head_selector", "embedded", "", "");
	}

	return s_setupFieldAppend(fields, record_id, kind, field,
		"head_unresolved", "unmapped", "", "");
}

static const char *s_setupWeaponCatalogIdByRuntime(s32 weaponnum)
{
	const char *catalog_id = catalogWeaponIdByRuntimeWeaponNum(weaponnum);

	if (catalog_id && catalog_id[0]) {
		return catalog_id;
	}

	if (weaponnum >= 0 && weaponnum < g_WeaponDataCount) {
		catalog_id = g_WeaponDataCatalogIds[weaponnum];
		if (catalog_id && catalog_id[0]) {
			return catalog_id;
		}
	}

	return NULL;
}

static s32 s_setupFieldWeaponRef(pdscenario_textbuf_t *fields,
                                 const char *record_id,
                                 const char *kind,
                                 const char *field,
                                 s32 weaponnum)
{
	char selector[32];
	const char *catalog_id = s_setupWeaponCatalogIdByRuntime(weaponnum);

	if (catalog_id && catalog_id[0]) {
		return s_setupFieldCatalog(fields, record_id, kind, field,
			"weapon_catalog_id", catalog_id);
	}

	if (weaponnum <= WEAPON_NONE) {
		return s_setupFieldAppend(fields, record_id, kind, field,
			"weapon_selector", "none", "", "");
	}

	if (weaponnum >= WEAPON_MPLOCATION00 && weaponnum <= WEAPON_MPLOCATION15) {
		snprintf(selector, sizeof(selector), "mp_location_%02d",
			weaponnum - WEAPON_MPLOCATION00);
		return s_setupFieldAppend(fields, record_id, kind, field,
			"weapon_selector", selector, "", "");
	}

	return s_setupFieldAppend(fields, record_id, kind, field,
		"weapon_unresolved", "unmapped", "", "");
}

static s32 s_setupFieldRecordIndex(pdscenario_textbuf_t *fields,
                                   const char *record_id,
                                   const char *kind,
                                   const char *field,
                                   s32 target_index,
                                   u32 record_count)
{
	char ref[32];
	ref[0] = '\0';
	if (target_index >= 0 && (u32)target_index < record_count) {
		snprintf(ref, sizeof(ref), "setup_%04u", (unsigned)target_index);
	}
	return s_setupFieldAppend(fields, record_id, kind, field, "record_ref",
		"", "", ref);
}

static const pdscenario_tag_target_t *s_findTagTarget(
	const pdscenario_tag_target_t *tag_targets, u32 tag_target_count,
	u32 tag_id)
{
	u32 i;

	if (!tag_targets) {
		return NULL;
	}

	for (i = 0; i < tag_target_count; i++) {
		if (tag_targets[i].tag_id == tag_id) {
			return &tag_targets[i];
		}
	}

	return NULL;
}

static void s_tagTargetRecordRef(
	const pdscenario_tag_target_t *tag_targets, u32 tag_target_count,
	u32 tag_id, char *out, size_t out_n)
{
	const pdscenario_tag_target_t *target;

	if (!out || out_n == 0) {
		return;
	}
	out[0] = '\0';

	target = s_findTagTarget(tag_targets, tag_target_count, tag_id);
	if (target && target->target_index >= 0) {
		snprintf(out, out_n, "setup_%04u",
			(unsigned)target->target_index);
	}
}

static s32 s_setupAppendCoordFields(pdscenario_textbuf_t *fields,
                                    const char *record_id,
                                    const char *kind,
                                    const char *prefix,
                                    const struct coord *coord)
{
	char field[96];
	if (!coord) {
		return 0;
	}
	snprintf(field, sizeof(field), "%s.x", prefix);
	if (s_setupFieldF32(fields, record_id, kind, field, coord->x) != 0) return -1;
	snprintf(field, sizeof(field), "%s.y", prefix);
	if (s_setupFieldF32(fields, record_id, kind, field, coord->y) != 0) return -1;
	snprintf(field, sizeof(field), "%s.z", prefix);
	if (s_setupFieldF32(fields, record_id, kind, field, coord->z) != 0) return -1;
	return 0;
}

static s32 s_setupAppendU8ArrayFields(pdscenario_textbuf_t *fields,
                                      const char *record_id,
                                      const char *kind,
                                      const char *prefix,
                                      const u8 *values,
                                      u32 count)
{
	char field[96];
	for (u32 i = 0; i < count; i++) {
		snprintf(field, sizeof(field), "%s[%u]", prefix, (unsigned)i);
		if (s_setupFieldS32(fields, record_id, kind, field,
				(s32)values[i]) != 0) {
			return -1;
		}
	}
	return 0;
}

static s32 s_setupAppendF32ArrayFields(pdscenario_textbuf_t *fields,
                                       const char *record_id,
                                       const char *kind,
                                       const char *prefix,
                                       const f32 *values,
                                       u32 count)
{
	char field[96];
	for (u32 i = 0; i < count; i++) {
		snprintf(field, sizeof(field), "%s[%u]", prefix, (unsigned)i);
		if (s_setupFieldF32(fields, record_id, kind, field,
				values[i]) != 0) {
			return -1;
		}
	}
	return 0;
}

static s32 s_setupAppendTvScreenFields(pdscenario_textbuf_t *fields,
                                       const char *record_id,
                                       const char *kind,
                                       const char *prefix,
                                       const struct tvscreen *screen)
{
	char field[96];
	if (!screen) {
		return 0;
	}
#define TV_S32(name) do { \
	snprintf(field, sizeof(field), "%s.%s", prefix, #name); \
	if (s_setupFieldS32(fields, record_id, kind, field, (s32)screen->name) != 0) return -1; \
} while (0)
#define TV_F32(name) do { \
	snprintf(field, sizeof(field), "%s.%s", prefix, #name); \
	if (s_setupFieldF32(fields, record_id, kind, field, screen->name) != 0) return -1; \
} while (0)
	TV_S32(offset);
	TV_S32(pause60);
	TV_F32(rot);
	TV_F32(xscale);
	TV_F32(xscalefrac);
	TV_F32(xscaleinc);
	TV_F32(xscaleold);
	TV_F32(xscalenew);
	TV_F32(yscale);
	TV_F32(yscalefrac);
	TV_F32(yscaleinc);
	TV_F32(yscaleold);
	TV_F32(yscalenew);
	TV_F32(xmid);
	TV_F32(xmidfrac);
	TV_F32(xmidinc);
	TV_F32(xmidold);
	TV_F32(xmidnew);
	TV_F32(ymid);
	TV_F32(ymidfrac);
	TV_F32(ymidinc);
	TV_F32(ymidold);
	TV_F32(ymidnew);
	TV_S32(red);
	TV_S32(redold);
	TV_S32(rednew);
	TV_S32(green);
	TV_S32(greenold);
	TV_S32(greennew);
	TV_S32(blue);
	TV_S32(blueold);
	TV_S32(bluenew);
	TV_S32(alpha);
	TV_S32(alphaold);
	TV_S32(alphanew);
	TV_F32(colfrac);
	TV_F32(colinc);
#undef TV_S32
#undef TV_F32
	return 0;
}

static s32 s_setupAppendHoverFields(pdscenario_textbuf_t *fields,
                                    const char *record_id,
                                    const char *kind,
                                    const char *prefix,
                                    const struct hov *hov)
{
	char field[96];
	if (!hov) {
		return 0;
	}
#define HOV_S32(name) do { \
	snprintf(field, sizeof(field), "%s.%s", prefix, #name); \
	if (s_setupFieldS32(fields, record_id, kind, field, (s32)hov->name) != 0) return -1; \
} while (0)
#define HOV_F32(name) do { \
	snprintf(field, sizeof(field), "%s.%s", prefix, #name); \
	if (s_setupFieldF32(fields, record_id, kind, field, hov->name) != 0) return -1; \
} while (0)
	HOV_S32(type);
	HOV_S32(flags);
	HOV_F32(bobycur);
	HOV_F32(bobytarget);
	HOV_F32(bobyspeed);
	HOV_F32(yrot);
	HOV_F32(bobpitchcur);
	HOV_F32(bobpitchtarget);
	HOV_F32(bobpitchspeed);
	HOV_F32(bobrollcur);
	HOV_F32(bobrolltarget);
	HOV_F32(bobrollspeed);
	HOV_F32(groundpitch);
	HOV_F32(y);
	HOV_F32(ground);
	HOV_S32(prevframe60);
	HOV_S32(prevgroundframe60);
#undef HOV_S32
#undef HOV_F32
	return 0;
}

static s32 s_setupAppendDefaultObjectFields(pdscenario_textbuf_t *fields,
                                            const char *record_id,
                                            const char *kind,
                                            const struct defaultobj *obj)
{
	char field[96];
	if (!obj) {
		return 0;
	}
	if (s_setupFieldS32(fields, record_id, kind, "base.extra_scale",
			(s32)obj->extrascale) != 0) return -1;
	if (s_setupFieldS32(fields, record_id, kind, "base.hidden2",
			(s32)obj->hidden2) != 0) return -1;
	if (s_setupFieldCatalog(fields, record_id, kind, "base.model",
			"model_catalog_id",
			catalogModelIdByModelnum(obj->modelnum)) != 0) return -1;
	if (s_setupFieldPad(fields, record_id, kind, "base.pad",
			(s32)obj->pad) != 0) return -1;
	if (s_setupFieldU32Hex(fields, record_id, kind, "base.flags",
			obj->flags) != 0) return -1;
	if (s_setupFieldU32Hex(fields, record_id, kind, "base.flags2",
			obj->flags2) != 0) return -1;
	if (s_setupFieldU32Hex(fields, record_id, kind, "base.flags3",
			obj->flags3) != 0) return -1;
	for (u32 r = 0; r < 3; r++) {
		for (u32 c = 0; c < 3; c++) {
			snprintf(field, sizeof(field), "base.rotation[%u][%u]",
				(unsigned)r, (unsigned)c);
			if (s_setupFieldF32(fields, record_id, kind, field,
					obj->realrot[r][c]) != 0) {
				return -1;
			}
		}
	}
	if (s_setupFieldU32Hex(fields, record_id, kind, "base.hidden",
			obj->hidden) != 0) return -1;
	if (s_setupFieldS32(fields, record_id, kind, "base.damage",
			(s32)obj->damage) != 0) return -1;
	if (s_setupFieldS32(fields, record_id, kind, "base.max_damage",
			(s32)obj->maxdamage) != 0) return -1;
	if (s_setupAppendU8ArrayFields(fields, record_id, kind,
			"base.shade_color", obj->shadecol,
			ARRAYCOUNT(obj->shadecol)) != 0) return -1;
	if (s_setupAppendU8ArrayFields(fields, record_id, kind,
			"base.next_color", obj->nextcol,
			ARRAYCOUNT(obj->nextcol)) != 0) return -1;
	if (s_setupFieldS32(fields, record_id, kind, "base.floor_color",
			(s32)obj->floorcol) != 0) return -1;
	if (s_setupFieldS32(fields, record_id, kind, "base.geo_count",
			(s32)obj->geocount) != 0) return -1;
	return 0;
}

static s32 s_setupAppendCommandFields(pdscenario_textbuf_t *fields,
                                      const char *record_id,
                                      const char *kind,
                                      const u8 *ptr,
                                      u8 type,
                                      u32 index,
                                      u32 record_count,
                                      const pdscenario_tag_target_t *tag_targets,
                                      u32 tag_target_count)
{
	char field[96];

	if (!fields || !record_id || !kind || !ptr) {
		return 0;
	}

	if (s_setupFieldS32(fields, record_id, kind, "command.order",
			(s32)index) != 0) return -1;

	if (type == OBJTYPE_CHR) {
		const struct packedchr *chr = (const struct packedchr *)ptr;
		if (s_setupFieldS32(fields, record_id, kind, "character.index",
				(s32)chr->chrindex) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind, "character.kind",
				(s32)chr->typenum) != 0) return -1;
		if (s_setupFieldU32Hex(fields, record_id, kind,
				"character.spawn_flags", chr->spawnflags) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind, "character.slot",
				(s32)chr->chrnum) != 0) return -1;
		if (s_setupFieldPad(fields, record_id, kind, "character.pad",
				(s32)chr->padnum) != 0) return -1;
		if (s_setupFieldCatalog(fields, record_id, kind, "character.body",
				"body_catalog_id",
				catalogBodyIdByBodynum(chr->bodynum)) != 0) return -1;
		if (s_setupFieldHeadRef(fields, record_id, kind,
				"character.head", (s32)chr->bodynum,
				(s32)chr->headnum) != 0) return -1;
		if (s_setupFieldAilist(fields, record_id, kind,
				"character.ai_list", (s32)chr->ailistnum) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"character.pad_preset", (s32)chr->padpreset) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"character.character_preset", (s32)chr->chrpreset) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"character.hearing_scale", (s32)chr->hearscale) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"character.view_distance", (s32)chr->viewdist) != 0) return -1;
		if (s_setupFieldU32Hex(fields, record_id, kind,
				"character.flags", chr->flags) != 0) return -1;
		if (s_setupFieldU32Hex(fields, record_id, kind,
				"character.flags2", chr->flags2) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"character.team", (s32)chr->team) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"character.squadron", (s32)chr->squadron) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"character.chair", (s32)chr->chair) != 0) return -1;
		if (s_setupFieldU32Hex(fields, record_id, kind,
				"character.conversation_talk", chr->convtalk) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"character.attitude", (s32)chr->tude) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"character.natural_animation", (s32)chr->naturalanim) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"character.visible_yaw_angle", (s32)chr->yvisang) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"character.team_scan_distance", (s32)chr->teamscandist) != 0) return -1;
		return 0;
	}

	if (s_objTypeHasDefaultBase(type)) {
		const struct defaultobj *base = (const struct defaultobj *)ptr;
		if (s_setupAppendDefaultObjectFields(fields, record_id, kind,
				base) != 0) {
			return -1;
		}
	}

	switch (type) {
	case OBJTYPE_DOOR: {
		const struct doorobj *door = (const struct doorobj *)ptr;
#define DOOR_F32(name) if (s_setupFieldF32(fields, record_id, kind, "door." #name, door->name) != 0) return -1
#define DOOR_S32(name) if (s_setupFieldS32(fields, record_id, kind, "door." #name, (s32)door->name) != 0) return -1
		DOOR_F32(maxfrac);
		DOOR_F32(perimfrac);
		DOOR_F32(accel);
		DOOR_F32(decel);
		DOOR_F32(maxspeed);
		DOOR_S32(doorflags);
		DOOR_S32(doortype);
		if (s_setupFieldU32Hex(fields, record_id, kind,
				"door.key_flags", door->keyflags) != 0) return -1;
		DOOR_S32(autoclosetime);
		DOOR_F32(frac);
		DOOR_F32(fracspeed);
		DOOR_S32(mode);
		DOOR_S32(glasshits);
		DOOR_S32(fadealpha);
		DOOR_S32(xludist);
		DOOR_S32(opadist);
		if (s_setupAppendCoordFields(fields, record_id, kind,
				"door.start_position", &door->startpos) != 0) return -1;
		for (u32 r = 0; r < 3; r++) {
			for (u32 c = 0; c < 3; c++) {
				snprintf(field, sizeof(field), "door.matrix[%u][%u]",
					(unsigned)r, (unsigned)c);
				if (s_setupFieldF32(fields, record_id, kind, field,
						door->mtx98[r][c]) != 0) return -1;
			}
		}
		DOOR_S32(lastopen60);
		DOOR_S32(portalnum);
		DOOR_S32(soundtype);
		DOOR_S32(fadetime60);
		DOOR_S32(lastcalc60);
		DOOR_S32(laserfade);
		if (s_setupAppendU8ArrayFields(fields, record_id, kind,
				"door.shade_info_player1", door->shadeinfo1,
				ARRAYCOUNT(door->shadeinfo1)) != 0) return -1;
		if (s_setupAppendU8ArrayFields(fields, record_id, kind,
				"door.shade_info_player2", door->shadeinfo2,
				ARRAYCOUNT(door->shadeinfo2)) != 0) return -1;
		DOOR_S32(actual1);
		DOOR_S32(actual2);
		DOOR_S32(extra1);
		DOOR_S32(extra2);
#undef DOOR_F32
#undef DOOR_S32
		break;
	}
	case OBJTYPE_DOORSCALE: {
		const struct doorscaleobj *scale = (const struct doorscaleobj *)ptr;
		if (s_setupFieldS32(fields, record_id, kind, "door_scale.scale",
				scale->scale) != 0) return -1;
		break;
	}
	case OBJTYPE_KEY: {
		const struct keyobj *key = (const struct keyobj *)ptr;
		if (s_setupFieldU32Hex(fields, record_id, kind, "key.flags",
				key->keyflags) != 0) return -1;
		break;
	}
	case OBJTYPE_CCTV: {
		const struct cctvobj *cctv = (const struct cctvobj *)ptr;
		if (s_setupFieldPad(fields, record_id, kind, "cctv.look_at_pad",
				(s32)cctv->lookatpadnum) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind, "cctv.to_left",
				(s32)cctv->toleft) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "cctv.y_zero",
				cctv->yzero) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "cctv.y_rot",
				cctv->yrot) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "cctv.y_left",
				cctv->yleft) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "cctv.y_right",
				cctv->yright) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "cctv.y_speed",
				cctv->yspeed) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "cctv.y_max_speed",
				cctv->ymaxspeed) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"cctv.see_bond_time60", cctv->seebondtime60) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "cctv.max_distance",
				cctv->maxdist) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "cctv.x_zero",
				cctv->xzero) != 0) return -1;
		break;
	}
	case OBJTYPE_AMMOCRATE: {
		const struct ammocrateobj *crate = (const struct ammocrateobj *)ptr;
		if (s_setupFieldS32(fields, record_id, kind,
				"ammo_crate.ammo_kind", crate->ammotype) != 0) return -1;
		break;
	}
	case OBJTYPE_WEAPON:
	case OBJTYPE_MINE: {
		const struct weaponobj *weapon = (const struct weaponobj *)ptr;
		if (s_setupFieldWeaponRef(fields, record_id, kind,
				"pickup.weapon", (s32)weapon->weaponnum) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"pickup.unknown_5d", (s32)weapon->unk5d) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"pickup.unknown_5e", (s32)weapon->unk5e) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"pickup.fire_mode", (s32)weapon->gunfunc) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"pickup.fadeout_timer60", (s32)weapon->fadeouttimer60) != 0) return -1;
		if (s_setupFieldWeaponRef(fields, record_id, kind,
				"pickup.dual_weapon", (s32)weapon->dualweaponnum) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"pickup.team_or_timer240", (s32)weapon->team) != 0) return -1;
		break;
	}
	case OBJTYPE_SINGLEMONITOR: {
		const struct singlemonitorobj *mon = (const struct singlemonitorobj *)ptr;
		if (s_setupAppendTvScreenFields(fields, record_id, kind,
				"monitor.screen", &mon->screen) != 0) return -1;
		if (s_setupFieldRecordIndex(fields, record_id, kind,
				"monitor.owner", (s32)index + mon->owneroffset,
				record_count) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"monitor.owner_part", (s32)mon->ownerpart) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"monitor.image", (s32)mon->imagenum) != 0) return -1;
		break;
	}
	case OBJTYPE_MULTIMONITOR: {
		const struct multimonitorobj *mon = (const struct multimonitorobj *)ptr;
		for (u32 i = 0; i < ARRAYCOUNT(mon->screens); i++) {
			snprintf(field, sizeof(field), "monitor.screens[%u]",
				(unsigned)i);
			if (s_setupAppendTvScreenFields(fields, record_id, kind,
					field, &mon->screens[i]) != 0) return -1;
			snprintf(field, sizeof(field), "monitor.images[%u]",
				(unsigned)i);
			if (s_setupFieldS32(fields, record_id, kind, field,
					(s32)mon->imagenums[i]) != 0) return -1;
		}
		break;
	}
	case OBJTYPE_AUTOGUN: {
		const struct autogunobj *gun = (const struct autogunobj *)ptr;
		if (s_setupFieldPad(fields, record_id, kind, "autogun.target_pad",
				(s32)gun->targetpad) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind, "autogun.firing",
				(s32)gun->firing) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind, "autogun.fire_count",
				(s32)gun->firecount) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "autogun.y_zero",
				gun->yzero) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "autogun.y_max_left",
				gun->ymaxleft) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "autogun.y_max_right",
				gun->ymaxright) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "autogun.y_rot",
				gun->yrot) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "autogun.y_speed",
				gun->yspeed) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "autogun.x_zero",
				gun->xzero) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "autogun.x_rot",
				gun->xrot) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "autogun.x_speed",
				gun->xspeed) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "autogun.max_speed",
				gun->maxspeed) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "autogun.aim_distance",
				gun->aimdist) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "autogun.barrel_speed",
				gun->barrelspeed) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind, "autogun.barrel_rot",
				gun->barrelrot) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"autogun.last_see_bond60", gun->lastseebond60) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"autogun.last_aim_bond60", gun->lastaimbond60) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"autogun.allow_sound_frame", gun->allowsoundframe) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"autogun.shot_bond_sum", gun->shotbondsum) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"autogun.target_team", (s32)gun->targetteam) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"autogun.ammo_quantity", (s32)gun->ammoquantity) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"autogun.next_character_test", (s32)gun->nextchrtest) != 0) return -1;
		break;
	}
	case OBJTYPE_LINKGUNS: {
		const struct linkgunsobj *link = (const struct linkgunsobj *)ptr;
		if (s_setupFieldRecordIndex(fields, record_id, kind,
				"linked_guns.weapon_1", (s32)index + link->offset1,
				record_count) != 0) return -1;
		if (s_setupFieldRecordIndex(fields, record_id, kind,
				"linked_guns.weapon_2", (s32)index + link->offset2,
				record_count) != 0) return -1;
		break;
	}
	case OBJTYPE_GRENADEPROB: {
		const struct grenadeprobobj *prob = (const struct grenadeprobobj *)ptr;
		if (s_setupFieldS32(fields, record_id, kind,
				"grenade_probability.character", (s32)prob->chrnum) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"grenade_probability.percent", (s32)prob->probability) != 0) return -1;
		break;
	}
	case OBJTYPE_LINKLIFTDOOR: {
		const struct linkliftdoorobj *link = (const struct linkliftdoorobj *)ptr;
		if (s_setupFieldRecordIndex(fields, record_id, kind,
				"lift_door_link.door", (s32)index + (s32)(uintptr_t)link->door,
				record_count) != 0) return -1;
		if (s_setupFieldRecordIndex(fields, record_id, kind,
				"lift_door_link.lift", (s32)index + (s32)(uintptr_t)link->lift,
				record_count) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"lift_door_link.stop", link->stopnum) != 0) return -1;
		break;
	}
	case OBJTYPE_MULTIAMMOCRATE: {
		const struct multiammocrateobj *crate = (const struct multiammocrateobj *)ptr;
		for (u32 i = 0; i < ARRAYCOUNT(crate->slots); i++) {
			snprintf(field, sizeof(field), "multi_ammo_crate.slots[%u].model",
				(unsigned)i);
			if (s_setupFieldCatalog(fields, record_id, kind, field,
					"model_catalog_id",
					catalogModelIdByModelnum(crate->slots[i].modelnum)) != 0) return -1;
			snprintf(field, sizeof(field), "multi_ammo_crate.slots[%u].quantity",
				(unsigned)i);
			if (s_setupFieldS32(fields, record_id, kind, field,
					(s32)crate->slots[i].quantity) != 0) return -1;
		}
		break;
	}
	case OBJTYPE_SHIELD: {
		const struct shieldobj *shield = (const struct shieldobj *)ptr;
		if (s_setupFieldF32(fields, record_id, kind,
				"shield.initial_amount", shield->initialamount) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"shield.amount", shield->amount) != 0) return -1;
		if (s_setupFieldU32Hex(fields, record_id, kind,
				"shield.unknown_64", shield->unk64) != 0) return -1;
		break;
	}
	case OBJTYPE_TAG: {
		const struct tag *tag = (const struct tag *)ptr;
		if (s_setupFieldS32(fields, record_id, kind,
				"tag.id", (s32)tag->tagnum) != 0) return -1;
		if (s_setupFieldRecordIndex(fields, record_id, kind,
				"tag.target", (s32)index + tag->cmdoffset,
				record_count) != 0) return -1;
		break;
	}
	case OBJTYPE_BEGINOBJECTIVE: {
		const struct objective *obj = (const struct objective *)ptr;
		if (s_setupFieldS32(fields, record_id, kind,
				"objective.index", obj->index) != 0) return -1;
		if (s_setupFieldU32Hex(fields, record_id, kind,
				"objective.text_token", obj->text) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"objective.flags", (s32)obj->flags) != 0) return -1;
		if (s_setupFieldU32Hex(fields, record_id, kind,
				"objective.difficulty_mask", (u32)(u8)obj->difficulties) != 0) return -1;
		break;
	}
	case OBJECTIVETYPE_DESTROYOBJ:
	case OBJECTIVETYPE_COLLECTOBJ:
	case OBJECTIVETYPE_THROWOBJ: {
		const u32 *words = (const u32 *)ptr;
		u32 tag_id = PD_BE32(words[1]);
		char target_ref[32];
		s_tagTargetRecordRef(tag_targets, tag_target_count, tag_id,
			target_ref, sizeof(target_ref));
		if (s_setupFieldTag(fields, record_id, kind,
				"objective_step.target_tag", tag_id,
				target_ref) != 0) return -1;
		break;
	}
	case OBJECTIVETYPE_COMPFLAGS:
	case OBJECTIVETYPE_FAILFLAGS: {
		const u32 *words = (const u32 *)ptr;
		u32 flag_mask = PD_BE32(words[1]);
		if (s_setupFieldStageFlag(fields, record_id, kind,
				"objective_step.stage_flag", flag_mask) != 0) return -1;
		break;
	}
	case OBJECTIVETYPE_HOLOGRAPH: {
		const struct criteria_holograph *criteria = (const struct criteria_holograph *)ptr;
		char target_ref[32];
		s_tagTargetRecordRef(tag_targets, tag_target_count,
			criteria->obj, target_ref, sizeof(target_ref));
		if (s_setupFieldTag(fields, record_id, kind,
				"objective_holograph.target_tag", criteria->obj,
				target_ref) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"objective_holograph.status", (s32)criteria->status) != 0) return -1;
		break;
	}
	case OBJECTIVETYPE_ENTERROOM: {
		const struct criteria_roomentered *criteria = (const struct criteria_roomentered *)ptr;
		if (s_setupFieldPad(fields, record_id, kind,
				"objective_enter_room.pad", (s32)criteria->pad) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"objective_enter_room.status", (s32)criteria->status) != 0) return -1;
		break;
	}
	case OBJECTIVETYPE_THROWINROOM: {
		const struct criteria_throwinroom *criteria = (const struct criteria_throwinroom *)ptr;
		if (s_setupFieldS32(fields, record_id, kind,
				"objective_throw_in_room.match_value", (s32)criteria->unk04) != 0) return -1;
		if (s_setupFieldPad(fields, record_id, kind,
				"objective_throw_in_room.pad", (s32)criteria->pad) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"objective_throw_in_room.status", (s32)criteria->status) != 0) return -1;
		break;
	}
	case OBJTYPE_BRIEFING: {
		const struct briefingobj *briefing = (const struct briefingobj *)ptr;
		if (s_setupFieldS32(fields, record_id, kind,
				"briefing.kind", (s32)briefing->type) != 0) return -1;
		if (s_setupFieldU32Hex(fields, record_id, kind,
				"briefing.text_token", briefing->text) != 0) return -1;
		break;
	}
	case OBJTYPE_RENAMEOBJ: {
		const struct textoverride *text = (const struct textoverride *)ptr;
		if (s_setupFieldRecordIndex(fields, record_id, kind,
				"rename_object.target", (s32)index + text->objoffset,
				record_count) != 0) return -1;
		if (s_setupFieldWeaponRef(fields, record_id, kind,
				"rename_object.weapon", text->weapon) != 0) return -1;
		if (s_setupFieldU32Hex(fields, record_id, kind,
				"rename_object.obtain_text", text->obtaintext) != 0) return -1;
		if (s_setupFieldU32Hex(fields, record_id, kind,
				"rename_object.owner_text", text->ownertext) != 0) return -1;
		if (s_setupFieldU32Hex(fields, record_id, kind,
				"rename_object.inventory_text", text->inventorytext) != 0) return -1;
		if (s_setupFieldU32Hex(fields, record_id, kind,
				"rename_object.inventory2_text", text->inventory2text) != 0) return -1;
		if (s_setupFieldU32Hex(fields, record_id, kind,
				"rename_object.pickup_text", text->pickuptext) != 0) return -1;
		break;
	}
	case OBJTYPE_PADLOCKEDDOOR: {
		const struct padlockeddoorobj *link = (const struct padlockeddoorobj *)ptr;
		if (s_setupFieldRecordIndex(fields, record_id, kind,
				"padlocked_door.door", (s32)index + (s32)(uintptr_t)link->door,
				record_count) != 0) return -1;
		if (s_setupFieldRecordIndex(fields, record_id, kind,
				"padlocked_door.lock", (s32)index + (s32)(uintptr_t)link->lock,
				record_count) != 0) return -1;
		break;
	}
	case OBJTYPE_TRUCK: {
		const struct truckobj *truck = (const struct truckobj *)ptr;
		if (s_setupFieldS32(fields, record_id, kind,
				"vehicle.ai_offset", (s32)truck->aioffset) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"vehicle.ai_return_list", (s32)truck->aireturnlist) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.speed", truck->speed) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.wheel_x_rot", truck->wheelxrot) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.wheel_y_rot", truck->wheelyrot) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.speed_aim", truck->speedaim) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.speed_time60", truck->speedtime60) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.turn_rot60", truck->turnrot60) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.rot_y", truck->roty) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"vehicle.next_step", truck->nextstep) != 0) return -1;
		break;
	}
	case OBJTYPE_HELI: {
		const struct heliobj *heli = (const struct heliobj *)ptr;
		if (s_setupFieldS32(fields, record_id, kind,
				"vehicle.ai_offset", (s32)heli->aioffset) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"vehicle.ai_return_list", (s32)heli->aireturnlist) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.rotor_y_rot", heli->rotoryrot) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.rotor_y_speed", heli->rotoryspeed) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.rotor_y_speed_aim", heli->rotoryspeedaim) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.rotor_y_speed_time", heli->rotoryspeedtime) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.speed", heli->speed) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.speed_aim", heli->speedaim) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.speed_time60", heli->speedtime60) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.rot_y", heli->yrot) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"vehicle.next_step", heli->nextstep) != 0) return -1;
		break;
	}
	case OBJTYPE_GLASS: {
		const struct glassobj *glass = (const struct glassobj *)ptr;
		if (s_setupFieldS32(fields, record_id, kind,
				"glass.portal", (s32)glass->portalnum) != 0) return -1;
		break;
	}
	case OBJTYPE_SAFEITEM: {
		const struct safeitemobj *link = (const struct safeitemobj *)ptr;
		if (s_setupFieldRecordIndex(fields, record_id, kind,
				"safe_item.item", (s32)index + (s32)(uintptr_t)link->item,
				record_count) != 0) return -1;
		if (s_setupFieldRecordIndex(fields, record_id, kind,
				"safe_item.safe", (s32)index + (s32)(uintptr_t)link->safe,
				record_count) != 0) return -1;
		if (s_setupFieldRecordIndex(fields, record_id, kind,
				"safe_item.door", (s32)index + (s32)(uintptr_t)link->door,
				record_count) != 0) return -1;
		break;
	}
	case OBJTYPE_CAMERAPOS: {
		const struct cameraposobj *cam = (const struct cameraposobj *)ptr;
		if (s_setupFieldF32(fields, record_id, kind,
				"camera_position.x", cam->x) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"camera_position.y", cam->y) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"camera_position.z", cam->z) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"camera_position.theta", cam->theta) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"camera_position.vertical_angle", cam->verta) != 0) return -1;
		if (s_setupFieldPad(fields, record_id, kind,
				"camera_position.pad", cam->pad) != 0) return -1;
		break;
	}
	case OBJTYPE_TINTEDGLASS: {
		const struct tintedglassobj *glass = (const struct tintedglassobj *)ptr;
		if (s_setupFieldS32(fields, record_id, kind,
				"tinted_glass.xlu_distance", (s32)glass->xludist) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"tinted_glass.opa_distance", (s32)glass->opadist) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"tinted_glass.opacity", (s32)glass->opacity) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"tinted_glass.portal", (s32)glass->portalnum) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"tinted_glass.unknown_64", glass->unk64) != 0) return -1;
		break;
	}
	case OBJTYPE_LIFT: {
		const struct liftobj *lift = (const struct liftobj *)ptr;
		for (u32 i = 0; i < ARRAYCOUNT(lift->pads); i++) {
			snprintf(field, sizeof(field), "lift.stops[%u].pad",
				(unsigned)i);
			if (s_setupFieldPad(fields, record_id, kind, field,
					(s32)lift->pads[i]) != 0) return -1;
			snprintf(field, sizeof(field), "lift.stops[%u].door",
				(unsigned)i);
			if (s_setupFieldRecordIndex(fields, record_id, kind, field,
					(s32)index + *(const s32 *)&lift->doors[i],
					record_count) != 0) return -1;
		}
		if (s_setupFieldF32(fields, record_id, kind,
				"lift.distance", lift->dist) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"lift.speed", lift->speed) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"lift.accel", lift->accel) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"lift.max_speed", lift->maxspeed) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"lift.sound", (s32)lift->soundtype) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"lift.current_level", (s32)lift->levelcur) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"lift.target_level", (s32)lift->levelaim) != 0) return -1;
		if (s_setupAppendCoordFields(fields, record_id, kind,
				"lift.previous_position", &lift->prevpos) != 0) return -1;
		break;
	}
	case OBJTYPE_CONDITIONALSCENERY: {
		const struct linksceneryobj *link = (const struct linksceneryobj *)ptr;
		if (s_setupFieldRecordIndex(fields, record_id, kind,
				"conditional_scenery.trigger", (s32)index + (s32)(uintptr_t)link->trigger,
				record_count) != 0) return -1;
		if (s_setupFieldRecordIndex(fields, record_id, kind,
				"conditional_scenery.unexploded", (s32)index + (s32)(uintptr_t)link->unexp,
				record_count) != 0) return -1;
		if (s_setupFieldRecordIndex(fields, record_id, kind,
				"conditional_scenery.exploded", (s32)index + (s32)(uintptr_t)link->exp,
				record_count) != 0) return -1;
		break;
	}
	case OBJTYPE_BLOCKEDPATH: {
		const struct blockedpathobj *blocked = (const struct blockedpathobj *)ptr;
		if (s_setupFieldRecordIndex(fields, record_id, kind,
				"blocked_path.blocker", (s32)index + (s32)(uintptr_t)blocked->blocker,
				record_count) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"blocked_path.waypoint_1", (s32)blocked->waypoint1) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"blocked_path.waypoint_2", (s32)blocked->waypoint2) != 0) return -1;
		break;
	}
	case OBJTYPE_HOVERBIKE: {
		const struct hoverbikeobj *bike = (const struct hoverbikeobj *)ptr;
		if (s_setupAppendHoverFields(fields, record_id, kind,
				"hover", &bike->hov) != 0) return -1;
		if (s_setupAppendF32ArrayFields(fields, record_id, kind,
				"hoverbike.speed", bike->speed,
				ARRAYCOUNT(bike->speed)) != 0) return -1;
		if (s_setupAppendF32ArrayFields(fields, record_id, kind,
				"hoverbike.previous_position", bike->prevpos,
				ARRAYCOUNT(bike->prevpos)) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"hoverbike.ex_real", bike->exreal) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"hoverbike.ez_real", bike->ezreal) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"hoverbike.ez_real2", bike->ezreal2) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"hoverbike.lean_speed", bike->leanspeed) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"hoverbike.lean_diff", bike->leandiff) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"hoverbike.max_speed_time240", bike->maxspeedtime240) != 0) return -1;
		if (s_setupAppendF32ArrayFields(fields, record_id, kind,
				"hoverbike.relative", bike->rels,
				ARRAYCOUNT(bike->rels)) != 0) return -1;
		if (s_setupAppendF32ArrayFields(fields, record_id, kind,
				"hoverbike.absolute_speed", bike->speedabs,
				ARRAYCOUNT(bike->speedabs)) != 0) return -1;
		if (s_setupAppendF32ArrayFields(fields, record_id, kind,
				"hoverbike.relative_speed", bike->speedrel,
				ARRAYCOUNT(bike->speedrel)) != 0) return -1;
		break;
	}
	case OBJTYPE_HOVERPROP: {
		const struct hoverpropobj *prop = (const struct hoverpropobj *)ptr;
		if (s_setupAppendHoverFields(fields, record_id, kind,
				"hover", &prop->hov) != 0) return -1;
		break;
	}
	case OBJTYPE_FAN: {
		const struct fanobj *fan = (const struct fanobj *)ptr;
		if (s_setupFieldF32(fields, record_id, kind,
				"fan.y_rot", fan->yrot) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"fan.previous_y_rot", fan->yrotprev) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"fan.y_max_speed", fan->ymaxspeed) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"fan.y_speed", fan->yspeed) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"fan.y_accel", fan->yaccel) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"fan.on", (s32)fan->on) != 0) return -1;
		break;
	}
	case OBJTYPE_HOVERCAR:
	case OBJTYPE_CHOPPER: {
		const struct hovercarobj *car = (const struct hovercarobj *)ptr;
		if (s_setupFieldS32(fields, record_id, kind,
				"vehicle.ai_offset", (s32)car->aioffset) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"vehicle.ai_return_list", (s32)car->aireturnlist) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.speed", car->speed) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.speed_aim", car->speedaim) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.speed_time60", car->speedtime60) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.turn_y_speed60", car->turnyspeed60) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.turn_x_speed60", car->turnxspeed60) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.turn_rot60", car->turnrot60) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.rot_y", car->roty) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.rot_x", car->rotx) != 0) return -1;
		if (s_setupFieldF32(fields, record_id, kind,
				"vehicle.rot_z", car->rotz) != 0) return -1;
		if (s_setupFieldS32(fields, record_id, kind,
				"vehicle.next_step", car->nextstep) != 0) return -1;
		if (type == OBJTYPE_CHOPPER) {
			const struct chopperobj *chopper = (const struct chopperobj *)ptr;
			if (s_setupFieldS32(fields, record_id, kind,
					"chopper.weapons_armed", (s32)chopper->weaponsarmed) != 0) return -1;
			if (s_setupFieldS32(fields, record_id, kind,
					"chopper.on_target", (s32)chopper->ontarget) != 0) return -1;
			if (s_setupFieldS32(fields, record_id, kind,
					"chopper.target", (s32)chopper->target) != 0) return -1;
			if (s_setupFieldS32(fields, record_id, kind,
					"chopper.attack_mode", (s32)chopper->attackmode) != 0) return -1;
			if (s_setupFieldS32(fields, record_id, kind,
					"chopper.clockwise", (s32)chopper->cw) != 0) return -1;
			if (s_setupFieldF32(fields, record_id, kind,
					"chopper.vx", chopper->vx) != 0) return -1;
			if (s_setupFieldF32(fields, record_id, kind,
					"chopper.vy", chopper->vy) != 0) return -1;
			if (s_setupFieldF32(fields, record_id, kind,
					"chopper.vz", chopper->vz) != 0) return -1;
			if (s_setupFieldF32(fields, record_id, kind,
					"chopper.power", chopper->power) != 0) return -1;
			if (s_setupFieldF32(fields, record_id, kind,
					"chopper.origin_target_x", chopper->otx) != 0) return -1;
			if (s_setupFieldF32(fields, record_id, kind,
					"chopper.origin_target_y", chopper->oty) != 0) return -1;
			if (s_setupFieldF32(fields, record_id, kind,
					"chopper.origin_target_z", chopper->otz) != 0) return -1;
			if (s_setupFieldF32(fields, record_id, kind,
					"chopper.bob", chopper->bob) != 0) return -1;
			if (s_setupFieldF32(fields, record_id, kind,
					"chopper.bob_strength", chopper->bobstrength) != 0) return -1;
			if (s_setupFieldS32(fields, record_id, kind,
					"chopper.target_visible", chopper->targetvisible ? 1 : 0) != 0) return -1;
			if (s_setupFieldS32(fields, record_id, kind,
					"chopper.timer60", chopper->timer60) != 0) return -1;
			if (s_setupFieldS32(fields, record_id, kind,
					"chopper.patrol_timer60", chopper->patroltimer60) != 0) return -1;
			if (s_setupFieldF32(fields, record_id, kind,
					"chopper.gun_turn_y_speed60", chopper->gunturnyspeed60) != 0) return -1;
			if (s_setupFieldF32(fields, record_id, kind,
					"chopper.gun_turn_x_speed60", chopper->gunturnxspeed60) != 0) return -1;
			if (s_setupFieldF32(fields, record_id, kind,
					"chopper.gun_rot_y", chopper->gunroty) != 0) return -1;
			if (s_setupFieldF32(fields, record_id, kind,
					"chopper.gun_rot_x", chopper->gunrotx) != 0) return -1;
			if (s_setupFieldF32(fields, record_id, kind,
					"chopper.barrel_rot_speed", chopper->barrelrotspeed) != 0) return -1;
			if (s_setupFieldF32(fields, record_id, kind,
					"chopper.barrel_rot", chopper->barrelrot) != 0) return -1;
			if (s_setupFieldS32(fields, record_id, kind,
					"chopper.dead", chopper->dead ? 1 : 0) != 0) return -1;
		} else {
			if (s_setupFieldS32(fields, record_id, kind,
					"vehicle.status", (s32)car->status) != 0) return -1;
			if (s_setupFieldS32(fields, record_id, kind,
					"vehicle.dead", (s32)car->dead) != 0) return -1;
			if (s_setupFieldS32(fields, record_id, kind,
					"vehicle.dead_timer60", (s32)car->deadtimer60) != 0) return -1;
			if (s_setupFieldS32(fields, record_id, kind,
					"vehicle.sparks_timer60", (s32)car->sparkstimer60) != 0) return -1;
		}
		break;
	}
	case OBJTYPE_PADEFFECT: {
		const struct padeffectobj *effect = (const struct padeffectobj *)ptr;
		if (s_setupFieldS32(fields, record_id, kind,
				"pad_effect.effect", effect->effect) != 0) return -1;
		if (s_setupFieldPad(fields, record_id, kind,
				"pad_effect.pad", effect->pad) != 0) return -1;
		break;
	}
	case OBJTYPE_ESCASTEP: {
		const struct escalatorobj *step = (const struct escalatorobj *)ptr;
		if (s_setupFieldS32(fields, record_id, kind,
				"escalator_step.frame", step->frame) != 0) return -1;
		if (s_setupAppendCoordFields(fields, record_id, kind,
				"escalator_step.previous_position", &step->prevpos) != 0) return -1;
		break;
	}
	default:
		break;
	}

	return 0;
}

static s32 s_appendScenarioObjectiveJsonRow(pdscenario_textbuf_t *objectives_json,
                                            u32 index,
                                            const char *objective_id,
                                            const char *kind,
                                            const char *text_token,
                                            const char *difficulty_mask,
                                            const char *graph_node,
                                            const char *operand_kind,
                                            const char *target_ref,
                                            const char *target_record_ref,
                                            const char *pad_ref,
                                            const char *state_ref,
                                            const char *match_value,
                                            const char *initial_status)
{
	if (!objectives_json || !objective_id || !kind) return -1;
	if (s_textbufAppend(objectives_json, index ? ",\n    {\n" : "    {\n") != 0 ||
			s_textbufAppend(objectives_json, "      \"objective_id\": ") != 0 ||
			s_textbufAppendJsonString(objectives_json, objective_id) != 0 ||
			s_textbufAppend(objectives_json, ",\n      \"kind\": ") != 0 ||
			s_textbufAppendJsonString(objectives_json, kind) != 0 ||
			s_textbufAppend(objectives_json, ",\n      \"text_token\": ") != 0 ||
			s_textbufAppendJsonString(objectives_json, text_token) != 0 ||
			s_textbufAppend(objectives_json, ",\n      \"difficulty_mask\": ") != 0 ||
			s_textbufAppendJsonString(objectives_json, difficulty_mask) != 0 ||
			s_textbufAppend(objectives_json, ",\n      \"graph_node\": ") != 0 ||
			s_textbufAppendJsonString(objectives_json, graph_node) != 0 ||
			s_textbufAppend(objectives_json, ",\n      \"operand_kind\": ") != 0 ||
			s_textbufAppendJsonString(objectives_json, operand_kind) != 0 ||
			s_textbufAppend(objectives_json, ",\n      \"target_ref\": ") != 0 ||
			s_textbufAppendJsonString(objectives_json, target_ref) != 0 ||
			s_textbufAppend(objectives_json, ",\n      \"target_record_ref\": ") != 0 ||
			s_textbufAppendJsonString(objectives_json, target_record_ref) != 0 ||
			s_textbufAppend(objectives_json, ",\n      \"pad_ref\": ") != 0 ||
			s_textbufAppendJsonString(objectives_json, pad_ref) != 0 ||
			s_textbufAppend(objectives_json, ",\n      \"state_ref\": ") != 0 ||
			s_textbufAppendJsonString(objectives_json, state_ref) != 0 ||
			s_textbufAppend(objectives_json, ",\n      \"match_value\": ") != 0 ||
			s_textbufAppendJsonString(objectives_json, match_value) != 0 ||
			s_textbufAppend(objectives_json, ",\n      \"initial_status\": ") != 0 ||
			s_textbufAppendJsonString(objectives_json, initial_status) != 0 ||
			s_textbufAppend(objectives_json, "\n    }") != 0) {
		return -1;
	}
	return 0;
}

static s32 s_appendScenarioObjectJsonRow(pdscenario_textbuf_t *objects_json,
                                         u32 index,
                                         const char *record_id,
                                         const char *kind,
                                         const char *pad_ref,
                                         const char *model_catalog_id,
                                         const char *weapon_catalog_id,
                                         const char *secondary_weapon_catalog_id,
                                         const char *body_catalog_id,
                                         const char *head_catalog_id,
                                         const char *ailist_ref,
                                         const char *flags,
                                         const char *flags2,
                                         const char *flags3)
{
	if (!objects_json || !record_id || !kind) return -1;
	if (s_textbufAppend(objects_json, index ? ",\n    {\n" : "    {\n") != 0 ||
			s_textbufAppend(objects_json, "      \"record_id\": ") != 0 ||
			s_textbufAppendJsonString(objects_json, record_id) != 0 ||
			s_textbufAppend(objects_json, ",\n      \"kind\": ") != 0 ||
			s_textbufAppendJsonString(objects_json, kind) != 0 ||
			s_textbufAppend(objects_json, ",\n      \"pad_ref\": ") != 0 ||
			s_textbufAppendJsonString(objects_json, pad_ref) != 0 ||
			s_textbufAppend(objects_json, ",\n      \"model_catalog_id\": ") != 0 ||
			s_textbufAppendJsonString(objects_json, model_catalog_id) != 0 ||
			s_textbufAppend(objects_json, ",\n      \"weapon_catalog_id\": ") != 0 ||
			s_textbufAppendJsonString(objects_json, weapon_catalog_id) != 0 ||
			s_textbufAppend(objects_json, ",\n      \"secondary_weapon_catalog_id\": ") != 0 ||
			s_textbufAppendJsonString(objects_json, secondary_weapon_catalog_id) != 0 ||
			s_textbufAppend(objects_json, ",\n      \"body_catalog_id\": ") != 0 ||
			s_textbufAppendJsonString(objects_json, body_catalog_id) != 0 ||
			s_textbufAppend(objects_json, ",\n      \"head_catalog_id\": ") != 0 ||
			s_textbufAppendJsonString(objects_json, head_catalog_id) != 0 ||
			s_textbufAppend(objects_json, ",\n      \"ailist_ref\": ") != 0 ||
			s_textbufAppendJsonString(objects_json, ailist_ref) != 0 ||
			s_textbufAppend(objects_json, ",\n      \"flags\": ") != 0 ||
			s_textbufAppendJsonString(objects_json, flags) != 0 ||
			s_textbufAppend(objects_json, ",\n      \"flags2\": ") != 0 ||
			s_textbufAppendJsonString(objects_json, flags2) != 0 ||
			s_textbufAppend(objects_json, ",\n      \"flags3\": ") != 0 ||
			s_textbufAppendJsonString(objects_json, flags3) != 0 ||
			s_textbufAppend(objects_json, "\n    }") != 0) {
		return -1;
	}
	return 0;
}

static s32 s_buildSetupTables(const u8 *data, u32 size,
                              pdscenario_textbuf_t *objects_json,
                              pdscenario_textbuf_t *objectives_json,
                              pdscenario_textbuf_t *setup_fields_json,
                              u32 *out_objects, u32 *out_objectives)
{
	if (out_objects) *out_objects = 0;
	if (out_objectives) *out_objectives = 0;
	if (!data || size < sizeof(struct stagesetup) || !objects_json ||
			!objectives_json || !setup_fields_json) {
		return -1;
	}

	const struct stagesetup *setup = (const struct stagesetup *)data;
	uintptr_t props_ofs = (uintptr_t)setup->props;
	if (props_ofs >= size) return -1;

	const u8 *end = data + size;
	const u8 *scan = data + props_ofs;
	u32 record_count = 0;
	while (scan + sizeof(u32) <= end && record_count < 16384u) {
		u8 type = s_setupCommandType(scan);
		s32 len = s_setupCommandLengthBytes(scan);
		if (len <= 0 || scan + len > end) return -1;
		if (type == OBJTYPE_END) break;
		record_count++;
		scan += len;
	}
	if (record_count >= 16384u) return -1;

	pdscenario_tag_target_t *tag_targets =
		(pdscenario_tag_target_t *)calloc(record_count ? record_count : 1,
			sizeof(*tag_targets));
	u32 tag_target_count = 0;
	if (!tag_targets) return -1;

	scan = data + props_ofs;
	for (u32 i = 0; i < record_count && scan + sizeof(u32) <= end; i++) {
		u8 type = s_setupCommandType(scan);
		s32 len = s_setupCommandLengthBytes(scan);
		if (len <= 0 || scan + len > end) {
			free(tag_targets);
			return -1;
		}
		if (type == OBJTYPE_TAG && len >= (s32)sizeof(struct tag)) {
			const struct tag *tag = (const struct tag *)scan;
			s32 target = (s32)i + tag->cmdoffset;
			if (target >= 0 && (u32)target < record_count) {
				tag_targets[tag_target_count].tag_id = tag->tagnum;
				tag_targets[tag_target_count].target_index = target;
				tag_target_count++;
			}
		}
		scan += len;
	}

	if (s_textbufAppend(objects_json,
			"{\n"
			"  \"schema\": \"pd2.scenario.objects.v1\",\n"
			"  \"rows\": [\n") != 0 ||
	    s_textbufAppend(setup_fields_json,
			"{\n"
			"  \"schema\": \"pd2.scenario.setup.fields.v1\",\n"
			"  \"rows\": [\n") != 0 ||
	    s_textbufAppend(objectives_json,
			"{\n"
			"  \"schema\": \"pd2.scenario.objectives.v1\",\n"
			"  \"rows\": [\n") != 0) {
		free(tag_targets);
		return -1;
	}

	const u8 *ptr = data + props_ofs;
	u32 object_count = 0;
	u32 objective_count = 0;
	while (ptr + sizeof(u32) <= end && object_count < 16384u) {
		u8 type = s_setupCommandType(ptr);
		s32 len = s_setupCommandLengthBytes(ptr);
		if (len <= 0 || ptr + len > end) {
			free(tag_targets);
			return -1;
		}
		if (type == OBJTYPE_END) break;

		char record_id[32];
		snprintf(record_id, sizeof(record_id), "setup_%04u", (unsigned)object_count);

		if (type == OBJTYPE_CHR) {
			const struct packedchr *chr = (const struct packedchr *)ptr;
			char pad_ref[32];
			char ailist_ref[32];
			char flags[16];
			char flags2[16];
			s_padRef((s32)chr->padnum, pad_ref, sizeof(pad_ref));
			snprintf(ailist_ref, sizeof(ailist_ref), "ailist_%04u",
				(unsigned)chr->ailistnum);
			snprintf(flags, sizeof(flags), "0x%08x", (unsigned)chr->flags);
			snprintf(flags2, sizeof(flags2), "0x%08x", (unsigned)chr->flags2);
			if (s_appendScenarioObjectJsonRow(objects_json, object_count,
					record_id, s_objTypeName(type), pad_ref,
					"", "", "",
					s_nonnullCatalogId(catalogBodyIdByBodynum(chr->bodynum)),
					s_nonnullCatalogId(catalogHeadIdByHeadnum(chr->headnum)),
					ailist_ref, flags, flags2, "") != 0) {
				free(tag_targets);
				return -1;
			}
		} else if (s_objTypeHasDefaultBase(type)) {
			const struct defaultobj *obj = (const struct defaultobj *)ptr;
			char pad_ref[32];
			const char *weapon_ref = "";
			const char *dual_weapon_ref = "";
			char flags[16];
			char flags2[16];
			char flags3[16];
			s_padRef((s32)obj->pad, pad_ref, sizeof(pad_ref));
			if ((type == OBJTYPE_WEAPON || type == OBJTYPE_MINE) &&
					len >= (s32)sizeof(struct weaponobj)) {
				const struct weaponobj *w = (const struct weaponobj *)ptr;
				weapon_ref = s_nonnullCatalogId(
					catalogWeaponIdByRuntimeWeaponNum(w->weaponnum));
				dual_weapon_ref = s_nonnullCatalogId(
					catalogWeaponIdByRuntimeWeaponNum(w->dualweaponnum));
			}
			snprintf(flags, sizeof(flags), "0x%08x", (unsigned)obj->flags);
			snprintf(flags2, sizeof(flags2), "0x%08x", (unsigned)obj->flags2);
			snprintf(flags3, sizeof(flags3), "0x%08x", (unsigned)obj->flags3);
			if (s_appendScenarioObjectJsonRow(objects_json, object_count,
					record_id, s_objTypeName(type), pad_ref,
					s_nonnullCatalogId(catalogModelIdByModelnum(obj->modelnum)),
					weapon_ref, dual_weapon_ref, "", "", "",
					flags, flags2, flags3) != 0) {
				free(tag_targets);
				return -1;
			}
		} else {
			if (s_appendScenarioObjectJsonRow(objects_json, object_count,
					record_id, s_objTypeName(type), "", "", "", "",
					"", "", "", "", "", "") != 0) {
				free(tag_targets);
				return -1;
			}
		}

		if (s_setupAppendCommandFields(setup_fields_json, record_id,
				s_objTypeName(type), ptr, type, object_count,
				record_count, tag_targets, tag_target_count) != 0) {
			free(tag_targets);
			return -1;
		}

		if (type == OBJTYPE_BEGINOBJECTIVE) {
			const struct objective *obj = (const struct objective *)ptr;
			char objective_id[32];
			char text_token[32];
			char difficulty_mask[16];
			char graph_node[48];
			snprintf(objective_id, sizeof(objective_id), "objective_%04u",
				(unsigned)objective_count);
			snprintf(text_token, sizeof(text_token), "objective_text_%04d",
				obj->index);
			snprintf(difficulty_mask, sizeof(difficulty_mask), "0x%02x",
				(unsigned)obj->difficulties);
			snprintf(graph_node, sizeof(graph_node), "level.objective.%04u",
				(unsigned)objective_count);
			if (s_appendScenarioObjectiveJsonRow(objectives_json,
					objective_count, objective_id, "objective",
					text_token, difficulty_mask, graph_node,
					"objective", "", "", "", "", "", "") != 0) {
				free(tag_targets);
				return -1;
			}
			objective_count++;
		} else if (type >= OBJECTIVETYPE_DESTROYOBJ &&
				type <= OBJECTIVETYPE_THROWINROOM) {
			char target_ref[32] = "";
			char target_record_ref[32] = "";
			char pad_ref[32] = "";
			char state_ref[64] = "";
			const char *operand_kind = "none";
			s32 match_value = 0;
			s32 initial_status = -1;
			if (type == OBJECTIVETYPE_DESTROYOBJ ||
					type == OBJECTIVETYPE_COLLECTOBJ ||
					type == OBJECTIVETYPE_THROWOBJ) {
				u32 tag_id = PD_BE32(((const u32 *)ptr)[1]);
				s_tagRef(tag_id, target_ref, sizeof(target_ref));
				s_tagTargetRecordRef(tag_targets, tag_target_count,
					tag_id, target_record_ref,
					sizeof(target_record_ref));
				operand_kind = "tag_target";
			} else if (type == OBJECTIVETYPE_COMPFLAGS ||
					type == OBJECTIVETYPE_FAILFLAGS) {
				u32 flag_mask = PD_BE32(((const u32 *)ptr)[1]);
				s_stageFlagRef(flag_mask, state_ref,
					sizeof(state_ref));
				operand_kind = "stage_flag";
			} else if (type == OBJECTIVETYPE_HOLOGRAPH) {
				const struct criteria_holograph *criteria =
					(const struct criteria_holograph *)ptr;
				s_tagRef(criteria->obj, target_ref,
					sizeof(target_ref));
				s_tagTargetRecordRef(tag_targets, tag_target_count,
					criteria->obj, target_record_ref,
					sizeof(target_record_ref));
				initial_status = (s32)criteria->status;
				operand_kind = "tag_target_status";
			} else if (type == OBJECTIVETYPE_ENTERROOM) {
				const struct criteria_roomentered *criteria =
					(const struct criteria_roomentered *)ptr;
				s_padRef((s32)criteria->pad, pad_ref,
					sizeof(pad_ref));
				initial_status = (s32)criteria->status;
				operand_kind = "pad_status";
			} else if (type == OBJECTIVETYPE_THROWINROOM) {
				const struct criteria_throwinroom *criteria =
					(const struct criteria_throwinroom *)ptr;
				s_padRef((s32)criteria->pad, pad_ref,
					sizeof(pad_ref));
				match_value = (s32)criteria->unk04;
				initial_status = (s32)criteria->status;
				operand_kind = "throw_match_pad_status";
			}
			char objective_id[32];
			char graph_node[48];
			char match_value_text[32];
			char initial_status_text[32];
			snprintf(objective_id, sizeof(objective_id),
				"objective_step_%04u", (unsigned)objective_count);
			snprintf(graph_node, sizeof(graph_node),
				"level.objective_step.%04u", (unsigned)objective_count);
			snprintf(match_value_text, sizeof(match_value_text), "%d",
				match_value);
			snprintf(initial_status_text, sizeof(initial_status_text), "%d",
				initial_status);
			if (s_appendScenarioObjectiveJsonRow(objectives_json,
					objective_count, objective_id, s_objTypeName(type),
					"", "", graph_node, operand_kind, target_ref,
					target_record_ref, pad_ref, state_ref,
					match_value_text, initial_status_text) != 0) {
				free(tag_targets);
				return -1;
			}
			objective_count++;
		}

		object_count++;
		ptr += len;
	}

	if (out_objects) *out_objects = object_count;
	if (out_objectives) *out_objectives = objective_count;
	free(tag_targets);
	if (s_textbufAppend(objects_json, "\n  ]\n}\n") != 0 ||
			s_textbufAppend(setup_fields_json, "\n  ]\n}\n") != 0 ||
			s_textbufAppend(objectives_json, "\n  ]\n}\n") != 0) {
		return -1;
	}
	return 0;
}

static s32 s_introCommandWordCount(s32 type)
{
	static const u8 sizes[] = {
		3,  /* INTROCMD_SPAWN */
		4,  /* INTROCMD_WEAPON */
		4,  /* INTROCMD_AMMO */
		8,  /* INTROCMD_3 */
		2,  /* INTROCMD_4 */
		2,  /* INTROCMD_OUTFIT */
		10, /* INTROCMD_6 */
		3,  /* INTROCMD_WATCHTIME */
		2,  /* INTROCMD_CREDITOFFSET */
		3,  /* INTROCMD_CASE */
		3,  /* INTROCMD_CASERESPAWN */
		2,  /* INTROCMD_HILL */
		1,  /* INTROCMD_END */
	};

	if (type < 0 || type > INTROCMD_END) {
		return 0;
	}

	return sizes[type];
}

static s32 s_buildIntroSpawnsTable(const u8 *data, u32 size,
	pdscenario_textbuf_t *spawns_json, u32 *out_spawns)
{
	if (out_spawns) *out_spawns = 0;
	if (!data || size < sizeof(struct stagesetup) || !spawns_json) {
		return -1;
	}
	if (s_textbufAppend(spawns_json,
			"{\n"
			"  \"schema\": \"pd2.scenario.spawns.v1\",\n"
			"  \"rows\": [\n") != 0) {
		return -1;
	}

	const struct stagesetup *setup = (const struct stagesetup *)data;
	uintptr_t intro_ofs = (uintptr_t)setup->intro;
	if (!intro_ofs) {
		if (s_textbufAppend(spawns_json, "  ]\n}\n") != 0) {
			return -1;
		}
		return 0;
	}
	if (intro_ofs >= size || size - (u32)intro_ofs < sizeof(s32)) {
		return -1;
	}

	const u8 *cursor = data + intro_ofs;
	const u8 *end = data + size;
	u32 spawn_count = 0;
	u32 safety = 0;
	while (cursor + sizeof(s32) <= end && safety++ < 10000u) {
		const s32 *cmd = (const s32 *)cursor;
		s32 type = cmd[0];
		s32 words = s_introCommandWordCount(type);
		if (words <= 0 || cursor + (size_t)words * sizeof(s32) > end) {
			return -1;
		}
		if (type == INTROCMD_END) {
			break;
		}
		if (type == INTROCMD_SPAWN) {
			char pad_ref[32];
			const char *team = cmd[2] == 0 ? "any" : "";
			char team_buf[32];
			s_padRef(cmd[1], pad_ref, sizeof(pad_ref));
			if (cmd[2] != 0) {
				snprintf(team_buf, sizeof(team_buf), "team_%d", cmd[2]);
				team = team_buf;
			}
			if (spawn_count > 0 &&
					s_textbufAppend(spawns_json, ",\n") != 0) {
				return -1;
			}
			if (s_textbufAppend(spawns_json,
					"    { \"spawn_id\": ") != 0 ||
			    s_textbufAppendf(spawns_json, "\"spawn_%04u\", \"pad_ref\": ",
					(unsigned)spawn_count) != 0 ||
			    s_textbufAppendJsonString(spawns_json, pad_ref) != 0 ||
			    s_textbufAppend(spawns_json,
					", \"room_ref\": \"\", \"team\": ") != 0 ||
			    s_textbufAppendJsonString(spawns_json, team) != 0 ||
			    s_textbufAppend(spawns_json,
					", \"profile\": \"intro\", \"position\": null, \"look\": null }") != 0) {
				return -1;
			}
			spawn_count++;
		}
		cursor += (size_t)words * sizeof(s32);
	}
	if (safety >= 10000u) {
		return -1;
	}

	if (out_spawns) *out_spawns = spawn_count;
	if (s_textbufAppend(spawns_json, "\n  ]\n}\n") != 0) {
		return -1;
	}
	return 0;
}

static const char *s_aiOpcodeName(u16 opcode)
{
	switch (opcode) {
	case CMD_LABEL:              return "label";
	case AICMD_END:              return "end";
	case AICMD_DROPITEM:         return "drop_item";
	case CMD_PRINT:              return "print";
	case 0x0091:                 return "noop_0091";
	case 0x00d8:                 return "noop_00d8";
	case 0x00d9:                 return "noop_00d9";
	case 0x00db:                 return "noop_00db";
	case 0x0100:                 return "noop_0100";
	case 0x0101:                 return "noop_0101";
	case 0x010d:                 return "noop_010d";
	case 0x016c:                 return "noop_016c";
	case 0x01bb:                 return "noop_01bb";
	case AICMD_SPAWNCHRATPAD:    return "spawn_chr_at_pad";
	case AICMD_SPAWNCHRATCHR:    return "spawn_chr_at_chr";
	case AICMD_EQUIPWEAPON:      return "try_equip_weapon";
	case AICMD_EQUIPHAT:         return "try_equip_hat";
	case 0x0073:                 return "if_objective_complete";
	case 0x0074:                 return "if_objective_failed";
	case 0x0077:                 return "if_difficulty_less_than";
	case 0x0078:                 return "if_difficulty_greater_than";
	case 0x0079:                 return "if_stage_timer_less_than";
	case 0x007a:                 return "if_stage_timer_greater_than";
	case 0x007b:                 return "if_stage_id_less_than";
	case 0x007c:                 return "if_stage_id_greater_than";
	case 0x00ea:                 return "if_num_players_less_than";
	case 0x00f7:                 return "if_all_objectives_complete";
	case 0x00f9:                 return "play_x_track";
	case 0x00fa:                 return "stop_x_track";
	case 0x00fc:                 return "if_kill_count_greater_than";
	case 0x01ab:                 return "if_num_knocked_out_chrs";
	case 0x00cd:                 return "speak";
	case 0x00ce:                 return "play_sound";
	case 0x017c:                 return "assign_sound";
	case 0x00d3:                 return "audio_mute_channel";
	case 0x0138:                 return "if_channel_free";
	case 0x00d1:                 return "set_object_sound_volume";
	case 0x00d2:                 return "set_object_sound_volume_by_distance";
	case 0x00cf:                 return "set_object_sound_playing";
	case 0x016b:                 return "play_repeating_sound_from_object";
	case 0x0179:                 return "play_sound_from_entity";
	case 0x00d0:                 return "play_repeating_sound_from_pad";
	case 0x00d4:                 return "if_object_sound_volume_less_than";
	case 0x015b:                 return "play_track_isolated";
	case 0x015c:                 return "play_default_tracks";
	case 0x017d:                 return "play_cutscene_track";
	case 0x017e:                 return "stop_cutscene_track";
	case 0x017f:                 return "play_temporary_track";
	case 0x0180:                 return "stop_ambient_track";
	case 0x00dc:                 return "end_level";
	case 0x00dd:                 return "end_cutscene";
	case 0x00de:                 return "warp_jo_to_pad";
	case 0x00df:                 return "warp_jo_to_tag";
	case 0x00e0:                 return "revoke_control";
	case 0x00e1:                 return "grant_control";
	case 0x00e3:                 return "player_fade_in";
	case 0x00e4:                 return "players_fade_out";
	case 0x00e5:                 return "if_colour_fade_complete";
	case 0x00f4:                 return "prepare_warp_orbit";
	case 0x00f5:                 return "begin_warp_latch";
	case 0x00f6:                 return "if_warp_latch_complete";
	case 0x0111:                 return "set_camera_animation";
	case 0x0113:                 return "if_in_cutscene";
	case 0x0174:                 return "if_cutscene_button_pressed";
	case 0x0175:                 return "reorient_for_cutscene_stop";
	case 0x00da:                 return "set_obj_image";
	case 0x0112:                 return "object_do_animation";
	case 0x00e8:                 return "set_door_open";
	case 0x00ca:                 return "duplicate_chr";
	case 0x0114:                 return "enable_chr";
	case 0x0115:                 return "disable_chr";
	case 0x0116:                 return "enable_obj";
	case 0x0117:                 return "disable_obj";
	case 0x00e2:                 return "chr_move_to_pad";
	case 0x00eb:                 return "if_chr_ammo_quantity_less_than";
	case 0x01a7:                 return "if_chr_not_talking";
	case 0x0103:                 return "if_prop_preset_blocking_sight_to_target";
	case 0x0104:                 return "remove_object_at_prop_preset";
	case 0x0105:                 return "if_prop_preset_height_less_than";
	case 0x0106:                 return "set_target";
	case 0x0107:                 return "if_presets_target_is_not_my_target";
	case 0x0108:                 return "if_chr_target";
	case 0x0109:                 return "set_chr_preset_to_chr_near_self";
	case 0x010a:                 return "set_chr_preset_to_chr_near_pad";
	case 0x010b:                 return "chr_set_team";
	case 0x010c:                 return "if_compare_chr_presets_team";
	case 0x011e:                 return "if_human";
	case 0x011f:                 return "if_skedar";
	case 0x0134:                 return "if_orders";
	case 0x0135:                 return "if_has_orders";
	case 0x0137:                 return "if_chr_in_squadron_doing_action";
	case 0x013d:                 return "if_dangerous_object_nearby";
	case 0x013f:                 return "if_heli_weapons_armed";
	case 0x0140:                 return "if_hoverbot_next_step";
	case 0x0141:                 return "shuffle_investigation_terminals";
	case 0x0142:                 return "set_pad_preset_to_investigation_terminal";
	case 0x0143:                 return "heli_arm_weapons";
	case 0x0144:                 return "heli_unarm_weapons";
	case 0x0149:                 return "if_chr_listening";
	case 0x014b:                 return "if_not_listening";
	case 0x0165:                 return "if_chr_injured_target";
	case 0x0166:                 return "if_action";
	case 0x016e:                 return "damage_chr_by_amount";
	case 0x01a3:                 return "do_preset_animation";
	case 0x01aa:                 return "if_player_chr_portal_distance_less_than";
	case 0x01b4:                 return "if_chr_reposition_valid";
	case 0x00ec:                 return "chr_draw_weapon";
	case 0x00ed:                 return "chr_draw_weapon_in_cutscene";
	case 0x00ee:                 return "set_player_force_speed";
	case 0x00ef:                 return "if_obj_in_room";
	case 0x00f3:                 return "chr_set_invincible";
	case 0x00f8:                 return "if_player_is_invincible";
	case 0x00e9:                 return "chr_delete_weapon";
	case 0x00fd:                 return "if_trigger_shot_list";
	case 0x012f:                 return "release_cover";
	case 0x016f:                 return "if_chr_has_no_gun";
	case 0x0170:                 return "do_gun_command";
	case 0x0171:                 return "if_distance_to_gun_less_than";
	case 0x0172:                 return "recover_gun";
	case 0x0184:                 return "try_attack_amount";
	case 0x01ad:                 return "release_object";
	case 0x01ae:                 return "clear_inventory";
	case 0x01af:                 return "chr_grab_object";
	case 0x01b3:                 return "toggle_p1p2";
	case 0x01b5:                 return "chr_set_p1p2";
	case 0x01b7:                 return "chr_set_cloaked";
	case 0x01b8:                 return "set_autogun_target_team";
	case 0x01bc:                 return "if_pouncebits_eq";
	case 0x01bd:                 return "if_training_pc_holographed";
	case 0x01be:                 return "if_player_using_device";
	case 0x01bf:                 return "chr_begin_or_end_teleport";
	case 0x01c0:                 return "if_chr_teleport_full_white";
	case 0x01ca:                 return "chr_set_cutscene_weapon";
	case 0x01cb:                 return "fade_screen";
	case 0x01cc:                 return "if_fade_complete";
	case 0x01cd:                 return "set_chr_hudpiece_visible";
	case 0x01ce:                 return "set_passive_mode";
	case 0x01cf:                 return "chr_set_firing_in_cutscene";
	case 0x01d0:                 return "set_portal_flag";
	case 0x01db:                 return "chr_kill";
	case 0x01dc:                 return "remove_weapon_from_inventory";
	case 0x01dd:                 return "if_music_event_queue_is_empty";
	case 0x01de:                 return "if_coop_mode";
	case 0x01df:                 return "if_chr_same_floor_distance_to_pad_less_than";
	case 0x01e0:                 return "remove_references_to_chr";
	case 0x0000:                 return "go_to_next";
	case 0x0001:                 return "go_to_first";
	case 0x0003:                 return "yield";
	case 0x0005:                 return "set_list";
	case 0x0006:                 return "set_return_list";
	case 0x0007:                 return "set_shot_list";
	case 0x0008:                 return "return";
	case 0x0009:                 return "stop";
	case 0x000a:                 return "kneel";
	case 0x000b:                 return "chr_do_animation";
	case 0x000c:                 return "if_idle";
	case 0x000d:                 return "be_surprised_one_hand";
	case 0x000e:                 return "be_surprised_look_around";
	case 0x000f:                 return "try_sidestep";
	case 0x0010:                 return "try_jump_out";
	case 0x0011:                 return "try_run_sideways";
	case 0x0012:                 return "try_attack_walk";
	case 0x0013:                 return "try_attack_run";
	case 0x0014:                 return "try_attack_roll";
	case 0x0015:                 return "try_attack_stand";
	case 0x0016:                 return "try_attack_kneel";
	case 0x0017:                 return "try_modify_attack";
	case 0x0018:                 return "face_entity";
	case 0x0019:                 return "ai_0019";
	case 0x001a:                 return "chr_damage_chr";
	case 0x001b:                 return "consider_grenade_throw";
	case 0x001d:                 return "jog_to_pad";
	case 0x001e:                 return "go_to_pad_preset";
	case 0x001f:                 return "walk_to_pad";
	case 0x0020:                 return "run_to_pad";
	case 0x0021:                 return "set_path";
	case 0x0022:                 return "start_patrol";
	case 0x0023:                 return "if_patrolling";
	case 0x0024:                 return "surrender";
	case 0x0025:                 return "fade_out";
	case 0x0026:                 return "remove_chr";
	case 0x0027:                 return "try_start_alarm";
	case 0x0028:                 return "activate_alarm";
	case 0x0029:                 return "deactivate_alarm";
	case 0x002a:                 return "try_run_from_target";
	case 0x002b:                 return "try_jog_to_target_prop";
	case 0x002c:                 return "try_walk_to_target_prop";
	case 0x002d:                 return "try_run_to_target_prop";
	case 0x002e:                 return "try_go_to_cover_prop";
	case 0x002f:                 return "try_jog_to_chr";
	case 0x0030:                 return "try_walk_to_chr";
	case 0x0031:                 return "try_run_to_chr";
	case 0x0032:                 return "if_stopped";
	case 0x0033:                 return "if_chr_dead";
	case 0x0034:                 return "if_chr_death_animation_finished";
	case 0x0035:                 return "if_can_see_target";
	case 0x0036:                 return "random";
	case 0x0037:                 return "if_random_less_than";
	case 0x0038:                 return "if_random_greater_than";
	case 0x0039:                 return "if_can_hear_alarm";
	case 0x003a:                 return "if_alarm_active";
	case 0x003b:                 return "if_gas_active";
	case 0x003c:                 return "if_hears_target";
	case 0x003d:                 return "if_saw_injury";
	case 0x003e:                 return "if_saw_death";
	case 0x003f:                 return "if_los_to_target";
	case 0x0040:                 return "if_target_nearly_in_sight";
	case 0x0041:                 return "if_nearly_in_targets_sight";
	case 0x0042:                 return "set_pad_preset_to_pad_on_route_to_target";
	case 0x0043:                 return "if_saw_target_recently";
	case 0x0044:                 return "if_heard_target_recently";
	case 0x0045:                 return "if_los_to_chr";
	case 0x0046:                 return "if_never_been_on_screen";
	case 0x0047:                 return "if_on_screen";
	case 0x0048:                 return "if_chr_in_on_screen_room";
	case 0x0049:                 return "if_room_is_on_screen";
	case 0x004a:                 return "if_target_aiming_at_me";
	case 0x004b:                 return "if_near_miss";
	case 0x004c:                 return "if_sees_suspicious_item";
	case 0x004d:                 return "if_target_in_fov_left";
	case 0x004e:                 return "if_check_fov_with_target";
	case 0x004f:                 return "if_target_out_of_fov_left";
	case 0x0050:                 return "if_target_in_fov";
	case 0x0051:                 return "if_target_out_of_fov";
	case 0x0052:                 return "if_distance_to_target_less_than";
	case 0x0053:                 return "if_distance_to_target_greater_than";
	case 0x0054:                 return "if_chr_distance_to_pad_less_than";
	case 0x0055:                 return "if_chr_distance_to_pad_greater_than";
	case 0x0056:                 return "if_distance_to_chr_less_than";
	case 0x0057:                 return "if_distance_to_chr_greater_than";
	case 0x0058:                 return "ai_0058";
	case 0x0059:                 return "if_distance_from_target_to_pad_less_than";
	case 0x005a:                 return "if_distance_from_target_to_pad_greater_than";
	case 0x005b:                 return "if_chr_in_room";
	case 0x005c:                 return "if_target_in_room";
	case 0x005d:                 return "if_chr_has_object";
	case 0x005e:                 return "if_weapon_thrown";
	case 0x005f:                 return "if_weapon_thrown_on_object";
	case 0x0060:                 return "if_chr_has_weapon_equipped";
	case 0x0061:                 return "if_gun_unclaimed";
	case 0x0062:                 return "if_object_healthy";
	case 0x0063:                 return "if_chr_activated_object";
	case 0x0065:                 return "obj_interact";
	case 0x0066:                 return "destroy_object";
	case 0x0067:                 return "ai_0067";
	case 0x0068:                 return "chr_drop_items";
	case 0x0069:                 return "chr_drop_weapon";
	case 0x006a:                 return "give_object_to_chr";
	case 0x006b:                 return "object_move_to_pad";
	case 0x006c:                 return "open_door";
	case 0x006d:                 return "close_door";
	case 0x006e:                 return "if_door_state";
	case 0x006f:                 return "if_object_is_door";
	case 0x0070:                 return "lock_door";
	case 0x0071:                 return "unlock_door";
	case 0x0072:                 return "if_door_locked";
	case 0x0075:                 return "ai_0075";
	case 0x0076:                 return "set_pad_preset_to_target_quadrant";
	case 0x007d:                 return "if_num_arghs_less_than";
	case 0x007e:                 return "if_num_arghs_greater_than";
	case 0x007f:                 return "if_num_close_arghs_less_than";
	case 0x0080:                 return "if_num_close_arghs_greater_than";
	case 0x0081:                 return "if_chr_health_greater_than";
	case 0x0082:                 return "if_chr_health_less_than";
	case 0x0083:                 return "if_injured";
	case 0x0084:                 return "set_morale";
	case 0x0085:                 return "add_morale";
	case 0x0086:                 return "chr_add_morale";
	case 0x0087:                 return "subtract_morale";
	case 0x0088:                 return "if_morale_less_than";
	case 0x0089:                 return "if_morale_less_than_random";
	case 0x008a:                 return "set_alertness";
	case 0x008b:                 return "add_alertness";
	case 0x008c:                 return "chr_add_alertness";
	case 0x008d:                 return "subtract_alertness";
	case 0x008e:                 return "if_alertness";
	case 0x008f:                 return "if_chr_alertness_less_than";
	case 0x0090:                 return "if_alertness_less_than_random";
	case 0x0092:                 return "set_hear_distance";
	case 0x0093:                 return "set_view_distance";
	case 0x0094:                 return "set_grenade_probability";
	case 0x0095:                 return "set_chr_num";
	case 0x0096:                 return "set_max_damage";
	case 0x0097:                 return "add_health";
	case 0x0098:                 return "set_reaction_speed";
	case 0x0099:                 return "set_recovery_speed";
	case 0x009a:                 return "set_accuracy";
	case 0x009b:                 return "set_flag";
	case 0x009c:                 return "unset_flag";
	case 0x009d:                 return "if_has_flag";
	case 0x009e:                 return "chr_set_flag";
	case 0x009f:                 return "chr_unset_flag";
	case 0x00a0:                 return "if_chr_has_flag";
	case 0x00a1:                 return "set_stage_flag";
	case 0x00a2:                 return "unset_stage_flag";
	case 0x00a3:                 return "if_stage_flag_eq";
	case 0x00a4:                 return "set_chrflag";
	case 0x00a5:                 return "unset_chrflag";
	case 0x00a6:                 return "if_has_chrflag";
	case 0x00a7:                 return "chr_set_chrflag";
	case 0x00a8:                 return "chr_unset_chrflag";
	case 0x00a9:                 return "if_chr_has_chrflag";
	case 0x00aa:                 return "set_obj_flag";
	case 0x00ab:                 return "unset_obj_flag";
	case 0x00ac:                 return "if_obj_has_flag";
	case 0x00ad:                 return "set_obj_flag_2";
	case 0x00ae:                 return "unset_obj_flag_2";
	case 0x00af:                 return "if_obj_has_flag_2";
	case 0x00b0:                 return "set_chr_preset";
	case 0x00b1:                 return "set_chr_target";
	case 0x00b2:                 return "set_pad_preset";
	case 0x00b3:                 return "chr_set_pad_preset";
	case 0x00b4:                 return "chr_copy_pad_preset";
	case 0x00b6:                 return "restart_timer";
	case 0x00b7:                 return "reset_timer";
	case 0x00b8:                 return "pause_timer";
	case 0x00b9:                 return "resume_timer";
	case 0x00ba:                 return "if_timer_stopped";
	case 0x00bb:                 return "if_timer_greater_than_random";
	case 0x00bc:                 return "if_timer_less_than";
	case 0x00bd:                 return "if_timer_greater_than";
	case 0x00be:                 return "show_countdown_timer";
	case 0x00bf:                 return "hide_countdown_timer";
	case 0x00c0:                 return "set_countdown_timer_value";
	case 0x00c1:                 return "stop_countdown_timer";
	case 0x00c2:                 return "start_countdown_timer";
	case 0x00c3:                 return "if_countdown_timer_stopped";
	case 0x00c4:                 return "if_countdown_timer_less_than";
	case 0x00c5:                 return "if_countdown_timer_greater_than";
	case 0x00cb:                 return "show_hudmsg";
	case 0x00cc:                 return "show_hudmsg_top_middle";
	case 0x00d5:                 return "hovercar_begin_path";
	case 0x00d6:                 return "set_vehicle_speed";
	case 0x00d7:                 return "set_rotor_speed";
	case 0x00f0:                 return "ai_00f0";
	case 0x00f1:                 return "if_attacking";
	case 0x00f2:                 return "switch_to_alt_sky";
	case 0x00fb:                 return "chr_explosions";
	case 0x00fe:                 return "kill_bond";
	case 0x00ff:                 return "be_surprised_surrender";
	case 0x0102:                 return "set_lights";
	case 0x010e:                 return "set_shield";
	case 0x010f:                 return "if_chr_shield_less_than";
	case 0x0110:                 return "if_chr_shield_greater_than";
	case 0x0118:                 return "set_obj_flag_3";
	case 0x0119:                 return "unset_obj_flag_3";
	case 0x011a:                 return "if_obj_has_flag_3";
	case 0x011b:                 return "chr_set_hidden_flag";
	case 0x011c:                 return "chr_unset_hidden_flag";
	case 0x011d:                 return "if_chr_has_hidden_flag";
	case 0x0120:                 return "if_safety_2_less_than";
	case 0x0121:                 return "find_cover";
	case 0x0122:                 return "find_cover_within_dist";
	case 0x0123:                 return "find_cover_outside_dist";
	case 0x0124:                 return "go_to_cover";
	case 0x0125:                 return "check_cover_out_of_sight";
	case 0x0126:                 return "if_player_using_cmp_or_ar_34";
	case 0x0127:                 return "detect_enemy_on_same_floor";
	case 0x0128:                 return "detect_enemy";
	case 0x0129:                 return "if_safety_less_than";
	case 0x012a:                 return "if_target_moving_slowly";
	case 0x012b:                 return "if_target_moving_closer";
	case 0x012c:                 return "if_target_moving_away";
	case 0x0130:                 return "say_quip";
	case 0x0131:                 return "increase_squadron_alertness";
	case 0x0132:                 return "set_action";
	case 0x0133:                 return "set_team_orders";
	case 0x0136:                 return "retreat";
	case 0x0139:                 return "ai_0139";
	case 0x013a:                 return "set_chr_preset_to_unalerted_teammate";
	case 0x013b:                 return "set_squadron";
	case 0x013c:                 return "face_cover";
	case 0x013e:                 return "ai_013e";
	case 0x0145:                 return "rebuild_teams";
	case 0x0146:                 return "rebuild_squadrons";
	case 0x0147:                 return "if_squadron_is_dead";
	case 0x0148:                 return "chr_set_listening";
	case 0x014a:                 return "if_true";
	case 0x0152:                 return "if_num_chrs_in_squadron_greater_than";
	case 0x0157:                 return "set_tinted_glass_enabled";
	case 0x0167:                 return "hovercopter_fire_rocket";
	case 0x0168:                 return "if_shield_damaged";
	case 0x0169:                 return "if_natural_anim";
	case 0x016a:                 return "if_y";
	case 0x016d:                 return "chr_adjust_motion_blur";
	case 0x0173:                 return "chr_copy_properties";
	case 0x0176:                 return "if_bot_respawning";
	case 0x0177:                 return "player_auto_walk";
	case 0x0178:                 return "if_player_auto_walk_finished";
	case 0x017a:                 return "if_los_to_attack_target";
	case 0x017b:                 return "if_chr_knocked_out";
	case 0x0181:                 return "if_player_looking_at_object";
	case 0x0182:                 return "punch_or_kick";
	case 0x0183:                 return "if_target_is_player";
	case 0x0185:                 return "mp_init_simulants";
	case 0x0186:                 return "if_sound_timer";
	case 0x0187:                 return "set_target_to_eyespy_if_in_sight";
	case 0x0188:                 return "if_lift_stationary";
	case 0x0189:                 return "lift_go_to_stop";
	case 0x018a:                 return "if_lift_at_stop";
	case 0x018b:                 return "configure_rain";
	case 0x018c:                 return "chr_toggle_model_part";
	case 0x018d:                 return "activate_lift";
	case 0x018e:                 return "mini_skedar_try_pounce";
	case 0x018f:                 return "if_object_distance_to_pad_less_than";
	case 0x0190:                 return "set_savefile_flag";
	case 0x0191:                 return "unset_savefile_flag";
	case 0x0192:                 return "if_savefile_flag_is_set";
	case 0x0193:                 return "if_savefile_flag_is_unset";
	case 0x019e:                 return "if_obj_health_less_than";
	case 0x019f:                 return "set_obj_health";
	case 0x01a0:                 return "set_chr_special_death_animation";
	case 0x01a1:                 return "set_room_to_search";
	case 0x01a2:                 return "say_ci_staff_quip";
	case 0x01a4:                 return "show_hudmsg_middle";
	case 0x01a5:                 return "if_using_lift";
	case 0x01a6:                 return "if_target_y_difference_less_than";
	case 0x01b1:                 return "shuffle_ruins_pillars";
	case 0x01b2:                 return "set_wind_speed";
	case 0x01b6:                 return "configure_snow";
	case 0x01b9:                 return "shuffle_pelagic_switches";
	case 0x01ba:                 return "try_attack_lie";
	case 0x01c1:                 return "set_punch_dodge_list";
	case 0x01c2:                 return "set_shooting_at_me_list";
	case 0x01c3:                 return "set_dark_room_list";
	case 0x01c4:                 return "set_player_dead_list";
	case 0x01c5:                 return "avoid";
	case 0x01c6:                 return "set_dodge_rating";
	case 0x01c7:                 return "set_unarmed_dodge_rating";
	case 0x01c8:                 return "title_init_mode";
	case 0x01c9:                 return "try_exit_title";
	case 0x01d1:                 return "obj_set_model_part_visible";
	case 0x01d2:                 return "chr_emit_sparks";
	case 0x01d3:                 return "set_dr_caroll_images";
	case 0x01d4:                 return "set_room_flag";
	case 0x01d5:                 return "show_cutscene_chrs";
	case 0x01d6:                 return "configure_environment";
	case 0x01d7:                 return "if_distance_to_target_2_less_than";
	case 0x01d8:                 return "if_distance_to_target_2_greater_than";
	case 0x01d9:                 return "play_sound_from_prop";
	case 0x01da:                 return "play_temporary_primary_track";
	default:                     return "command";
	}
}

static void s_aiOpcodeSemanticKind(u16 opcode, const char *opcode_name,
	char *out, u32 out_size)
{
	const char *mapped = NULL;
	if (!out || out_size == 0) {
		return;
	}
	if (!opcode_name) {
		opcode_name = "command";
	}

	switch (opcode) {
	case CMD_LABEL: mapped = "scenario.ai.control.label"; break;
	case AICMD_END: mapped = "scenario.ai.control.end"; break;
	case 0x0000: mapped = "scenario.ai.control.go_to_next"; break;
	case 0x0001: mapped = "scenario.ai.control.go_to_first"; break;
	case 0x0003: mapped = "scenario.ai.control.yield"; break;
	case 0x0008: mapped = "scenario.ai.control.return"; break;
	case 0x0019: mapped = "scenario.ai.action.apply_gset_damage"; break;
	case 0x0075: mapped = "scenario.ai.condition.if_waypoint_within_quadrant"; break;
	case 0x00ad:
	case 0x0118:
		mapped = "scenario.ai.action.set_obj_flag";
		break;
	case 0x00ae:
	case 0x0119:
		mapped = "scenario.ai.action.unset_obj_flag";
		break;
	case 0x00af:
	case 0x011a:
		mapped = "scenario.ai.action.if_obj_has_flag";
		break;
	case 0x00c0: mapped = "scenario.ai.action.set_countdown_timer"; break;
	case 0x0139: mapped = "scenario.ai.action.orbit_target"; break;
	case 0x016c: mapped = "scenario.ai.action.noop"; break;
	case 0x0192: mapped = "scenario.ai.action.if_savefile_flag_set"; break;
	case 0x0193: mapped = "scenario.ai.action.if_savefile_flag_unset"; break;
	default:
		break;
	}

	if (mapped) {
		snprintf(out, out_size, "%s", mapped);
	} else if (strncmp(opcode_name, "if_", 3) == 0 ||
			strncmp(opcode_name, "consider_", 9) == 0) {
		snprintf(out, out_size, "scenario.ai.condition.%s", opcode_name);
	} else if (strncmp(opcode_name, "noop_", 5) == 0) {
		snprintf(out, out_size, "scenario.ai.action.noop");
	} else {
		snprintf(out, out_size, "scenario.ai.action.%s", opcode_name);
	}
}

static s32 s_appendAiOperandsJsonArray(pdscenario_textbuf_t *out,
	const u8 *cmd, u32 len)
{
	if (s_textbufAppend(out, "[") != 0) {
		return -1;
	}
	for (u32 i = 2; i < len; i++) {
		if (s_textbufAppendf(out, "%s\"0x%02x\"",
				i > 2 ? ", " : "", (unsigned)cmd[i]) != 0) {
			return -1;
		}
	}
	if (s_textbufAppend(out, "]") != 0) {
		return -1;
	}
	return 0;
}

static s32 s_buildAiListsTable(const u8 *data, u32 size,
	pdscenario_textbuf_t *ai_lists_json,
	pdscenario_textbuf_t *ai_command_nodes_json,
	pdscenario_textbuf_t *ai_command_links_json,
	u32 *out_lists, u32 *out_commands)
{
	if (out_lists) *out_lists = 0;
	if (out_commands) *out_commands = 0;
	if (!data || size < sizeof(struct stagesetup) || !ai_lists_json ||
			!ai_command_nodes_json || !ai_command_links_json) {
		return -1;
	}

	if (s_textbufAppend(ai_lists_json,
			"{\n"
			"  \"schema\": \"pd2.scenario.ai.lists.v1\",\n"
			"  \"rows\": [\n") != 0) {
		return -1;
	}

	const struct stagesetup *setup = (const struct stagesetup *)data;
	uintptr_t table_ofs = (uintptr_t)setup->ailists;
	if (!table_ofs) {
		if (s_textbufAppend(ai_lists_json, "  ]\n}\n") != 0) {
			return -1;
		}
		return 0;
	}
	if (table_ofs >= size || size - (u32)table_ofs < sizeof(struct ailist)) {
		return -1;
	}

	const struct ailist *lists = (const struct ailist *)(data + table_ofs);
	u32 list_cap = (size - (u32)table_ofs) / (u32)sizeof(struct ailist);
	u32 list_count = 0;
	u32 command_count = 0;

	for (u32 i = 0; i < list_cap && i < 16384u; i++) {
		uintptr_t list_ofs = (uintptr_t)lists[i].list;
		if (!list_ofs) {
			break;
		}
		if (list_ofs >= size) {
			return -1;
		}

		const u8 *list = data + list_ofs;
		u32 max_len = size - (u32)list_ofs;
		u32 offset = 0;
		u32 command_index = 0;

		while (offset + 2 <= max_len && command_index < 65536u) {
			const u8 *cmd = list + offset;
			u16 opcode = (u16)(((u16)cmd[0] << 8) | cmd[1]);
			u32 len = chraiGetCommandLength((u8 *)list, offset);
			if (len < 2 || len > max_len - offset) {
				return -1;
			}

			const char *model_id = "";
			const char *weapon_id = "";
			const char *body_id = "";
			const char *head_id = "";
			const char *opcode_name = s_aiOpcodeName(opcode);
			char graph_node[64];
			char semantic_kind[128];
			snprintf(graph_node, sizeof(graph_node),
				"scenario.ai.ailist_%04u.command.%04u",
				(unsigned)i, (unsigned)command_index);
			s_aiOpcodeSemanticKind(opcode, opcode_name, semantic_kind,
				sizeof(semantic_kind));

			if ((opcode == AICMD_DROPITEM || opcode == AICMD_EQUIPHAT) && len >= 4) {
				s32 modelnum = ((s32)cmd[2] << 8) | cmd[3];
				model_id = s_nonnullCatalogId(catalogModelIdByModelnum(modelnum));
			} else if (opcode == AICMD_EQUIPWEAPON && len >= 5) {
				s32 modelnum = ((s32)cmd[2] << 8) | cmd[3];
				model_id = s_nonnullCatalogId(catalogModelIdByModelnum(modelnum));
				weapon_id = s_nonnullCatalogId(catalogWeaponIdByRuntimeWeaponNum(cmd[4]));
			} else if (opcode == 0x01dc && len >= 3) {
				weapon_id = s_nonnullCatalogId(
					catalogWeaponIdByRuntimeWeaponNum(cmd[2]));
			} else if ((opcode == AICMD_SPAWNCHRATPAD || opcode == AICMD_SPAWNCHRATCHR) && len >= 4) {
				body_id = s_nonnullCatalogId(catalogBodyIdByBodynum(cmd[2]));
				head_id = s_nonnullCatalogId(catalogHeadIdByHeadnum((s8)cmd[3]));
			}

			if (s_textbufAppendf(ai_lists_json,
					"%s    { \"ailist_ref\": \"ailist_%04u\", \"list_id\": \"0x%04x\", \"graph_node\": ",
					command_count ? ",\n" : "",
					(unsigned)i, (unsigned)(u16)lists[i].id) != 0 ||
					s_textbufAppendJsonString(ai_lists_json, graph_node) != 0 ||
					s_textbufAppendf(ai_lists_json,
					", \"command_index\": %u, \"offset\": %u, \"opcode\": \"0x%04x\", \"opcode_name\": ",
					(unsigned)command_index, (unsigned)offset,
					(unsigned)opcode) != 0 ||
					s_textbufAppendJsonString(ai_lists_json, opcode_name) != 0 ||
					s_textbufAppend(ai_lists_json,
						", \"operands\": ") != 0 ||
					s_appendAiOperandsJsonArray(ai_lists_json, cmd, len) != 0 ||
					s_textbufAppend(ai_lists_json,
						", \"model_catalog_id\": ") != 0 ||
					s_textbufAppendJsonString(ai_lists_json, model_id) != 0 ||
					s_textbufAppend(ai_lists_json,
						", \"weapon_catalog_id\": ") != 0 ||
					s_textbufAppendJsonString(ai_lists_json, weapon_id) != 0 ||
					s_textbufAppend(ai_lists_json,
						", \"body_catalog_id\": ") != 0 ||
					s_textbufAppendJsonString(ai_lists_json, body_id) != 0 ||
					s_textbufAppend(ai_lists_json,
						", \"head_catalog_id\": ") != 0 ||
					s_textbufAppendJsonString(ai_lists_json, head_id) != 0 ||
					s_textbufAppend(ai_lists_json, " }") != 0) {
				return -1;
			}

			if (s_textbufAppend(ai_command_nodes_json,
					",\n    { \"id\": ") != 0 ||
					s_textbufAppendJsonString(ai_command_nodes_json,
						graph_node) != 0 ||
					s_textbufAppend(ai_command_nodes_json,
						", \"kind\": \"scenario.ai.command\", \"source\": \"ai/ailists.json\", \"ailist_ref\": ") != 0 ||
					s_textbufAppendf(ai_command_nodes_json,
						"\"ailist_%04u\", \"list_id\": \"0x%04x\", \"command_index\": %u, \"offset\": %u, \"opcode\": \"0x%04x\", \"opcode_name\": ",
						(unsigned)i, (unsigned)(u16)lists[i].id,
						(unsigned)command_index, (unsigned)offset,
						(unsigned)opcode) != 0 ||
					s_textbufAppendJsonString(ai_command_nodes_json,
						opcode_name) != 0 ||
					s_textbufAppend(ai_command_nodes_json,
						", \"semantic_kind\": ") != 0 ||
					s_textbufAppendJsonString(ai_command_nodes_json,
						semantic_kind) != 0 ||
					s_textbufAppend(ai_command_nodes_json, " }") != 0 ||
					s_textbufAppend(ai_command_links_json,
						",\n    { \"from\": \"scenario.ai.lists\", \"to\": ") != 0 ||
					s_textbufAppendJsonString(ai_command_links_json,
						graph_node) != 0 ||
					s_textbufAppend(ai_command_links_json,
						" }") != 0) {
				return -1;
			}

			command_count++;
			offset += len;
			command_index++;
			if (opcode == AICMD_END) {
				break;
			}
		}
		list_count++;
	}

	if (out_lists) *out_lists = list_count;
	if (out_commands) *out_commands = command_count;
	if (s_textbufAppend(ai_lists_json, "\n  ]\n}\n") != 0) {
		return -1;
	}
	return 0;
}

static s32 s_buildScenarioSourceFiles(const char *scenario_id,
                                      const char *kind,
                                      u32 room_count, u32 tri_count,
                                      u32 portal_count,
                                      u32 pad_count, u32 object_count,
                                      u32 objective_count,
                                      u32 ai_list_count,
                                      u32 ai_command_count,
                                      u32 waypoint_count,
                                      u32 waygroup_count,
                                      u32 cover_count,
                                      u32 path_count,
                                      const u8 *scene_glb,
                                      u32 scene_glb_size,
                                      const pdscenario_textbuf_t *collision_obj,
                                      const pdscenario_textbuf_t *portals_json,
                                      const pdscenario_textbuf_t *pads_json,
                                      const pdscenario_textbuf_t *spawns_json,
                                      const pdscenario_textbuf_t *volumes_json,
                                      const pdscenario_textbuf_t *waypoints_json,
                                      const pdscenario_textbuf_t *waygroups_json,
                                      const pdscenario_textbuf_t *covers_json,
                                      const pdscenario_textbuf_t *paths_json,
                                      const pdscenario_textbuf_t *ai_command_nodes_json,
                                      const pdscenario_textbuf_t *ai_command_links_json,
                                      pdscenario_textbuf_t *navigation_ini,
                                      pdscenario_textbuf_t *level_graph_json,
                                      pdscenario_textbuf_t *collision_meta_json,
                                      pdscenario_textbuf_t *navmesh_meta_json)
{
	char scene_hash[SHA256_HEX_SIZE];
	char collision_hash[SHA256_HEX_SIZE];
	char navigation_hash[SHA256_HEX_SIZE];
	char portals_hash[SHA256_HEX_SIZE];
	char pads_hash[SHA256_HEX_SIZE];
	char spawns_hash[SHA256_HEX_SIZE];
	char volumes_hash[SHA256_HEX_SIZE];
	char waypoints_hash[SHA256_HEX_SIZE];
	char waygroups_hash[SHA256_HEX_SIZE];
	char covers_hash[SHA256_HEX_SIZE];
	char paths_hash[SHA256_HEX_SIZE];

	if (!scenario_id || !ai_command_nodes_json || !ai_command_links_json ||
			!navigation_ini || !level_graph_json ||
			!collision_meta_json || !navmesh_meta_json) {
		return -1;
	}

	if (s_textbufAppendf(navigation_ini,
			"[navigation]\n"
			"source = scene.glb\n"
			"collision_source = collision.obj\n"
			"generator = deterministic.surface_graph.v1\n"
			"supports_walk = true\n"
			"supports_jump = true\n"
			"supports_drop = true\n"
			"supports_wall = true\n"
			"supports_ceiling = true\n"
			"portals_file = portals.json\n"
			"pads_file = pads.json\n"
			"spawns_file = spawns.json\n"
			"volumes_file = volumes.json\n"
			"waypoints_file = navigation/waypoints.json\n"
			"waygroups_file = navigation/waygroups.json\n"
			"covers_file = navigation/covers.json\n"
			"paths_file = navigation/paths.json\n"
			"generated_cache = _meta/generated-navmesh.json\n") != 0) {
		return -1;
	}

	if (s_textbufAppendf(level_graph_json,
			"{\n"
			"  \"schema\": \"pd2.level.graph.v1\",\n"
			"  \"scenario\": \"%s\",\n"
			"  \"source\": \"scene.glb\",\n"
			"  \"kind\": \"%s\",\n"
			"  \"tables\": {\n"
			"    \"portals\": \"portals.json\",\n"
			"    \"pads\": \"pads.json\",\n"
			"    \"spawns\": \"spawns.json\",\n"
			"    \"volumes\": \"volumes.json\",\n"
			"    \"objects\": \"objects.json\",\n"
			"    \"setup_fields\": \"setup.fields.json\",\n"
			"    \"objectives\": \"objectives.json\",\n"
			"    \"ai_lists\": \"ai/ailists.json\",\n"
			"    \"waypoints\": \"navigation/waypoints.json\",\n"
			"    \"waygroups\": \"navigation/waygroups.json\",\n"
			"    \"covers\": \"navigation/covers.json\",\n"
			"    \"paths\": \"navigation/paths.json\"\n"
			"  },\n"
			"  \"nodes\": [\n"
			"    { \"id\": \"scenario.load\", \"kind\": \"event.scenario.load\" },\n"
			"    { \"id\": \"source.scene\", \"kind\": \"scenario.scene.source\", \"file\": \"scene.glb\" },\n"
			"    { \"id\": \"collision.generate\", \"kind\": \"scenario.collision.generate\", \"source\": \"collision.obj\", \"cache\": \"_meta/generated-collision.json\" },\n"
			"    { \"id\": \"scenario.portals\", \"kind\": \"scenario.portals.source\", \"source\": \"portals.json\", \"portals\": %u },\n"
			"    { \"id\": \"navigation.generate\", \"kind\": \"scenario.navigation.generate\", \"source\": \"navigation.ini\", \"cache\": \"_meta/generated-navmesh.json\" },\n"
			"    { \"id\": \"scenario.pads\", \"kind\": \"scenario.pads.source\", \"source\": \"pads.json\", \"pads\": %u },\n"
			"    { \"id\": \"navigation.paths\", \"kind\": \"scenario.navigation.paths.source\", \"source\": \"navigation/paths.json\", \"paths\": %u },\n"
			"    { \"id\": \"setup.tables\", \"kind\": \"scenario.setup.tables\", \"objects\": \"objects.json\", \"fields\": \"setup.fields.json\", \"objectives\": \"objectives.json\" },\n"
			"    { \"id\": \"scenario.ai.lists\", \"kind\": \"scenario.ai.lists.source\", \"source\": \"ai/ailists.json\", \"lists\": %u },\n"
			"    { \"id\": \"scenario.global.settings\", \"kind\": \"scenario.global.settings.source\", \"scenario\": \"%s\", \"source\": \"scenario.ini\", \"scene\": \"scene.glb\", \"collision\": \"collision.obj\", \"navigation\": \"navigation.ini\", \"pads\": %u, \"volumes\": %u },\n"
			"    { \"id\": \"scenario.ai.action.set_list\", \"kind\": \"scenario.ai.action.set_list\", \"source\": \"ai/ailists.json\", \"target\": \"interpreter.ailist\", \"opcode\": \"0x0005\" },\n"
			"    { \"id\": \"scenario.ai.action.set_return_list\", \"kind\": \"scenario.ai.action.set_return_list\", \"source\": \"ai/ailists.json\", \"target\": \"chr.aireturnlist\", \"opcode\": \"0x0006\" },\n"
			"    { \"id\": \"scenario.ai.action.set_shot_list\", \"kind\": \"scenario.ai.action.set_shot_list\", \"source\": \"ai/ailists.json\", \"target\": \"chr.aishotlist\", \"opcode\": \"0x0007\" },\n"
			"    { \"id\": \"scenario.ai.action.return_list\", \"kind\": \"scenario.ai.action.return_list\", \"source\": \"ai/ailists.json\", \"target\": \"interpreter.ailist\", \"opcode\": \"0x0008\" },\n"
			"    { \"id\": \"scenario.ai.action.stop\", \"kind\": \"scenario.ai.action.stop\", \"source\": \"ai/ailists.json\", \"target\": \"chr.motion_state\", \"opcode\": \"0x0009\" },\n"
			"    { \"id\": \"scenario.ai.action.kneel\", \"kind\": \"scenario.ai.action.kneel\", \"source\": \"ai/ailists.json\", \"target\": \"chr.posture\", \"opcode\": \"0x000a\" },\n"
			"    { \"id\": \"scenario.ai.action.surrender\", \"kind\": \"scenario.ai.action.surrender\", \"source\": \"ai/ailists.json\", \"target\": \"chr.lifecycle\", \"opcode\": \"0x0024\" },\n"
			"    { \"id\": \"scenario.ai.action.fade_out\", \"kind\": \"scenario.ai.action.fade_out\", \"source\": \"ai/ailists.json\", \"target\": \"chr.lifecycle\", \"opcode\": \"0x0025\" },\n"
			"    { \"id\": \"scenario.ai.action.remove_chr\", \"kind\": \"scenario.ai.action.remove_chr\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.visibility\", \"opcode\": \"0x0026\" },\n"
			"    { \"id\": \"scenario.ai.action.try_sidestep\", \"kind\": \"scenario.ai.action.try_sidestep\", \"source\": \"ai/ailists.json\", \"target\": \"chr.combat_evasion\", \"opcode\": \"0x000f\" },\n"
			"    { \"id\": \"scenario.ai.action.try_jump_out\", \"kind\": \"scenario.ai.action.try_jump_out\", \"source\": \"ai/ailists.json\", \"target\": \"chr.combat_evasion\", \"opcode\": \"0x0010\" },\n"
			"    { \"id\": \"scenario.ai.action.try_run_sideways\", \"kind\": \"scenario.ai.action.try_run_sideways\", \"source\": \"ai/ailists.json\", \"target\": \"chr.combat_movement\", \"opcode\": \"0x0011\" },\n"
			"    { \"id\": \"scenario.ai.action.try_attack_walk\", \"kind\": \"scenario.ai.action.try_attack_walk\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x0012\" },\n"
			"    { \"id\": \"scenario.ai.action.try_attack_run\", \"kind\": \"scenario.ai.action.try_attack_run\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x0013\" },\n"
			"    { \"id\": \"scenario.ai.action.try_attack_roll\", \"kind\": \"scenario.ai.action.try_attack_roll\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x0014\" },\n"
			"    { \"id\": \"scenario.ai.action.try_attack_stand\", \"kind\": \"scenario.ai.action.try_attack_stand\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x0015\" },\n"
			"    { \"id\": \"scenario.ai.action.try_attack_kneel\", \"kind\": \"scenario.ai.action.try_attack_kneel\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x0016\" },\n"
			"    { \"id\": \"scenario.ai.action.try_attack_lie\", \"kind\": \"scenario.ai.action.try_attack_lie\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x01ba\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_attack_locked\", \"kind\": \"scenario.ai.condition.if_attack_locked\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_flags\", \"opcode\": \"0x00f0\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_attacking\", \"kind\": \"scenario.ai.condition.if_attacking\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x00f1\" },\n"
			"    { \"id\": \"scenario.ai.action.try_modify_attack\", \"kind\": \"scenario.ai.action.try_modify_attack\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x0017\" },\n"
			"    { \"id\": \"scenario.ai.action.face_entity\", \"kind\": \"scenario.ai.action.face_entity\", \"source\": \"ai/ailists.json\", \"target\": \"chr.facing\", \"opcode\": \"0x0018\" },\n"
			"    { \"id\": \"scenario.ai.action.apply_gset_damage\", \"kind\": \"scenario.ai.action.apply_gset_damage\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.damage\", \"opcode\": \"0x0019\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_damage_chr\", \"kind\": \"scenario.ai.action.chr_damage_chr\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.damage\", \"opcode\": \"0x001a\" },\n"
			"    { \"id\": \"scenario.ai.condition.consider_grenade_throw\", \"kind\": \"scenario.ai.condition.consider_grenade_throw\", \"source\": \"ai/ailists.json\", \"target\": \"chr.grenade_throw_decision\", \"opcode\": \"0x001b\" },\n"
			"    { \"id\": \"scenario.ai.action.drop_item\", \"kind\": \"scenario.ai.action.drop_item\", \"source\": \"ai/ailists.json\", \"target\": \"chr.inventory_drop\", \"opcode\": \"0x001c\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_do_animation\", \"kind\": \"scenario.ai.action.chr_do_animation\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.animation\", \"opcode\": \"0x000b\" },\n"
			"    { \"id\": \"scenario.ai.action.be_surprised_one_hand\", \"kind\": \"scenario.ai.action.be_surprised_one_hand\", \"source\": \"ai/ailists.json\", \"target\": \"chr.reaction\", \"opcode\": \"0x000d\" },\n"
			"    { \"id\": \"scenario.ai.action.be_surprised_look_around\", \"kind\": \"scenario.ai.action.be_surprised_look_around\", \"source\": \"ai/ailists.json\", \"target\": \"chr.reaction\", \"opcode\": \"0x000e\" },\n"
			"    { \"id\": \"scenario.ai.action.be_surprised_surrender\", \"kind\": \"scenario.ai.action.be_surprised_surrender\", \"source\": \"ai/ailists.json\", \"target\": \"chr.reaction\", \"opcode\": \"0x00ff\" },\n"
			"    { \"id\": \"scenario.ai.action.random\", \"kind\": \"scenario.ai.action.random\", \"source\": \"ai/ailists.json\", \"target\": \"chr.random\", \"opcode\": \"0x0036\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_random_less_than\", \"kind\": \"scenario.ai.condition.if_random_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.random\", \"opcode\": \"0x0037\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_random_greater_than\", \"kind\": \"scenario.ai.condition.if_random_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.random\", \"opcode\": \"0x0038\" },\n"
			"    { \"id\": \"scenario.ai.action.print\", \"kind\": \"scenario.ai.action.print\", \"source\": \"ai/ailists.json\", \"target\": \"debug.console\", \"opcode\": \"0x00b5\" },\n"
			"    { \"id\": \"scenario.ai.action.noop\", \"kind\": \"scenario.ai.action.noop\", \"source\": \"ai/ailists.json\", \"target\": \"interpreter.offset\", \"opcodes\": [\"0x0091\", \"0x00d8\", \"0x00d9\", \"0x00db\", \"0x0100\", \"0x0101\", \"0x010d\", \"0x016c\", \"0x01bb\"] },\n"
			"    { \"id\": \"scenario.ai.action.set_punch_dodge_list\", \"kind\": \"scenario.ai.action.set_punch_dodge_list\", \"source\": \"ai/ailists.json\", \"target\": \"chr.aipunchdodgelist\", \"opcode\": \"0x01c1\" },\n"
			"    { \"id\": \"scenario.ai.action.set_shooting_at_me_list\", \"kind\": \"scenario.ai.action.set_shooting_at_me_list\", \"source\": \"ai/ailists.json\", \"target\": \"chr.aishootingatmelist\", \"opcode\": \"0x01c2\" },\n"
			"    { \"id\": \"scenario.ai.action.set_dark_room_list\", \"kind\": \"scenario.ai.action.set_dark_room_list\", \"source\": \"ai/ailists.json\", \"target\": \"chr.aidarkroomlist\", \"opcode\": \"0x01c3\" },\n"
			"    { \"id\": \"scenario.ai.action.set_player_dead_list\", \"kind\": \"scenario.ai.action.set_player_dead_list\", \"source\": \"ai/ailists.json\", \"target\": \"chr.aiplayerdeadlist\", \"opcode\": \"0x01c4\" },\n"
			"    { \"id\": \"scenario.ai.action.jog_to_pad\", \"kind\": \"scenario.ai.action.jog_to_pad\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"opcode\": \"0x001d\", \"speed\": \"jog\" },\n"
			"    { \"id\": \"scenario.ai.action.go_to_pad_preset\", \"kind\": \"scenario.ai.action.go_to_pad_preset\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"opcode\": \"0x001e\", \"pad\": \"chr.padpreset1\" },\n"
			"    { \"id\": \"scenario.ai.action.walk_to_pad\", \"kind\": \"scenario.ai.action.walk_to_pad\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"opcode\": \"0x001f\", \"speed\": \"walk\" },\n"
			"    { \"id\": \"scenario.ai.action.run_to_pad\", \"kind\": \"scenario.ai.action.run_to_pad\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"opcode\": \"0x0020\", \"speed\": \"run\" },\n"
			"    { \"id\": \"scenario.ai.action.set_path\", \"kind\": \"scenario.ai.action.set_path\", \"source\": \"ai/ailists.json\", \"paths\": \"navigation/paths.json\", \"opcode\": \"0x0021\" },\n"
			"    { \"id\": \"scenario.ai.action.start_patrol\", \"kind\": \"scenario.ai.action.start_patrol\", \"source\": \"ai/ailists.json\", \"paths\": \"navigation/paths.json\", \"opcode\": \"0x0022\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_patrolling\", \"kind\": \"scenario.ai.condition.if_patrolling\", \"source\": \"ai/ailists.json\", \"target\": \"chr.patrol_state\", \"opcode\": \"0x0023\" },\n"
			"    { \"id\": \"scenario.ai.action.try_start_alarm\", \"kind\": \"scenario.ai.action.try_start_alarm\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"global.alarm\", \"opcode\": \"0x0027\" },\n"
			"    { \"id\": \"scenario.ai.action.activate_alarm\", \"kind\": \"scenario.ai.action.activate_alarm\", \"source\": \"ai/ailists.json\", \"target\": \"global.alarm\", \"opcode\": \"0x0028\" },\n"
			"    { \"id\": \"scenario.ai.action.deactivate_alarm\", \"kind\": \"scenario.ai.action.deactivate_alarm\", \"source\": \"ai/ailists.json\", \"target\": \"global.alarm\", \"opcode\": \"0x0029\" },\n"
			"    { \"id\": \"scenario.ai.action.try_run_from_target\", \"kind\": \"scenario.ai.action.try_run_from_target\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target_movement\", \"opcode\": \"0x002a\", \"speed\": \"run\" },\n"
			"    { \"id\": \"scenario.ai.action.try_jog_to_target_prop\", \"kind\": \"scenario.ai.action.try_jog_to_target_prop\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target_prop_movement\", \"opcode\": \"0x002b\", \"speed\": \"jog\" },\n"
			"    { \"id\": \"scenario.ai.action.try_walk_to_target_prop\", \"kind\": \"scenario.ai.action.try_walk_to_target_prop\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target_prop_movement\", \"opcode\": \"0x002c\", \"speed\": \"walk\" },\n"
			"    { \"id\": \"scenario.ai.action.try_run_to_target_prop\", \"kind\": \"scenario.ai.action.try_run_to_target_prop\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target_prop_movement\", \"opcode\": \"0x002d\", \"speed\": \"run\" },\n"
			"    { \"id\": \"scenario.ai.action.try_go_to_cover_prop\", \"kind\": \"scenario.ai.action.try_go_to_cover_prop\", \"source\": \"ai/ailists.json\", \"target\": \"chr.cover_prop_movement\", \"opcode\": \"0x002e\" },\n"
			"    { \"id\": \"scenario.ai.action.try_jog_to_chr\", \"kind\": \"scenario.ai.action.try_jog_to_chr\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.movement\", \"opcode\": \"0x002f\", \"speed\": \"jog\" },\n"
			"    { \"id\": \"scenario.ai.action.try_walk_to_chr\", \"kind\": \"scenario.ai.action.try_walk_to_chr\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.movement\", \"opcode\": \"0x0030\", \"speed\": \"walk\" },\n"
			"    { \"id\": \"scenario.ai.action.try_run_to_chr\", \"kind\": \"scenario.ai.action.try_run_to_chr\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.movement\", \"opcode\": \"0x0031\", \"speed\": \"run\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_can_hear_alarm\", \"kind\": \"scenario.ai.condition.if_can_hear_alarm\", \"source\": \"ai/ailists.json\", \"target\": \"global.alarm\", \"opcode\": \"0x0039\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_alarm_active\", \"kind\": \"scenario.ai.condition.if_alarm_active\", \"source\": \"ai/ailists.json\", \"target\": \"global.alarm\", \"opcode\": \"0x003a\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_gas_active\", \"kind\": \"scenario.ai.condition.if_gas_active\", \"source\": \"ai/ailists.json\", \"target\": \"global.gas\", \"opcode\": \"0x003b\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_hears_target\", \"kind\": \"scenario.ai.condition.if_hears_target\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.audio\", \"opcode\": \"0x003c\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_saw_injury\", \"kind\": \"scenario.ai.condition.if_saw_injury\", \"source\": \"ai/ailists.json\", \"target\": \"chr.perception.injury\", \"opcode\": \"0x003d\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_saw_death\", \"kind\": \"scenario.ai.condition.if_saw_death\", \"source\": \"ai/ailists.json\", \"target\": \"chr.perception.death\", \"opcode\": \"0x003e\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_los_to_target\", \"kind\": \"scenario.ai.condition.if_los_to_target\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.line_of_sight\", \"opcode\": \"0x003f\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_target_nearly_in_sight\", \"kind\": \"scenario.ai.condition.if_target_nearly_in_sight\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.nearly_in_sight\", \"opcode\": \"0x0040\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_nearly_in_targets_sight\", \"kind\": \"scenario.ai.condition.if_nearly_in_targets_sight\", \"source\": \"ai/ailists.json\", \"target\": \"chr.nearly_in_targets_sight\", \"opcode\": \"0x0041\" },\n"
			"    { \"id\": \"scenario.ai.action.set_pad_preset_to_pad_on_route_to_target\", \"kind\": \"scenario.ai.action.set_pad_preset_to_pad_on_route_to_target\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"paths\": \"navigation/paths.json\", \"target\": \"chr.padpreset1\", \"opcode\": \"0x0042\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_saw_target_recently\", \"kind\": \"scenario.ai.condition.if_saw_target_recently\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.recent_sight\", \"opcode\": \"0x0043\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_heard_target_recently\", \"kind\": \"scenario.ai.condition.if_heard_target_recently\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.recent_audio\", \"opcode\": \"0x0044\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_los_to_chr\", \"kind\": \"scenario.ai.condition.if_los_to_chr\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.line_of_sight\", \"opcode\": \"0x0045\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_never_been_on_screen\", \"kind\": \"scenario.ai.condition.if_never_been_on_screen\", \"source\": \"ai/ailists.json\", \"target\": \"chr.screen_history\", \"opcode\": \"0x0046\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_on_screen\", \"kind\": \"scenario.ai.condition.if_on_screen\", \"source\": \"ai/ailists.json\", \"target\": \"chr.screen_state\", \"opcode\": \"0x0047\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_in_on_screen_room\", \"kind\": \"scenario.ai.condition.if_chr_in_on_screen_room\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.room_visibility\", \"opcode\": \"0x0048\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_room_is_on_screen\", \"kind\": \"scenario.ai.condition.if_room_is_on_screen\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"pad.room_visibility\", \"opcode\": \"0x0049\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_target_aiming_at_me\", \"kind\": \"scenario.ai.condition.if_target_aiming_at_me\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.aim\", \"opcode\": \"0x004a\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_near_miss\", \"kind\": \"scenario.ai.condition.if_near_miss\", \"source\": \"ai/ailists.json\", \"target\": \"chr.near_miss_latch\", \"opcode\": \"0x004b\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_sees_suspicious_item\", \"kind\": \"scenario.ai.condition.if_sees_suspicious_item\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.suspicious_visibility\", \"opcode\": \"0x004c\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_target_in_fov_left\", \"kind\": \"scenario.ai.condition.if_target_in_fov_left\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.fov_left\", \"opcode\": \"0x004d\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_check_fov_with_target\", \"kind\": \"scenario.ai.condition.if_check_fov_with_target\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.fov\", \"opcode\": \"0x004e\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_target_out_of_fov_left\", \"kind\": \"scenario.ai.condition.if_target_out_of_fov_left\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.fov_left\", \"opcode\": \"0x004f\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_target_in_fov\", \"kind\": \"scenario.ai.condition.if_target_in_fov\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.fov\", \"opcode\": \"0x0050\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_target_out_of_fov\", \"kind\": \"scenario.ai.condition.if_target_out_of_fov\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.fov\", \"opcode\": \"0x0051\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_distance_to_target_less_than\", \"kind\": \"scenario.ai.condition.if_distance_to_target_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.distance\", \"opcode\": \"0x0052\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_distance_to_target_greater_than\", \"kind\": \"scenario.ai.condition.if_distance_to_target_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.distance\", \"opcode\": \"0x0053\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_distance_to_pad_less_than\", \"kind\": \"scenario.ai.condition.if_chr_distance_to_pad_less_than\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"target_chr.pad_distance\", \"opcode\": \"0x0054\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_distance_to_pad_greater_than\", \"kind\": \"scenario.ai.condition.if_chr_distance_to_pad_greater_than\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"target_chr.pad_distance\", \"opcode\": \"0x0055\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_distance_to_chr_less_than\", \"kind\": \"scenario.ai.condition.if_distance_to_chr_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.distance\", \"opcode\": \"0x0056\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_distance_to_chr_greater_than\", \"kind\": \"scenario.ai.condition.if_distance_to_chr_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.distance\", \"opcode\": \"0x0057\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_any_chr_near_self\", \"kind\": \"scenario.ai.condition.if_any_chr_near_self\", \"source\": \"ai/ailists.json\", \"target\": \"chr.preset_nearby\", \"opcode\": \"0x0058\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_distance_from_target_to_pad_less_than\", \"kind\": \"scenario.ai.condition.if_distance_from_target_to_pad_less_than\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"target_chr.pad_distance\", \"opcode\": \"0x0059\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_distance_from_target_to_pad_greater_than\", \"kind\": \"scenario.ai.condition.if_distance_from_target_to_pad_greater_than\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"target_chr.pad_distance\", \"opcode\": \"0x005a\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_in_room\", \"kind\": \"scenario.ai.condition.if_chr_in_room\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"target_chr.room\", \"opcode\": \"0x005b\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_target_in_room\", \"kind\": \"scenario.ai.condition.if_target_in_room\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"target_chr.room\", \"opcode\": \"0x005c\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_has_object\", \"kind\": \"scenario.ai.condition.if_chr_has_object\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"player.inventory\", \"opcode\": \"0x005d\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_weapon_thrown\", \"kind\": \"scenario.ai.condition.if_weapon_thrown\", \"source\": \"ai/ailists.json\", \"target\": \"weapon.landed\", \"opcode\": \"0x005e\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_weapon_thrown_on_object\", \"kind\": \"scenario.ai.condition.if_weapon_thrown_on_object\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.child_weapon\", \"opcode\": \"0x005f\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_has_weapon_equipped\", \"kind\": \"scenario.ai.condition.if_chr_has_weapon_equipped\", \"source\": \"ai/ailists.json\", \"target\": \"player.weapon\", \"opcode\": \"0x0060\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_gun_unclaimed\", \"kind\": \"scenario.ai.condition.if_gun_unclaimed\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"weapon.claim\", \"opcode\": \"0x0061\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_object_healthy\", \"kind\": \"scenario.ai.condition.if_object_healthy\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.health\", \"opcode\": \"0x0062\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_activated_object\", \"kind\": \"scenario.ai.condition.if_chr_activated_object\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.activation_latch\", \"opcode\": \"0x0063\" },\n"
			"    { \"id\": \"scenario.ai.action.obj_interact\", \"kind\": \"scenario.ai.action.obj_interact\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.interaction\", \"opcode\": \"0x0065\" },\n"
			"    { \"id\": \"scenario.ai.action.destroy_object\", \"kind\": \"scenario.ai.action.destroy_object\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.destroyed_state\", \"opcode\": \"0x0066\" },\n"
			"    { \"id\": \"scenario.ai.action.drop_object_from_chr\", \"kind\": \"scenario.ai.action.drop_object_from_chr\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.parent\", \"opcode\": \"0x0067\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_drop_items\", \"kind\": \"scenario.ai.action.chr_drop_items\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"chr.concealed_items\", \"opcode\": \"0x0068\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_drop_weapon\", \"kind\": \"scenario.ai.action.chr_drop_weapon\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"chr.weapon_inventory\", \"opcode\": \"0x0069\" },\n"
			"    { \"id\": \"scenario.ai.action.give_object_to_chr\", \"kind\": \"scenario.ai.action.give_object_to_chr\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.owner\", \"opcode\": \"0x006a\" },\n"
			"    { \"id\": \"scenario.ai.action.object_move_to_pad\", \"kind\": \"scenario.ai.action.object_move_to_pad\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"target\": \"object.transform\", \"opcode\": \"0x006b\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_waypoint_within_quadrant\", \"kind\": \"scenario.ai.condition.if_waypoint_within_quadrant\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"navigation\": \"navigation/waypoints.json\", \"target\": \"chr.padpreset1\", \"opcode\": \"0x0075\" },\n"
			"    { \"id\": \"scenario.ai.action.set_pad_preset_to_target_quadrant\", \"kind\": \"scenario.ai.action.set_pad_preset_to_target_quadrant\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"navigation\": \"navigation/waypoints.json\", \"target\": \"chr.padpreset1\", \"opcode\": \"0x0076\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_los_to_attack_target\", \"kind\": \"scenario.ai.condition.if_los_to_attack_target\", \"source\": \"ai/ailists.json\", \"target\": \"attack_target.line_of_sight\", \"opcode\": \"0x017a\" },\n"
			"    { \"id\": \"scenario.ai.action.set_morale\", \"kind\": \"scenario.ai.action.set_morale\", \"source\": \"ai/ailists.json\", \"target\": \"chr.morale\", \"operation\": \"set\", \"opcode\": \"0x0084\" },\n"
			"    { \"id\": \"scenario.ai.action.add_morale\", \"kind\": \"scenario.ai.action.add_morale\", \"source\": \"ai/ailists.json\", \"target\": \"chr.morale\", \"operation\": \"add\", \"opcode\": \"0x0085\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_add_morale\", \"kind\": \"scenario.ai.action.chr_add_morale\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.morale\", \"operation\": \"add\", \"opcode\": \"0x0086\" },\n"
			"    { \"id\": \"scenario.ai.action.subtract_morale\", \"kind\": \"scenario.ai.action.subtract_morale\", \"source\": \"ai/ailists.json\", \"target\": \"chr.morale\", \"operation\": \"subtract\", \"opcode\": \"0x0087\" },\n"
			"    { \"id\": \"scenario.ai.action.set_alertness\", \"kind\": \"scenario.ai.action.set_alertness\", \"source\": \"ai/ailists.json\", \"target\": \"chr.alertness\", \"operation\": \"set\", \"opcode\": \"0x008a\" },\n"
			"    { \"id\": \"scenario.ai.action.add_alertness\", \"kind\": \"scenario.ai.action.add_alertness\", \"source\": \"ai/ailists.json\", \"target\": \"chr.alertness\", \"operation\": \"add\", \"opcode\": \"0x008b\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_add_alertness\", \"kind\": \"scenario.ai.action.chr_add_alertness\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.alertness\", \"operation\": \"add\", \"opcode\": \"0x008c\" },\n"
			"    { \"id\": \"scenario.ai.action.subtract_alertness\", \"kind\": \"scenario.ai.action.subtract_alertness\", \"source\": \"ai/ailists.json\", \"target\": \"chr.alertness\", \"operation\": \"subtract\", \"opcode\": \"0x008d\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_num_arghs_less_than\", \"kind\": \"scenario.ai.condition.if_num_arghs_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.recovery_reactions\", \"opcode\": \"0x007d\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_num_arghs_greater_than\", \"kind\": \"scenario.ai.condition.if_num_arghs_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.recovery_reactions\", \"opcode\": \"0x007e\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_num_close_arghs_less_than\", \"kind\": \"scenario.ai.condition.if_num_close_arghs_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.close_recovery_reactions\", \"opcode\": \"0x007f\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_num_close_arghs_greater_than\", \"kind\": \"scenario.ai.condition.if_num_close_arghs_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.close_recovery_reactions\", \"opcode\": \"0x0080\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_health_greater_than\", \"kind\": \"scenario.ai.condition.if_chr_health_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.health\", \"opcode\": \"0x0081\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_health_less_than\", \"kind\": \"scenario.ai.condition.if_chr_health_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.health\", \"opcode\": \"0x0082\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_shield_less_than\", \"kind\": \"scenario.ai.condition.if_chr_shield_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.shield\", \"opcode\": \"0x010f\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_shield_greater_than\", \"kind\": \"scenario.ai.condition.if_chr_shield_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.shield\", \"opcode\": \"0x0110\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_injured\", \"kind\": \"scenario.ai.condition.if_injured\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.injury_latch\", \"opcode\": \"0x0083\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_shield_damaged\", \"kind\": \"scenario.ai.condition.if_shield_damaged\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.shield_damage_latch\", \"opcode\": \"0x0168\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_morale_less_than\", \"kind\": \"scenario.ai.condition.if_morale_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.morale\", \"opcode\": \"0x0088\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_morale_less_than_random\", \"kind\": \"scenario.ai.condition.if_morale_less_than_random\", \"source\": \"ai/ailists.json\", \"target\": \"chr.morale_random\", \"opcode\": \"0x0089\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_alertness\", \"kind\": \"scenario.ai.condition.if_alertness\", \"source\": \"ai/ailists.json\", \"target\": \"chr.alertness\", \"opcode\": \"0x008e\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_alertness_less_than\", \"kind\": \"scenario.ai.condition.if_chr_alertness_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.alertness\", \"opcode\": \"0x008f\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_alertness_less_than_random\", \"kind\": \"scenario.ai.condition.if_alertness_less_than_random\", \"source\": \"ai/ailists.json\", \"target\": \"chr.alertness_random\", \"opcode\": \"0x0090\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_idle\", \"kind\": \"scenario.ai.condition.if_idle\", \"source\": \"ai/ailists.json\", \"target\": \"chr.action_state\", \"opcode\": \"0x000c\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_stopped\", \"kind\": \"scenario.ai.condition.if_stopped\", \"source\": \"ai/ailists.json\", \"target\": \"chr.motion_state\", \"opcode\": \"0x0032\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_dead\", \"kind\": \"scenario.ai.condition.if_chr_dead\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.life_state\", \"opcode\": \"0x0033\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_death_animation_finished\", \"kind\": \"scenario.ai.condition.if_chr_death_animation_finished\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.death_animation\", \"opcode\": \"0x0034\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_knocked_out\", \"kind\": \"scenario.ai.condition.if_chr_knocked_out\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.knockout_state\", \"opcode\": \"0x017b\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_can_see_target\", \"kind\": \"scenario.ai.condition.if_can_see_target\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target_visibility\", \"opcode\": \"0x0035\" },\n"
			"    { \"id\": \"scenario.ai.action.increase_squadron_alertness\", \"kind\": \"scenario.ai.action.increase_squadron_alertness\", \"source\": \"ai/ailists.json\", \"target\": \"squadron.alertness\", \"operation\": \"increase\", \"opcode\": \"0x0131\" },\n"
			"    { \"id\": \"scenario.ai.action.set_hear_distance\", \"kind\": \"scenario.ai.action.set_hear_distance\", \"source\": \"ai/ailists.json\", \"target\": \"chr.hearing_scale\", \"opcode\": \"0x0092\" },\n"
			"    { \"id\": \"scenario.ai.action.set_view_distance\", \"kind\": \"scenario.ai.action.set_view_distance\", \"source\": \"ai/ailists.json\", \"target\": \"chr.vision_range\", \"opcode\": \"0x0093\" },\n"
			"    { \"id\": \"scenario.ai.action.set_grenade_probability\", \"kind\": \"scenario.ai.action.set_grenade_probability\", \"source\": \"ai/ailists.json\", \"target\": \"chr.grenade_probability\", \"opcode\": \"0x0094\" },\n"
			"    { \"id\": \"scenario.ai.action.set_chr_num\", \"kind\": \"scenario.ai.action.set_chr_num\", \"source\": \"ai/ailists.json\", \"target\": \"chr.number\", \"opcode\": \"0x0095\" },\n"
			"    { \"id\": \"scenario.ai.action.set_max_damage\", \"kind\": \"scenario.ai.action.set_max_damage\", \"source\": \"ai/ailists.json\", \"target\": \"chr.max_damage\", \"opcode\": \"0x0096\" },\n"
			"    { \"id\": \"scenario.ai.action.add_health\", \"kind\": \"scenario.ai.action.add_health\", \"source\": \"ai/ailists.json\", \"target\": \"chr.health\", \"operation\": \"add\", \"opcode\": \"0x0097\" },\n"
			"    { \"id\": \"scenario.ai.action.set_shield\", \"kind\": \"scenario.ai.action.set_shield\", \"source\": \"ai/ailists.json\", \"target\": \"chr.shield\", \"opcode\": \"0x010e\" },\n"
			"    { \"id\": \"scenario.ai.action.set_reaction_speed\", \"kind\": \"scenario.ai.action.set_reaction_speed\", \"source\": \"ai/ailists.json\", \"target\": \"chr.speed_rating\", \"opcode\": \"0x0098\" },\n"
			"    { \"id\": \"scenario.ai.action.set_recovery_speed\", \"kind\": \"scenario.ai.action.set_recovery_speed\", \"source\": \"ai/ailists.json\", \"target\": \"chr.recovery_rating\", \"opcode\": \"0x0099\" },\n"
			"    { \"id\": \"scenario.ai.action.set_accuracy\", \"kind\": \"scenario.ai.action.set_accuracy\", \"source\": \"ai/ailists.json\", \"target\": \"chr.accuracy_rating\", \"opcode\": \"0x009a\" },\n"
			"    { \"id\": \"scenario.ai.action.set_dodge_rating\", \"kind\": \"scenario.ai.action.set_dodge_rating\", \"source\": \"ai/ailists.json\", \"target\": \"chr.dodge_rating\", \"opcode\": \"0x01c6\" },\n"
			"    { \"id\": \"scenario.ai.action.set_unarmed_dodge_rating\", \"kind\": \"scenario.ai.action.set_unarmed_dodge_rating\", \"source\": \"ai/ailists.json\", \"target\": \"chr.unarmed_dodge_rating\", \"opcode\": \"0x01c7\" },\n"
			"    { \"id\": \"scenario.ai.action.set_flag\", \"kind\": \"scenario.ai.action.set_flag\", \"source\": \"ai/ailists.json\", \"target\": \"chr.flags\", \"opcode\": \"0x009b\" },\n"
			"    { \"id\": \"scenario.ai.action.unset_flag\", \"kind\": \"scenario.ai.action.unset_flag\", \"source\": \"ai/ailists.json\", \"target\": \"chr.flags\", \"opcode\": \"0x009c\" },\n"
			"    { \"id\": \"scenario.ai.action.if_has_flag\", \"kind\": \"scenario.ai.action.if_has_flag\", \"source\": \"ai/ailists.json\", \"target\": \"chr.flags\", \"opcode\": \"0x009d\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_set_flag\", \"kind\": \"scenario.ai.action.chr_set_flag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.flags\", \"opcode\": \"0x009e\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_unset_flag\", \"kind\": \"scenario.ai.action.chr_unset_flag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.flags\", \"opcode\": \"0x009f\" },\n"
			"    { \"id\": \"scenario.ai.action.if_chr_has_flag\", \"kind\": \"scenario.ai.action.if_chr_has_flag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.flags\", \"opcode\": \"0x00a0\" },\n"
			"    { \"id\": \"scenario.ai.action.set_stage_flag\", \"kind\": \"scenario.ai.action.set_stage_flag\", \"source\": \"ai/ailists.json\", \"target\": \"mission.stage_flags\", \"opcode\": \"0x00a1\" },\n"
			"    { \"id\": \"scenario.ai.action.unset_stage_flag\", \"kind\": \"scenario.ai.action.unset_stage_flag\", \"source\": \"ai/ailists.json\", \"target\": \"mission.stage_flags\", \"opcode\": \"0x00a2\" },\n"
			"    { \"id\": \"scenario.ai.action.if_stage_flag_eq\", \"kind\": \"scenario.ai.action.if_stage_flag_eq\", \"source\": \"ai/ailists.json\", \"target\": \"mission.stage_flags\", \"opcode\": \"0x00a3\" },\n"
			"    { \"id\": \"scenario.ai.action.set_chrflag\", \"kind\": \"scenario.ai.action.set_chrflag\", \"source\": \"ai/ailists.json\", \"target\": \"chr.chrflags\", \"opcode\": \"0x00a4\" },\n"
			"    { \"id\": \"scenario.ai.action.unset_chrflag\", \"kind\": \"scenario.ai.action.unset_chrflag\", \"source\": \"ai/ailists.json\", \"target\": \"chr.chrflags\", \"opcode\": \"0x00a5\" },\n"
			"    { \"id\": \"scenario.ai.action.if_has_chrflag\", \"kind\": \"scenario.ai.action.if_has_chrflag\", \"source\": \"ai/ailists.json\", \"target\": \"chr.chrflags\", \"opcode\": \"0x00a6\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_set_chrflag\", \"kind\": \"scenario.ai.action.chr_set_chrflag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.chrflags\", \"opcode\": \"0x00a7\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_unset_chrflag\", \"kind\": \"scenario.ai.action.chr_unset_chrflag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.chrflags\", \"opcode\": \"0x00a8\" },\n"
			"    { \"id\": \"scenario.ai.action.if_chr_has_chrflag\", \"kind\": \"scenario.ai.action.if_chr_has_chrflag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.chrflags\", \"opcode\": \"0x00a9\" },\n"
			"    { \"id\": \"scenario.ai.action.set_obj_flag\", \"kind\": \"scenario.ai.action.set_obj_flag\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.flags\", \"opcodes\": [\"0x00aa\", \"0x00ad\", \"0x0118\"] },\n"
			"    { \"id\": \"scenario.ai.action.unset_obj_flag\", \"kind\": \"scenario.ai.action.unset_obj_flag\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.flags\", \"opcodes\": [\"0x00ab\", \"0x00ae\", \"0x0119\"] },\n"
			"    { \"id\": \"scenario.ai.action.if_obj_has_flag\", \"kind\": \"scenario.ai.action.if_obj_has_flag\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.flags\", \"opcodes\": [\"0x00ac\", \"0x00af\", \"0x011a\"] },\n"
			"    { \"id\": \"scenario.ai.action.open_door\", \"kind\": \"scenario.ai.action.open_door\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"door.mode\", \"opcode\": \"0x006c\" },\n"
			"    { \"id\": \"scenario.ai.action.close_door\", \"kind\": \"scenario.ai.action.close_door\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"door.mode\", \"opcode\": \"0x006d\" },\n"
			"    { \"id\": \"scenario.ai.action.if_door_state\", \"kind\": \"scenario.ai.action.if_door_state\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"door.mode\", \"opcode\": \"0x006e\" },\n"
			"    { \"id\": \"scenario.ai.action.if_object_is_door\", \"kind\": \"scenario.ai.action.if_object_is_door\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.type\", \"opcode\": \"0x006f\" },\n"
			"    { \"id\": \"scenario.ai.action.lock_door\", \"kind\": \"scenario.ai.action.lock_door\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"door.keyflags\", \"opcode\": \"0x0070\" },\n"
			"    { \"id\": \"scenario.ai.action.unlock_door\", \"kind\": \"scenario.ai.action.unlock_door\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"door.keyflags\", \"opcode\": \"0x0071\" },\n"
			"    { \"id\": \"scenario.ai.action.if_door_locked\", \"kind\": \"scenario.ai.action.if_door_locked\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"door.keyflags\", \"opcode\": \"0x0072\" },\n"
			"    { \"id\": \"scenario.ai.action.if_lift_stationary\", \"kind\": \"scenario.ai.action.if_lift_stationary\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"target\": \"lift.motion\", \"opcode\": \"0x0188\" },\n"
			"    { \"id\": \"scenario.ai.action.lift_go_to_stop\", \"kind\": \"scenario.ai.action.lift_go_to_stop\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"target\": \"lift.target_level\", \"opcode\": \"0x0189\" },\n"
			"    { \"id\": \"scenario.ai.action.if_lift_at_stop\", \"kind\": \"scenario.ai.action.if_lift_at_stop\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"target\": \"lift.current_level\", \"opcode\": \"0x018a\" },\n"
			"    { \"id\": \"scenario.ai.action.activate_lift\", \"kind\": \"scenario.ai.action.activate_lift\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"target\": \"lift.registration\", \"opcode\": \"0x018d\" },\n"
			"    { \"id\": \"scenario.ai.action.if_using_lift\", \"kind\": \"scenario.ai.action.if_using_lift\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"target\": \"chr.lift\", \"opcode\": \"0x01a5\" },\n"
			"    { \"id\": \"scenario.ai.action.configure_rain\", \"kind\": \"scenario.ai.action.configure_rain\", \"source\": \"ai/ailists.json\", \"globals\": \"scenario.ini\", \"target\": \"weather.rain\", \"opcode\": \"0x018b\" },\n"
			"    { \"id\": \"scenario.ai.action.configure_snow\", \"kind\": \"scenario.ai.action.configure_snow\", \"source\": \"ai/ailists.json\", \"globals\": \"scenario.ini\", \"target\": \"weather.snow\", \"opcode\": \"0x01b6\" },\n"
			"    { \"id\": \"scenario.ai.action.switch_to_alt_sky\", \"kind\": \"scenario.ai.action.switch_to_alt_sky\", \"source\": \"ai/ailists.json\", \"target\": \"sky.transition\", \"opcode\": \"0x00f2\" },\n"
			"    { \"id\": \"scenario.ai.action.set_wind_speed\", \"kind\": \"scenario.ai.action.set_wind_speed\", \"source\": \"ai/ailists.json\", \"target\": \"sky.wind_speed\", \"opcode\": \"0x01b2\" },\n"
			"    { \"id\": \"scenario.ai.action.set_lights\", \"kind\": \"scenario.ai.action.set_lights\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"room.lights\", \"opcode\": \"0x0102\" },\n"
			"    { \"id\": \"scenario.ai.action.set_room_flag\", \"kind\": \"scenario.ai.action.set_room_flag\", \"source\": \"ai/ailists.json\", \"scene\": \"scene.glb\", \"target\": \"room.flags\", \"opcode\": \"0x01d4\" },\n"
			"    { \"id\": \"scenario.ai.action.show_cutscene_chrs\", \"kind\": \"scenario.ai.action.show_cutscene_chrs\", \"source\": \"ai/ailists.json\", \"target\": \"chr.cutscene_visibility\", \"opcode\": \"0x01d5\" },\n"
			"    { \"id\": \"scenario.ai.action.configure_environment\", \"kind\": \"scenario.ai.action.configure_environment\", \"source\": \"ai/ailists.json\", \"globals\": \"scenario.ini\", \"scene\": \"scene.glb\", \"target\": \"environment.room_global_audio\", \"opcode\": \"0x01d6\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_distance_to_target2_less_than\", \"kind\": \"scenario.ai.condition.if_distance_to_target2_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target_distance2\", \"opcode\": \"0x01d7\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_distance_to_target2_greater_than\", \"kind\": \"scenario.ai.condition.if_distance_to_target2_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target_distance2\", \"opcode\": \"0x01d8\" },\n"
			"    { \"id\": \"scenario.ai.action.speak\", \"kind\": \"scenario.ai.action.speak\", \"source\": \"ai/ailists.json\", \"target\": \"chr.subtitle_audio\", \"opcode\": \"0x00cd\" },\n"
			"    { \"id\": \"scenario.ai.action.play_sound\", \"kind\": \"scenario.ai.action.play_sound\", \"source\": \"ai/ailists.json\", \"target\": \"audio.channel\", \"opcode\": \"0x00ce\" },\n"
			"    { \"id\": \"scenario.ai.action.assign_sound\", \"kind\": \"scenario.ai.action.assign_sound\", \"source\": \"ai/ailists.json\", \"target\": \"audio.marker\", \"opcode\": \"0x017c\" },\n"
			"    { \"id\": \"scenario.ai.action.audio_mute_channel\", \"kind\": \"scenario.ai.action.audio_mute_channel\", \"source\": \"ai/ailists.json\", \"target\": \"audio.channel\", \"opcode\": \"0x00d3\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_channel_free\", \"kind\": \"scenario.ai.condition.if_channel_free\", \"source\": \"ai/ailists.json\", \"target\": \"audio.channel\", \"opcode\": \"0x0138\" },\n"
			"    { \"id\": \"scenario.ai.action.set_object_sound_volume\", \"kind\": \"scenario.ai.action.set_object_sound_volume\", \"source\": \"ai/ailists.json\", \"target\": \"audio.channel.volume\", \"opcode\": \"0x00d1\" },\n"
			"    { \"id\": \"scenario.ai.action.set_object_sound_volume_by_distance\", \"kind\": \"scenario.ai.action.set_object_sound_volume_by_distance\", \"source\": \"ai/ailists.json\", \"target\": \"audio.channel.volume\", \"opcode\": \"0x00d2\" },\n"
			"    { \"id\": \"scenario.ai.action.set_object_sound_playing\", \"kind\": \"scenario.ai.action.set_object_sound_playing\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.audio\", \"opcode\": \"0x00cf\" },\n"
			"    { \"id\": \"scenario.ai.action.play_repeating_sound_from_object\", \"kind\": \"scenario.ai.action.play_repeating_sound_from_object\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.audio.repeating\", \"opcode\": \"0x016b\" },\n"
			"    { \"id\": \"scenario.ai.action.play_sound_from_entity\", \"kind\": \"scenario.ai.action.play_sound_from_entity\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"entity.audio\", \"opcode\": \"0x0179\" },\n"
			"    { \"id\": \"scenario.ai.action.play_repeating_sound_from_pad\", \"kind\": \"scenario.ai.action.play_repeating_sound_from_pad\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"pad.audio.repeating\", \"opcode\": \"0x00d0\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_object_sound_volume_less_than\", \"kind\": \"scenario.ai.condition.if_object_sound_volume_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"audio.channel.volume\", \"opcode\": \"0x00d4\" },\n"
			"    { \"id\": \"scenario.ai.action.play_sound_from_prop\", \"kind\": \"scenario.ai.action.play_sound_from_prop\", \"source\": \"ai/ailists.json\", \"target\": \"prop.audio\", \"opcode\": \"0x01d9\" },\n"
			"    { \"id\": \"scenario.ai.action.play_temporary_primary_track\", \"kind\": \"scenario.ai.action.play_temporary_primary_track\", \"source\": \"ai/ailists.json\", \"target\": \"music.primary\", \"opcode\": \"0x01da\" },\n"
			"    { \"id\": \"scenario.ai.action.play_x_track\", \"kind\": \"scenario.ai.action.play_x_track\", \"source\": \"ai/ailists.json\", \"target\": \"music.x_track\", \"opcode\": \"0x00f9\" },\n"
			"    { \"id\": \"scenario.ai.action.stop_x_track\", \"kind\": \"scenario.ai.action.stop_x_track\", \"source\": \"ai/ailists.json\", \"target\": \"music.x_track\", \"opcode\": \"0x00fa\" },\n"
			"    { \"id\": \"scenario.ai.action.play_track_isolated\", \"kind\": \"scenario.ai.action.play_track_isolated\", \"source\": \"ai/ailists.json\", \"target\": \"music.isolated\", \"opcode\": \"0x015b\" },\n"
			"    { \"id\": \"scenario.ai.action.play_default_tracks\", \"kind\": \"scenario.ai.action.play_default_tracks\", \"source\": \"ai/ailists.json\", \"target\": \"music.default\", \"opcode\": \"0x015c\" },\n"
			"    { \"id\": \"scenario.ai.action.play_cutscene_track\", \"kind\": \"scenario.ai.action.play_cutscene_track\", \"source\": \"ai/ailists.json\", \"target\": \"music.cutscene\", \"opcode\": \"0x017d\" },\n"
			"    { \"id\": \"scenario.ai.action.stop_cutscene_track\", \"kind\": \"scenario.ai.action.stop_cutscene_track\", \"source\": \"ai/ailists.json\", \"target\": \"music.cutscene\", \"opcode\": \"0x017e\" },\n"
			"    { \"id\": \"scenario.ai.action.play_temporary_track\", \"kind\": \"scenario.ai.action.play_temporary_track\", \"source\": \"ai/ailists.json\", \"target\": \"music.ambient_temporary\", \"opcode\": \"0x017f\" },\n"
			"    { \"id\": \"scenario.ai.action.stop_ambient_track\", \"kind\": \"scenario.ai.action.stop_ambient_track\", \"source\": \"ai/ailists.json\", \"target\": \"music.ambient_temporary\", \"opcode\": \"0x0180\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_draw_weapon\", \"kind\": \"scenario.ai.action.chr_draw_weapon\", \"source\": \"ai/ailists.json\", \"target\": \"player.weapon\", \"opcode\": \"0x00ec\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_draw_weapon_in_cutscene\", \"kind\": \"scenario.ai.action.chr_draw_weapon_in_cutscene\", \"source\": \"ai/ailists.json\", \"target\": \"player.weapon.cutscene\", \"opcode\": \"0x00ed\" },\n"
			"    { \"id\": \"scenario.ai.action.set_player_force_speed\", \"kind\": \"scenario.ai.action.set_player_force_speed\", \"source\": \"ai/ailists.json\", \"target\": \"player.force_speed\", \"opcode\": \"0x00ee\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_set_invincible\", \"kind\": \"scenario.ai.action.chr_set_invincible\", \"source\": \"ai/ailists.json\", \"target\": \"player.invincible\", \"opcode\": \"0x00f3\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_player_is_invincible\", \"kind\": \"scenario.ai.condition.if_player_is_invincible\", \"source\": \"ai/ailists.json\", \"target\": \"player.invincible\", \"opcode\": \"0x00f8\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_has_no_gun\", \"kind\": \"scenario.ai.condition.if_chr_has_no_gun\", \"source\": \"ai/ailists.json\", \"target\": \"chr.weapon_state\", \"opcode\": \"0x016f\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_delete_weapon\", \"kind\": \"scenario.ai.action.chr_delete_weapon\", \"source\": \"ai/ailists.json\", \"target\": \"chr.weapon_inventory\", \"opcode\": \"0x00e9\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_trigger_shot_list\", \"kind\": \"scenario.ai.condition.if_trigger_shot_list\", \"source\": \"ai/ailists.json\", \"target\": \"chr.shot_list_latch\", \"opcode\": \"0x00fd\" },\n"
			"    { \"id\": \"scenario.ai.action.end_level\", \"kind\": \"scenario.ai.action.end_level\", \"source\": \"ai/ailists.json\", \"target\": \"mission.flow\", \"opcode\": \"0x00dc\" },\n"
			"    { \"id\": \"scenario.ai.action.end_cutscene\", \"kind\": \"scenario.ai.action.end_cutscene\", \"source\": \"ai/ailists.json\", \"target\": \"player.cutscene\", \"opcode\": \"0x00dd\" },\n"
			"    { \"id\": \"scenario.ai.action.warp_jo_to_pad\", \"kind\": \"scenario.ai.action.warp_jo_to_pad\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"player.warp\", \"opcode\": \"0x00de\" },\n"
			"    { \"id\": \"scenario.ai.action.warp_jo_to_tag\", \"kind\": \"scenario.ai.action.warp_jo_to_tag\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"player.warp\", \"opcode\": \"0x00df\" },\n"
			"    { \"id\": \"scenario.ai.action.revoke_control\", \"kind\": \"scenario.ai.action.revoke_control\", \"source\": \"ai/ailists.json\", \"target\": \"player.control\", \"opcode\": \"0x00e0\" },\n"
			"    { \"id\": \"scenario.ai.action.grant_control\", \"kind\": \"scenario.ai.action.grant_control\", \"source\": \"ai/ailists.json\", \"target\": \"player.control\", \"opcode\": \"0x00e1\" },\n"
			"    { \"id\": \"scenario.ai.action.player_fade_in\", \"kind\": \"scenario.ai.action.player_fade_in\", \"source\": \"ai/ailists.json\", \"target\": \"player.fade\", \"opcode\": \"0x00e3\" },\n"
			"    { \"id\": \"scenario.ai.action.players_fade_out\", \"kind\": \"scenario.ai.action.players_fade_out\", \"source\": \"ai/ailists.json\", \"target\": \"player.fade\", \"opcode\": \"0x00e4\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_colour_fade_complete\", \"kind\": \"scenario.ai.condition.if_colour_fade_complete\", \"source\": \"ai/ailists.json\", \"target\": \"player.fade\", \"opcode\": \"0x00e5\" },\n"
			"    { \"id\": \"scenario.ai.action.prepare_warp_orbit\", \"kind\": \"scenario.ai.action.prepare_warp_orbit\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"player.warp\", \"opcode\": \"0x00f4\" },\n"
			"    { \"id\": \"scenario.ai.action.begin_warp_latch\", \"kind\": \"scenario.ai.action.begin_warp_latch\", \"source\": \"ai/ailists.json\", \"target\": \"player.warp_latch\", \"opcode\": \"0x00f5\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_warp_latch_complete\", \"kind\": \"scenario.ai.condition.if_warp_latch_complete\", \"source\": \"ai/ailists.json\", \"target\": \"player.warp_latch\", \"opcode\": \"0x00f6\" },\n"
			"    { \"id\": \"scenario.ai.action.set_camera_animation\", \"kind\": \"scenario.ai.action.set_camera_animation\", \"source\": \"ai/ailists.json\", \"target\": \"player.camera_animation\", \"opcode\": \"0x0111\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_in_cutscene\", \"kind\": \"scenario.ai.condition.if_in_cutscene\", \"source\": \"ai/ailists.json\", \"target\": \"player.cutscene\", \"opcode\": \"0x0113\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_cutscene_button_pressed\", \"kind\": \"scenario.ai.condition.if_cutscene_button_pressed\", \"source\": \"ai/ailists.json\", \"target\": \"player.cutscene\", \"opcode\": \"0x0174\" },\n"
			"    { \"id\": \"scenario.ai.action.reorient_for_cutscene_stop\", \"kind\": \"scenario.ai.action.reorient_for_cutscene_stop\", \"source\": \"ai/ailists.json\", \"target\": \"player.cutscene\", \"opcode\": \"0x0175\" },\n"
			"    { \"id\": \"scenario.ai.action.spawn_chr_at_pad\", \"kind\": \"scenario.ai.action.spawn_chr_at_pad\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"chr.spawn\", \"opcode\": \"0x00c6\" },\n"
			"    { \"id\": \"scenario.ai.action.spawn_chr_at_chr\", \"kind\": \"scenario.ai.action.spawn_chr_at_chr\", \"source\": \"ai/ailists.json\", \"target\": \"chr.spawn\", \"opcode\": \"0x00c7\" },\n"
			"    { \"id\": \"scenario.ai.action.try_equip_weapon\", \"kind\": \"scenario.ai.action.try_equip_weapon\", \"source\": \"ai/ailists.json\", \"target\": \"chr.weapon_inventory\", \"opcode\": \"0x00c8\" },\n"
			"    { \"id\": \"scenario.ai.action.try_equip_hat\", \"kind\": \"scenario.ai.action.try_equip_hat\", \"source\": \"ai/ailists.json\", \"target\": \"chr.hat\", \"opcode\": \"0x00c9\" },\n"
			"    { \"id\": \"scenario.ai.action.set_obj_image\", \"kind\": \"scenario.ai.action.set_obj_image\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.monitor_image\", \"opcode\": \"0x00da\" },\n"
			"    { \"id\": \"scenario.ai.action.object_do_animation\", \"kind\": \"scenario.ai.action.object_do_animation\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.animation\", \"opcode\": \"0x0112\" },\n"
			"    { \"id\": \"scenario.ai.action.set_door_open\", \"kind\": \"scenario.ai.action.set_door_open\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"door.open_state\", \"opcode\": \"0x00e8\" },\n"
			"    { \"id\": \"scenario.ai.action.duplicate_chr\", \"kind\": \"scenario.ai.action.duplicate_chr\", \"source\": \"ai/ailists.json\", \"target\": \"chr.clone\", \"opcode\": \"0x00ca\" },\n"
			"    { \"id\": \"scenario.ai.action.enable_chr\", \"kind\": \"scenario.ai.action.enable_chr\", \"source\": \"ai/ailists.json\", \"target\": \"chr.enabled\", \"opcode\": \"0x0114\" },\n"
			"    { \"id\": \"scenario.ai.action.disable_chr\", \"kind\": \"scenario.ai.action.disable_chr\", \"source\": \"ai/ailists.json\", \"target\": \"chr.enabled\", \"opcode\": \"0x0115\" },\n"
			"    { \"id\": \"scenario.ai.action.enable_obj\", \"kind\": \"scenario.ai.action.enable_obj\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.enabled\", \"opcode\": \"0x0116\" },\n"
			"    { \"id\": \"scenario.ai.action.disable_obj\", \"kind\": \"scenario.ai.action.disable_obj\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.enabled\", \"opcode\": \"0x0117\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_move_to_pad\", \"kind\": \"scenario.ai.action.chr_move_to_pad\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"chr.transform\", \"opcode\": \"0x00e2\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_set_team\", \"kind\": \"scenario.ai.action.chr_set_team\", \"source\": \"ai/ailists.json\", \"target\": \"chr.team\", \"opcode\": \"0x010b\" },\n"
			"    { \"id\": \"scenario.ai.action.damage_chr_by_amount\", \"kind\": \"scenario.ai.action.damage_chr_by_amount\", \"source\": \"ai/ailists.json\", \"target\": \"chr.damage\", \"opcode\": \"0x016e\" },\n"
			"    { \"id\": \"scenario.ai.action.do_preset_animation\", \"kind\": \"scenario.ai.action.do_preset_animation\", \"source\": \"ai/ailists.json\", \"target\": \"chr.animation\", \"opcode\": \"0x01a3\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_player_chr_portal_distance_less_than\", \"kind\": \"scenario.ai.condition.if_player_chr_portal_distance_less_than\", \"source\": \"ai/ailists.json\", \"scene\": \"scene.glb\", \"target\": \"player_chr.portal_distance\", \"opcode\": \"0x01aa\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_reposition_valid\", \"kind\": \"scenario.ai.condition.if_chr_reposition_valid\", \"source\": \"ai/ailists.json\", \"scene\": \"scene.glb\", \"target\": \"chr.reposition\", \"opcode\": \"0x01b4\" },\n"
			"    { \"id\": \"scenario.ai.action.do_gun_command\", \"kind\": \"scenario.ai.action.do_gun_command\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"scene\": \"scene.glb\", \"target\": \"chr.gunprop.command\", \"opcode\": \"0x0170\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_distance_to_gun_less_than\", \"kind\": \"scenario.ai.condition.if_distance_to_gun_less_than\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"scene\": \"scene.glb\", \"target\": \"chr.gunprop.distance\", \"opcode\": \"0x0171\" },\n"
			"    { \"id\": \"scenario.ai.action.recover_gun\", \"kind\": \"scenario.ai.action.recover_gun\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"scene\": \"scene.glb\", \"target\": \"chr.inventory.weapon\", \"opcode\": \"0x0172\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_copy_properties\", \"kind\": \"scenario.ai.action.chr_copy_properties\", \"source\": \"ai/ailists.json\", \"target\": \"chr.properties\", \"opcode\": \"0x0173\" },\n"
			"    { \"id\": \"scenario.ai.action.player_auto_walk\", \"kind\": \"scenario.ai.action.player_auto_walk\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"player.autowalk\", \"opcode\": \"0x0177\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_player_auto_walk_finished\", \"kind\": \"scenario.ai.condition.if_player_auto_walk_finished\", \"source\": \"ai/ailists.json\", \"target\": \"player.autowalk\", \"opcode\": \"0x0178\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_obj_in_room\", \"kind\": \"scenario.ai.condition.if_obj_in_room\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"scene\": \"scene.glb\", \"target\": \"object.room\", \"opcode\": \"0x00ef\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_player_looking_at_object\", \"kind\": \"scenario.ai.condition.if_player_looking_at_object\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"scene\": \"scene.glb\", \"target\": \"player.view.object\", \"opcode\": \"0x0181\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_target_is_player\", \"kind\": \"scenario.ai.condition.if_target_is_player\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target.type\", \"opcode\": \"0x0183\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_kill\", \"kind\": \"scenario.ai.action.chr_kill\", \"source\": \"ai/ailists.json\", \"target\": \"character.state\", \"opcode\": \"0x01db\" },\n"
			"    { \"id\": \"scenario.ai.action.remove_weapon_from_inventory\", \"kind\": \"scenario.ai.action.remove_weapon_from_inventory\", \"source\": \"ai/ailists.json\", \"target\": \"player.inventory\", \"opcode\": \"0x01dc\" },\n"
			"    { \"id\": \"scenario.ai.action.clear_inventory\", \"kind\": \"scenario.ai.action.clear_inventory\", \"source\": \"ai/ailists.json\", \"target\": \"player.inventory\", \"opcode\": \"0x01ae\" },\n"
			"    { \"id\": \"scenario.ai.action.release_object\", \"kind\": \"scenario.ai.action.release_object\", \"source\": \"ai/ailists.json\", \"target\": \"player.carry_state\", \"opcode\": \"0x01ad\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_grab_object\", \"kind\": \"scenario.ai.action.chr_grab_object\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"scene\": \"scene.glb\", \"target\": \"player.grab_object\", \"opcode\": \"0x01af\" },\n"
			"    { \"id\": \"scenario.ai.action.toggle_p1p2\", \"kind\": \"scenario.ai.action.toggle_p1p2\", \"source\": \"ai/ailists.json\", \"target\": \"player.assignment\", \"opcode\": \"0x01b3\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_set_p1p2\", \"kind\": \"scenario.ai.action.chr_set_p1p2\", \"source\": \"ai/ailists.json\", \"target\": \"player.assignment\", \"opcode\": \"0x01b5\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_set_cloaked\", \"kind\": \"scenario.ai.action.chr_set_cloaked\", \"source\": \"ai/ailists.json\", \"target\": \"chr.cloak\", \"opcode\": \"0x01b7\" },\n"
			"    { \"id\": \"scenario.ai.action.set_autogun_target_team\", \"kind\": \"scenario.ai.action.set_autogun_target_team\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"autogun.target_team\", \"opcode\": \"0x01b8\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_objective_complete\", \"kind\": \"scenario.ai.condition.if_objective_complete\", \"source\": \"ai/ailists.json\", \"mission\": \"mission.graph.json\", \"target\": \"mission.objective_status\", \"opcode\": \"0x0073\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_objective_failed\", \"kind\": \"scenario.ai.condition.if_objective_failed\", \"source\": \"ai/ailists.json\", \"mission\": \"mission.graph.json\", \"target\": \"mission.objective_status\", \"opcode\": \"0x0074\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_all_objectives_complete\", \"kind\": \"scenario.ai.condition.if_all_objectives_complete\", \"source\": \"ai/ailists.json\", \"mission\": \"mission.graph.json\", \"target\": \"mission.objectives_complete\", \"opcode\": \"0x00f7\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_difficulty_less_than\", \"kind\": \"scenario.ai.condition.if_difficulty_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"mission.difficulty\", \"opcode\": \"0x0077\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_difficulty_greater_than\", \"kind\": \"scenario.ai.condition.if_difficulty_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"mission.difficulty\", \"opcode\": \"0x0078\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_stage_timer_less_than\", \"kind\": \"scenario.ai.condition.if_stage_timer_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"mission.stage_timer\", \"opcode\": \"0x0079\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_stage_timer_greater_than\", \"kind\": \"scenario.ai.condition.if_stage_timer_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"mission.stage_timer\", \"opcode\": \"0x007a\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_stage_id_less_than\", \"kind\": \"scenario.ai.condition.if_stage_id_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"mission.stage_id\", \"opcode\": \"0x007b\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_stage_id_greater_than\", \"kind\": \"scenario.ai.condition.if_stage_id_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"mission.stage_id\", \"opcode\": \"0x007c\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_num_players_less_than\", \"kind\": \"scenario.ai.condition.if_num_players_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"game.local_player_count\", \"opcode\": \"0x00ea\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_kill_count_greater_than\", \"kind\": \"scenario.ai.condition.if_kill_count_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"mission.kill_count\", \"opcode\": \"0x00fc\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_num_knocked_out_chrs\", \"kind\": \"scenario.ai.condition.if_num_knocked_out_chrs\", \"source\": \"ai/ailists.json\", \"target\": \"match.knockout_count\", \"opcode\": \"0x01ab\" },\n"
			"    { \"id\": \"scenario.ai.action.kill_bond\", \"kind\": \"scenario.ai.action.kill_bond\", \"source\": \"ai/ailists.json\", \"mission\": \"mission.graph.json\", \"target\": \"player.bond.dead\", \"opcode\": \"0x00fe\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_pouncebits_eq\", \"kind\": \"scenario.ai.condition.if_pouncebits_eq\", \"source\": \"ai/ailists.json\", \"target\": \"chr.pouncebits\", \"opcode\": \"0x01bc\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_training_pc_holographed\", \"kind\": \"scenario.ai.condition.if_training_pc_holographed\", \"source\": \"ai/ailists.json\", \"target\": \"training.pc_hologram\", \"opcode\": \"0x01bd\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_player_using_device\", \"kind\": \"scenario.ai.condition.if_player_using_device\", \"source\": \"ai/ailists.json\", \"target\": \"player.device_state\", \"opcode\": \"0x01be\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_begin_or_end_teleport\", \"kind\": \"scenario.ai.action.chr_begin_or_end_teleport\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"player.teleport_state\", \"opcode\": \"0x01bf\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_teleport_full_white\", \"kind\": \"scenario.ai.condition.if_chr_teleport_full_white\", \"source\": \"ai/ailists.json\", \"target\": \"player.teleport_state\", \"opcode\": \"0x01c0\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_set_cutscene_weapon\", \"kind\": \"scenario.ai.action.chr_set_cutscene_weapon\", \"source\": \"ai/ailists.json\", \"target\": \"chr.cutscene_weapon\", \"opcode\": \"0x01ca\" },\n"
			"    { \"id\": \"scenario.ai.action.fade_screen\", \"kind\": \"scenario.ai.action.fade_screen\", \"source\": \"ai/ailists.json\", \"target\": \"screen.fade\", \"opcode\": \"0x01cb\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_fade_complete\", \"kind\": \"scenario.ai.condition.if_fade_complete\", \"source\": \"ai/ailists.json\", \"target\": \"screen.fade\", \"opcode\": \"0x01cc\" },\n"
			"    { \"id\": \"scenario.ai.action.set_chr_hudpiece_visible\", \"kind\": \"scenario.ai.action.set_chr_hudpiece_visible\", \"source\": \"ai/ailists.json\", \"target\": \"chr.hudpiece\", \"opcode\": \"0x01cd\" },\n"
			"    { \"id\": \"scenario.ai.action.set_passive_mode\", \"kind\": \"scenario.ai.action.set_passive_mode\", \"source\": \"ai/ailists.json\", \"target\": \"player.weapon_passive_mode\", \"opcode\": \"0x01ce\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_set_firing_in_cutscene\", \"kind\": \"scenario.ai.action.chr_set_firing_in_cutscene\", \"source\": \"ai/ailists.json\", \"target\": \"chr.weapon_firing\", \"opcode\": \"0x01cf\" },\n"
			"    { \"id\": \"scenario.ai.action.set_portal_flag\", \"kind\": \"scenario.ai.action.set_portal_flag\", \"source\": \"ai/ailists.json\", \"scene\": \"scene.glb\", \"target\": \"portal.flags\", \"opcode\": \"0x01d0\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_music_event_queue_is_empty\", \"kind\": \"scenario.ai.condition.if_music_event_queue_is_empty\", \"source\": \"ai/ailists.json\", \"target\": \"music.event_queue\", \"opcode\": \"0x01dd\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_coop_mode\", \"kind\": \"scenario.ai.condition.if_coop_mode\", \"source\": \"ai/ailists.json\", \"target\": \"game.mode\", \"opcode\": \"0x01de\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than\", \"kind\": \"scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"chr.pad_same_floor_distance\", \"opcode\": \"0x01df\" },\n"
			"    { \"id\": \"scenario.ai.action.remove_references_to_chr\", \"kind\": \"scenario.ai.action.remove_references_to_chr\", \"source\": \"ai/ailists.json\", \"target\": \"chr.references\", \"opcode\": \"0x01e0\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_toggle_model_part\", \"kind\": \"scenario.ai.action.chr_toggle_model_part\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"chr.model_part.visibility\", \"opcode\": \"0x018c\" },\n"
			"    { \"id\": \"scenario.ai.action.obj_set_model_part_visible\", \"kind\": \"scenario.ai.action.obj_set_model_part_visible\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.model_part.visibility\", \"opcode\": \"0x01d1\" },\n"
			"    { \"id\": \"scenario.ai.action.if_obj_health_less_than\", \"kind\": \"scenario.ai.action.if_obj_health_less_than\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.damage\", \"opcode\": \"0x019e\" },\n"
			"    { \"id\": \"scenario.ai.action.set_obj_health\", \"kind\": \"scenario.ai.action.set_obj_health\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.damage\", \"opcode\": \"0x019f\" },\n"
			"    { \"id\": \"scenario.ai.action.set_chr_special_death_animation\", \"kind\": \"scenario.ai.action.set_chr_special_death_animation\", \"source\": \"ai/ailists.json\", \"target\": \"chr.special_death_animation\", \"opcode\": \"0x01a0\" },\n"
			"    { \"id\": \"scenario.ai.action.set_room_to_search\", \"kind\": \"scenario.ai.action.set_room_to_search\", \"source\": \"ai/ailists.json\", \"scene\": \"scene.glb\", \"target\": \"chr.room_to_search\", \"opcode\": \"0x01a1\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_set_hidden_flag\", \"kind\": \"scenario.ai.action.chr_set_hidden_flag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.hidden\", \"opcode\": \"0x011b\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_unset_hidden_flag\", \"kind\": \"scenario.ai.action.chr_unset_hidden_flag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.hidden\", \"opcode\": \"0x011c\" },\n"
			"    { \"id\": \"scenario.ai.action.if_chr_has_hidden_flag\", \"kind\": \"scenario.ai.action.if_chr_has_hidden_flag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.hidden\", \"opcode\": \"0x011d\" },\n"
			"    { \"id\": \"scenario.ai.action.set_savefile_flag\", \"kind\": \"scenario.ai.action.set_savefile_flag\", \"source\": \"ai/ailists.json\", \"target\": \"savefile.flags\", \"opcode\": \"0x0190\" },\n"
			"    { \"id\": \"scenario.ai.action.unset_savefile_flag\", \"kind\": \"scenario.ai.action.unset_savefile_flag\", \"source\": \"ai/ailists.json\", \"target\": \"savefile.flags\", \"opcode\": \"0x0191\" },\n"
			"    { \"id\": \"scenario.ai.action.if_savefile_flag_set\", \"kind\": \"scenario.ai.action.if_savefile_flag_set\", \"source\": \"ai/ailists.json\", \"target\": \"savefile.flags\", \"opcode\": \"0x0192\" },\n"
			"    { \"id\": \"scenario.ai.action.if_savefile_flag_unset\", \"kind\": \"scenario.ai.action.if_savefile_flag_unset\", \"source\": \"ai/ailists.json\", \"target\": \"savefile.flags\", \"opcode\": \"0x0193\" },\n"
			"    { \"id\": \"scenario.ai.action.restart_timer\", \"kind\": \"scenario.ai.action.restart_timer\", \"source\": \"ai/ailists.json\", \"target\": \"chr.timer\", \"opcode\": \"0x00b6\" },\n"
			"    { \"id\": \"scenario.ai.action.reset_timer\", \"kind\": \"scenario.ai.action.reset_timer\", \"source\": \"ai/ailists.json\", \"target\": \"chr.timer\", \"opcode\": \"0x00b7\" },\n"
			"    { \"id\": \"scenario.ai.action.pause_timer\", \"kind\": \"scenario.ai.action.pause_timer\", \"source\": \"ai/ailists.json\", \"target\": \"chr.timer\", \"opcode\": \"0x00b8\" },\n"
			"    { \"id\": \"scenario.ai.action.resume_timer\", \"kind\": \"scenario.ai.action.resume_timer\", \"source\": \"ai/ailists.json\", \"target\": \"chr.timer\", \"opcode\": \"0x00b9\" },\n"
			"    { \"id\": \"scenario.ai.action.if_timer_stopped\", \"kind\": \"scenario.ai.action.if_timer_stopped\", \"source\": \"ai/ailists.json\", \"target\": \"chr.timer\", \"opcode\": \"0x00ba\" },\n"
			"    { \"id\": \"scenario.ai.action.if_timer_greater_than_random\", \"kind\": \"scenario.ai.action.if_timer_greater_than_random\", \"source\": \"ai/ailists.json\", \"target\": \"chr.timer\", \"opcode\": \"0x00bb\" },\n"
			"    { \"id\": \"scenario.ai.action.if_timer_less_than\", \"kind\": \"scenario.ai.action.if_timer_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr_or_hovercar.timer\", \"opcode\": \"0x00bc\" },\n"
			"    { \"id\": \"scenario.ai.action.if_timer_greater_than\", \"kind\": \"scenario.ai.action.if_timer_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr_or_hovercar.timer\", \"opcode\": \"0x00bd\" },\n"
			"    { \"id\": \"scenario.ai.action.show_countdown_timer\", \"kind\": \"scenario.ai.action.show_countdown_timer\", \"source\": \"ai/ailists.json\", \"target\": \"hud.countdown\", \"opcode\": \"0x00be\" },\n"
			"    { \"id\": \"scenario.ai.action.hide_countdown_timer\", \"kind\": \"scenario.ai.action.hide_countdown_timer\", \"source\": \"ai/ailists.json\", \"target\": \"hud.countdown\", \"opcode\": \"0x00bf\" },\n"
			"    { \"id\": \"scenario.ai.action.set_countdown_timer\", \"kind\": \"scenario.ai.action.set_countdown_timer\", \"source\": \"ai/ailists.json\", \"target\": \"hud.countdown\", \"opcode\": \"0x00c0\" },\n"
			"    { \"id\": \"scenario.ai.action.stop_countdown_timer\", \"kind\": \"scenario.ai.action.stop_countdown_timer\", \"source\": \"ai/ailists.json\", \"target\": \"hud.countdown\", \"opcode\": \"0x00c1\" },\n"
			"    { \"id\": \"scenario.ai.action.start_countdown_timer\", \"kind\": \"scenario.ai.action.start_countdown_timer\", \"source\": \"ai/ailists.json\", \"target\": \"hud.countdown\", \"opcode\": \"0x00c2\" },\n"
			"    { \"id\": \"scenario.ai.action.if_countdown_timer_stopped\", \"kind\": \"scenario.ai.action.if_countdown_timer_stopped\", \"source\": \"ai/ailists.json\", \"target\": \"hud.countdown\", \"opcode\": \"0x00c3\" },\n"
			"    { \"id\": \"scenario.ai.action.if_countdown_timer_less_than\", \"kind\": \"scenario.ai.action.if_countdown_timer_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"hud.countdown\", \"opcode\": \"0x00c4\" },\n"
			"    { \"id\": \"scenario.ai.action.if_countdown_timer_greater_than\", \"kind\": \"scenario.ai.action.if_countdown_timer_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"hud.countdown\", \"opcode\": \"0x00c5\" },\n"
			"    { \"id\": \"scenario.ai.action.set_action\", \"kind\": \"scenario.ai.action.set_action\", \"source\": \"ai/ailists.json\", \"target\": \"chr.myaction\", \"opcode\": \"0x0132\" },\n"
			"    { \"id\": \"scenario.ai.action.set_team_orders\", \"kind\": \"scenario.ai.action.set_team_orders\", \"source\": \"ai/ailists.json\", \"target\": \"squadron.orders\", \"opcode\": \"0x0133\" },\n"
			"    { \"id\": \"scenario.ai.action.retreat\", \"kind\": \"scenario.ai.action.retreat\", \"source\": \"ai/ailists.json\", \"target\": \"chr.navigation\", \"opcode\": \"0x0136\" },\n"
			"    { \"id\": \"scenario.ai.action.find_cover\", \"kind\": \"scenario.ai.action.find_cover\", \"source\": \"ai/ailists.json\", \"covers\": \"navigation/covers.json\", \"target\": \"chr.cover\", \"opcode\": \"0x0121\" },\n"
			"    { \"id\": \"scenario.ai.action.find_cover_within_dist\", \"kind\": \"scenario.ai.action.find_cover_within_dist\", \"source\": \"ai/ailists.json\", \"covers\": \"navigation/covers.json\", \"target\": \"chr.cover\", \"opcode\": \"0x0122\" },\n"
			"    { \"id\": \"scenario.ai.action.find_cover_outside_dist\", \"kind\": \"scenario.ai.action.find_cover_outside_dist\", \"source\": \"ai/ailists.json\", \"covers\": \"navigation/covers.json\", \"target\": \"chr.cover\", \"opcode\": \"0x0123\" },\n"
			"    { \"id\": \"scenario.ai.action.go_to_cover\", \"kind\": \"scenario.ai.action.go_to_cover\", \"source\": \"ai/ailists.json\", \"covers\": \"navigation/covers.json\", \"target\": \"chr.navigation\", \"opcode\": \"0x0124\" },\n"
			"    { \"id\": \"scenario.ai.action.check_cover_out_of_sight\", \"kind\": \"scenario.ai.action.check_cover_out_of_sight\", \"source\": \"ai/ailists.json\", \"covers\": \"navigation/covers.json\", \"target\": \"chr.cover.visibility\", \"opcode\": \"0x0125\" },\n"
			"    { \"id\": \"scenario.ai.action.orbit_target\", \"kind\": \"scenario.ai.action.orbit_target\", \"source\": \"ai/ailists.json\", \"target\": \"chr.navigation\", \"opcode\": \"0x0139\" },\n"
			"    { \"id\": \"scenario.ai.action.set_chr_preset_to_unalerted_teammate\", \"kind\": \"scenario.ai.action.set_chr_preset_to_unalerted_teammate\", \"source\": \"ai/ailists.json\", \"target\": \"chr.chrpreset\", \"opcode\": \"0x013a\" },\n"
			"    { \"id\": \"scenario.ai.action.set_squadron\", \"kind\": \"scenario.ai.action.set_squadron\", \"source\": \"ai/ailists.json\", \"target\": \"chr.squadron\", \"opcode\": \"0x013b\" },\n"
			"    { \"id\": \"scenario.ai.action.face_cover\", \"kind\": \"scenario.ai.action.face_cover\", \"source\": \"ai/ailists.json\", \"covers\": \"navigation/covers.json\", \"target\": \"chr.cover\", \"opcode\": \"0x013c\" },\n"
			"    { \"id\": \"scenario.ai.action.danger_cover\", \"kind\": \"scenario.ai.action.danger_cover\", \"source\": \"ai/ailists.json\", \"covers\": \"navigation/covers.json\", \"target\": \"chr.cover\", \"opcode\": \"0x013e\" },\n"
			"    { \"id\": \"scenario.ai.action.release_cover\", \"kind\": \"scenario.ai.action.release_cover\", \"source\": \"ai/ailists.json\", \"covers\": \"navigation/covers.json\", \"target\": \"chr.cover\", \"opcode\": \"0x012f\" },\n"
			"    { \"id\": \"scenario.ai.action.rebuild_teams\", \"kind\": \"scenario.ai.action.rebuild_teams\", \"source\": \"ai/ailists.json\", \"target\": \"team.index\", \"opcode\": \"0x0145\" },\n"
			"    { \"id\": \"scenario.ai.action.rebuild_squadrons\", \"kind\": \"scenario.ai.action.rebuild_squadrons\", \"source\": \"ai/ailists.json\", \"target\": \"squadron.index\", \"opcode\": \"0x0146\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_set_listening\", \"kind\": \"scenario.ai.action.chr_set_listening\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.listening\", \"opcode\": \"0x0148\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_not_talking\", \"kind\": \"scenario.ai.condition.if_chr_not_talking\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.talk_state\", \"opcode\": \"0x01a7\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_orders\", \"kind\": \"scenario.ai.condition.if_orders\", \"source\": \"ai/ailists.json\", \"target\": \"chr.orders\", \"opcode\": \"0x0134\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_has_orders\", \"kind\": \"scenario.ai.condition.if_has_orders\", \"source\": \"ai/ailists.json\", \"target\": \"chr.orders\", \"opcode\": \"0x0135\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_in_squadron_doing_action\", \"kind\": \"scenario.ai.condition.if_chr_in_squadron_doing_action\", \"source\": \"ai/ailists.json\", \"target\": \"squadron.myaction\", \"opcode\": \"0x0137\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_listening\", \"kind\": \"scenario.ai.condition.if_chr_listening\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.listening\", \"opcode\": \"0x0149\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_not_listening\", \"kind\": \"scenario.ai.condition.if_not_listening\", \"source\": \"ai/ailists.json\", \"target\": \"chr.listening\", \"opcode\": \"0x014b\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_injured_target\", \"kind\": \"scenario.ai.condition.if_chr_injured_target\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.injured_target_latch\", \"opcode\": \"0x0165\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_action\", \"kind\": \"scenario.ai.condition.if_action\", \"source\": \"ai/ailists.json\", \"target\": \"chr.myaction\", \"opcode\": \"0x0166\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_ammo_quantity_less_than\", \"kind\": \"scenario.ai.condition.if_chr_ammo_quantity_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"player.ammo\", \"opcode\": \"0x00eb\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_chr_target\", \"kind\": \"scenario.ai.condition.if_chr_target\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.target\", \"opcode\": \"0x0108\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_compare_chr_presets_team\", \"kind\": \"scenario.ai.condition.if_compare_chr_presets_team\", \"source\": \"ai/ailists.json\", \"target\": \"chr.preset.team\", \"opcode\": \"0x010c\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_human\", \"kind\": \"scenario.ai.condition.if_human\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.race\", \"opcode\": \"0x011e\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_skedar\", \"kind\": \"scenario.ai.condition.if_skedar\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.race\", \"opcode\": \"0x011f\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_prop_preset_blocking_sight_to_target\", \"kind\": \"scenario.ai.condition.if_prop_preset_blocking_sight_to_target\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"chr.proppreset1.line_of_sight\", \"opcode\": \"0x0103\" },\n"
			"    { \"id\": \"scenario.ai.action.remove_object_at_prop_preset\", \"kind\": \"scenario.ai.action.remove_object_at_prop_preset\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"chr.proppreset1\", \"opcode\": \"0x0104\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_prop_preset_height_less_than\", \"kind\": \"scenario.ai.condition.if_prop_preset_height_less_than\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"chr.proppreset1.height\", \"opcode\": \"0x0105\" },\n"
			"    { \"id\": \"scenario.ai.action.set_target\", \"kind\": \"scenario.ai.action.set_target\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target\", \"opcode\": \"0x0106\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_presets_target_is_not_my_target\", \"kind\": \"scenario.ai.condition.if_presets_target_is_not_my_target\", \"source\": \"ai/ailists.json\", \"target\": \"chr.preset.target\", \"opcode\": \"0x0107\" },\n"
			"    { \"id\": \"scenario.ai.action.set_chr_preset_to_chr_near_self\", \"kind\": \"scenario.ai.action.set_chr_preset_to_chr_near_self\", \"source\": \"ai/ailists.json\", \"target\": \"chr.chrpreset1\", \"opcode\": \"0x0109\" },\n"
			"    { \"id\": \"scenario.ai.action.set_chr_preset_to_chr_near_pad\", \"kind\": \"scenario.ai.action.set_chr_preset_to_chr_near_pad\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"chr.chrpreset1\", \"opcode\": \"0x010a\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_dangerous_object_nearby\", \"kind\": \"scenario.ai.condition.if_dangerous_object_nearby\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"chr.danger\", \"opcode\": \"0x013d\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_heli_weapons_armed\", \"kind\": \"scenario.ai.condition.if_heli_weapons_armed\", \"source\": \"ai/ailists.json\", \"target\": \"vehicle.weapons\", \"opcode\": \"0x013f\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_hoverbot_next_step\", \"kind\": \"scenario.ai.condition.if_hoverbot_next_step\", \"source\": \"ai/ailists.json\", \"target\": \"vehicle.nextstep\", \"opcode\": \"0x0140\" },\n"
			"    { \"id\": \"scenario.ai.action.shuffle_investigation_terminals\", \"kind\": \"scenario.ai.action.shuffle_investigation_terminals\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"setup.tags\", \"opcode\": \"0x0141\" },\n"
			"    { \"id\": \"scenario.ai.action.set_pad_preset_to_investigation_terminal\", \"kind\": \"scenario.ai.action.set_pad_preset_to_investigation_terminal\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"target\": \"chr.padpreset1\", \"opcode\": \"0x0142\" },\n"
			"    { \"id\": \"scenario.ai.action.heli_arm_weapons\", \"kind\": \"scenario.ai.action.heli_arm_weapons\", \"source\": \"ai/ailists.json\", \"target\": \"vehicle.weapons\", \"opcode\": \"0x0143\" },\n"
			"    { \"id\": \"scenario.ai.action.heli_unarm_weapons\", \"kind\": \"scenario.ai.action.heli_unarm_weapons\", \"source\": \"ai/ailists.json\", \"target\": \"vehicle.weapons\", \"opcode\": \"0x0144\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_safety2_less_than\", \"kind\": \"scenario.ai.condition.if_safety2_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.safety.weapon_support\", \"opcode\": \"0x0120\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_player_using_cmp_or_ar34\", \"kind\": \"scenario.ai.condition.if_player_using_cmp_or_ar34\", \"source\": \"ai/ailists.json\", \"target\": \"player.weapon\", \"opcode\": \"0x0126\" },\n"
			"    { \"id\": \"scenario.ai.condition.detect_enemy_on_same_floor\", \"kind\": \"scenario.ai.condition.detect_enemy_on_same_floor\", \"source\": \"ai/ailists.json\", \"scene\": \"scene.glb\", \"target\": \"chr.target.scan_same_floor\", \"opcode\": \"0x0127\" },\n"
			"    { \"id\": \"scenario.ai.condition.detect_enemy\", \"kind\": \"scenario.ai.condition.detect_enemy\", \"source\": \"ai/ailists.json\", \"scene\": \"scene.glb\", \"target\": \"chr.target.scan\", \"opcode\": \"0x0128\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_safety_less_than\", \"kind\": \"scenario.ai.condition.if_safety_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.safety.support\", \"opcode\": \"0x0129\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_target_moving_slowly\", \"kind\": \"scenario.ai.condition.if_target_moving_slowly\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target.motion\", \"opcode\": \"0x012a\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_target_moving_closer\", \"kind\": \"scenario.ai.condition.if_target_moving_closer\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target.motion\", \"opcode\": \"0x012b\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_target_moving_away\", \"kind\": \"scenario.ai.condition.if_target_moving_away\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target.motion\", \"opcode\": \"0x012c\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_squadron_is_dead\", \"kind\": \"scenario.ai.condition.if_squadron_is_dead\", \"source\": \"ai/ailists.json\", \"target\": \"squadron.alive\", \"opcode\": \"0x0147\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_true\", \"kind\": \"scenario.ai.condition.if_true\", \"source\": \"ai/ailists.json\", \"target\": \"ai.branch\", \"opcode\": \"0x014a\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_num_chrs_in_squadron_greater_than\", \"kind\": \"scenario.ai.condition.if_num_chrs_in_squadron_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"squadron.count\", \"opcode\": \"0x0152\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_natural_anim\", \"kind\": \"scenario.ai.condition.if_natural_anim\", \"source\": \"ai/ailists.json\", \"target\": \"chr.naturalanim\", \"opcode\": \"0x0169\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_y\", \"kind\": \"scenario.ai.condition.if_y\", \"source\": \"ai/ailists.json\", \"target\": \"chr.position.y\", \"opcode\": \"0x016a\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_sound_timer\", \"kind\": \"scenario.ai.condition.if_sound_timer\", \"source\": \"ai/ailists.json\", \"target\": \"chr.soundtimer\", \"opcode\": \"0x0186\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_target_y_difference_less_than\", \"kind\": \"scenario.ai.condition.if_target_y_difference_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target.position.y\", \"opcode\": \"0x01a6\" },\n"
			"    { \"id\": \"scenario.ai.action.try_attack_amount\", \"kind\": \"scenario.ai.action.try_attack_amount\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x0184\" },\n"
			"    { \"id\": \"scenario.ai.action.set_chr_preset\", \"kind\": \"scenario.ai.action.set_chr_preset\", \"source\": \"ai/ailists.json\", \"opcode\": \"0x00b0\" },\n"
			"    { \"id\": \"scenario.ai.action.set_chr_target\", \"kind\": \"scenario.ai.action.set_chr_target\", \"source\": \"ai/ailists.json\", \"opcode\": \"0x00b1\" },\n"
			"    { \"id\": \"scenario.ai.action.set_pad_preset\", \"kind\": \"scenario.ai.action.set_pad_preset\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"opcode\": \"0x00b2\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_set_pad_preset\", \"kind\": \"scenario.ai.action.chr_set_pad_preset\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"opcode\": \"0x00b3\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_copy_pad_preset\", \"kind\": \"scenario.ai.action.chr_copy_pad_preset\", \"source\": \"ai/ailists.json\", \"pad\": \"source_chr.padpreset1\", \"opcode\": \"0x00b4\" },\n"
			"    { \"id\": \"scenario.ai.action.show_hudmsg\", \"kind\": \"scenario.ai.action.show_hudmsg\", \"source\": \"ai/ailists.json\", \"target\": \"hud.message\", \"opcode\": \"0x00cb\" },\n"
			"    { \"id\": \"scenario.ai.action.show_hudmsg_top_middle\", \"kind\": \"scenario.ai.action.show_hudmsg_top_middle\", \"source\": \"ai/ailists.json\", \"target\": \"hud.subtitle\", \"opcode\": \"0x00cc\" },\n"
			"    { \"id\": \"scenario.ai.action.show_hudmsg_middle\", \"kind\": \"scenario.ai.action.show_hudmsg_middle\", \"source\": \"ai/ailists.json\", \"target\": \"hud.message.middle\", \"opcode\": \"0x01a4\" },\n"
			"    { \"id\": \"scenario.ai.action.hovercar_begin_path\", \"kind\": \"scenario.ai.action.hovercar_begin_path\", \"source\": \"ai/ailists.json\", \"paths\": \"navigation/paths.json\", \"target\": \"vehicle.path\", \"opcode\": \"0x00d5\" },\n"
			"    { \"id\": \"scenario.ai.action.set_vehicle_speed\", \"kind\": \"scenario.ai.action.set_vehicle_speed\", \"source\": \"ai/ailists.json\", \"target\": \"vehicle.speed\", \"opcode\": \"0x00d6\" },\n"
			"    { \"id\": \"scenario.ai.action.set_rotor_speed\", \"kind\": \"scenario.ai.action.set_rotor_speed\", \"source\": \"ai/ailists.json\", \"target\": \"vehicle.rotor_speed\", \"opcode\": \"0x00d7\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_explosions\", \"kind\": \"scenario.ai.action.chr_explosions\", \"source\": \"ai/ailists.json\", \"target\": \"player.explosions\", \"opcode\": \"0x00fb\" },\n"
			"    { \"id\": \"scenario.ai.action.set_tinted_glass_enabled\", \"kind\": \"scenario.ai.action.set_tinted_glass_enabled\", \"source\": \"ai/ailists.json\", \"target\": \"scene.tinted_glass\", \"opcode\": \"0x0157\" },\n"
			"    { \"id\": \"scenario.ai.action.hovercopter_fire_rocket\", \"kind\": \"scenario.ai.action.hovercopter_fire_rocket\", \"source\": \"ai/ailists.json\", \"target\": \"vehicle.rocket\", \"opcode\": \"0x0167\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_adjust_motion_blur\", \"kind\": \"scenario.ai.action.chr_adjust_motion_blur\", \"source\": \"ai/ailists.json\", \"target\": \"chr.motion_blur\", \"opcode\": \"0x016d\" },\n"
			"    { \"id\": \"scenario.ai.action.punch_or_kick\", \"kind\": \"scenario.ai.action.punch_or_kick\", \"source\": \"ai/ailists.json\", \"target\": \"chr.melee\", \"opcode\": \"0x0182\" },\n"
			"    { \"id\": \"scenario.ai.action.set_target_to_eyespy_if_in_sight\", \"kind\": \"scenario.ai.action.set_target_to_eyespy_if_in_sight\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target.eyespy\", \"opcode\": \"0x0187\" },\n"
			"    { \"id\": \"scenario.ai.action.mini_skedar_try_pounce\", \"kind\": \"scenario.ai.action.mini_skedar_try_pounce\", \"source\": \"ai/ailists.json\", \"target\": \"chr.pounce\", \"opcode\": \"0x018e\" },\n"
			"    { \"id\": \"scenario.ai.condition.if_object_distance_to_pad_less_than\", \"kind\": \"scenario.ai.condition.if_object_distance_to_pad_less_than\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"target\": \"object.pad_distance\", \"opcode\": \"0x018f\" },\n"
			"    { \"id\": \"scenario.ai.action.avoid\", \"kind\": \"scenario.ai.action.avoid\", \"source\": \"ai/ailists.json\", \"target\": \"chr.avoidance\", \"opcode\": \"0x01c5\" },\n"
			"    { \"id\": \"scenario.ai.action.title_init_mode\", \"kind\": \"scenario.ai.action.title_init_mode\", \"source\": \"ai/ailists.json\", \"target\": \"title.mode\", \"opcode\": \"0x01c8\" },\n"
			"    { \"id\": \"scenario.ai.action.try_exit_title\", \"kind\": \"scenario.ai.action.try_exit_title\", \"source\": \"ai/ailists.json\", \"target\": \"title.exit\", \"opcode\": \"0x01c9\" },\n"
			"    { \"id\": \"scenario.ai.action.chr_emit_sparks\", \"kind\": \"scenario.ai.action.chr_emit_sparks\", \"source\": \"ai/ailists.json\", \"target\": \"chr.sparks\", \"opcode\": \"0x01d2\" },\n"
			"    { \"id\": \"scenario.ai.action.set_dr_caroll_images\", \"kind\": \"scenario.ai.action.set_dr_caroll_images\", \"source\": \"ai/ailists.json\", \"target\": \"chr.dr_caroll_images\", \"opcode\": \"0x01d3\" },\n"
			"    { \"id\": \"scenario.ai.action.say_quip\", \"kind\": \"scenario.ai.action.say_quip\", \"source\": \"ai/ailists.json\", \"target\": \"chr.quip\", \"opcode\": \"0x0130\" },\n"
			"    { \"id\": \"scenario.ai.action.say_ci_staff_quip\", \"kind\": \"scenario.ai.action.say_ci_staff_quip\", \"source\": \"ai/ailists.json\", \"target\": \"chr.ci_staff_quip\", \"opcode\": \"0x01a2\" },\n"
			"    { \"id\": \"scenario.ai.action.shuffle_ruins_pillars\", \"kind\": \"scenario.ai.action.shuffle_ruins_pillars\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"setup.tags\", \"opcode\": \"0x01b1\" },\n"
			"    { \"id\": \"scenario.ai.action.shuffle_pelagic_switches\", \"kind\": \"scenario.ai.action.shuffle_pelagic_switches\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"setup.tags\", \"opcode\": \"0x01b9\" }%s\n",
			scenario_id, kind ? kind : "scenario",
			(unsigned)portal_count,
			(unsigned)pad_count, (unsigned)path_count,
			(unsigned)ai_list_count,
			scenario_id, (unsigned)pad_count, (unsigned)pad_count,
			pad_count > 0 ? "," : "") != 0) {
		return -1;
	}

	for (u32 i = 0; i < pad_count; i++) {
		if (s_textbufAppendf(level_graph_json,
				"    { \"id\": \"trigger.volume.%04u\", \"kind\": \"scenario.trigger.volume.source\", \"table\": \"volumes.json\", \"volume\": \"volume_pad_%04u\", \"pad\": \"pad_%04u\" }%s\n",
				(unsigned)i, (unsigned)i, (unsigned)i,
				i + 1 < pad_count ? "," : "") != 0) {
			return -1;
		}
	}

	if (ai_command_nodes_json->data &&
			s_textbufAppend(level_graph_json,
				ai_command_nodes_json->data) != 0) {
		return -1;
	}

	if (s_textbufAppendf(level_graph_json,
			"  ],\n"
			"  \"links\": [\n"
			"    { \"from\": \"scenario.load\", \"to\": \"source.scene\" },\n"
			"    { \"from\": \"source.scene\", \"to\": \"collision.generate\" },\n"
			"    { \"from\": \"collision.generate\", \"to\": \"navigation.generate\" },\n"
			"    { \"from\": \"scenario.load\", \"to\": \"scenario.pads\" },\n"
			"    { \"from\": \"navigation.generate\", \"to\": \"navigation.paths\" },\n"
			"    { \"from\": \"scenario.load\", \"to\": \"scenario.ai.lists\" },\n"
			"    { \"from\": \"scenario.load\", \"to\": \"scenario.global.settings\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.load\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_list\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_return_list\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_shot_list\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.return_list\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.stop\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.kneel\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.surrender\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.fade_out\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.remove_chr\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_sidestep\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_jump_out\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_run_sideways\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_attack_walk\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_attack_run\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_attack_roll\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_attack_stand\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_attack_kneel\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_attack_lie\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_attack_locked\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_attacking\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_modify_attack\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.face_entity\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.apply_gset_damage\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_damage_chr\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.consider_grenade_throw\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.drop_item\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_do_animation\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.be_surprised_one_hand\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.be_surprised_look_around\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.be_surprised_surrender\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.random\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_random_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_random_greater_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_punch_dodge_list\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_shooting_at_me_list\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_dark_room_list\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_player_dead_list\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.jog_to_pad\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.go_to_pad_preset\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.walk_to_pad\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.run_to_pad\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_path\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.start_patrol\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_patrolling\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_start_alarm\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.activate_alarm\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.deactivate_alarm\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_run_from_target\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_jog_to_target_prop\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_walk_to_target_prop\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_run_to_target_prop\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_go_to_cover_prop\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_jog_to_chr\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_walk_to_chr\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_run_to_chr\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_can_hear_alarm\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_alarm_active\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_gas_active\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_hears_target\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_saw_injury\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_saw_death\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_los_to_target\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_los_to_attack_target\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_nearly_in_sight\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_nearly_in_targets_sight\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_pad_preset_to_pad_on_route_to_target\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_saw_target_recently\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_heard_target_recently\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_los_to_chr\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_never_been_on_screen\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_on_screen\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_in_on_screen_room\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_room_is_on_screen\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_aiming_at_me\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_near_miss\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_sees_suspicious_item\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_in_fov_left\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_check_fov_with_target\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_out_of_fov_left\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_in_fov\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_out_of_fov\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_to_target_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_to_target_greater_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_distance_to_pad_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_distance_to_pad_greater_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_to_chr_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_to_chr_greater_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_any_chr_near_self\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_from_target_to_pad_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_from_target_to_pad_greater_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_in_room\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_in_room\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_has_object\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_weapon_thrown\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_weapon_thrown_on_object\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_has_weapon_equipped\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_gun_unclaimed\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_object_healthy\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_activated_object\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.obj_interact\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.destroy_object\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.drop_object_from_chr\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_drop_items\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_drop_weapon\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.give_object_to_chr\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.object_move_to_pad\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_waypoint_within_quadrant\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_pad_preset_to_target_quadrant\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_chr_distance_to_pad_less_than\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_chr_distance_to_pad_greater_than\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_distance_from_target_to_pad_less_than\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_distance_from_target_to_pad_greater_than\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_chr_in_room\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_target_in_room\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_chr_has_object\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_weapon_thrown_on_object\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_gun_unclaimed\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_object_healthy\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.object_move_to_pad\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_chr_activated_object\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.obj_interact\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.destroy_object\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.drop_object_from_chr\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.chr_drop_items\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.chr_drop_weapon\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.give_object_to_chr\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.object_move_to_pad\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_waypoint_within_quadrant\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.set_pad_preset_to_target_quadrant\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_morale\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.add_morale\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_add_morale\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.subtract_morale\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_alertness\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.add_alertness\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_add_alertness\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.subtract_alertness\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_num_arghs_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_num_arghs_greater_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_num_close_arghs_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_num_close_arghs_greater_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_health_greater_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_health_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_shield_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_shield_greater_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_injured\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_shield_damaged\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_morale_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_morale_less_than_random\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_alertness\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_alertness_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_alertness_less_than_random\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_idle\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_stopped\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_dead\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_death_animation_finished\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_knocked_out\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_can_see_target\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.increase_squadron_alertness\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_hear_distance\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_view_distance\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_grenade_probability\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chr_num\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_max_damage\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.add_health\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_shield\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_reaction_speed\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_recovery_speed\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_accuracy\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_dodge_rating\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_unarmed_dodge_rating\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.unset_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_has_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_unset_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_chr_has_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_stage_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.unset_stage_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_stage_flag_eq\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chrflag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.unset_chrflag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_has_chrflag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_chrflag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_unset_chrflag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_chr_has_chrflag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_obj_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.unset_obj_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_obj_has_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.open_door\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.close_door\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_door_state\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_object_is_door\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.lock_door\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.unlock_door\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_door_locked\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_lift_stationary\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.lift_go_to_stop\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_lift_at_stop\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.activate_lift\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_using_lift\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.configure_rain\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.configure_snow\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.switch_to_alt_sky\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_wind_speed\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_lights\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_room_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.show_cutscene_chrs\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.configure_environment\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_to_target2_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_to_target2_greater_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.speak\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_sound\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.assign_sound\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.audio_mute_channel\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_channel_free\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_object_sound_volume\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_object_sound_volume_by_distance\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_object_sound_playing\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_repeating_sound_from_object\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_sound_from_entity\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_repeating_sound_from_pad\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_object_sound_volume_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_sound_from_prop\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_temporary_primary_track\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_x_track\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.stop_x_track\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_track_isolated\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_default_tracks\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_cutscene_track\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.stop_cutscene_track\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_temporary_track\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.stop_ambient_track\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_draw_weapon\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_draw_weapon_in_cutscene\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_player_force_speed\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_invincible\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_player_is_invincible\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_has_no_gun\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_delete_weapon\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_trigger_shot_list\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.end_level\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.end_cutscene\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.warp_jo_to_pad\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.warp_jo_to_pad\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.warp_jo_to_tag\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.warp_jo_to_tag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.revoke_control\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.grant_control\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.player_fade_in\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.players_fade_out\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_colour_fade_complete\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.prepare_warp_orbit\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.prepare_warp_orbit\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.begin_warp_latch\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_warp_latch_complete\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_camera_animation\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_in_cutscene\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_cutscene_button_pressed\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.reorient_for_cutscene_stop\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.spawn_chr_at_pad\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.spawn_chr_at_pad\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.spawn_chr_at_chr\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_equip_weapon\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_equip_hat\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_obj_image\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.set_obj_image\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.object_do_animation\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.object_do_animation\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_door_open\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.set_door_open\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.duplicate_chr\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.enable_chr\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.disable_chr\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.enable_obj\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.enable_obj\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.disable_obj\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.disable_obj\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_move_to_pad\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.chr_move_to_pad\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_team\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.damage_chr_by_amount\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.do_preset_animation\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_player_chr_portal_distance_less_than\" },\n"
			"    { \"from\": \"source.scene\", \"to\": \"scenario.ai.condition.if_player_chr_portal_distance_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_reposition_valid\" },\n"
			"    { \"from\": \"source.scene\", \"to\": \"scenario.ai.condition.if_chr_reposition_valid\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.do_gun_command\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_to_gun_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.recover_gun\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.do_gun_command\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_distance_to_gun_less_than\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.recover_gun\" },\n"
			"    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.do_gun_command\" },\n"
			"    { \"from\": \"source.scene\", \"to\": \"scenario.ai.condition.if_distance_to_gun_less_than\" },\n"
			"    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.recover_gun\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_copy_properties\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.player_auto_walk\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.player_auto_walk\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_player_auto_walk_finished\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_obj_in_room\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_obj_in_room\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_obj_in_room\" },\n"
			"    { \"from\": \"source.scene\", \"to\": \"scenario.ai.condition.if_obj_in_room\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_player_looking_at_object\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_player_looking_at_object\" },\n"
			"    { \"from\": \"source.scene\", \"to\": \"scenario.ai.condition.if_player_looking_at_object\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_is_player\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_kill\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.remove_weapon_from_inventory\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.clear_inventory\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.release_object\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_grab_object\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.chr_grab_object\" },\n"
			"    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.chr_grab_object\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.toggle_p1p2\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_p1p2\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_cloaked\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_autogun_target_team\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.set_autogun_target_team\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_objective_complete\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_objective_failed\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_all_objectives_complete\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_difficulty_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_difficulty_greater_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_stage_timer_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_stage_timer_greater_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_stage_id_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_stage_id_greater_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_num_players_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_kill_count_greater_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_num_knocked_out_chrs\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.kill_bond\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_pouncebits_eq\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_training_pc_holographed\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_player_using_device\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_begin_or_end_teleport\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.chr_begin_or_end_teleport\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_teleport_full_white\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_cutscene_weapon\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.fade_screen\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_fade_complete\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chr_hudpiece_visible\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_passive_mode\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_firing_in_cutscene\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_portal_flag\" },\n"
			"    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.set_portal_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_music_event_queue_is_empty\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_coop_mode\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.remove_references_to_chr\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_toggle_model_part\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.obj_set_model_part_visible\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_obj_health_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_obj_health\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chr_special_death_animation\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_room_to_search\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_hidden_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_unset_hidden_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_chr_has_hidden_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_savefile_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.unset_savefile_flag\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_savefile_flag_set\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_savefile_flag_unset\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.restart_timer\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.reset_timer\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.pause_timer\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.resume_timer\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_timer_stopped\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_timer_greater_than_random\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_timer_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_timer_greater_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.show_countdown_timer\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.hide_countdown_timer\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_countdown_timer\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.stop_countdown_timer\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.start_countdown_timer\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_countdown_timer_stopped\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_countdown_timer_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_countdown_timer_greater_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.show_hudmsg\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.show_hudmsg_middle\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.show_hudmsg_top_middle\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.hovercar_begin_path\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_vehicle_speed\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_rotor_speed\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_explosions\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_tinted_glass_enabled\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.hovercopter_fire_rocket\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_adjust_motion_blur\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.punch_or_kick\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_target_to_eyespy_if_in_sight\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.mini_skedar_try_pounce\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_object_distance_to_pad_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.avoid\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.title_init_mode\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_exit_title\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_emit_sparks\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_dr_caroll_images\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.say_quip\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.say_ci_staff_quip\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.shuffle_ruins_pillars\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.shuffle_pelagic_switches\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.shuffle_ruins_pillars\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.shuffle_pelagic_switches\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_action\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_team_orders\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.retreat\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.find_cover\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.find_cover_within_dist\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.find_cover_outside_dist\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.go_to_cover\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.check_cover_out_of_sight\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.orbit_target\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chr_preset_to_unalerted_teammate\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_squadron\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.face_cover\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.danger_cover\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.release_cover\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.rebuild_teams\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.rebuild_squadrons\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_listening\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_not_talking\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_orders\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_has_orders\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_in_squadron_doing_action\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_listening\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_not_listening\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_injured_target\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_action\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_ammo_quantity_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_target\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_compare_chr_presets_team\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_human\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_skedar\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_prop_preset_blocking_sight_to_target\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.remove_object_at_prop_preset\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_prop_preset_height_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_target\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_presets_target_is_not_my_target\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chr_preset_to_chr_near_self\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chr_preset_to_chr_near_pad\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_dangerous_object_nearby\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_heli_weapons_armed\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_hoverbot_next_step\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.shuffle_investigation_terminals\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_pad_preset_to_investigation_terminal\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.heli_arm_weapons\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.heli_unarm_weapons\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_safety2_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_player_using_cmp_or_ar34\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.detect_enemy_on_same_floor\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.detect_enemy\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_safety_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_moving_slowly\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_moving_closer\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_moving_away\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_squadron_is_dead\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_true\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_num_chrs_in_squadron_greater_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_natural_anim\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_y\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_sound_timer\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_y_difference_less_than\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_prop_preset_blocking_sight_to_target\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.remove_object_at_prop_preset\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_prop_preset_height_less_than\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.set_chr_preset_to_chr_near_pad\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_dangerous_object_nearby\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.shuffle_investigation_terminals\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.set_pad_preset_to_investigation_terminal\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.set_pad_preset_to_investigation_terminal\" },\n"
			"    { \"from\": \"source.scene\", \"to\": \"scenario.ai.condition.detect_enemy_on_same_floor\" },\n"
			"    { \"from\": \"source.scene\", \"to\": \"scenario.ai.condition.detect_enemy\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_object_distance_to_pad_less_than\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_object_distance_to_pad_less_than\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_attack_amount\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chr_preset\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chr_target\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_pad_preset\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_pad_preset\" },\n"
			"    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_copy_pad_preset\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.jog_to_pad\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.go_to_pad_preset\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.walk_to_pad\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.run_to_pad\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.set_pad_preset_to_pad_on_route_to_target\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.set_pad_preset\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.chr_set_pad_preset\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.try_start_alarm\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.set_lights\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.if_lift_stationary\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.lift_go_to_stop\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.if_lift_at_stop\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.activate_lift\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.if_using_lift\" },\n"
			"    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_room_is_on_screen\" },\n"
			"    { \"from\": \"scenario.global.settings\", \"to\": \"scenario.ai.action.configure_rain\" },\n"
			"    { \"from\": \"scenario.global.settings\", \"to\": \"scenario.ai.action.configure_snow\" },\n"
			"    { \"from\": \"scenario.global.settings\", \"to\": \"scenario.ai.action.configure_environment\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.chr_toggle_model_part\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.obj_set_model_part_visible\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.if_obj_health_less_than\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.set_obj_health\" },\n"
			"    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.set_room_to_search\" },\n"
			"    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.set_room_flag\" },\n"
			"    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.configure_environment\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.set_obj_flag\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.unset_obj_flag\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.if_obj_has_flag\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.open_door\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.close_door\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.if_door_state\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.if_object_is_door\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.lock_door\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.unlock_door\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.if_door_locked\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.if_lift_stationary\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.lift_go_to_stop\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.if_lift_at_stop\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.activate_lift\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.if_using_lift\" },\n"
			"    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_sees_suspicious_item\" },\n"
			"    { \"from\": \"navigation.paths\", \"to\": \"scenario.ai.action.set_path\" },\n"
			"    { \"from\": \"navigation.paths\", \"to\": \"scenario.ai.action.start_patrol\" },\n"
			"    { \"from\": \"navigation.paths\", \"to\": \"scenario.ai.action.set_pad_preset_to_pad_on_route_to_target\" },\n"
			"    { \"from\": \"navigation.paths\", \"to\": \"scenario.ai.action.hovercar_begin_path\" },\n"
			"    { \"from\": \"navigation.generate\", \"to\": \"scenario.ai.action.find_cover\" },\n"
			"    { \"from\": \"navigation.generate\", \"to\": \"scenario.ai.action.find_cover_within_dist\" },\n"
			"    { \"from\": \"navigation.generate\", \"to\": \"scenario.ai.action.find_cover_outside_dist\" },\n"
			"    { \"from\": \"navigation.generate\", \"to\": \"scenario.ai.action.go_to_cover\" },\n"
			"    { \"from\": \"navigation.generate\", \"to\": \"scenario.ai.action.check_cover_out_of_sight\" },\n"
			"    { \"from\": \"navigation.generate\", \"to\": \"scenario.ai.action.face_cover\" },\n"
			"    { \"from\": \"navigation.generate\", \"to\": \"scenario.ai.action.danger_cover\" },\n"
			"    { \"from\": \"navigation.generate\", \"to\": \"scenario.ai.action.release_cover\" },\n"
			"    { \"from\": \"navigation.generate\", \"to\": \"scenario.ai.condition.if_waypoint_within_quadrant\" },\n"
			"    { \"from\": \"navigation.generate\", \"to\": \"scenario.ai.action.set_pad_preset_to_target_quadrant\" }%s\n",
			pad_count > 0 ? "," : "") != 0) {
		return -1;
	}

	for (u32 i = 0; i < pad_count; i++) {
		if (s_textbufAppendf(level_graph_json,
				"    { \"from\": \"scenario.load\", \"to\": \"trigger.volume.%04u\" }%s\n",
				(unsigned)i, i + 1 < pad_count ? "," : "") != 0) {
			return -1;
		}
	}

	if (ai_command_links_json->data &&
			s_textbufAppend(level_graph_json,
				ai_command_links_json->data) != 0) {
		return -1;
	}

	if (s_textbufAppendf(level_graph_json,
			"  ],\n"
			"  \"counts\": { \"rooms\": %u, \"triangles\": %u, \"pads\": %u, \"volumes\": %u, \"objects\": %u, \"objectives\": %u, \"ai_lists\": %u, \"ai_commands\": %u, \"waypoints\": %u, \"waygroups\": %u, \"covers\": %u, \"paths\": %u }\n"
			"}\n",
			(unsigned)room_count, (unsigned)tri_count,
			(unsigned)pad_count, (unsigned)pad_count,
			(unsigned)object_count,
			(unsigned)objective_count, (unsigned)ai_list_count,
			(unsigned)ai_command_count,
			(unsigned)waypoint_count,
			(unsigned)waygroup_count, (unsigned)cover_count,
			(unsigned)path_count) != 0) {
		return -1;
	}

	if (s_textbufAppendf(collision_meta_json,
			"{\n"
			"  \"schema\": \"pd2.generated.collision.v1\",\n"
			"  \"scenario\": \"%s\",\n"
			"  \"derived_from\": \"collision.obj\",\n"
			"  \"override\": \"collision.obj\",\n"
			"  \"generator\": \"deterministic.collision_obj.v1\",\n"
			"  \"policy\": {\n"
			"    \"default_collidable\": true,\n"
			"    \"material_tags\": true,\n"
			"    \"node_tags\": true,\n"
			"    \"fallback_when_missing_override\": false\n"
			"  }\n"
			"}\n",
			scenario_id) != 0) {
		return -1;
	}

	s_bytesSha256Hex(scene_glb, scene_glb_size, scene_hash);
	s_textbufSha256Hex(collision_obj, collision_hash);
	s_textbufSha256Hex(navigation_ini, navigation_hash);
	s_textbufSha256Hex(portals_json, portals_hash);
	s_textbufSha256Hex(pads_json, pads_hash);
	s_textbufSha256Hex(spawns_json, spawns_hash);
	s_textbufSha256Hex(volumes_json, volumes_hash);
	s_textbufSha256Hex(waypoints_json, waypoints_hash);
	s_textbufSha256Hex(waygroups_json, waygroups_hash);
	s_textbufSha256Hex(covers_json, covers_hash);
	s_textbufSha256Hex(paths_json, paths_hash);

	if (s_textbufAppendf(navmesh_meta_json,
			"{\n"
			"  \"schema\": \"pd2.generated.navmesh.v1\",\n"
			"  \"scenario\": \"%s\",\n"
			"  \"derived_from\": \"scene.glb\",\n"
			"  \"inputs\": [\"scene.glb\", \"collision.obj\", \"navigation.ini\", \"portals.json\", \"pads.json\", \"spawns.json\", \"volumes.json\", \"navigation/waypoints.json\", \"navigation/waygroups.json\", \"navigation/covers.json\", \"navigation/paths.json\"],\n"
			"  \"generator\": \"deterministic.surface_graph.v1\",\n"
			"  \"capabilities\": [\"walk\", \"jump\", \"drop\", \"wall\", \"ceiling\"],\n"
			"  \"source_counts\": { \"pads\": %u, \"volumes\": %u, \"waypoints\": %u, \"waygroups\": %u, \"covers\": %u, \"paths\": %u },\n"
			"  \"source_hashes\": { \"scene.glb\": \"%s\", \"collision.obj\": \"%s\", \"navigation.ini\": \"%s\", \"portals.json\": \"%s\", \"pads.json\": \"%s\", \"spawns.json\": \"%s\", \"volumes.json\": \"%s\", \"navigation/waypoints.json\": \"%s\", \"navigation/waygroups.json\": \"%s\", \"navigation/covers.json\": \"%s\", \"navigation/paths.json\": \"%s\" },\n"
			"  \"cache_only\": true\n"
			"}\n",
			scenario_id, (unsigned)pad_count, (unsigned)pad_count,
			(unsigned)waypoint_count, (unsigned)waygroup_count,
			(unsigned)cover_count, (unsigned)path_count,
			scene_hash, collision_hash, navigation_hash, portals_hash,
			pads_hash, spawns_hash, volumes_hash, waypoints_hash,
			waygroups_hash, covers_hash, paths_hash) != 0) {
		return -1;
	}

	return 0;
}

static u16 s_readBe16(const u8 *p)
{
	return (u16)(((u16)p[0] << 8) | p[1]);
}

static u32 s_readBe32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
		((u32)p[2] << 8) | (u32)p[3];
}

static u32 s_alignTo(u32 value, u32 align)
{
	return (value + align - 1u) & ~(align - 1u);
}

static s32 s_rangeInBuffer(const u8 *base, u32 size, const void *ptr, u32 len)
{
	const u8 *p = (const u8 *)ptr;
	if (!base || !p) return 0;
	if (p < base) return 0;
	if ((uintptr_t)(p - base) > size) return 0;
	if (len > size - (u32)(p - base)) return 0;
	return 1;
}

static void *s_promoteRoomPtr(u8 *base, u32 size, uintptr_t raw, u32 room_ofs)
{
	if (!raw) return NULL;
	if (raw < room_ofs) return NULL;
	u32 off = (u32)(raw - room_ofs);
	if (off >= size) return NULL;
	return base + off;
}

static void s_bgSceneBounds(pdscenario_bgscene_t *scene, f32 x, f32 y, f32 z)
{
	if (!scene->has_bounds) {
		scene->min_x = scene->max_x = x;
		scene->min_y = scene->max_y = y;
		scene->min_z = scene->max_z = z;
		scene->has_bounds = 1;
		return;
	}
	if (x < scene->min_x) scene->min_x = x;
	if (y < scene->min_y) scene->min_y = y;
	if (z < scene->min_z) scene->min_z = z;
	if (x > scene->max_x) scene->max_x = x;
	if (y > scene->max_y) scene->max_y = y;
	if (z > scene->max_z) scene->max_z = z;
}

static s32 s_bgSceneReserveTextures(pdscenario_bgscene_t *scene, u32 add)
{
	if (add > 0xffffffffu - scene->texture_count) return -1;
	u32 need = scene->texture_count + add;
	if (need <= scene->texture_cap) return 0;
	u32 cap = scene->texture_cap ? scene->texture_cap : 64;
	while (cap < need) {
		if (cap > 0x80000000u) return -1;
		cap *= 2;
	}
	pdscenario_bgtexture_t *p = (pdscenario_bgtexture_t *)realloc(
		scene->textures, (size_t)cap * sizeof(*scene->textures));
	if (!p) return -1;
	memset(p + scene->texture_cap, 0,
		(size_t)(cap - scene->texture_cap) * sizeof(*p));
	scene->textures = p;
	scene->texture_cap = cap;
	return 0;
}

static s32 s_bgSceneFindTexture(const pdscenario_bgscene_t *scene, u16 texnum)
{
	for (u32 i = 0; i < scene->texture_count; i++) {
		if (scene->textures[i].texnum == texnum) return (s32)i;
	}
	return -1;
}

static s32 s_bgSceneEnsureTexture(pdscenario_bgscene_t *scene, u16 texnum,
                                  s32 used_by_geometry)
{
	s32 existing = s_bgSceneFindTexture(scene, texnum);
	if (existing >= 0) {
		if (used_by_geometry) scene->textures[existing].used_by_geometry = 1;
		return existing;
	}
	if (s_bgSceneReserveTextures(scene, 1) != 0) return -1;
	pdscenario_bgtexture_t *tex = &scene->textures[scene->texture_count];
	memset(tex, 0, sizeof(*tex));
	tex->texnum = texnum;
	tex->used_by_geometry = used_by_geometry ? 1 : 0;
	snprintf(tex->path, sizeof(tex->path),
		"visual/textures/tex_%04x.tga", (unsigned)texnum);
	scene->texture_count++;
	return (s32)(scene->texture_count - 1u);
}

static s32 s_bgSceneReserveMaterials(pdscenario_bgscene_t *scene, u32 add)
{
	if (add > 0xffffffffu - scene->material_count) return -1;
	u32 need = scene->material_count + add;
	if (need <= scene->material_cap) return 0;
	u32 cap = scene->material_cap ? scene->material_cap : 32;
	while (cap < need) {
		if (cap > 0x80000000u) return -1;
		cap *= 2;
	}
	pdscenario_bgmaterial_t *p = (pdscenario_bgmaterial_t *)realloc(
		scene->materials, (size_t)cap * sizeof(*scene->materials));
	if (!p) return -1;
	memset(p + scene->material_cap, 0,
		(size_t)(cap - scene->material_cap) * sizeof(*p));
	scene->materials = p;
	scene->material_cap = cap;
	return 0;
}

static s32 s_bgMaterialReserveVertices(pdscenario_bgmaterial_t *m, u32 add)
{
	if (!m || add > 0xffffffffu - m->vertex_count) return -1;
	u32 need = m->vertex_count + add;
	if (need <= m->vertex_cap) return 0;
	u32 cap = m->vertex_cap ? m->vertex_cap : 1024;
	while (cap < need) {
		if (cap > 0x80000000u) return -1;
		cap *= 2;
	}
	pdscenario_visual_vertex_t *p =
		(pdscenario_visual_vertex_t *)realloc(m->vertices,
			(size_t)cap * sizeof(*m->vertices));
	if (!p) return -1;
	m->vertices = p;
	m->vertex_cap = cap;
	return 0;
}

static s32 s_bgMaterialAddTri(pdscenario_bgmaterial_t *m,
                              f32 x0, f32 y0, f32 z0, f32 u0, f32 v0,
                              Col c0,
                              f32 x1, f32 y1, f32 z1, f32 u1, f32 v1,
                              Col c1,
                              f32 x2, f32 y2, f32 z2, f32 u2, f32 v2,
                              Col c2,
                              u32 roomnum)
{
	if (!m || s_bgMaterialReserveVertices(m, 3) != 0) return -1;
	u16 room = roomnum > 0xffffu ? 0xffffu : (u16)roomnum;
	m->vertices[m->vertex_count++] =
		(pdscenario_visual_vertex_t){ x0, y0, z0, u0, v0,
			c0.r, c0.g, c0.b, c0.a, room };
	m->vertices[m->vertex_count++] =
		(pdscenario_visual_vertex_t){ x1, y1, z1, u1, v1,
			c1.r, c1.g, c1.b, c1.a, room };
	m->vertices[m->vertex_count++] =
		(pdscenario_visual_vertex_t){ x2, y2, z2, u2, v2,
			c2.r, c2.g, c2.b, c2.a, room };
	return 0;
}

static s32 s_bgSceneEnsureMaterial(pdscenario_bgscene_t *scene, s32 texnum,
                                   s32 texnum2, s32 subcmd, u32 w0, u32 w1)
{
	s32 smode = (s32)((w0 >> 22) & 3u);
	s32 tmode = (s32)((w0 >> 20) & 3u);
	s32 offset = (s32)((w0 >> 18) & 3u);
	s32 shifts = (s32)((w0 >> 14) & 0x0fu);
	s32 shiftt = (s32)((w0 >> 10) & 0x0fu);
	s32 min = (s32)((w1 >> 24) & 0xffu);
	s32 flag = (w0 & 0x200u) ? 1 : 0;

	for (u32 i = 0; i < scene->material_count; i++) {
		pdscenario_bgmaterial_t *m = &scene->materials[i];
		if (m->texnum == texnum && m->texnum2 == texnum2 &&
		    m->subcmd == subcmd && m->w0 == w0 && m->w1 == w1) {
			return (s32)i;
		}
	}

	if (s_bgSceneReserveMaterials(scene, 1) != 0) return -1;
	pdscenario_bgmaterial_t *m = &scene->materials[scene->material_count];
	memset(m, 0, sizeof(*m));
	m->texnum = texnum;
	m->texnum2 = texnum2;
	m->subcmd = subcmd;
	m->w0 = w0;
	m->w1 = w1;
	m->smode = smode;
	m->tmode = tmode;
	m->offset = offset;
	m->shifts = shifts;
	m->shiftt = shiftt;
	m->min = min;
	m->flag = flag;
	if (texnum >= 0 && texnum2 >= 0) {
		snprintf(m->name, sizeof(m->name), "mat_%03u_tex_%04x_%04x_c%d",
			(unsigned)scene->material_count, (unsigned)texnum,
			(unsigned)texnum2, subcmd);
	} else if (texnum >= 0) {
		snprintf(m->name, sizeof(m->name), "mat_%03u_tex_%04x_c%d",
			(unsigned)scene->material_count, (unsigned)texnum, subcmd);
	} else {
		snprintf(m->name, sizeof(m->name), "mat_%03u_untextured",
			(unsigned)scene->material_count);
	}
	if (texnum >= 0) {
		if (s_bgSceneEnsureTexture(scene, (u16)texnum, 1) < 0) return -1;
	}
	if (texnum2 >= 0) {
		if (s_bgSceneEnsureTexture(scene, (u16)texnum2, 1) < 0) return -1;
	}
	scene->material_count++;
	return (s32)(scene->material_count - 1u);
}

static s32 s_bgSceneInit(pdscenario_bgscene_t *scene)
{
	memset(scene, 0, sizeof(*scene));
	scene->next_obj_index = 1;
	scene->current_material = -1;
	if (s_textbufAppend(&scene->obj,
			"# Perfect Dark 2 BG visual scene source intermediate\n"
			"# Converted into the public scene.glb archive entry.\n"
			"mtllib scene.mtl\n") != 0) {
		return -1;
	}
	return s_bgSceneEnsureMaterial(scene, -1, -1, -1, 0, 0);
}

static s32 s_bgSceneAddTri(pdscenario_bgscene_t *scene, s32 material_index,
                           const Vtx *a, const Vtx *b, const Vtx *c,
                           Col ca, Col cb, Col cc,
                           const struct coord *room_pos, u32 roomnum,
                           s32 s0, s32 t0, s32 s1, s32 t1, s32 s2, s32 t2)
{
	if (!scene || !a || !b || !c || !room_pos) return -1;
	if (material_index < 0 || (u32)material_index >= scene->material_count) {
		material_index = 0;
	}
	if (scene->current_material != material_index) {
		if (s_textbufAppendf(&scene->obj, "usemtl %s\n",
				scene->materials[material_index].name) != 0) {
			return -1;
		}
		scene->current_material = material_index;
	}

	f32 x0 = room_pos->x + (f32)a->x;
	f32 y0 = room_pos->y + (f32)a->y;
	f32 z0 = room_pos->z + (f32)a->z;
	f32 x1 = room_pos->x + (f32)b->x;
	f32 y1 = room_pos->y + (f32)b->y;
	f32 z1 = room_pos->z + (f32)b->z;
	f32 x2 = room_pos->x + (f32)c->x;
	f32 y2 = room_pos->y + (f32)c->y;
	f32 z2 = room_pos->z + (f32)c->z;
	f32 u0 = (f32)s0 / 32.0f;
	f32 v0 = 1.0f - ((f32)t0 / 32.0f);
	f32 u1 = (f32)s1 / 32.0f;
	f32 v1 = 1.0f - ((f32)t1 / 32.0f);
	f32 u2 = (f32)s2 / 32.0f;
	f32 v2 = 1.0f - ((f32)t2 / 32.0f);
	u32 i0 = scene->next_obj_index++;
	u32 i1 = scene->next_obj_index++;
	u32 i2 = scene->next_obj_index++;

	if (s_textbufAppendf(&scene->obj,
			"v %.6f %.6f %.6f\n"
			"v %.6f %.6f %.6f\n"
			"v %.6f %.6f %.6f\n"
			"vt %.6f %.6f\n"
			"vt %.6f %.6f\n"
			"vt %.6f %.6f\n"
			"f %u/%u %u/%u %u/%u\n",
			(double)x0, (double)y0, (double)z0,
			(double)x1, (double)y1, (double)z1,
			(double)x2, (double)y2, (double)z2,
			(double)u0, (double)v0,
			(double)u1, (double)v1,
			(double)u2, (double)v2,
			i0, i0, i1, i1, i2, i2) != 0) {
		return -1;
	}
	if (s_bgMaterialAddTri(&scene->materials[material_index],
			x0, y0, z0, u0, v0, ca,
			x1, y1, z1, u1, v1, cb,
			x2, y2, z2, u2, v2, cc, roomnum) != 0) {
		return -1;
	}

	s_bgSceneBounds(scene, x0, y0, z0);
	s_bgSceneBounds(scene, x1, y1, z1);
	s_bgSceneBounds(scene, x2, y2, z2);
	scene->materials[material_index].tri_count++;
	scene->tri_count++;
	return 0;
}

static s32 s_tgaFromRgba(const u8 *rgba, u32 width, u32 height,
                         u8 **out_data, u32 *out_size)
{
	if (out_data) *out_data = NULL;
	if (out_size) *out_size = 0;
	if (!rgba || !width || !height || width > 8192 || height > 8192) return -1;
	if (width > (0xffffffffu - 18u) / 4u / height) return -1;
	u32 pixels = width * height;
	u32 size = 18u + pixels * 4u;
	u8 *tga = (u8 *)malloc(size);
	if (!tga) return -1;
	memset(tga, 0, 18);
	tga[2] = 2; /* uncompressed true-colour */
	tga[12] = (u8)(width & 0xff);
	tga[13] = (u8)(width >> 8);
	tga[14] = (u8)(height & 0xff);
	tga[15] = (u8)(height >> 8);
	tga[16] = 32;
	tga[17] = 0x28; /* top-left origin, 8 alpha bits */
	for (u32 i = 0; i < pixels; i++) {
		tga[18u + i * 4u + 0u] = rgba[i * 4u + 2u];
		tga[18u + i * 4u + 1u] = rgba[i * 4u + 1u];
		tga[18u + i * 4u + 2u] = rgba[i * 4u + 0u];
		tga[18u + i * 4u + 3u] = rgba[i * 4u + 3u];
	}
	*out_data = tga;
	*out_size = size;
	return 0;
}

static void s_rgba16ToRgba(u16 p, u8 *out)
{
	u8 r = (u8)((((p >> 11) & 0x1f) * 255u) / 31u);
	u8 g = (u8)((((p >> 6) & 0x1f) * 255u) / 31u);
	u8 b = (u8)((((p >> 1) & 0x1f) * 255u) / 31u);
	out[0] = r;
	out[1] = g;
	out[2] = b;
	out[3] = (p & 1u) ? 255u : 0u;
}

static s32 s_texExportFormat(const struct tex *tex)
{
	if (!tex) return -1;
	if (tex->lutmodeindex != 0) {
		if (tex->depth == G_IM_SIZ_4b) {
			return tex->lutmodeindex == (G_TT_RGBA16 >> G_MDSFT_TEXTLUT)
				? TEXFORMAT_RGBA16_CI4 : TEXFORMAT_IA16_CI4;
		}
		return tex->lutmodeindex == (G_TT_RGBA16 >> G_MDSFT_TEXTLUT)
			? TEXFORMAT_RGBA16_CI8 : TEXFORMAT_IA16_CI8;
	}
	if (tex->gbiformat == G_IM_FMT_RGBA) {
		return tex->depth == G_IM_SIZ_32b ? TEXFORMAT_RGBA32 : TEXFORMAT_RGBA16;
	}
	if (tex->gbiformat == G_IM_FMT_IA) {
		if (tex->depth == G_IM_SIZ_16b) return TEXFORMAT_IA16;
		if (tex->depth == G_IM_SIZ_4b) return TEXFORMAT_IA4;
		return TEXFORMAT_IA8;
	}
	if (tex->gbiformat == G_IM_FMT_I) {
		return tex->depth == G_IM_SIZ_4b ? TEXFORMAT_I4 : TEXFORMAT_I8;
	}
	return -1;
}

static u32 s_texFormatStrideBytes(s32 format, u32 width)
{
	switch (format) {
	case TEXFORMAT_RGBA32:
	case TEXFORMAT_RGB24:
		return s_alignTo(width, 4) * 4u;
	case TEXFORMAT_RGBA16:
	case TEXFORMAT_RGB15:
	case TEXFORMAT_IA16:
		return s_alignTo(width, 4) * 2u;
	case TEXFORMAT_IA8:
	case TEXFORMAT_I8:
	case TEXFORMAT_RGBA16_CI8:
	case TEXFORMAT_IA16_CI8:
		return s_alignTo(width, 8);
	case TEXFORMAT_IA4:
	case TEXFORMAT_I4:
		return s_alignTo(width, 16) >> 1;
	case TEXFORMAT_RGBA16_CI4:
	case TEXFORMAT_IA16_CI4:
		return s_alignTo((width + 1u) >> 1, 8);
	}
	return 0;
}

static u32 s_texFormatImageBytes(s32 format, u32 width, u32 height)
{
	return s_texFormatStrideBytes(format, width) * height;
}

static s32 s_decodeTexToRgba(const struct tex *tex, u8 **out_rgba)
{
	if (out_rgba) *out_rgba = NULL;
	if (!tex || !tex->data || !tex->width || !tex->height) return -1;
	s32 format = s_texExportFormat(tex);
	if (format < 0) return -1;
	u32 width = tex->width;
	u32 height = tex->height;
	if (width > 4096 || height > 4096) return -1;
	u8 *rgba = (u8 *)malloc((size_t)width * height * 4u);
	if (!rgba) return -1;

	u32 stride = s_texFormatStrideBytes(format, width);
	if (stride == 0) {
		free(rgba);
		return -1;
	}

	const u8 *palette = NULL;
	u32 palette_count = 0;
	if (tex->lutmodeindex != 0) {
		u32 lods = tex->numlods ? tex->numlods : 1u;
		u32 pw = width;
		u32 ph = height;
		u32 palette_off = 0;
		for (u32 lod = 0; lod < lods; lod++) {
			palette_off += s_texFormatImageBytes(format, pw, ph);
			if (pw > 1) pw = (pw + 1u) >> 1;
			if (ph > 1) ph = (ph + 1u) >> 1;
		}
		palette = tex->data + palette_off;
		palette_count = (u32)tex->unk0a + 1u;
		if (palette_count > 256u) palette_count = 256u;
	}

	for (u32 y = 0; y < height; y++) {
		const u8 *row = tex->data + y * stride;
		for (u32 x = 0; x < width; x++) {
			u8 *dst = rgba + ((size_t)y * width + x) * 4u;
			switch (format) {
			case TEXFORMAT_RGBA32:
			case TEXFORMAT_RGB24: {
				u32 p = 0;
				memcpy(&p, row + x * 4u, 4);
				dst[0] = (u8)((p >> 24) & 0xffu);
				dst[1] = (u8)((p >> 16) & 0xffu);
				dst[2] = (u8)((p >> 8) & 0xffu);
				dst[3] = format == TEXFORMAT_RGB24 ? 255u : (u8)(p & 0xffu);
				break;
			}
			case TEXFORMAT_RGBA16:
			case TEXFORMAT_RGB15:
				s_rgba16ToRgba(s_readBe16(row + x * 2u), dst);
				if (format == TEXFORMAT_RGB15) dst[3] = 255u;
				break;
			case TEXFORMAT_IA16: {
				u16 p = 0;
				memcpy(&p, row + x * 2u, 2);
				dst[0] = dst[1] = dst[2] = (u8)((p >> 8) & 0xffu);
				dst[3] = (u8)(p & 0xffu);
				break;
			}
			case TEXFORMAT_IA8: {
				u8 p = row[x];
				u8 i = (u8)(((p >> 4) & 0x0fu) * 17u);
				u8 a = (u8)((p & 0x0fu) * 17u);
				dst[0] = dst[1] = dst[2] = i;
				dst[3] = a;
				break;
			}
			case TEXFORMAT_I8:
				dst[0] = dst[1] = dst[2] = row[x];
				dst[3] = 255u;
				break;
			case TEXFORMAT_IA4: {
				u8 p = row[x >> 1];
				u8 n = (x & 1u) ? (p & 0x0fu) : (p >> 4);
				u8 i = (u8)((((n >> 1) & 0x07u) * 255u) / 7u);
				dst[0] = dst[1] = dst[2] = i;
				dst[3] = (n & 1u) ? 255u : 0u;
				break;
			}
			case TEXFORMAT_I4: {
				u8 p = row[x >> 1];
				u8 n = (x & 1u) ? (p & 0x0fu) : (p >> 4);
				dst[0] = dst[1] = dst[2] = (u8)(n * 17u);
				dst[3] = 255u;
				break;
			}
			case TEXFORMAT_RGBA16_CI8:
			case TEXFORMAT_IA16_CI8:
			case TEXFORMAT_RGBA16_CI4:
			case TEXFORMAT_IA16_CI4: {
				u32 idx;
				if (format == TEXFORMAT_RGBA16_CI8 ||
				    format == TEXFORMAT_IA16_CI8) {
					idx = row[x];
				} else {
					u8 p = row[x >> 1];
					idx = (x & 1u) ? (p & 0x0fu) : (p >> 4);
				}
				if (!palette || idx >= palette_count) {
					dst[0] = 255u; dst[1] = 0u; dst[2] = 255u; dst[3] = 255u;
				} else if (format == TEXFORMAT_RGBA16_CI8 ||
				           format == TEXFORMAT_RGBA16_CI4) {
					s_rgba16ToRgba(s_readBe16(palette + idx * 2u), dst);
				} else {
					u16 p = s_readBe16(palette + idx * 2u);
					dst[0] = dst[1] = dst[2] = (u8)((p >> 8) & 0xffu);
					dst[3] = (u8)(p & 0xffu);
				}
				break;
			}
			default:
				dst[0] = 255u; dst[1] = 0u; dst[2] = 255u; dst[3] = 255u;
				break;
			}
		}
	}

	*out_rgba = rgba;
	return 0;
}

static s32 s_rgbaHasNonOpaqueAlpha(const u8 *rgba, u32 width, u32 height)
{
	if (!rgba || !width || !height) return 0;
	for (u32 i = 0; i < width * height; i++) {
		if (rgba[i * 4u + 3u] < 255u) return 1;
	}
	return 0;
}

s32 romExtractDecodeTextureImages(u16 texnum,
                                  u8 **out_tga, u32 *out_tga_size,
                                  u8 **out_png, u32 *out_png_size,
                                  u32 *out_width, u32 *out_height,
                                  s32 *out_has_alpha)
{
	if (out_tga) *out_tga = NULL;
	if (out_tga_size) *out_tga_size = 0;
	if (out_png) *out_png = NULL;
	if (out_png_size) *out_png_size = 0;
	if (out_width) *out_width = 0;
	if (out_height) *out_height = 0;
	if (out_has_alpha) *out_has_alpha = 0;

	u8 *list_data = romdataSegGetData("textureslist");
	u8 *data = romdataSegGetData("texturesdata");
	u32 list_size = romdataSegGetSize("textureslist");
	u32 data_size = romdataSegGetSize("texturesdata");
	if (!list_data || !data || list_size < sizeof(struct texture) * 2u) return -1;
	u32 list_count = list_size / (u32)sizeof(struct texture);
	if ((u32)texnum + 1u >= list_count || (u32)texnum >= NUM_TEXTURES) return -1;
	const struct texture *list = (const struct texture *)list_data;
	u32 thisoffset = list[texnum].dataoffset;
	u32 nextoffset = list[(u32)texnum + 1u].dataoffset;
	if (thisoffset == nextoffset || nextoffset < thisoffset) return -1;
	u32 base = thisoffset & 0xfffffff8u;
	u32 rel = thisoffset & 7u;
	u32 need = s_alignTo((nextoffset - thisoffset) + rel, 16);
	if (base > data_size || need > data_size - base) return -1;
	u8 *compptr = data + base + rel;
	s32 hasloddata = (*compptr & 0x80) >> 7;
	s32 iszlib = (*compptr & 0x40) >> 6;
	s32 numlods = *compptr & 0x3f;
	if (numlods > 5) numlods = 5;
	compptr++;

	const u32 pool_len = 256u * 1024u;
	u8 *pool_mem = (u8 *)calloc(1, pool_len);
	if (!pool_mem) return -1;
	struct texpool pool;
	texInitPool(&pool, pool_mem, (s32)pool_len);
	*(s16 *)pool.leftpos = (s16)texnum;
	pool.leftpos += 8;
	pool.rightpos--;
	struct tex *tex = pool.rightpos;
	memset(tex, 0, sizeof(*tex));
	tex->texturenum = texnum;
	tex->data = pool.leftpos;
	tex->unk0c_03 = false;

	s32 bytesout = iszlib
		? texInflateZlib(compptr, pool.leftpos, hasloddata, numlods, &pool, 0)
		: texInflateNonZlib(compptr, pool.leftpos, hasloddata, numlods, &pool, 0);
	if (bytesout <= 0 || tex->width == 0 || tex->height == 0) {
		free(pool_mem);
		return -1;
	}

	u8 *rgba = NULL;
	if (s_decodeTexToRgba(tex, &rgba) != 0) {
		free(pool_mem);
		return -1;
	}
	s32 has_alpha = s_rgbaHasNonOpaqueAlpha(rgba, tex->width, tex->height);
	u8 *tga = NULL;
	u32 tga_size = 0;
	s32 ok = s_tgaFromRgba(rgba, tex->width, tex->height, &tga, &tga_size);
	if (ok != 0) {
		free(rgba);
		free(pool_mem);
		return -1;
	}
	int png_len = 0;
	u8 *png = stbi_write_png_to_mem(rgba, (int)tex->width * 4,
		(int)tex->width, (int)tex->height, 4, &png_len);
	free(rgba);
	if (!png || png_len <= 0) {
		if (png) free(png);
		free(tga);
		free(pool_mem);
		return -1;
	}
	if (out_width) *out_width = tex->width;
	if (out_height) *out_height = tex->height;
	if (out_has_alpha) *out_has_alpha = has_alpha;
	*out_tga = tga;
	*out_tga_size = tga_size;
	*out_png = png;
	*out_png_size = (u32)png_len;
	free(pool_mem);
	return 0;
}

s32 romExtractTextureSlotIsEmpty(u16 texnum)
{
	u8 *list_data = romdataSegGetData("textureslist");
	u32 list_size = romdataSegGetSize("textureslist");
	if (!list_data || list_size < sizeof(struct texture) * 2u) return 0;
	u32 list_count = list_size / (u32)sizeof(struct texture);
	if ((u32)texnum + 1u >= list_count || (u32)texnum >= NUM_TEXTURES) return 0;
	const struct texture *list = (const struct texture *)list_data;
	return list[texnum].dataoffset == list[(u32)texnum + 1u].dataoffset;
}

static void s_bgSceneDecodeTextures(pdscenario_bgscene_t *scene)
{
	for (u32 i = 0; i < scene->texture_count; i++) {
		pdscenario_bgtexture_t *tex = &scene->textures[i];
		if (tex->decoded || tex->tga) continue;
		if (romExtractDecodeTextureImages(tex->texnum,
				&tex->tga, &tex->tga_size,
				&tex->png, &tex->png_size,
				&tex->width, &tex->height,
				&tex->has_alpha) == 0) {
			tex->decoded = 1;
			scene->decoded_texture_count++;
		} else {
			scene->failed_texture_count++;
		}
	}
}

static const pdscenario_bgtexture_t *s_bgSceneTextureByNum(
	const pdscenario_bgscene_t *scene, s32 texnum)
{
	if (texnum < 0) return NULL;
	s32 idx = s_bgSceneFindTexture(scene, (u16)texnum);
	return idx >= 0 ? &scene->textures[idx] : NULL;
}

static s32 s_bgSceneFinalizeMaterials(pdscenario_bgscene_t *scene)
{
	if (s_textbufAppend(&scene->mtl,
			"# Perfect Dark 2 BG visual material export\n") != 0) {
		return -1;
	}

	for (u32 i = 0; i < scene->material_count; i++) {
		const pdscenario_bgmaterial_t *m = &scene->materials[i];
		const pdscenario_bgtexture_t *tex = s_bgSceneTextureByNum(scene, m->texnum);
		const char *texture_path = (tex && tex->decoded) ? tex->path : "";
		const char *mtl_texture_path = texture_path;
		if (strncmp(mtl_texture_path, "visual/", 7) == 0) {
			mtl_texture_path += 7;
		}
		if (s_textbufAppendf(&scene->mtl,
				"newmtl %s\n"
				"Kd 1.000000 1.000000 1.000000\n"
				"Ka 0.150000 0.150000 0.150000\n"
				"Ks 0.000000 0.000000 0.000000\n",
				m->name) != 0) {
			return -1;
		}
		if (texture_path[0]) {
			if (s_textbufAppendf(&scene->mtl, "map_Kd %s\n",
					mtl_texture_path) != 0) {
				return -1;
			}
		}
		if (s_textbufAppend(&scene->mtl, "\n") != 0) return -1;
	}

	return 0;
}

typedef struct {
	u32 pos_view;
	u32 uv_view;
	u32 runtime_uv_view;
	u32 color_view;
	u32 room_view;
	u32 pos_accessor;
	u32 uv_accessor;
	u32 runtime_uv_accessor;
	u32 color_accessor;
	u32 room_accessor;
	u32 texture_index;
	u32 secondary_texture_index;
	f32 min_x, min_y, min_z;
	f32 max_x, max_y, max_z;
} pdscenario_gltf_material_t;

static s32 s_binbufAppendLe16(pdscenario_binbuf_t *b, u16 value)
{
	u8 tmp[2];
	tmp[0] = (u8)(value & 0xffu);
	tmp[1] = (u8)((value >> 8) & 0xffu);
	return s_binbufAppend(b, tmp, sizeof(tmp));
}

static s32 s_binbufAppendF32(pdscenario_binbuf_t *b, f32 value)
{
	u32 raw;
	memcpy(&raw, &value, sizeof(raw));
	return s_binbufAppendLe32(b, raw);
}

static s32 s_bgMaterialComputeBounds(const pdscenario_bgmaterial_t *m,
                                     pdscenario_gltf_material_t *out)
{
	if (!m || !out || m->vertex_count == 0) return -1;
	out->min_x = out->max_x = m->vertices[0].x;
	out->min_y = out->max_y = m->vertices[0].y;
	out->min_z = out->max_z = m->vertices[0].z;
	for (u32 i = 1; i < m->vertex_count; i++) {
		const pdscenario_visual_vertex_t *v = &m->vertices[i];
		if (v->x < out->min_x) out->min_x = v->x;
		if (v->y < out->min_y) out->min_y = v->y;
		if (v->z < out->min_z) out->min_z = v->z;
		if (v->x > out->max_x) out->max_x = v->x;
		if (v->y > out->max_y) out->max_y = v->y;
		if (v->z > out->max_z) out->max_z = v->z;
	}
	return 0;
}

static s32 s_bgSceneDecodedTextureIndex(const pdscenario_bgscene_t *scene,
                                        s32 texnum)
{
	if (!scene || texnum < 0) return -1;
	s32 idx = 0;
	for (u32 i = 0; i < scene->texture_count; i++) {
		const pdscenario_bgtexture_t *tex = &scene->textures[i];
		if (!tex->decoded || !tex->png || tex->png_size == 0) continue;
		if ((s32)tex->texnum == texnum) return idx;
		idx++;
	}
	return -1;
}

static u32 s_bgSceneDecodedTextureCount(const pdscenario_bgscene_t *scene)
{
	u32 count = 0;
	if (!scene) return 0;
	for (u32 i = 0; i < scene->texture_count; i++) {
		const pdscenario_bgtexture_t *tex = &scene->textures[i];
		if (tex->decoded && tex->png && tex->png_size > 0) count++;
	}
	return count;
}

static u32 s_bgGltfWrapMode(s32 mode)
{
	if (mode & G_TX_CLAMP) return 33071u; /* GL_CLAMP_TO_EDGE */
	if (mode & G_TX_MIRROR) return 33648u; /* GL_MIRRORED_REPEAT */
	return 10497u; /* GL_REPEAT */
}

static const char *s_bgGltfWrapName(s32 mode)
{
	if (mode & G_TX_CLAMP) return "clamp";
	if (mode & G_TX_MIRROR) return "mirror";
	return "repeat";
}

static const char *s_bgMaterialTextureCommandName(s32 subcmd)
{
	switch (subcmd) {
	case 0: return "single_texture";
	case 1: return "dual_texture";
	case 2: return "lod_texture";
	case 3: return "special_texture";
	default: return "unknown";
	}
}

static void s_bgMaterialUvScaleForGlb(const pdscenario_bgscene_t *scene,
                                      const pdscenario_bgmaterial_t *m,
                                      f32 *u_scale, f32 *v_scale)
{
	if (u_scale) *u_scale = 1.0f;
	if (v_scale) *v_scale = 1.0f;
	if (!scene || !m || !u_scale || !v_scale) return;

	const pdscenario_bgtexture_t *tex = s_bgSceneTextureByNum(scene, m->texnum);
	if (!tex || !tex->decoded || tex->width == 0 || tex->height == 0) return;

	f32 s_shift_scale = 1.0f;
	f32 t_shift_scale = 1.0f;
	if (m->shifts != 0) {
		if (m->shifts <= 10) {
			s_shift_scale = 1.0f / (f32)(1 << m->shifts);
		} else {
			s_shift_scale = (f32)(1 << (16 - m->shifts));
		}
	}
	if (m->shiftt != 0) {
		if (m->shiftt <= 10) {
			t_shift_scale = 1.0f / (f32)(1 << m->shiftt);
		} else {
			t_shift_scale = (f32)(1 << (16 - m->shiftt));
		}
	}

	*u_scale = s_shift_scale / (f32)tex->width;
	*v_scale = t_shift_scale / (f32)tex->height;
}

static void s_bgMaterialGlbUv(f32 raw_u, f32 raw_v, f32 u_scale, f32 v_scale,
                              f32 *out_u, f32 *out_v)
{
	if (out_u) *out_u = raw_u * u_scale;
	if (out_v) *out_v = 1.0f - ((1.0f - raw_v) * v_scale);
}

static void s_bgMaterialGlbRuntimeUvBounds(const pdscenario_bgmaterial_t *m,
                                           f32 u_scale, f32 v_scale,
                                           f32 *out_min_u, f32 *out_min_v,
                                           f32 *out_max_u, f32 *out_max_v)
{
	f32 min_u = 0.0f;
	f32 min_v = 0.0f;
	f32 max_u = 1.0f;
	f32 max_v = 1.0f;

	if (m && m->vertex_count > 0) {
		s_bgMaterialGlbUv(m->vertices[0].u, m->vertices[0].v,
			u_scale, v_scale, &min_u, &min_v);
		max_u = min_u;
		max_v = min_v;

		for (u32 i = 1; i < m->vertex_count; i++) {
			f32 u = 0.0f;
			f32 v = 0.0f;
			s_bgMaterialGlbUv(m->vertices[i].u, m->vertices[i].v,
				u_scale, v_scale, &u, &v);
			if (u < min_u) min_u = u;
			if (v < min_v) min_v = v;
			if (u > max_u) max_u = u;
			if (v > max_v) max_v = v;
		}
	}

	if (out_min_u) *out_min_u = min_u;
	if (out_min_v) *out_min_v = min_v;
	if (out_max_u) *out_max_u = max_u;
	if (out_max_v) *out_max_v = max_v;
}

static void s_bgMaterialGlbAuthorUv(f32 runtime_u, f32 runtime_v,
                                    f32 min_u, f32 min_v,
                                    f32 max_u, f32 max_v,
                                    f32 *out_u, f32 *out_v)
{
	f32 span_u = max_u - min_u;
	f32 span_v = max_v - min_v;
	f32 span = span_u > span_v ? span_u : span_v;

	if (span < 1.0f) span = 1.0f;

	if (out_u) *out_u = (runtime_u - min_u) / span;
	if (out_v) *out_v = (runtime_v - min_v) / span;
}

static s32 s_bgSceneBuildSceneGlb(pdscenario_bgscene_t *scene)
{
	if (!scene || scene->tri_count == 0 || scene->material_count == 0) return -1;

	pdscenario_gltf_material_t *mr =
		(pdscenario_gltf_material_t *)calloc(scene->material_count, sizeof(*mr));
	u32 *texture_views =
		(u32 *)calloc(scene->texture_count ? scene->texture_count : 1u,
			sizeof(*texture_views));
	if (!mr || !texture_views) {
		free(mr);
		free(texture_views);
		return -1;
	}
	for (u32 i = 0; i < scene->material_count; i++) {
		mr[i].pos_view = mr[i].uv_view = mr[i].runtime_uv_view =
			mr[i].room_view = 0xffffffffu;
		mr[i].pos_accessor = mr[i].uv_accessor = mr[i].runtime_uv_accessor =
			mr[i].room_accessor = 0xffffffffu;
		mr[i].texture_index = 0xffffffffu;
		mr[i].secondary_texture_index = 0xffffffffu;
	}
	for (u32 i = 0; i < scene->texture_count; i++) {
		texture_views[i] = 0xffffffffu;
	}

	pdscenario_binbuf_t bin = { 0 };
	u32 view_count = 0;
	u32 accessor_count = 0;

	for (u32 i = 0; i < scene->material_count; i++) {
		const pdscenario_bgmaterial_t *m = &scene->materials[i];
		if (m->vertex_count == 0) continue;
		if (s_bgMaterialComputeBounds(m, &mr[i]) != 0) goto fail;
		if (s_binbufPad4(&bin) != 0) goto fail;
		mr[i].pos_view = view_count++;
		mr[i].pos_accessor = accessor_count++;
		for (u32 v = 0; v < m->vertex_count; v++) {
			if (s_binbufAppendF32(&bin, m->vertices[v].x) != 0 ||
			    s_binbufAppendF32(&bin, m->vertices[v].y) != 0 ||
			    s_binbufAppendF32(&bin, m->vertices[v].z) != 0) goto fail;
		}
		if (s_binbufPad4(&bin) != 0) goto fail;
		mr[i].uv_view = view_count++;
		mr[i].uv_accessor = accessor_count++;
		f32 u_scale = 1.0f;
		f32 v_scale = 1.0f;
		f32 min_u = 0.0f;
		f32 min_v = 0.0f;
		f32 max_u = 1.0f;
		f32 max_v = 1.0f;
		s_bgMaterialUvScaleForGlb(scene, m, &u_scale, &v_scale);
		s_bgMaterialGlbRuntimeUvBounds(m, u_scale, v_scale,
			&min_u, &min_v, &max_u, &max_v);
		for (u32 v = 0; v < m->vertex_count; v++) {
			f32 runtime_u = 0.0f;
			f32 runtime_v = 0.0f;
			f32 u = 0.0f;
			f32 vt = 0.0f;
			s_bgMaterialGlbUv(m->vertices[v].u, m->vertices[v].v,
				u_scale, v_scale, &runtime_u, &runtime_v);
			s_bgMaterialGlbAuthorUv(runtime_u, runtime_v,
				min_u, min_v, max_u, max_v, &u, &vt);
			if (s_binbufAppendF32(&bin, u) != 0 ||
			    s_binbufAppendF32(&bin, vt) != 0) goto fail;
		}
		if (s_binbufPad4(&bin) != 0) goto fail;
		mr[i].runtime_uv_view = view_count++;
		mr[i].runtime_uv_accessor = accessor_count++;
		s32 has_sampled_texture = s_bgSceneDecodedTextureIndex(scene, m->texnum) >= 0;
		for (u32 v = 0; v < m->vertex_count; v++) {
			f32 u = 0.0f;
			f32 vt = 0.0f;
			s_bgMaterialGlbUv(m->vertices[v].u, m->vertices[v].v,
				u_scale, v_scale, &u, &vt);
			if (!has_sampled_texture) {
				s_bgMaterialGlbAuthorUv(u, vt,
					min_u, min_v, max_u, max_v, &u, &vt);
			}
			if (s_binbufAppendF32(&bin, u) != 0 ||
			    s_binbufAppendF32(&bin, vt) != 0) goto fail;
		}
		if (s_binbufPad4(&bin) != 0) goto fail;
		mr[i].color_view = view_count++;
		mr[i].color_accessor = accessor_count++;
		for (u32 v = 0; v < m->vertex_count; v++) {
			if (s_binbufAppendF32(&bin,
					(f32)m->vertices[v].r / 255.0f) != 0 ||
			    s_binbufAppendF32(&bin,
					(f32)m->vertices[v].g / 255.0f) != 0 ||
			    s_binbufAppendF32(&bin,
					(f32)m->vertices[v].b / 255.0f) != 0 ||
			    s_binbufAppendF32(&bin,
					(f32)m->vertices[v].a / 255.0f) != 0) {
				goto fail;
			}
		}
		if (s_binbufPad4(&bin) != 0) goto fail;
		mr[i].room_view = view_count++;
		mr[i].room_accessor = accessor_count++;
		for (u32 v = 0; v < m->vertex_count; v++) {
			if (s_binbufAppendLe16(&bin, m->vertices[v].roomnum) != 0) {
				goto fail;
			}
		}
	}

	for (u32 i = 0; i < scene->texture_count; i++) {
		const pdscenario_bgtexture_t *tex = &scene->textures[i];
		if (!tex->decoded || !tex->png || tex->png_size == 0) continue;
		if (s_binbufPad4(&bin) != 0) goto fail;
		texture_views[i] = view_count++;
		if (s_binbufAppend(&bin, tex->png, tex->png_size) != 0) goto fail;
	}
	if (s_binbufPad4(&bin) != 0) goto fail;

	u32 material_texture_count = 0;
	for (u32 i = 0; i < scene->material_count; i++) {
		if (s_bgSceneDecodedTextureIndex(scene,
				scene->materials[i].texnum) >= 0) {
			mr[i].texture_index = material_texture_count++;
		}
		if (scene->materials[i].texnum2 >= 0 &&
				s_bgSceneDecodedTextureIndex(scene,
					scene->materials[i].texnum2) >= 0) {
			mr[i].secondary_texture_index = material_texture_count++;
		}
	}

	pdscenario_textbuf_t json = { 0 };
	if (s_textbufAppend(&json,
			"{\"asset\":{\"version\":\"2.0\","
			"\"generator\":\"Perfect Dark 2 PDSCENARIO scene.glb exporter "
			PDSCENARIO_BG_VISUAL_EXPORT_VERSION "\"},"
			"\"scene\":0,"
			"\"scenes\":[{\"nodes\":[0]}],"
			"\"nodes\":[{\"name\":\"scenario_visual_scene\",\"mesh\":0}],"
			"\"meshes\":[{\"name\":\"scene\",\"primitives\":[") != 0) goto fail_json;

	s32 first = 1;
	for (u32 i = 0; i < scene->material_count; i++) {
		if (scene->materials[i].vertex_count == 0) continue;
		if (!first && s_textbufAppend(&json, ",") != 0) goto fail_json;
		first = 0;
		if (s_textbufAppendf(&json,
				"{\"attributes\":{\"POSITION\":%u,\"TEXCOORD_0\":%u,"
				"\"TEXCOORD_1\":%u,"
				"\"COLOR_0\":%u,"
				"\"_PD_ROOM\":%u},"
				"\"material\":%u,\"mode\":4}",
				(unsigned)mr[i].pos_accessor,
				(unsigned)mr[i].uv_accessor,
				(unsigned)mr[i].runtime_uv_accessor,
				(unsigned)mr[i].color_accessor,
				(unsigned)mr[i].room_accessor,
				(unsigned)i) != 0) goto fail_json;
	}
	if (s_textbufAppend(&json, "]}],\"materials\":[") != 0) goto fail_json;

	for (u32 i = 0; i < scene->material_count; i++) {
		const pdscenario_bgmaterial_t *m = &scene->materials[i];
		const pdscenario_bgtexture_t *tex =
			s_bgSceneTextureByNum(scene, m->texnum);
		const pdscenario_bgtexture_t *tex2 =
			s_bgSceneTextureByNum(scene, m->texnum2);
		const char *primary_image =
			(tex && tex->decoded) ? tex->path : "";
		const char *secondary_image =
			(tex2 && tex2->decoded) ? tex2->path : "";
		if (i && s_textbufAppend(&json, ",") != 0) goto fail_json;
		if (mr[i].texture_index != 0xffffffffu) {
			if (tex && tex->decoded && tex->has_alpha) {
				if (s_textbufAppendf(&json,
						"{\"name\":\"%s\",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.01,"
						"\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":%d,\"texCoord\":0},"
						"\"metallicFactor\":0.0,\"roughnessFactor\":1.0}",
						m->name, (s32)mr[i].texture_index) != 0) goto fail_json;
			} else if (s_textbufAppendf(&json,
					"{\"name\":\"%s\","
					"\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":%d,\"texCoord\":0},"
					"\"metallicFactor\":0.0,\"roughnessFactor\":1.0}",
					m->name, (s32)mr[i].texture_index) != 0) goto fail_json;
		} else {
			if (s_textbufAppendf(&json,
					"{\"name\":\"%s\","
					"\"pbrMetallicRoughness\":{\"baseColorFactor\":[1.0,1.0,1.0,1.0],"
					"\"metallicFactor\":0.0,\"roughnessFactor\":1.0}",
					m->name) != 0) goto fail_json;
		}
		if (s_textbufAppendf(&json,
				",\"extras\":{\"pd2_material\":{\"texture_command\":\"%s\","
				"\"primary_image\":\"%s\",\"secondary_image\":\"%s\","
				"\"wrap_s\":\"%s\",\"wrap_t\":\"%s\","
				"\"offset\":%d,\"shift_s\":%d,\"shift_t\":%d,"
				"\"min_lod\":%d,\"tile_flag\":%d",
				s_bgMaterialTextureCommandName(m->subcmd),
				primary_image, secondary_image,
				s_bgGltfWrapName(m->smode), s_bgGltfWrapName(m->tmode),
				m->offset, m->shifts, m->shiftt, m->min,
				m->flag ? 1 : 0) != 0) goto fail_json;
		if (mr[i].secondary_texture_index != 0xffffffffu) {
			if (s_textbufAppendf(&json,
					",\"secondaryTexture\":{\"index\":%d,\"texCoord\":1}",
					(s32)mr[i].secondary_texture_index) != 0) goto fail_json;
		}
		if (s_textbufAppend(&json, "}}}") != 0) goto fail_json;
	}

	u32 decoded_texture_count = s_bgSceneDecodedTextureCount(scene);
	if (s_textbufAppend(&json, "],\"textures\":[") != 0) goto fail_json;
	for (u32 i = 0, idx = 0; i < scene->material_count; i++) {
		const pdscenario_bgmaterial_t *m = &scene->materials[i];
		s32 image_index = s_bgSceneDecodedTextureIndex(scene, m->texnum);
		if (mr[i].texture_index != 0xffffffffu && image_index >= 0) {
			if (idx && s_textbufAppend(&json, ",") != 0) goto fail_json;
			if (s_textbufAppendf(&json,
					"{\"sampler\":%u,\"source\":%d}",
					(unsigned)idx, image_index) != 0) {
				goto fail_json;
			}
			idx++;
		}
		image_index = s_bgSceneDecodedTextureIndex(scene, m->texnum2);
		if (mr[i].secondary_texture_index != 0xffffffffu && image_index >= 0) {
			if (idx && s_textbufAppend(&json, ",") != 0) goto fail_json;
			if (s_textbufAppendf(&json,
					"{\"sampler\":%u,\"source\":%d}",
					(unsigned)idx, image_index) != 0) {
				goto fail_json;
			}
			idx++;
		}
	}
	if (s_textbufAppend(&json, "],\"images\":[") != 0) goto fail_json;
	for (u32 i = 0, idx = 0; i < scene->texture_count; i++) {
		const pdscenario_bgtexture_t *tex = &scene->textures[i];
		if (!tex->decoded || !tex->png || tex->png_size == 0) continue;
		if (idx && s_textbufAppend(&json, ",") != 0) goto fail_json;
		if (s_textbufAppendf(&json,
				"{\"name\":\"tex_%04x\",\"mimeType\":\"image/png\","
				"\"bufferView\":%u}",
				(unsigned)tex->texnum, (unsigned)texture_views[i]) != 0) {
			goto fail_json;
		}
		idx++;
	}

	if (s_textbufAppend(&json, "],\"samplers\":[") != 0) goto fail_json;
	for (u32 i = 0, idx = 0; i < scene->material_count; i++) {
		const pdscenario_bgmaterial_t *m = &scene->materials[i];
		if (mr[i].texture_index != 0xffffffffu) {
			if (idx && s_textbufAppend(&json, ",") != 0) goto fail_json;
			if (s_textbufAppendf(&json,
					"{\"magFilter\":9729,\"minFilter\":9987,"
					"\"wrapS\":%u,\"wrapT\":%u}",
					(unsigned)s_bgGltfWrapMode(m->smode),
					(unsigned)s_bgGltfWrapMode(m->tmode)) != 0) {
				goto fail_json;
			}
			idx++;
		}
		if (mr[i].secondary_texture_index != 0xffffffffu) {
			if (idx && s_textbufAppend(&json, ",") != 0) goto fail_json;
			if (s_textbufAppendf(&json,
					"{\"magFilter\":9729,\"minFilter\":9987,"
					"\"wrapS\":%u,\"wrapT\":%u}",
					(unsigned)s_bgGltfWrapMode(m->smode),
					(unsigned)s_bgGltfWrapMode(m->tmode)) != 0) {
				goto fail_json;
			}
			idx++;
		}
	}

	if (s_textbufAppendf(&json,
			"],\"buffers\":[{\"byteLength\":%u}],\"bufferViews\":[",
			(unsigned)bin.len) != 0) goto fail_json;

	u32 emitted_view = 0;
	u32 offset = 0;
	for (u32 i = 0; i < scene->material_count; i++) {
		const pdscenario_bgmaterial_t *m = &scene->materials[i];
		if (m->vertex_count == 0) continue;
		u32 pos_len = m->vertex_count * 12u;
		u32 uv_len = m->vertex_count * 8u;
		u32 color_len = m->vertex_count * 16u;
		u32 room_len = m->vertex_count * 2u;
		offset = (offset + 3u) & ~3u;
		if (emitted_view++ && s_textbufAppend(&json, ",") != 0) goto fail_json;
		if (s_textbufAppendf(&json,
				"{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u,\"target\":34962}",
				(unsigned)offset, (unsigned)pos_len) != 0) goto fail_json;
		offset += pos_len;
		offset = (offset + 3u) & ~3u;
		if (s_textbufAppend(&json, ",") != 0) goto fail_json;
		emitted_view++;
		if (s_textbufAppendf(&json,
				"{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u,\"target\":34962}",
				(unsigned)offset, (unsigned)uv_len) != 0) goto fail_json;
		offset += uv_len;
		offset = (offset + 3u) & ~3u;
		if (s_textbufAppend(&json, ",") != 0) goto fail_json;
		emitted_view++;
		if (s_textbufAppendf(&json,
				"{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u,\"target\":34962}",
				(unsigned)offset, (unsigned)uv_len) != 0) goto fail_json;
		offset += uv_len;
		offset = (offset + 3u) & ~3u;
		if (s_textbufAppend(&json, ",") != 0) goto fail_json;
		emitted_view++;
		if (s_textbufAppendf(&json,
				"{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u,\"target\":34962}",
				(unsigned)offset, (unsigned)color_len) != 0) goto fail_json;
		offset += color_len;
		offset = (offset + 3u) & ~3u;
		if (s_textbufAppend(&json, ",") != 0) goto fail_json;
		emitted_view++;
		if (s_textbufAppendf(&json,
				"{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u,\"target\":34962}",
				(unsigned)offset, (unsigned)room_len) != 0) goto fail_json;
		offset += room_len;
	}
	for (u32 i = 0; i < scene->texture_count; i++) {
		const pdscenario_bgtexture_t *tex = &scene->textures[i];
		if (!tex->decoded || !tex->png || tex->png_size == 0) continue;
		offset = (offset + 3u) & ~3u;
		if (emitted_view++ && s_textbufAppend(&json, ",") != 0) goto fail_json;
		if (s_textbufAppendf(&json,
				"{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}",
				(unsigned)offset, (unsigned)tex->png_size) != 0) goto fail_json;
		offset += tex->png_size;
	}

	if (s_textbufAppend(&json, "],\"accessors\":[") != 0) goto fail_json;
	u32 emitted_accessor = 0;
	for (u32 i = 0; i < scene->material_count; i++) {
		const pdscenario_bgmaterial_t *m = &scene->materials[i];
		if (m->vertex_count == 0) continue;
		if (emitted_accessor++ && s_textbufAppend(&json, ",") != 0) goto fail_json;
		if (s_textbufAppendf(&json,
				"{\"bufferView\":%u,\"componentType\":5126,\"count\":%u,"
				"\"type\":\"VEC3\",\"min\":[%.6f,%.6f,%.6f],"
				"\"max\":[%.6f,%.6f,%.6f]}",
				(unsigned)mr[i].pos_view, (unsigned)m->vertex_count,
				(double)mr[i].min_x, (double)mr[i].min_y, (double)mr[i].min_z,
				(double)mr[i].max_x, (double)mr[i].max_y, (double)mr[i].max_z) != 0) {
			goto fail_json;
		}
		if (s_textbufAppend(&json, ",") != 0) goto fail_json;
		emitted_accessor++;
		if (s_textbufAppendf(&json,
				"{\"bufferView\":%u,\"componentType\":5126,\"count\":%u,"
				"\"type\":\"VEC2\"}",
				(unsigned)mr[i].uv_view, (unsigned)m->vertex_count) != 0) {
			goto fail_json;
		}
		if (s_textbufAppend(&json, ",") != 0) goto fail_json;
		emitted_accessor++;
		if (s_textbufAppendf(&json,
				"{\"bufferView\":%u,\"componentType\":5126,\"count\":%u,"
				"\"type\":\"VEC2\"}",
				(unsigned)mr[i].runtime_uv_view, (unsigned)m->vertex_count) != 0) {
			goto fail_json;
		}
		if (s_textbufAppend(&json, ",") != 0) goto fail_json;
		emitted_accessor++;
		if (s_textbufAppendf(&json,
				"{\"bufferView\":%u,\"componentType\":5126,\"count\":%u,"
				"\"type\":\"VEC4\"}",
				(unsigned)mr[i].color_view, (unsigned)m->vertex_count) != 0) {
			goto fail_json;
		}
		if (s_textbufAppend(&json, ",") != 0) goto fail_json;
		emitted_accessor++;
		if (s_textbufAppendf(&json,
				"{\"bufferView\":%u,\"componentType\":5123,\"count\":%u,"
				"\"type\":\"SCALAR\"}",
				(unsigned)mr[i].room_view, (unsigned)m->vertex_count) != 0) {
			goto fail_json;
		}
	}
	if (s_textbufAppend(&json, "]}") != 0) goto fail_json;

	u32 json_padded = (json.len + 3u) & ~3u;
	u32 bin_padded = (bin.len + 3u) & ~3u;
	u32 total_len = 12u + 8u + json_padded + 8u + bin_padded;
	pdscenario_binbuf_t glb = { 0 };
	if (s_binbufAppend(&glb, "glTF", 4) != 0 ||
	    s_binbufAppendLe32(&glb, 2u) != 0 ||
	    s_binbufAppendLe32(&glb, total_len) != 0 ||
	    s_binbufAppendLe32(&glb, json_padded) != 0 ||
	    s_binbufAppendLe32(&glb, 0x4e4f534au) != 0 ||
	    s_binbufAppend(&glb, json.data, json.len) != 0) goto fail_glb;
	while (glb.len < 12u + 8u + json_padded) {
		u8 space = 0x20;
		if (s_binbufAppend(&glb, &space, 1) != 0) goto fail_glb;
	}
	if (s_binbufAppendLe32(&glb, bin_padded) != 0 ||
	    s_binbufAppendLe32(&glb, 0x004e4942u) != 0 ||
	    s_binbufAppend(&glb, bin.data, bin.len) != 0) goto fail_glb;
	while (glb.len < total_len) {
		u8 zero = 0;
		if (s_binbufAppend(&glb, &zero, 1) != 0) goto fail_glb;
	}

	scene->scene_glb = glb.data;
	scene->scene_glb_size = glb.len;
	glb.data = NULL;
	(void)decoded_texture_count;
	s_textbufFree(&json);
	s_binbufFree(&bin);
	free(mr);
	free(texture_views);
	return 0;

fail_glb:
	s_binbufFree(&glb);
fail_json:
	s_textbufFree(&json);
fail:
	s_binbufFree(&bin);
	free(mr);
	free(texture_views);
	return -1;
}

static s32 s_promoteBgRoomPointers(u8 *room, u32 size, u32 room_ofs)
{
	if (!room || size < sizeof(struct roomgfxdata)) return -1;
	struct roomgfxdata *gfx = (struct roomgfxdata *)room;
	gfx->vertices = (Vtx *)s_promoteRoomPtr(room, size, (uintptr_t)gfx->vertices, room_ofs);
	gfx->colours = (Col *)s_promoteRoomPtr(room, size, (uintptr_t)gfx->colours, room_ofs);
	gfx->opablocks = (struct roomblock *)s_promoteRoomPtr(room, size, (uintptr_t)gfx->opablocks, room_ofs);
	gfx->xlublocks = (struct roomblock *)s_promoteRoomPtr(room, size, (uintptr_t)gfx->xlublocks, room_ofs);
	if (!gfx->vertices || !gfx->colours) return -1;

	uintptr_t end = (uintptr_t)gfx->vertices;
	struct roomblock *block = gfx->blocks;
	while (s_rangeInBuffer(room, size, block, (u32)sizeof(*block)) &&
	       (uintptr_t)(block + 1) <= end) {
		if (block->next) {
			block->next = (struct roomblock *)s_promoteRoomPtr(
				room, size, (uintptr_t)block->next, room_ofs);
		}
		if (block->type == ROOMBLOCKTYPE_LEAF) {
			if (block->gdl) {
				block->gdl = (Gfx *)s_promoteRoomPtr(
					room, size, (uintptr_t)block->gdl, room_ofs);
			}
			if (block->vertices) {
				block->vertices = (Vtx *)s_promoteRoomPtr(
					room, size, (uintptr_t)block->vertices, room_ofs);
			}
			if (block->colours) {
				block->colours = (Col *)s_promoteRoomPtr(
					room, size, (uintptr_t)block->colours, room_ofs);
			}
		} else if (block->type == ROOMBLOCKTYPE_PARENT) {
			if (block->child) {
				block->child = (struct roomblock *)s_promoteRoomPtr(
					room, size, (uintptr_t)block->child, room_ofs);
			}
			if (block->unk0c) {
				block->unk0c = (struct coord *)s_promoteRoomPtr(
					room, size, (uintptr_t)block->unk0c, room_ofs);
				if (block->unk0c && (uintptr_t)block->unk0c < end) {
					end = (uintptr_t)block->unk0c;
				}
			}
		} else {
			break;
		}
		block++;
	}
	if ((uintptr_t)gfx->colours > (uintptr_t)gfx->vertices) {
		gfx->numvertices = (s16)(((uintptr_t)gfx->colours -
			(uintptr_t)gfx->vertices) / sizeof(Vtx));
	}
	return 0;
}

static s32 s_bgGdlCommand(const Gfx *cmd)
{
	return (s32)((cmd->words.w0 >> 24) & 0xffu);
}

static s32 s_exportBgTriByIndices(pdscenario_bgscene_t *scene,
                                  s32 material_index,
                                  const Vtx loaded[16],
                                  const Col loaded_colours[16],
                                  const s32 loaded_s[16],
                                  const s32 loaded_t[16],
                                  const u8 valid[16],
                                  u32 a, u32 b, u32 c,
                                  const struct coord *room_pos,
                                  u32 roomnum)
{
	if (a >= 16u || b >= 16u || c >= 16u) return 0;
	if (!valid[a] || !valid[b] || !valid[c]) return 0;
	if (a == b && b == c) return 0;
	return s_bgSceneAddTri(scene, material_index,
		&loaded[a], &loaded[b], &loaded[c],
		loaded_colours[a], loaded_colours[b], loaded_colours[c],
		room_pos, roomnum,
		loaded_s[a], loaded_t[a], loaded_s[b], loaded_t[b],
		loaded_s[c], loaded_t[c]);
}

static s32 s_exportBgGdl(pdscenario_bgscene_t *scene, Gfx *gdl,
                         const u8 *room_base, u32 room_size,
                         const Vtx *vertices, u32 vertex_span,
                         const Col *colours, u32 colour_span,
                         const struct coord *room_pos,
                         u32 roomnum)
{
	if (!gdl || !vertices || !room_pos) return 0;
	if (!s_rangeInBuffer(room_base, room_size, gdl, (u32)sizeof(Gfx))) return 0;
	Vtx loaded[16];
	Col loaded_colours[16];
	s32 loaded_s[16];
	s32 loaded_t[16];
	u8 valid[16];
	const Col white = { .r = 255, .g = 255, .b = 255, .a = 255 };
	const Col *current_colours = colours;
	u32 current_colour_count = colour_span;
	memset(loaded, 0, sizeof(loaded));
	for (u32 i = 0; i < 16u; i++) {
		loaded_colours[i] = white;
	}
	memset(loaded_s, 0, sizeof(loaded_s));
	memset(loaded_t, 0, sizeof(loaded_t));
	memset(valid, 0, sizeof(valid));
	s32 material_index = 0;
	u16 texture_scale_s = 0xffffu;
	u16 texture_scale_t = 0xffffu;
	u32 max_cmds = room_size / (u32)sizeof(Gfx);
	if (max_cmds > 20000u) max_cmds = 20000u;

	for (u32 i = 0; i < max_cmds; i++) {
		Gfx *cmd = &gdl[i];
		if (!s_rangeInBuffer(room_base, room_size, cmd, (u32)sizeof(*cmd))) {
			break;
		}
		s32 op = s_bgGdlCommand(cmd);
		if (op == (u8)G_ENDDL) break;
		if (op == G_NOOP) {
			u32 w0 = (u32)cmd->words.w0;
			u32 w1 = (u32)cmd->words.w1;
			s32 texnum = (s32)(w1 & 0xfffu);
			s32 texnum2 = -1;
			s32 subcmd = (s32)cmd->unkc0.subcmd;
			if (subcmd == 1) texnum2 = (s32)((w1 >> 12) & 0xfffu);
			if (texnum >= 0 && texnum < NUM_TEXTURES) {
				s32 next_mat = s_bgSceneEnsureMaterial(scene, texnum,
					(texnum2 >= 0 && texnum2 < NUM_TEXTURES) ? texnum2 : -1,
					subcmd, w0, w1);
				if (next_mat >= 0) material_index = next_mat;
			}
		} else if (op == G_COL) {
			u32 count = ((u32)cmd->words.w0 & 0xffffu) / (u32)sizeof(Col);
			u32 off = (u32)(UNSEGADDR(cmd->words.w1) & 0x00ffffffu);
			u32 colour_bytes = colour_span * (u32)sizeof(Col);
			if (colours && off <= colour_bytes) {
				u32 index = off / (u32)sizeof(Col);
				current_colours = colours + index;
				current_colour_count = colour_span - index;
				if (count < current_colour_count) {
					current_colour_count = count;
				}
			}
		} else if (op == (u8)G_TEXTURE) {
			u32 w1 = (u32)cmd->words.w1;
			texture_scale_s = (u16)((w1 >> 16) & 0xffffu);
			texture_scale_t = (u16)(w1 & 0xffffu);
		} else if (op == G_VTX) {
			u32 w0 = (u32)cmd->words.w0;
			u32 count = (w0 & 0xffffu) / (u32)sizeof(Vtx);
			u32 dest = (w0 >> 16) & 0x0fu;
			u32 off = (u32)(UNSEGADDR(cmd->words.w1) & 0x00ffffffu);
			if (count == 0 || count > 16u || dest + count > 16u) continue;
			if (off > vertex_span ||
			    count > (vertex_span - off) / (u32)sizeof(Vtx)) {
				continue;
			}
			const Vtx *src = (const Vtx *)((const u8 *)vertices + off);
			for (u32 v = 0; v < count; v++) {
				loaded[dest + v] = src[v];
				u32 colour_index = (u32)src[v].colour >> 2;
				if (current_colours && colour_index < current_colour_count) {
					loaded_colours[dest + v] =
						current_colours[colour_index];
				} else {
					loaded_colours[dest + v] = white;
				}
				loaded_s[dest + v] =
					((s32)src[v].s * (s32)texture_scale_s) >> 16;
				loaded_t[dest + v] =
					((s32)src[v].t * (s32)texture_scale_t) >> 16;
				valid[dest + v] = 1;
			}
		} else if (op == (u8)G_TRI1) {
			u32 w1 = (u32)cmd->words.w1;
			u32 a = ((w1 >> 16) & 0xffu) / 10u;
			u32 b = ((w1 >> 8) & 0xffu) / 10u;
			u32 c = (w1 & 0xffu) / 10u;
			if (s_exportBgTriByIndices(scene, material_index, loaded,
					loaded_colours, loaded_s, loaded_t, valid, a, b, c,
					room_pos, roomnum) != 0) {
				return -1;
			}
		} else if (op == (u8)G_TRI4) {
			u32 w0 = (u32)cmd->words.w0;
			u32 w1 = (u32)cmd->words.w1;
			u32 x1 = (w1 >> 0) & 0x0fu, y1 = (w1 >> 4) & 0x0fu, z1 = (w0 >> 0) & 0x0fu;
			u32 x2 = (w1 >> 8) & 0x0fu, y2 = (w1 >> 12) & 0x0fu, z2 = (w0 >> 4) & 0x0fu;
			u32 x3 = (w1 >> 16) & 0x0fu, y3 = (w1 >> 20) & 0x0fu, z3 = (w0 >> 8) & 0x0fu;
			u32 x4 = (w1 >> 24) & 0x0fu, y4 = (w1 >> 28) & 0x0fu, z4 = (w0 >> 12) & 0x0fu;
			if (s_exportBgTriByIndices(scene, material_index, loaded,
					loaded_colours, loaded_s, loaded_t, valid, x1, y1, z1,
					room_pos, roomnum) != 0 ||
			    s_exportBgTriByIndices(scene, material_index, loaded,
					loaded_colours, loaded_s, loaded_t, valid, x2, y2, z2,
					room_pos, roomnum) != 0 ||
			    s_exportBgTriByIndices(scene, material_index, loaded,
					loaded_colours, loaded_s, loaded_t, valid, x3, y3, z3,
					room_pos, roomnum) != 0 ||
			    s_exportBgTriByIndices(scene, material_index, loaded,
					loaded_colours, loaded_s, loaded_t, valid, x4, y4, z4,
					room_pos, roomnum) != 0) {
				return -1;
			}
		}
	}
	scene->gdl_count++;
	return 0;
}

static s32 s_exportBgRoomBlocks(pdscenario_bgscene_t *scene,
                                struct roomgfxdata *gfx,
                                const u8 *room_base, u32 room_size,
                                const struct coord *room_pos,
                                u32 roomnum)
{
	if (!scene || !gfx || !gfx->vertices || !gfx->colours) return 0;
	uintptr_t end = (uintptr_t)gfx->vertices;
	struct roomblock *block = gfx->blocks;
	while (s_rangeInBuffer(room_base, room_size, block, (u32)sizeof(*block)) &&
	       (uintptr_t)(block + 1) <= end) {
		if (block->type == ROOMBLOCKTYPE_LEAF && block->gdl && block->vertices) {
			if (s_rangeInBuffer(room_base, room_size, block->vertices, (u32)sizeof(Vtx))) {
				u32 vertex_span = 0;
				if (gfx->colours && (uintptr_t)gfx->colours > (uintptr_t)block->vertices) {
					vertex_span = (u32)((uintptr_t)gfx->colours -
						(uintptr_t)block->vertices);
				}
				if (vertex_span > 0) {
					u32 colour_span = 0;
					if (block->colours &&
							s_rangeInBuffer(room_base, room_size,
								block->colours, (u32)sizeof(Col))) {
						colour_span = (u32)(((uintptr_t)room_base + room_size -
							(uintptr_t)block->colours) / sizeof(Col));
					}
					if (s_exportBgGdl(scene, block->gdl, room_base,
							room_size, block->vertices, vertex_span,
							block->colours, colour_span,
							room_pos, roomnum) != 0) {
						return -1;
					}
				}
			}
		} else if (block->type == ROOMBLOCKTYPE_PARENT && block->unk0c &&
		           (uintptr_t)block->unk0c < end) {
			end = (uintptr_t)block->unk0c;
		} else if (block->type != ROOMBLOCKTYPE_LEAF &&
		           block->type != ROOMBLOCKTYPE_PARENT) {
			break;
		}
		block++;
	}
	return 0;
}

static s32 s_loadBgPrimary(const u8 *bg, u32 bg_size, u8 **out_primary,
                           u32 *out_primary_size, struct bgroom **out_rooms,
                           u32 *out_room_count)
{
	if (out_primary) *out_primary = NULL;
	if (out_primary_size) *out_primary_size = 0;
	if (out_rooms) *out_rooms = NULL;
	if (out_room_count) *out_room_count = 0;
	if (!bg || bg_size < 12) return -1;
	u32 primary_infsize = s_readBe32(bg + 0);
	u32 section1_cmpsize = s_readBe32(bg + 4);
	u32 primary_cmpsize = s_readBe32(bg + 8);
	if (!primary_infsize || !primary_cmpsize ||
	    12u + primary_cmpsize > bg_size ||
	    section1_cmpsize < primary_cmpsize ||
	    12u + section1_cmpsize > bg_size) {
		return -1;
	}
	u32 cap = ALIGN16(primary_infsize + BG_INFLATE_SCRATCH_LARGE +
		BG_INFLATE_SCRATCH_HEADER);
	if (cap < primary_infsize) return -1;
	u8 *primary = (u8 *)sysMemZeroAlloc(cap);
	if (!primary) return -1;
	if (rzipIs1173((u8 *)(bg + 12))) {
		u8 scratch[5 * 1024];
		s32 inflated = rzipInflate((u8 *)(bg + 12), primary, scratch);
		if (inflated <= 0) {
			sysMemFree(primary);
			return -1;
		}
	} else {
		memcpy(primary, bg + 12, primary_cmpsize);
	}
	preprocessBgSection1(primary, cap, 0x0f000000);

	uintptr_t *hdr = (uintptr_t *)primary;
	struct bgroom *rooms = (struct bgroom *)s_promoteRoomPtr(primary, cap,
		hdr[1], 0x0f000000);
	if (!rooms) {
		sysMemFree(primary);
		return -1;
	}
	u32 room_count = 0;
	for (u32 i = 1; i < 4096u; i++) {
		if (!s_rangeInBuffer(primary, cap, &rooms[i], (u32)sizeof(rooms[i]))) break;
		if (rooms[i].unk00 == 0) break;
		room_count++;
	}
	if (room_count == 0) {
		sysMemFree(primary);
		return -1;
	}
	*out_primary = primary;
	*out_primary_size = cap;
	*out_rooms = rooms;
	*out_room_count = room_count;
	return 0;
}

static s32 s_buildBgPortalsJson(const u8 *bg, u32 bg_size,
                                pdscenario_textbuf_t *out,
                                u32 *out_portal_count)
{
	u8 *primary = NULL;
	u32 primary_size = 0;
	struct bgroom *rooms = NULL;
	u32 room_count = 0;
	uintptr_t *hdr;
	struct bgportal *portals;
	u8 *portal_base;
	u32 portal_available;
	u32 portal_count = 0;
	u32 emitted_count = 0;
	u32 *offsets = NULL;
	u32 cursor;

	if (out_portal_count) {
		*out_portal_count = 0;
	}
	if (!out || s_textbufAppend(out,
			"{\n"
			"  \"schema\": \"pd2.scenario.portals.v1\",\n"
			"  \"rows\": [\n") != 0) {
		return -1;
	}
	if (!bg || bg_size < 12) {
		return -1;
	}
	if (s_loadBgPrimary(bg, bg_size, &primary, &primary_size,
			&rooms, &room_count) != 0) {
		return -1;
	}
	hdr = (uintptr_t *)primary;
	portals = (struct bgportal *)s_promoteRoomPtr(primary, primary_size,
		hdr[2], 0x0f000000);
	if (!portals) {
		sysMemFree(primary);
		return s_textbufAppend(out,
			"  ]\n"
			"}\n");
	}
	while (portal_count < 4096u &&
			s_rangeInBuffer(primary, primary_size, &portals[portal_count],
				(u32)sizeof(portals[portal_count])) &&
			portals[portal_count].verticesoffset != 0) {
		portal_count++;
	}
	if (portal_count == 0) {
		sysMemFree(primary);
		return s_textbufAppend(out,
			"  ]\n"
			"}\n");
	}
	offsets = (u32 *)calloc((size_t)portal_count, sizeof(*offsets));
	if (!offsets) {
		sysMemFree(primary);
		return -1;
	}
	portal_base = (u8 *)portals;
	portal_available = (u32)((primary + primary_size) - portal_base);
	cursor = portal_count * (u32)sizeof(struct bgportal) +
		(u32)sizeof(struct bgportal);
	for (u32 vertex_index = 1; cursor < portal_available; vertex_index++) {
		struct portalvertices *verts =
			(struct portalvertices *)(portal_base + cursor);
		if (!s_rangeInBuffer(primary, primary_size, verts,
				(u32)sizeof(struct portalvertices)) ||
				verts->count <= 0 || verts->count > 32) {
			break;
		}
		for (u32 i = 0; i < portal_count; i++) {
			if (portals[i].verticesoffset == vertex_index) {
				offsets[i] = cursor;
			}
		}
		cursor += 4u + (u32)verts->count * (u32)sizeof(struct coord);
	}
	for (u32 i = 0; i < portal_count; i++) {
		struct portalvertices *verts;
		if (offsets[i] == 0) {
			continue;
		}
		verts = (struct portalvertices *)(portal_base + offsets[i]);
		if (!s_rangeInBuffer(primary, primary_size, verts,
				(u32)(4u + (u32)verts->count * sizeof(struct coord))) ||
				verts->count < 3 || verts->count > 32) {
			continue;
		}
		if (emitted_count > 0 && s_textbufAppend(out, ",\n") != 0) {
			free(offsets);
			sysMemFree(primary);
			return -1;
		}
		if (s_textbufAppendf(out,
				"    { \"portal_ref\": \"portal_%04u\", \"room_a\": \"room_%d\", \"room_b\": \"room_%d\", \"flags\": %u, \"vertices\": [",
				(unsigned)i, (s32)portals[i].roomnum1,
				(s32)portals[i].roomnum2, (unsigned)portals[i].flags) != 0) {
			free(offsets);
			sysMemFree(primary);
			return -1;
		}
		for (u32 j = 0; j < (u32)verts->count; j++) {
			if (j > 0 && s_textbufAppend(out, ", ") != 0) {
				free(offsets);
				sysMemFree(primary);
				return -1;
			}
			if (s_textbufAppendf(out, "[%.6f, %.6f, %.6f]",
					(double)verts->vertices[j].x,
					(double)verts->vertices[j].y,
					(double)verts->vertices[j].z) != 0) {
				free(offsets);
				sysMemFree(primary);
				return -1;
			}
		}
		if (s_textbufAppend(out, "] }") != 0) {
			free(offsets);
			sysMemFree(primary);
			return -1;
		}
		if (out_portal_count) {
			(*out_portal_count)++;
		}
		emitted_count++;
	}
	free(offsets);
	sysMemFree(primary);
	return s_textbufAppend(out,
		"\n"
		"  ]\n"
		"}\n");
}

static s32 s_exportBgSection2Textures(const u8 *bg, u32 bg_size,
                                      pdscenario_bgscene_t *scene)
{
	if (!bg || bg_size < 16 || !scene) return 0;
	u32 section1_cmpsize = s_readBe32(bg + 4);
	u32 section2start = section1_cmpsize + 12u;
	if (section2start + 4u > bg_size) return 0;
	u16 inf_masked = s_readBe16(bg + section2start);
	u16 cmpsize = s_readBe16(bg + section2start + 2u);
	u32 count = (inf_masked & 0x7fffu) >> 1;
	u32 infsize = s_alignTo((inf_masked & 0x7fffu), 16);
	if (count == 0 || cmpsize == 0 || section2start + 4u + cmpsize > bg_size) return 0;
	u8 *section2 = (u8 *)sysMemZeroAlloc(infsize + 16u);
	if (!section2) return -1;
	if (rzipIs1173((u8 *)(bg + section2start + 4u))) {
		u8 scratch[5 * 1024];
		s32 inflated = rzipInflate((u8 *)(bg + section2start + 4u),
			section2, scratch);
		if (inflated <= 0) {
			sysMemFree(section2);
			return -1;
		}
	} else {
		memcpy(section2, bg + section2start + 4u, cmpsize);
	}
	preprocessBgSection2(section2, count);
	u16 *ids = (u16 *)section2;
	for (u32 i = 0; i < count; i++) {
		if (ids[i] < NUM_TEXTURES) {
			if (s_bgSceneEnsureTexture(scene, ids[i], 0) < 0) {
				sysMemFree(section2);
				return -1;
			}
		}
	}
	sysMemFree(section2);
	return 0;
}

static s32 s_buildBgVisualExports(const u8 *bg, u32 bg_size,
                                  pdscenario_bgscene_t *scene)
{
	if (!bg || !bg_size || !scene) return -1;
	if (s_bgSceneInit(scene) != 0) return -1;

	u8 *primary = NULL;
	u32 primary_size = 0;
	struct bgroom *rooms = NULL;
	u32 room_count = 0;
	if (s_loadBgPrimary(bg, bg_size, &primary, &primary_size,
			&rooms, &room_count) != 0) {
		return -1;
	}

	u32 primary_infsize = s_readBe32(bg + 0);
	u32 primary_cmpsize = s_readBe32(bg + 8);
	s32 file_bias = (s32)primary_infsize - (s32)primary_cmpsize - 12;

	for (u32 roomnum = 1; roomnum < room_count; roomnum++) {
		if (!rooms[roomnum].unk00 || !rooms[roomnum + 1].unk00 ||
		    rooms[roomnum + 1].unk00 <= rooms[roomnum].unk00) {
			continue;
		}
		u32 room_ofs = (u32)rooms[roomnum].unk00;
		u32 next_ofs = (u32)rooms[roomnum + 1].unk00;
		u32 room_span = next_ofs - room_ofs;
		if (room_span > 0x1ffff000u) continue;
		u32 readlen = ALIGN16(room_span);
		s32 fileoffset = (s32)(room_ofs - 0x0f000000u) - file_bias;
		if (fileoffset < 0 || (u32)fileoffset >= bg_size ||
		    readlen > bg_size - (u32)fileoffset) {
			continue;
		}
		u32 room_cap = s_alignTo(room_span * 8u + 4096u, 16);
		if (room_cap < 65536u) room_cap = 65536u;
		u8 *room = (u8 *)sysMemZeroAlloc(room_cap);
		if (!room) {
			sysMemFree(primary);
			return -1;
		}
		const u8 *src = bg + (u32)fileoffset;
		s32 inflatedlen = 0;
		if (rzipIs1173((u8 *)src)) {
			u8 scratch[5 * 1024];
			inflatedlen = rzipInflate((u8 *)src, room, scratch);
		} else {
			u32 copylen = room_span;
			if (copylen > room_cap) copylen = room_cap;
			memcpy(room, src, copylen);
			inflatedlen = (s32)copylen;
		}
		if (inflatedlen <= 0 || (u32)inflatedlen > room_cap / 8u) {
			sysMemFree(room);
			continue;
		}
		u32 new_len = preprocessBgRoom(room, (u32)inflatedlen, room_ofs);
		if (new_len == 0 || new_len > room_cap ||
		    s_promoteBgRoomPointers(room, new_len, room_ofs) != 0) {
			sysMemFree(room);
			continue;
		}
		if (s_textbufAppendf(&scene->obj, "g room_%u\n",
				(unsigned)roomnum) != 0) {
			sysMemFree(room);
			sysMemFree(primary);
			return -1;
		}
		struct roomgfxdata *gfx = (struct roomgfxdata *)room;
		if (s_exportBgRoomBlocks(scene, gfx, room, new_len,
				&rooms[roomnum].pos, roomnum) != 0) {
			sysMemFree(room);
			sysMemFree(primary);
			return -1;
		}
		scene->room_count++;
		sysMemFree(room);
	}

	if (s_exportBgSection2Textures(bg, bg_size, scene) != 0) {
		sysMemFree(primary);
		return -1;
	}
	s_bgSceneDecodeTextures(scene);
	if (s_bgSceneFinalizeMaterials(scene) != 0) {
		sysMemFree(primary);
		return -1;
	}
	if (s_bgSceneBuildSceneGlb(scene) != 0) {
		sysMemFree(primary);
		return -1;
	}
	sysMemFree(primary);
	return scene->tri_count > 0 ? 0 : -1;
}

static s32 s_existingZipArchive(const char *relpath)
{
	u32 size = 0;
	void *bytes = fsFileLoad(relpath, &size);
	if (!bytes) return 0;
	s32 is_zip = size >= 2
		&& ((const u8 *)bytes)[0] == 'P'
		&& ((const u8 *)bytes)[1] == 'K';
	sysMemFree(bytes);
	return is_zip;
}

static s32 s_existingArchiveHasEntry(const char *relpath, const char *entry)
{
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return 0;
	mod_archive_t *arc = modArchiveOpen(full);
	if (!arc) return 0;
	s32 has_entry = modArchiveFindEntry(arc, entry) >= 0;
	modArchiveClose(arc);
	return has_entry;
}

static s32 s_existingArchiveEntryContains(const char *relpath,
                                          const char *entry,
                                          const char *needle)
{
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	s32 found = 0;

	if (!full || !full[0] || !entry || !needle) return 0;

	mod_archive_t *arc = modArchiveOpen(full);
	if (!arc) return 0;

	s32 idx = modArchiveFindEntry(arc, entry);
	if (idx >= 0) {
		u32 size = 0;
		char *bytes = (char *)modArchiveExtractAlloc(arc, idx, &size);
		if (bytes && size > 0) {
			char *text = (char *)malloc((size_t)size + 1u);
			if (text) {
				memcpy(text, bytes, size);
				text[size] = '\0';
				found = strstr(text, needle) != NULL;
				free(text);
			}
		}
		if (bytes) free(bytes);
	}

	modArchiveClose(arc);
	return found;
}

static s32 s_existingArchiveHasEntryPrefix(const char *relpath,
                                           const char *prefix)
{
	if (!relpath || !prefix || !prefix[0]) return 0;
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return 0;
	mod_archive_t *arc = modArchiveOpen(full);
	if (!arc) return 0;
	size_t prefix_len = strlen(prefix);
	s32 count = modArchiveGetEntryCount(arc);
	for (s32 i = 0; i < count; i++) {
		const char *name = modArchiveGetEntryName(arc, i);
		if (name && strncmp(name, prefix, prefix_len) == 0) {
			modArchiveClose(arc);
			return 1;
		}
	}
	modArchiveClose(arc);
	return 0;
}

static s32 s_existingArchiveTextContains(const char *text, u32 size,
                                         const char *needle)
{
	if (!text || !needle || !needle[0]) return 0;
	u32 needle_len = (u32)strlen(needle);
	if (needle_len == 0 || size < needle_len) return 0;
	for (u32 i = 0; i <= size - needle_len; i++) {
		if (memcmp(text + i, needle, needle_len) == 0) return 1;
	}
	return 0;
}

static s32 s_countExistingArchiveJsonRows(const char *text, u32 size,
	const char *schema)
{
	const char *rows_key;
	const char *cursor;
	const char *end;
	s32 rows = 0;
	s32 depth = 0;
	s32 in_string = 0;
	s32 escape = 0;

	if (!text || size == 0 || !schema || !schema[0]) return -1;
	if (!s_existingArchiveTextContains(text, size, schema)) return -1;
	end = text + size;
	rows_key = strstr(text, "\"rows\"");
	if (!rows_key || rows_key >= end) return -1;
	cursor = rows_key + strlen("\"rows\"");
	while (cursor < end && isspace((unsigned char)*cursor)) cursor++;
	if (cursor >= end || *cursor != ':') return -1;
	cursor++;
	while (cursor < end && isspace((unsigned char)*cursor)) cursor++;
	if (cursor >= end || *cursor != '[') return -1;
	cursor++;

	for (; cursor < end; cursor++) {
		char c = *cursor;
		if (in_string) {
			if (escape) {
				escape = 0;
			} else if (c == '\\') {
				escape = 1;
			} else if (c == '"') {
				in_string = 0;
			}
			continue;
		}
		if (c == '"') {
			in_string = 1;
		} else if (c == '{') {
			if (depth == 0) rows++;
			depth++;
		} else if (c == '}') {
			if (depth <= 0) return -1;
			depth--;
		} else if (c == ']' && depth == 0) {
			return rows;
		}
	}
	return -1;
}

static s32 s_existingPdscenarioNavmeshMetadataMatches(
	mod_archive_t *arc, const char *entry, const char *count_key,
	const char *schema, const char *navmesh_text, u32 navmesh_size)
{
	s32 idx = modArchiveFindEntry(arc, entry);
	if (idx < 0) return 0;

	u32 size = 0;
	char *bytes = (char *)modArchiveExtractAlloc(arc, idx, &size);
	if (!bytes) return 0;

	s32 row_count = s_countExistingArchiveJsonRows(bytes, size, schema);
	if (row_count < 0) {
		free(bytes);
		return 0;
	}
	char hash[SHA256_HEX_SIZE];
	s_bytesSha256Hex((const u8 *)bytes, size, hash);
	free(bytes);

	char needle[256];
	snprintf(needle, sizeof(needle), "\"%s\": %d", count_key, row_count);
	if (!s_existingArchiveTextContains(navmesh_text, navmesh_size, needle)) {
		return 0;
	}

	snprintf(needle, sizeof(needle), "\"%s\": \"%s\"", entry, hash);
	return s_existingArchiveTextContains(navmesh_text, navmesh_size, needle);
}

static s32 s_existingPdscenarioGeneratedNavmeshIsCurrent(const char *relpath)
{
	static const struct {
		const char *entry;
		const char *count_key;
		const char *schema;
	} k_counted_sources[] = {
		{ "pads.json", "pads", "pd2.scenario.pads.v1" },
		{ "volumes.json", "volumes", "pd2.scenario.volumes.v1" },
		{ "navigation/waypoints.json", "waypoints", "pd2.scenario.waypoints.v1" },
		{ "navigation/waygroups.json", "waygroups", "pd2.scenario.waygroups.v1" },
		{ "navigation/covers.json", "covers", "pd2.scenario.covers.v1" },
		{ "navigation/paths.json", "paths", "pd2.scenario.paths.v1" },
	};
	static const char *k_hashed_only_sources[] = {
		"scene.glb",
		"collision.obj",
		"navigation.ini",
		"portals.json",
		"spawns.json",
	};

	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return 0;

	mod_archive_t *arc = modArchiveOpen(full);
	if (!arc) return 0;

	s32 nav_idx = modArchiveFindEntry(arc, "_meta/generated-navmesh.json");
	if (nav_idx < 0) {
		modArchiveClose(arc);
		return 0;
	}

	u32 navmesh_size = 0;
	char *navmesh = (char *)modArchiveExtractAlloc(arc, nav_idx,
		&navmesh_size);
	if (!navmesh || navmesh_size == 0) {
		if (navmesh) free(navmesh);
		modArchiveClose(arc);
		return 0;
	}

	s32 ok = s_existingArchiveTextContains(navmesh, navmesh_size,
		"\"source_counts\": {") &&
		s_existingArchiveTextContains(navmesh, navmesh_size,
			"\"source_hashes\": {");

	for (u32 i = 0; ok && i < sizeof(k_counted_sources) /
			sizeof(k_counted_sources[0]); i++) {
		ok = s_existingPdscenarioNavmeshMetadataMatches(arc,
			k_counted_sources[i].entry, k_counted_sources[i].count_key,
			k_counted_sources[i].schema, navmesh, navmesh_size);
	}

	for (u32 i = 0; ok && i < sizeof(k_hashed_only_sources) /
			sizeof(k_hashed_only_sources[0]); i++) {
		const char *entry = k_hashed_only_sources[i];
		s32 idx = modArchiveFindEntry(arc, entry);
		if (idx < 0) {
			ok = 0;
			break;
		}
		u32 size = 0;
		char *bytes = (char *)modArchiveExtractAlloc(arc, idx, &size);
		if (!bytes) {
			ok = 0;
			break;
		}
		char hash[SHA256_HEX_SIZE];
		s_bytesSha256Hex((const u8 *)bytes, size, hash);
		free(bytes);

		char needle[256];
		snprintf(needle, sizeof(needle), "\"%s\": \"%s\"", entry, hash);
		ok = s_existingArchiveTextContains(navmesh, navmesh_size, needle);
	}

	free(navmesh);
	modArchiveClose(arc);
	return ok;
}

static void s_scenarioArchiveRelPath(const char *out_dir, const char *scenario_id,
                                     char *out, size_t out_size)
{
	if (!out || out_size == 0) return;
	out[0] = '\0';
	if (!out_dir || !scenario_id || !scenario_id[0]) return;
	char filename[128];
	s_idToFilename(scenario_id, filename, sizeof(filename));
	snprintf(out, out_size, "%s/%s.pdscenario", out_dir, filename);
}

static void s_removeRelpathIfExists(const char *relpath, const char *kind)
{
	if (!relpath || fsFileSize(relpath) <= 0) return;
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (full && full[0] && remove(full) == 0) {
		sysLogPrintf(LOG_NOTE,
			"romextract %s: removed stale typed-archive zip \"%s\"",
			kind ? kind : "pdasset", relpath);
	}
}

static void s_removeLegacyZipForCatalog(const char *out_dir,
                                        const char *catalog_id,
                                        const char *kind)
{
	if (!out_dir || !catalog_id || !catalog_id[0]) return;
	char filename[128];
	s_idToFilename(catalog_id, filename, sizeof(filename));
	char relpath[FS_MAXPATH];
	snprintf(relpath, sizeof(relpath), "%s/%s.zip", out_dir, filename);
	s_removeRelpathIfExists(relpath, kind);
}

static void s_cleanupLegacyPdarenaZipSiblings(const char *arenas_dir,
                                              const char *scenarios_dir)
{
	for (s32 i = 0; i < g_ArenaDataCount; i++) {
		const arena_authored_record_t *a = &g_ArenaData[i];
		s_removeLegacyZipForCatalog(arenas_dir, a->catalog_id, "pdarena");
		if (s_arenaStageIndexForScenario(a->stagenum) >= 0) {
			char scenario_id[96];
			s_scenarioCatalogIdFromSlug(a->slug, scenario_id,
				sizeof(scenario_id));
			s_removeLegacyZipForCatalog(scenarios_dir, scenario_id,
				"pdscenario");
		}
	}
}

static s32 s_existingPdarenaArchiveIsClean(const char *relpath,
                                           s32 expect_scenario)
{
	if (!relpath || fsFileSize(relpath) <= 0 || !s_existingZipArchive(relpath)) {
		return 0;
	}
	if (s_existingArchiveHasEntryPrefix(relpath, "scenario/") ||
	    s_existingArchiveHasEntryPrefix(relpath, "_meta/scenario/") ||
	    s_existingArchiveHasEntry(relpath, "geometry.obj") ||
	    s_existingArchiveHasEntry(relpath, "setup.ini") ||
	    s_existingArchiveHasEntry(relpath, "pads.ini") ||
	    s_existingArchiveHasEntry(relpath, "arena.mtl")) {
		return 0;
	}
	if (!s_existingArchiveHasEntry(relpath, "arena.ini") ||
	    !s_existingArchiveHasEntry(relpath, "_meta/manifest.json")) {
		return 0;
	}
	if (!expect_scenario) return 1;
	return s_existingArchiveHasEntryPrefix(relpath,
		"dependencies/assets/scenarios/") &&
		s_existingArchiveEntryContains(relpath, "arena.ini",
			"scenario_graph_cache = " ROMEXTRACT_PDSCENARIO_FAST_CACHE_KIND);
}

static s32 s_existingPdscenarioArchiveIsClean(const char *relpath)
{
	if (!relpath || fsFileSize(relpath) <= 0 || !s_existingZipArchive(relpath)) {
		return 0;
	}
	if (s_existingArchiveHasEntryPrefix(relpath, "_meta/_meta/")) {
		return 0;
	}
	if (s_existingArchiveHasEntry(relpath, "rooms.obj") ||
	    s_existingArchiveHasEntry(relpath, "tiles.tsv") ||
	    s_existingArchiveHasEntry(relpath, "scenario.mtl") ||
	    s_existingArchiveHasEntryPrefix(relpath, "visual/")) {
		return 0;
	}
	return s_existingArchiveHasEntry(relpath, "scenario.ini") &&
		s_existingArchiveHasEntry(relpath, "_meta/manifest.json") &&
		s_existingArchiveHasEntry(relpath, "scene.glb") &&
		s_existingArchiveHasEntry(relpath, "collision.obj") &&
		s_existingArchiveHasEntry(relpath, "portals.json") &&
		!s_existingArchiveHasEntry(relpath, "portals.tsv") &&
		s_existingArchiveHasEntry(relpath, "pads.json") &&
		s_existingArchiveHasEntry(relpath, "spawns.json") &&
		s_existingArchiveHasEntry(relpath, "volumes.json") &&
		s_existingArchiveHasEntry(relpath, "navigation/waypoints.json") &&
		s_existingArchiveHasEntry(relpath, "navigation/waygroups.json") &&
		s_existingArchiveHasEntry(relpath, "navigation/covers.json") &&
		!s_existingArchiveHasEntry(relpath, "navigation/waypoints.tsv") &&
		!s_existingArchiveHasEntry(relpath, "navigation/waygroups.tsv") &&
		!s_existingArchiveHasEntry(relpath, "navigation/covers.tsv") &&
		s_existingArchiveEntryContains(relpath, "navigation/waypoints.json",
			"pd2.scenario.waypoints.v1") &&
		s_existingArchiveEntryContains(relpath, "navigation/waygroups.json",
			"pd2.scenario.waygroups.v1") &&
		s_existingArchiveEntryContains(relpath, "navigation/covers.json",
			"pd2.scenario.covers.v1") &&
		s_existingArchiveHasEntry(relpath, "navigation/paths.json") &&
		s_existingArchiveEntryContains(relpath, "navigation/paths.json",
			"pd2.scenario.paths.v1") &&
		!s_existingArchiveHasEntry(relpath, "navigation/paths.tsv") &&
		s_existingArchiveHasEntry(relpath, "objects.json") &&
		!s_existingArchiveHasEntry(relpath, "objects.tsv") &&
		s_existingArchiveEntryContains(relpath, "objects.json",
			"pd2.scenario.objects.v1") &&
		s_existingArchiveHasEntry(relpath, "setup.fields.json") &&
		!s_existingArchiveHasEntry(relpath, "setup.fields.tsv") &&
		s_existingArchiveEntryContains(relpath, "setup.fields.json",
			"pd2.scenario.setup.fields.v1") &&
		s_existingArchiveHasEntry(relpath, "ai/ailists.json") &&
		s_existingArchiveHasEntry(relpath, "objectives.json") &&
		s_existingArchiveEntryContains(relpath, "objectives.json",
			"pd2.scenario.objectives.v1") &&
		!s_existingArchiveHasEntry(relpath, "objectives.tsv") &&
		!s_existingArchiveEntryContains(relpath, "setup.fields.json",
			"objective_step.argument") &&
		s_existingArchiveHasEntry(relpath, "navigation.ini") &&
		s_existingArchiveHasEntry(relpath, "level.graph.json") &&
		s_existingArchiveEntryContains(relpath, "scene.glb",
			PDSCENARIO_BG_VISUAL_EXPORT_VERSION) &&
		s_existingArchiveEntryContains(relpath, "scene.glb",
			"\"texCoord\":0") &&
		s_existingArchiveEntryContains(relpath, "scene.glb",
			"\"TEXCOORD_1\"") &&
		s_existingArchiveEntryContains(relpath, "scene.glb",
			"\"COLOR_0\"") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.global.settings.source") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.portals.source") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"\"portals\": \"portals.json\"") &&
		s_existingArchiveEntryContains(relpath, "scenario.ini",
			"collision_source = collision.obj") &&
		s_existingArchiveEntryContains(relpath, "_meta/manifest.json",
			"\"collision_source\": \"collision.obj\"") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"\"collision\": \"collision.obj\"") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.pads.source") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.lists.source") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_list") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_return_list") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_shot_list") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.return_list") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.stop") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.kneel") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.surrender") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.fade_out") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.remove_chr") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_sidestep") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_jump_out") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_run_sideways") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_attack_walk") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_attack_run") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_attack_roll") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_attack_stand") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_attack_kneel") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_attack_lie") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_attack_locked") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_attacking") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_modify_attack") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.face_entity") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.apply_gset_damage") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_damage_chr") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.consider_grenade_throw") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.drop_item") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_run_from_target") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_jog_to_target_prop") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_walk_to_target_prop") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_run_to_target_prop") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_go_to_cover_prop") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_jog_to_chr") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_walk_to_chr") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_run_to_chr") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_can_hear_alarm") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_patrolling") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_alarm_active") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_gas_active") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_hears_target") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_saw_injury") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_saw_death") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_los_to_target") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_los_to_attack_target") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_target_nearly_in_sight") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_nearly_in_targets_sight") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_pad_preset_to_pad_on_route_to_target") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_saw_target_recently") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_heard_target_recently") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_los_to_chr") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_never_been_on_screen") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_on_screen") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_in_on_screen_room") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_room_is_on_screen") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_target_aiming_at_me") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_near_miss") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_sees_suspicious_item") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_target_in_fov_left") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_check_fov_with_target") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_target_out_of_fov_left") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_target_in_fov") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_target_out_of_fov") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_distance_to_target_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_distance_to_target_greater_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_distance_to_pad_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_distance_to_pad_greater_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_distance_to_chr_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_distance_to_chr_greater_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_any_chr_near_self") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_distance_from_target_to_pad_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_distance_from_target_to_pad_greater_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_in_room") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_target_in_room") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_has_object") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_weapon_thrown") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_weapon_thrown_on_object") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_has_weapon_equipped") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_gun_unclaimed") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_object_healthy") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_activated_object") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.obj_interact") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.destroy_object") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.drop_object_from_chr") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_drop_items") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_drop_weapon") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.give_object_to_chr") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.object_move_to_pad") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_waypoint_within_quadrant") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_pad_preset_to_target_quadrant") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_do_animation") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.be_surprised_one_hand") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.be_surprised_look_around") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.be_surprised_surrender") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.random") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_random_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_random_greater_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_punch_dodge_list") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_shooting_at_me_list") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_dark_room_list") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_player_dead_list") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.navigation.paths.source") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.jog_to_pad") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.go_to_pad_preset") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.walk_to_pad") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.run_to_pad") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_path") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.start_patrol") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_start_alarm") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.activate_alarm") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.deactivate_alarm") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_morale") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.add_morale") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_add_morale") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.subtract_morale") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_alertness") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.add_alertness") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_add_alertness") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.subtract_alertness") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.increase_squadron_alertness") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_hear_distance") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_view_distance") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_grenade_probability") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_chr_num") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_max_damage") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.add_health") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_shield") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_reaction_speed") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_recovery_speed") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_accuracy") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_dodge_rating") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_unarmed_dodge_rating") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.unset_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_has_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_set_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_unset_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_chr_has_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_stage_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.unset_stage_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_stage_flag_eq") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_chrflag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.unset_chrflag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_has_chrflag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_set_chrflag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_unset_chrflag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_chr_has_chrflag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_set_hidden_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_unset_hidden_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_chr_has_hidden_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_obj_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.unset_obj_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_obj_has_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_savefile_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.unset_savefile_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_savefile_flag_set") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_savefile_flag_unset") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.restart_timer") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.reset_timer") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.pause_timer") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.resume_timer") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_timer_stopped") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_timer_greater_than_random") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_timer_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_timer_greater_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.show_countdown_timer") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.hide_countdown_timer") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_countdown_timer") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.stop_countdown_timer") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.start_countdown_timer") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_countdown_timer_stopped") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_countdown_timer_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_countdown_timer_greater_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.show_hudmsg") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.show_hudmsg_middle") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.show_hudmsg_top_middle") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.hovercar_begin_path") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_vehicle_speed") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_rotor_speed") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_explosions") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_tinted_glass_enabled") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.hovercopter_fire_rocket") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_adjust_motion_blur") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.punch_or_kick") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_target_to_eyespy_if_in_sight") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.mini_skedar_try_pounce") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_object_distance_to_pad_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.avoid") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.title_init_mode") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_exit_title") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_emit_sparks") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_dr_caroll_images") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.say_quip") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.say_ci_staff_quip") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.shuffle_ruins_pillars") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.shuffle_pelagic_switches") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.open_door") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.close_door") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_door_state") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_object_is_door") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.lock_door") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.unlock_door") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_door_locked") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_lift_stationary") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.lift_go_to_stop") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_lift_at_stop") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.activate_lift") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_using_lift") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.configure_rain") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.configure_snow") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.switch_to_alt_sky") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_wind_speed") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_lights") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_room_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.show_cutscene_chrs") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.configure_environment") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_distance_to_target2_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_distance_to_target2_greater_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.speak") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.play_sound") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.assign_sound") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.audio_mute_channel") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_channel_free") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_object_sound_volume") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_object_sound_volume_by_distance") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_object_sound_playing") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.play_repeating_sound_from_object") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.play_sound_from_entity") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.play_repeating_sound_from_pad") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_object_sound_volume_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.play_sound_from_prop") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.play_temporary_primary_track") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.play_x_track") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.stop_ambient_track") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_draw_weapon") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_has_no_gun") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_delete_weapon") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_trigger_shot_list") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.end_level") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.end_cutscene") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.warp_jo_to_pad") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.warp_jo_to_tag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.revoke_control") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.grant_control") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.player_fade_in") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.players_fade_out") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_colour_fade_complete") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.prepare_warp_orbit") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.begin_warp_latch") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_warp_latch_complete") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_camera_animation") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_in_cutscene") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_cutscene_button_pressed") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.reorient_for_cutscene_stop") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.spawn_chr_at_pad") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.spawn_chr_at_chr") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_equip_weapon") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_equip_hat") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_obj_image") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.object_do_animation") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_door_open") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.duplicate_chr") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.enable_chr") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.disable_chr") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.enable_obj") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.disable_obj") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_move_to_pad") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_set_team") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.damage_chr_by_amount") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.do_preset_animation") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_player_chr_portal_distance_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_reposition_valid") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.do_gun_command") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_distance_to_gun_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.recover_gun") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_copy_properties") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.player_auto_walk") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_player_auto_walk_finished") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_obj_in_room") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_player_looking_at_object") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_target_is_player") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_kill") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.remove_weapon_from_inventory") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.clear_inventory") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.release_object") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_grab_object") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.toggle_p1p2") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_set_p1p2") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_set_cloaked") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_autogun_target_team") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_objective_complete") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_objective_failed") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_all_objectives_complete") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_difficulty_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_difficulty_greater_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_stage_timer_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_stage_timer_greater_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_stage_id_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_stage_id_greater_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_num_players_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_kill_count_greater_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_num_knocked_out_chrs") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.kill_bond") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_num_arghs_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_num_arghs_greater_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_num_close_arghs_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_num_close_arghs_greater_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_health_greater_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_health_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_shield_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_shield_greater_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_injured") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_shield_damaged") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_morale_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_morale_less_than_random") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_alertness") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_alertness_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_alertness_less_than_random") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_idle") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_stopped") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_dead") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_death_animation_finished") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_knocked_out") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_can_see_target") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_pouncebits_eq") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_training_pc_holographed") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_player_using_device") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_begin_or_end_teleport") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_teleport_full_white") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_set_cutscene_weapon") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.fade_screen") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_fade_complete") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_chr_hudpiece_visible") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_passive_mode") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_set_firing_in_cutscene") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_portal_flag") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_music_event_queue_is_empty") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_coop_mode") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.remove_references_to_chr") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_toggle_model_part") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.obj_set_model_part_visible") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.if_obj_health_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_obj_health") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_chr_special_death_animation") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_room_to_search") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_action") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_team_orders") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.retreat") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.find_cover") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.find_cover_within_dist") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.find_cover_outside_dist") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.go_to_cover") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.check_cover_out_of_sight") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.orbit_target") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_chr_preset_to_unalerted_teammate") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_squadron") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.face_cover") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.danger_cover") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.release_cover") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.rebuild_teams") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.rebuild_squadrons") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_set_listening") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_not_talking") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_orders") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_has_orders") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_in_squadron_doing_action") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_listening") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_not_listening") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_injured_target") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_action") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_ammo_quantity_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_chr_target") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_compare_chr_presets_team") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_human") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_skedar") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_prop_preset_blocking_sight_to_target") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.remove_object_at_prop_preset") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_prop_preset_height_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_target") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_presets_target_is_not_my_target") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_chr_preset_to_chr_near_self") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_chr_preset_to_chr_near_pad") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_dangerous_object_nearby") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_heli_weapons_armed") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_hoverbot_next_step") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.shuffle_investigation_terminals") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_pad_preset_to_investigation_terminal") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.heli_arm_weapons") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.heli_unarm_weapons") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_safety2_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_player_using_cmp_or_ar34") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.detect_enemy_on_same_floor") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.detect_enemy") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_safety_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_target_moving_slowly") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_target_moving_closer") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_target_moving_away") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_squadron_is_dead") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_true") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_num_chrs_in_squadron_greater_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_natural_anim") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_y") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_sound_timer") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.condition.if_target_y_difference_less_than") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.try_attack_amount") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_chr_preset") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_chr_target") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.set_pad_preset") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_set_pad_preset") &&
		s_existingArchiveEntryContains(relpath, "level.graph.json",
			"scenario.ai.action.chr_copy_pad_preset") &&
		s_existingArchiveHasEntry(relpath, "_meta/generated-collision.json") &&
		s_existingArchiveHasEntry(relpath, "_meta/generated-navmesh.json") &&
		s_existingPdscenarioGeneratedNavmeshIsCurrent(relpath);
}

static s32 s_pdarenaOutputsCleanForFastCache(const char *arenas_dir,
                                             const char *scenarios_dir,
                                             const pdscenario_stage_specs_t *stage_specs)
{
	for (s32 i = 0; i < g_ArenaDataCount; i++) {
		const arena_authored_record_t *a = &g_ArenaData[i];
		char filename[128];
		s_idToFilename(a->catalog_id, filename, sizeof(filename));

		char arena_rel[FS_MAXPATH];
		snprintf(arena_rel, sizeof(arena_rel), "%s/%s.pdarena",
			arenas_dir, filename);
		s32 has_scenario = s_arenaStageIndexForScenario(a->stagenum) >= 0;
		if (fsFileSize(arena_rel) > 0 &&
		    !s_existingPdarenaArchiveIsClean(arena_rel, has_scenario)) {
			sysLogPrintf(LOG_NOTE,
				"romextract pdarena: fast-cache blocked by stale archive \"%s\"",
				arena_rel);
			return 0;
		}

		if (has_scenario) {
			char scenario_id[96];
			s_scenarioCatalogIdFromSlug(a->slug, scenario_id,
				sizeof(scenario_id));
			char scenario_rel[FS_MAXPATH];
			s_scenarioArchiveRelPath(scenarios_dir, scenario_id,
				scenario_rel, sizeof(scenario_rel));
			if (fsFileSize(scenario_rel) > 0 &&
			    !s_existingPdscenarioArchiveIsClean(scenario_rel)) {
				sysLogPrintf(LOG_NOTE,
					"romextract pdscenario: fast-cache blocked by stale archive \"%s\"",
					scenario_rel);
				return 0;
			}
		}
	}
	if (stage_specs) {
		for (s32 i = 0; i < stage_specs->count; i++) {
			char scenario_rel[FS_MAXPATH];
			s_scenarioArchiveRelPath(scenarios_dir,
				stage_specs->specs[i].scenario_id,
				scenario_rel, sizeof(scenario_rel));
			if (fsFileSize(scenario_rel) <= 0 ||
			    !s_existingPdscenarioArchiveIsClean(scenario_rel)) {
				sysLogPrintf(LOG_NOTE,
					"romextract pdscenario: fast-cache blocked by missing/stale standalone stage archive \"%s\"",
					scenario_rel);
				return 0;
			}
		}
	}
	return 1;
}

static s32 s_addScenarioArchiveDependency(asset_archive_writer_t *writer,
                                          const char *src_rel,
                                          const char *scenario_id)
{
	if (!writer || !src_rel || !scenario_id || !scenario_id[0]) return -1;
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(src_rel, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return -1;

	char filename[128];
	s_idToFilename(scenario_id, filename, sizeof(filename));
	char dst_name[FS_MAXPATH];
	int n = snprintf(dst_name, sizeof(dst_name),
		"dependencies/assets/scenarios/%s.pdscenario", filename);
	if (n <= 0 || (size_t)n >= sizeof(dst_name)) {
		return -1;
	}
	return assetArchiveWriterAddPublicDisk(writer, dst_name, full,
		"scenario") == MODARCHIVE_OK ? 0 : -1;
}

/* Emit one .pdarena ZIP-openable archive. Returns 1 written, 0 skipped,
 * -1 failed. */
static s32 s_emitOnePdarena(const arena_authored_record_t *a, s32 arena_index,
                             const char *out_dir, const char *scenarios_dir,
                             s32 force_rewrite)
{
	const char *catalog_id = a->catalog_id;
	if (!catalog_id || !catalog_id[0]) return 0;

	char filename[128];
	s_idToFilename(catalog_id, filename, sizeof(filename));

	char relpath[FS_MAXPATH];
	snprintf(relpath, sizeof(relpath), "%s/%s.pdarena", out_dir, filename);

	char scenario_id[96];
	s32 stage_idx = s_arenaStageIndexForScenario(a->stagenum);
	if (stage_idx >= 0) {
		s_scenarioCatalogIdFromSlug(a->slug, scenario_id, sizeof(scenario_id));
	} else {
		scenario_id[0] = '\0';
	}
	char scenario_filename[128];
	if (scenario_id[0]) {
		s_idToFilename(scenario_id, scenario_filename,
			sizeof(scenario_filename));
	} else {
		scenario_filename[0] = '\0';
	}

	char scenario_rel[FS_MAXPATH];
	s_scenarioArchiveRelPath(scenarios_dir, scenario_id,
		scenario_rel, sizeof(scenario_rel));

	if (!force_rewrite &&
	    s_existingPdarenaArchiveIsClean(relpath, scenario_id[0] != '\0')) {
		return 0;
	}

	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) {
		sysLoudFailf("EXTRACT.PDARENA",
			"fsFullPath failed for \"%s\"", relpath);
		return -1;
	}

	const char *load_mode_str = loaderEnumNameForArenaLoadMode(a->load_mode);

	char manifest_buf[1536];
	int manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
		"{\n"
		"  \"pd_kind\": \"arena\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"arena_index\": %d,\n"
		"  \"slug\": \"%s\",\n"
		"  \"category\": \"%s\",\n"
		"  \"stagenum\": %d,\n"
		"  \"requirefeature\": %u,\n"
		"  \"name_langid\": %d,\n",
		catalog_id, arena_index, a->slug, a->category, (s32)a->stagenum,
		(unsigned)a->requirefeature, a->name_langid);
	if (load_mode_str) {
		manifest_len += snprintf(manifest_buf + manifest_len,
			sizeof(manifest_buf) - manifest_len,
			"  \"load_mode\": \"%s\",\n", load_mode_str);
	} else {
		manifest_len += snprintf(manifest_buf + manifest_len,
			sizeof(manifest_buf) - manifest_len,
			"  \"load_mode\": %u,\n", (unsigned)a->load_mode);
	}
	manifest_len += snprintf(manifest_buf + manifest_len,
		sizeof(manifest_buf) - manifest_len,
		scenario_id[0]
			? "  \"scenario\": \"%s\",\n"
			  "  \"scenario_archive\": \"dependencies/assets/scenarios/%s.pdscenario\"\n}\n"
			: "  \"scenario\": null\n}\n",
		scenario_id, scenario_filename);

	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		sysLoudFailf("EXTRACT.PDARENA",
			"manifest snprintf truncated for \"%s\"", catalog_id);
		return -1;
	}

	char ini_buf[1024];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[arena]\n"
		"catalog_id = %s\n"
		"arena_index = %d\n"
		"slug = %s\n"
		"category = %s\n"
		"requirefeature = %u\n"
		"name_langid = %d\n"
		"load_mode = %s\n",
		catalog_id, arena_index, a->slug, a->category,
		(unsigned)a->requirefeature, a->name_langid,
		load_mode_str ? load_mode_str : "ARENA_LOADMODE_PLAYABLE");
	if (scenario_id[0]) {
		ini_len += snprintf(ini_buf + ini_len, sizeof(ini_buf) - ini_len,
			"scenario = %s\n"
			"scenario_archive = dependencies/assets/scenarios/%s.pdscenario\n",
			scenario_id, scenario_filename);
		ini_len += snprintf(ini_buf + ini_len, sizeof(ini_buf) - ini_len,
			"scenario_graph_cache = %s\n",
			ROMEXTRACT_PDSCENARIO_FAST_CACHE_KIND);
	}
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		sysLoudFailf("EXTRACT.PDARENA",
			"arena.ini snprintf truncated for \"%s\"", catalog_id);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDARENA",
			"modArchiveBegin failed for \"%s\"", full);
		return -1;
	}
	asset_archive_writer_t asset_writer;
	if (assetArchiveWriterInit(&asset_writer, aw, "arena", catalog_id) !=
			MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDARENA",
			"assetArchiveWriterInit failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}
	assetArchiveWriterSetProvenance(&asset_writer, "romextract_pdarena",
		"arenadata_authored", arena_index, a->slug);

	if (assetArchiveWriterAddDescriptor(&asset_writer, "arena.ini",
			ini_buf, (u32)ini_len) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDARENA",
			"AddFileMem arena.ini failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}
	if (assetArchiveWriterAddManifestJson(&asset_writer,
			manifest_buf, (u32)manifest_len) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDARENA",
			"AddFileMem _meta/manifest.json failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}
	if (scenario_id[0]) {
		if (!scenario_rel[0] ||
		    s_addScenarioArchiveDependency(&asset_writer,
				scenario_rel, scenario_id) != 0) {
			sysLoudFailf("EXTRACT.PDARENA",
				"scenario dependency copy failed for \"%s\" from \"%s\"",
				full, scenario_rel[0] ? scenario_rel : "(missing)");
			modArchiveAbort(aw);
			return -1;
		}
	}
	if (assetArchiveWriterFinishMetadata(&asset_writer) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDARENA",
			"assetArchiveWriterFinishMetadata failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}
	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDARENA",
			"modArchiveFinish failed for \"%s\"", full);
		return -1;
	}
	return 1;
}

/* Emit one .pdscenario ZIP. Returns 1 written, 0 skipped, -1 failed. */
static s32 s_emitOnePdscenarioSpec(const pdscenario_emit_spec_t *spec,
                                const char *out_dir, s32 force_rewrite)
{
	s32 stage_idx = spec ? spec->stage_idx : -1;
	if (stage_idx < 0) {
		return 0;
	}

	const struct stagetableentry *st = stageGetEntry(stage_idx);
	if (!st) return 0;

	const char *scenario_id = spec->scenario_id;

	char dst_rel[FS_MAXPATH];
	s_scenarioArchiveRelPath(out_dir, scenario_id, dst_rel, sizeof(dst_rel));

	if (!force_rewrite && s_existingPdscenarioArchiveIsClean(dst_rel)) return 0;

	char dst_full_buf[FS_MAXPATH + 1];
	const char *dst_full = fsFullPath(dst_rel, dst_full_buf, sizeof(dst_full_buf));
	if (!dst_full || !dst_full[0]) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"fsFullPath failed for \"%s\"", dst_rel);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"modArchiveBegin failed for \"%s\"", dst_full);
		return -1;
	}
	asset_archive_writer_t asset_writer;
	if (assetArchiveWriterInit(&asset_writer, aw, "scenario",
			scenario_id) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"assetArchiveWriterInit failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		return -1;
	}
	assetArchiveWriterSetProvenance(&asset_writer, "romextract_pdarena",
		spec->provenance_source ? spec->provenance_source : "stagetable",
		spec->provenance_index, spec->slug);

	pdscenario_textbuf_t rooms_obj = { 0 };
	pdscenario_textbuf_t portals_json = { 0 };
	pdscenario_textbuf_t pads_json = { 0 };
	pdscenario_textbuf_t spawns_json = { 0 };
	pdscenario_textbuf_t volumes_json = { 0 };
	pdscenario_textbuf_t waypoints_json = { 0 };
	pdscenario_textbuf_t waygroups_json = { 0 };
	pdscenario_textbuf_t covers_json = { 0 };
	pdscenario_textbuf_t paths_json = { 0 };
	pdscenario_textbuf_t objects_json = { 0 };
	pdscenario_textbuf_t setup_fields_json = { 0 };
	pdscenario_textbuf_t ai_lists_json = { 0 };
	pdscenario_textbuf_t ai_command_nodes_json = { 0 };
	pdscenario_textbuf_t ai_command_links_json = { 0 };
	pdscenario_textbuf_t objectives_json = { 0 };
	pdscenario_textbuf_t navigation_ini = { 0 };
	pdscenario_textbuf_t level_graph_json = { 0 };
	pdscenario_textbuf_t collision_meta_json = { 0 };
	pdscenario_textbuf_t navmesh_meta_json = { 0 };
	pdscenario_visualmesh_t visual_mesh = { 0 };
	pdscenario_bgscene_t bg_scene = { 0 };

	const char *kind = spec->kind[0] ? spec->kind : "solo";
	u32 room_count = 0;
	u32 geo_count = 0;
	u32 tri_count = 0;
	u32 portal_count = 0;
	u32 pad_count = 0;
	u32 waypoint_count = 0;
	u32 waygroup_count = 0;
	u32 cover_count = 0;
	u32 path_count = 0;
	u32 object_count = 0;
	u32 objective_count = 0;
	u32 spawn_count = 0;
	u32 ai_list_count = 0;
	u32 ai_command_count = 0;
	s32 has_visual = 0;

	u8 *tiles_data = NULL;
	u32 tiles_size = 0;
	s32 has_tiles = s_loadStageFilePreprocessed(st->tilefileid, LOADTYPE_TILES,
		&tiles_data, &tiles_size);
	if (has_tiles > 0) {
		if (s_buildTilesExports(tiles_data, tiles_size, &rooms_obj,
				&visual_mesh, &room_count, &geo_count, &tri_count) != 0) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdscenario: tile conversion failed for \"%s\"",
				scenario_id);
			has_tiles = -1;
		}
		sysMemFree(tiles_data);
	}

	u8 *pads_data = NULL;
	u32 pads_size = 0;
	s32 has_pads = s_loadStageFilePreprocessed(st->padsfileid, LOADTYPE_PADS,
		&pads_data, &pads_size);
	if (has_pads > 0) {
		if (s_buildPadsJson(pads_data, pads_size, &pads_json,
				NULL, &volumes_json, &pad_count) != 0) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdscenario: pads conversion failed for \"%s\"",
				scenario_id);
			has_pads = -1;
		}
		if (has_pads > 0 &&
				s_buildNavigationJson(pads_data, pads_size, &waypoints_json,
					&waygroups_json, &covers_json, &waypoint_count,
					&waygroup_count, &cover_count) != 0) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdscenario: navigation table conversion failed for \"%s\"",
				scenario_id);
			has_pads = -1;
		}
		sysMemFree(pads_data);
	}

	u8 *setup_data = NULL;
	u32 setup_size = 0;
	s32 has_setup = s_loadStageFilePreprocessed(st->setupfileid,
		LOADTYPE_SETUP, &setup_data, &setup_size);
	if (has_setup > 0) {
		if (s_buildSetupTables(setup_data, setup_size, &objects_json,
				&objectives_json, &setup_fields_json, &object_count,
				&objective_count) != 0) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdscenario: setup table conversion failed for \"%s\"",
				scenario_id);
			has_setup = -1;
		}
		if (has_setup > 0 &&
				s_buildIntroSpawnsTable(setup_data, setup_size,
					&spawns_json, &spawn_count) != 0) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdscenario: intro spawn conversion failed for \"%s\"",
				scenario_id);
			has_setup = -1;
		}
		if (has_setup > 0 &&
				s_buildAiListsTable(setup_data, setup_size, &ai_lists_json,
					&ai_command_nodes_json, &ai_command_links_json,
					&ai_list_count, &ai_command_count) != 0) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdscenario: AI list conversion failed for \"%s\"",
				scenario_id);
			has_setup = -1;
		}
		if (has_setup > 0 &&
				s_buildPathsJson(setup_data, setup_size, &paths_json,
					&path_count) != 0) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdscenario: path table conversion failed for \"%s\"",
				scenario_id);
			has_setup = -1;
		}
		sysMemFree(setup_data);
	} else {
		if (s_textbufAppend(&objects_json,
				"{\n"
				"  \"schema\": \"pd2.scenario.objects.v1\",\n"
				"  \"rows\": [\n"
				"  ]\n"
				"}\n") != 0 ||
		    s_textbufAppend(&setup_fields_json,
				"{\n"
				"  \"schema\": \"pd2.scenario.setup.fields.v1\",\n"
				"  \"rows\": [\n"
				"  ]\n"
				"}\n") != 0 ||
		    s_textbufAppend(&spawns_json,
				"{\n"
				"  \"schema\": \"pd2.scenario.spawns.v1\",\n"
				"  \"rows\": [\n"
				"  ]\n"
				"}\n") != 0 ||
		    s_textbufAppend(&ai_lists_json,
				"{\n"
				"  \"schema\": \"pd2.scenario.ai.lists.v1\",\n"
				"  \"rows\": [\n"
				"  ]\n"
				"}\n") != 0 ||
		    s_textbufAppend(&objectives_json,
				"{\n"
				"  \"schema\": \"pd2.scenario.objectives.v1\",\n"
				"  \"rows\": [\n"
				"  ]\n"
				"}\n") != 0) {
			has_setup = -1;
		}
		if (s_textbufAppend(&paths_json,
				"{\n"
				"  \"schema\": \"pd2.scenario.paths.v1\",\n"
				"  \"rows\": [\n"
				"  ]\n"
				"}\n") != 0) {
			has_setup = -1;
		}
	}

	u8 *bg_data = NULL;
	u32 bg_size = 0;
	s32 has_bg = s_loadStageFileRaw(st->bgfileid, &bg_data, &bg_size);
	if (has_bg > 0) {
		if (s_buildBgVisualExports(bg_data, bg_size, &bg_scene) == 0) {
			has_visual = 1;
		} else {
			sysLogPrintf(LOG_WARNING,
				"romextract pdscenario: BG visual display-list export failed for \"%s\"",
				scenario_id);
		}
		if (s_buildBgPortalsJson(bg_data, bg_size, &portals_json,
				&portal_count) != 0) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdscenario: portal table conversion failed for \"%s\"",
				scenario_id);
			has_bg = -1;
		}
		sysMemFree(bg_data);
	}

	if (has_visual <= 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdscenario: scene.glb source missing for \"%s\"",
			scenario_id);
		has_bg = -1;
	}

	if (s_buildScenarioSourceFiles(scenario_id, kind, room_count, tri_count,
			portal_count, pad_count, object_count, objective_count,
			ai_list_count, ai_command_count,
			waypoint_count, waygroup_count, cover_count, path_count,
			bg_scene.scene_glb, bg_scene.scene_glb_size, &rooms_obj,
			&portals_json, &pads_json, &spawns_json, &volumes_json,
			&waypoints_json, &waygroups_json, &covers_json, &paths_json,
			&ai_command_nodes_json, &ai_command_links_json,
			&navigation_ini,
			&level_graph_json, &collision_meta_json,
			&navmesh_meta_json) != 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdscenario: source metadata conversion failed for \"%s\"",
			scenario_id);
		has_bg = -1;
	}

	if (has_tiles < 0 || has_pads < 0 || has_setup < 0 || has_bg < 0) {
		s_pdscenarioScratchFree(&rooms_obj, &portals_json, &pads_json,
			&spawns_json, &volumes_json, &waypoints_json, &waygroups_json,
			&covers_json, &paths_json, &objects_json, &setup_fields_json, &ai_lists_json, &ai_command_nodes_json, &ai_command_links_json, &objectives_json,
			&navigation_ini, &level_graph_json, &collision_meta_json,
			&navmesh_meta_json,
			&visual_mesh, &bg_scene);
		modArchiveAbort(aw);
		return -1;
	}

	if (has_visual > 0) {
		if (assetArchiveWriterAddPublicMem(&asset_writer, "scene.glb",
				bg_scene.scene_glb, bg_scene.scene_glb_size,
				"scene") != MODARCHIVE_OK) {
			s_pdscenarioScratchFree(&rooms_obj, &portals_json, &pads_json,
				&spawns_json, &volumes_json, &waypoints_json, &waygroups_json,
				&covers_json, &paths_json, &objects_json, &setup_fields_json, &ai_lists_json, &ai_command_nodes_json, &ai_command_links_json, &objectives_json,
				&navigation_ini, &level_graph_json, &collision_meta_json,
				&navmesh_meta_json,
				&visual_mesh, &bg_scene);
			modArchiveAbort(aw);
			return -1;
		}
	}

	if (has_tiles > 0) {
		if (assetArchiveWriterAddPublicMem(&asset_writer, "collision.obj",
				rooms_obj.data, rooms_obj.len, "collision") != MODARCHIVE_OK) {
			s_pdscenarioScratchFree(&rooms_obj, &portals_json, &pads_json,
				&spawns_json, &volumes_json, &waypoints_json, &waygroups_json,
				&covers_json, &paths_json, &objects_json, &setup_fields_json, &ai_lists_json, &ai_command_nodes_json, &ai_command_links_json, &objectives_json,
				&navigation_ini, &level_graph_json,
				&collision_meta_json, &navmesh_meta_json,
				&visual_mesh, &bg_scene);
			modArchiveAbort(aw);
			return -1;
		}
	}

	if (has_bg > 0) {
		if (assetArchiveWriterAddPublicMem(&asset_writer, "portals.json",
				portals_json.data, portals_json.len, "portals") != MODARCHIVE_OK) {
			s_pdscenarioScratchFree(&rooms_obj, &portals_json, &pads_json,
				&spawns_json, &volumes_json, &waypoints_json, &waygroups_json,
				&covers_json, &paths_json, &objects_json, &setup_fields_json, &ai_lists_json, &ai_command_nodes_json, &ai_command_links_json, &objectives_json,
				&navigation_ini, &level_graph_json,
				&collision_meta_json, &navmesh_meta_json,
				&visual_mesh, &bg_scene);
			modArchiveAbort(aw);
			return -1;
		}
	}

	if (has_pads > 0) {
		if (assetArchiveWriterAddPublicMem(&asset_writer, "pads.json",
				pads_json.data, pads_json.len, "pads") != MODARCHIVE_OK ||
		    assetArchiveWriterAddPublicMem(&asset_writer, "spawns.json",
				spawns_json.data, spawns_json.len, "spawns") != MODARCHIVE_OK ||
		    assetArchiveWriterAddPublicMem(&asset_writer, "volumes.json",
				volumes_json.data, volumes_json.len, "volumes") != MODARCHIVE_OK ||
		    assetArchiveWriterAddPublicMem(&asset_writer,
				"navigation/waypoints.json", waypoints_json.data,
				waypoints_json.len, "waypoints") != MODARCHIVE_OK ||
		    assetArchiveWriterAddPublicMem(&asset_writer,
				"navigation/waygroups.json", waygroups_json.data,
				waygroups_json.len, "waygroups") != MODARCHIVE_OK ||
		    assetArchiveWriterAddPublicMem(&asset_writer,
				"navigation/covers.json", covers_json.data,
				covers_json.len, "covers") != MODARCHIVE_OK) {
			s_pdscenarioScratchFree(&rooms_obj, &portals_json, &pads_json,
				&spawns_json, &volumes_json, &waypoints_json, &waygroups_json,
				&covers_json, &paths_json, &objects_json, &setup_fields_json, &ai_lists_json, &ai_command_nodes_json, &ai_command_links_json, &objectives_json,
				&navigation_ini, &level_graph_json, &collision_meta_json,
				&navmesh_meta_json,
				&visual_mesh, &bg_scene);
			modArchiveAbort(aw);
			return -1;
		}
	}

	if (assetArchiveWriterAddPublicMem(&asset_writer, "objects.json",
			objects_json.data, objects_json.len, "objects") != MODARCHIVE_OK ||
	    assetArchiveWriterAddPublicMem(&asset_writer, "setup.fields.json",
			setup_fields_json.data, setup_fields_json.len, "setup_fields") != MODARCHIVE_OK ||
	    assetArchiveWriterAddPublicMem(&asset_writer, "ai/ailists.json",
			ai_lists_json.data, ai_lists_json.len, "ai_lists") != MODARCHIVE_OK ||
	    assetArchiveWriterAddPublicMem(&asset_writer, "navigation/paths.json",
			paths_json.data, paths_json.len, "paths") != MODARCHIVE_OK ||
	    assetArchiveWriterAddPublicMem(&asset_writer, "objectives.json",
			objectives_json.data, objectives_json.len, "objectives") != MODARCHIVE_OK ||
	    assetArchiveWriterAddPublicMem(&asset_writer, "navigation.ini",
			navigation_ini.data, navigation_ini.len, "navigation") != MODARCHIVE_OK ||
	    assetArchiveWriterAddPublicMem(&asset_writer, "level.graph.json",
			level_graph_json.data, level_graph_json.len, "level_graph") != MODARCHIVE_OK ||
	    assetArchiveWriterAddBundledMem(&asset_writer, "_meta/generated-collision.json",
			collision_meta_json.data, collision_meta_json.len, "generated_collision") != MODARCHIVE_OK ||
	    assetArchiveWriterAddBundledMem(&asset_writer, "_meta/generated-navmesh.json",
			navmesh_meta_json.data, navmesh_meta_json.len, "generated_navmesh") != MODARCHIVE_OK) {
		s_pdscenarioScratchFree(&rooms_obj, &portals_json, &pads_json,
			&spawns_json, &volumes_json, &waypoints_json, &waygroups_json,
			&covers_json, &paths_json, &objects_json, &setup_fields_json, &ai_lists_json, &ai_command_nodes_json, &ai_command_links_json, &objectives_json,
			&navigation_ini, &level_graph_json, &collision_meta_json,
			&navmesh_meta_json,
			&visual_mesh, &bg_scene);
		modArchiveAbort(aw);
		return -1;
	}

	/* Build _meta/manifest.json */

	char manifest_buf[4096];
	int n = 0;
	n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		"{\n"
		"  \"pd_kind\": \"scenario\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"kind\": \"%s\",\n"
		"  \"stagenum\": %d,\n"
		"  \"display_name\": \"%s\",\n"
		"  \"scene\": \"scene.glb\",\n"
		"  \"scene_format\": \"GLB\",\n"
		"  \"runtime_source\": \"scene.glb\",\n"
		"  \"collision_source\": \"collision.obj\",\n"
		"  \"collision_fallback\": \"override\",\n"
		"  \"navigation\": \"navigation.ini\",\n"
		"  \"level_graph\": \"level.graph.json\",\n"
		"  \"objects\": \"objects.json\",\n"
		"  \"setup_fields\": \"setup.fields.json\",\n"
		"  \"ai_lists\": \"ai/ailists.json\",\n"
		"  \"objectives\": \"objectives.json\",\n"
		"  \"generated_collision\": \"_meta/generated-collision.json\",\n"
		"  \"generated_navmesh\": \"_meta/generated-navmesh.json\"",
		scenario_id, kind, spec->stagenum, spec->slug);

	if (has_tiles > 0) n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"room_count\": %u"
		",\n  \"geo_count\": %u"
		",\n  \"triangle_count\": %u",
		(unsigned)room_count, (unsigned)geo_count, (unsigned)tri_count);
	if (has_visual > 0) n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"blender_scene\": \"scene.glb\""
		",\n  \"scene_source_format\": \"GLB\""
		",\n  \"scene_export_version\": \"%s\""
		",\n  \"visual_room_count\": %u"
		",\n  \"visual_gdl_count\": %u"
		",\n  \"visual_triangle_count\": %u"
		",\n  \"visual_texture_count\": %u"
		",\n  \"visual_textures_decoded\": %u"
		",\n  \"visual_textures_failed\": %u",
		PDSCENARIO_BG_VISUAL_EXPORT_VERSION,
		(unsigned)bg_scene.room_count, (unsigned)bg_scene.gdl_count,
		(unsigned)bg_scene.tri_count, (unsigned)bg_scene.texture_count,
		(unsigned)bg_scene.decoded_texture_count,
		(unsigned)bg_scene.failed_texture_count);
	if (has_bg > 0) n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"portals\": \"portals.json\""
		",\n  \"portal_count\": %u",
		(unsigned)portal_count);
	if (has_pads > 0) n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"pads\": \"pads.json\""
		",\n  \"spawns\": \"spawns.json\""
		",\n  \"volumes\": \"volumes.json\""
		",\n  \"waypoints\": \"navigation/waypoints.json\""
		",\n  \"waygroups\": \"navigation/waygroups.json\""
		",\n  \"covers\": \"navigation/covers.json\""
		",\n  \"paths\": \"navigation/paths.json\""
		",\n  \"pad_count\": %u"
		",\n  \"spawn_count\": %u"
		",\n  \"waypoint_count\": %u"
		",\n  \"waygroup_count\": %u"
		",\n  \"cover_count\": %u"
		",\n  \"path_count\": %u",
		(unsigned)pad_count, (unsigned)spawn_count, (unsigned)waypoint_count,
		(unsigned)waygroup_count, (unsigned)cover_count,
		(unsigned)path_count);
	n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"ai_list_count\": %u"
		",\n  \"ai_command_count\": %u",
		(unsigned)ai_list_count, (unsigned)ai_command_count);

	n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n, "\n}\n");

	if (n <= 0 || (size_t)n >= sizeof(manifest_buf)) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"manifest snprintf truncated for \"%s\"", scenario_id);
		s_pdscenarioScratchFree(&rooms_obj, &portals_json, &pads_json,
			&spawns_json, &volumes_json, &waypoints_json, &waygroups_json,
			&covers_json, &paths_json, &objects_json, &setup_fields_json, &ai_lists_json, &ai_command_nodes_json, &ai_command_links_json, &objectives_json,
			&navigation_ini, &level_graph_json, &collision_meta_json,
			&navmesh_meta_json,
			&visual_mesh, &bg_scene);
		modArchiveAbort(aw);
		return -1;
	}

	char ini_buf[2048];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[scenario]\n"
		"catalog_id = %s\n"
		"kind = %s\n"
		"display_name = %s\n"
		"scene_file = scene.glb\n"
		"scene_format = GLB\n"
		"runtime_source_file = scene.glb\n"
		"collision_source = collision.obj\n"
		"collision_fallback = override\n"
		"portals_file = portals.json\n"
		"navigation_file = navigation.ini\n"
		"level_graph_file = level.graph.json\n"
		"objects_file = objects.json\n"
		"setup_fields_file = setup.fields.json\n"
		"ai_lists_file = ai/ailists.json\n"
		"objectives_file = objectives.json\n",
		scenario_id, kind, spec->slug);
	if (has_tiles > 0) ini_len += snprintf(ini_buf + ini_len,
		sizeof(ini_buf) - ini_len,
		"source_room_count = %u\n"
		"source_triangle_count = %u\n",
		(unsigned)room_count, (unsigned)tri_count);
	if (has_visual > 0) ini_len += snprintf(ini_buf + ini_len,
		sizeof(ini_buf) - ini_len,
		"blender_scene_file = scene.glb\n"
		"scene_export_version = " PDSCENARIO_BG_VISUAL_EXPORT_VERSION "\n");
	if (has_pads > 0) ini_len += snprintf(ini_buf + ini_len,
		sizeof(ini_buf) - ini_len,
		"pads_file = pads.json\n"
		"spawns_file = spawns.json\n"
		"volumes_file = volumes.json\n"
		"waypoints_file = navigation/waypoints.json\n"
		"waygroups_file = navigation/waygroups.json\n"
		"covers_file = navigation/covers.json\n"
		"paths_file = navigation/paths.json\n");
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"scenario.ini snprintf truncated for \"%s\"", scenario_id);
		s_pdscenarioScratchFree(&rooms_obj, &portals_json, &pads_json,
			&spawns_json, &volumes_json, &waypoints_json, &waygroups_json,
			&covers_json, &paths_json, &objects_json, &setup_fields_json, &ai_lists_json, &ai_command_nodes_json, &ai_command_links_json, &objectives_json,
			&navigation_ini, &level_graph_json, &collision_meta_json,
			&navmesh_meta_json,
			&visual_mesh, &bg_scene);
		modArchiveAbort(aw);
		return -1;
	}

	if (assetArchiveWriterAddDescriptor(&asset_writer, "scenario.ini",
			ini_buf, (u32)ini_len) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"AddFileMem scenario.ini failed for \"%s\"", dst_full);
		s_pdscenarioScratchFree(&rooms_obj, &portals_json, &pads_json,
			&spawns_json, &volumes_json, &waypoints_json, &waygroups_json,
			&covers_json, &paths_json, &objects_json, &setup_fields_json, &ai_lists_json, &ai_command_nodes_json, &ai_command_links_json, &objectives_json,
			&navigation_ini, &level_graph_json, &collision_meta_json,
			&navmesh_meta_json,
			&visual_mesh, &bg_scene);
		modArchiveAbort(aw);
		return -1;
	}
	if (assetArchiveWriterAddManifestJson(&asset_writer,
			manifest_buf, (u32)n) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"AddFileMem _meta/manifest.json failed for \"%s\"", dst_full);
		s_pdscenarioScratchFree(&rooms_obj, &portals_json, &pads_json,
			&spawns_json, &volumes_json, &waypoints_json, &waygroups_json,
			&covers_json, &paths_json, &objects_json, &setup_fields_json, &ai_lists_json, &ai_command_nodes_json, &ai_command_links_json, &objectives_json,
			&navigation_ini, &level_graph_json, &collision_meta_json,
			&navmesh_meta_json,
			&visual_mesh, &bg_scene);
		modArchiveAbort(aw);
		return -1;
	}
	if (assetArchiveWriterFinishMetadata(&asset_writer) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"assetArchiveWriterFinishMetadata failed for \"%s\"", dst_full);
		s_pdscenarioScratchFree(&rooms_obj, &portals_json, &pads_json,
			&spawns_json, &volumes_json, &waypoints_json, &waygroups_json,
			&covers_json, &paths_json, &objects_json, &setup_fields_json, &ai_lists_json, &ai_command_nodes_json, &ai_command_links_json, &objectives_json,
			&navigation_ini, &level_graph_json, &collision_meta_json,
			&navmesh_meta_json,
			&visual_mesh, &bg_scene);
		modArchiveAbort(aw);
		return -1;
	}

	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"modArchiveFinish failed for \"%s\"", dst_full);
		s_pdscenarioScratchFree(&rooms_obj, &portals_json, &pads_json,
			&spawns_json, &volumes_json, &waypoints_json, &waygroups_json,
			&covers_json, &paths_json, &objects_json, &setup_fields_json, &ai_lists_json, &ai_command_nodes_json, &ai_command_links_json, &objectives_json,
			&navigation_ini, &level_graph_json, &collision_meta_json,
			&navmesh_meta_json,
			&visual_mesh, &bg_scene);
		return -1;
	}
	s_pdscenarioScratchFree(&rooms_obj, &portals_json, &pads_json,
		&spawns_json, &volumes_json, &waypoints_json, &waygroups_json,
		&covers_json, &paths_json, &objects_json, &setup_fields_json, &ai_lists_json, &ai_command_nodes_json, &ai_command_links_json, &objectives_json,
		&navigation_ini, &level_graph_json, &collision_meta_json,
		&navmesh_meta_json,
		&visual_mesh, &bg_scene);
	return 1;
}

static s32 s_emitOnePdscenario(const arena_authored_record_t *a,
                                const char *out_dir, s32 force_rewrite)
{
	pdscenario_emit_spec_t spec;

	if (!a) {
		return 0;
	}
	memset(&spec, 0, sizeof(spec));
	spec.stage_idx = s_arenaStageIndexForScenario(a->stagenum);
	if (spec.stage_idx < 0) {
		/* Random meta arenas carry no scenario. */
		return 0;
	}
	s_scenarioCatalogIdFromSlug(a->slug, spec.scenario_id,
		sizeof(spec.scenario_id));
	strncpy(spec.slug, a->slug, sizeof(spec.slug) - 1);
	strncpy(spec.kind, s_kindFromCategory(a->category), sizeof(spec.kind) - 1);
	spec.stagenum = a->stagenum;
	spec.provenance_source = "stagetable";
	spec.provenance_index = spec.stage_idx;
	return s_emitOnePdscenarioSpec(&spec, out_dir, force_rewrite);
}

/* Engine Phase 4: file-scope fan-out context for the dual-emit pdarena
 * + pdscenario walk. */
typedef struct {
	const char  *arenas_dir;
	const char  *scenarios_dir;
	s32          force_rewrite;
	s32          arena_count;
	s32          count;
	SDL_atomic_t arenas_written;
	SDL_atomic_t arenas_skipped;
	SDL_atomic_t arenas_failed;
	SDL_atomic_t scenarios_written;
	SDL_atomic_t scenarios_skipped;
	SDL_atomic_t scenarios_failed;
	SDL_atomic_t processed;
} pdarena_fanout_ctx_t;

static void s_pdscenarioStageWork(const pdscenario_emit_spec_t *spec,
	pdarena_fanout_ctx_t *c)
{
	s32 r;
	int done;

	if (!spec || !c) {
		return;
	}

	r = s_emitOnePdscenarioSpec(spec, c->scenarios_dir, c->force_rewrite);
	if (r > 0)       SDL_AtomicAdd(&c->scenarios_written, 1);
	else if (r == 0) SDL_AtomicAdd(&c->scenarios_skipped, 1);
	else             SDL_AtomicAdd(&c->scenarios_failed,  1);

	done = SDL_AtomicAdd(&c->processed, 1) + 1;
	if ((done & 0x07) == 0 || done == c->count) {
		bootProgressUpdate(done, c->count);
	}
}

static void s_pdarenaWork(int i, void *user)
{
	pdarena_fanout_ctx_t *c = (pdarena_fanout_ctx_t *)user;
	if (i < 0 || i >= c->arena_count) return;

	const arena_authored_record_t *a = &g_ArenaData[i];

	s32 r2 = s_emitOnePdscenario(a, c->scenarios_dir, c->force_rewrite);
	if (r2 > 0)       SDL_AtomicAdd(&c->scenarios_written, 1);
	else if (r2 == 0) SDL_AtomicAdd(&c->scenarios_skipped, 1);
	else              SDL_AtomicAdd(&c->scenarios_failed,  1);

	s32 r1 = s_emitOnePdarena(a, i, c->arenas_dir, c->scenarios_dir,
		c->force_rewrite);
	if (r1 > 0)       SDL_AtomicAdd(&c->arenas_written, 1);
	else if (r1 == 0) SDL_AtomicAdd(&c->arenas_skipped, 1);
	else              SDL_AtomicAdd(&c->arenas_failed,  1);

	int done = SDL_AtomicAdd(&c->processed, 1) + 1;
	if ((done & 0x07) == 0 || done == c->count) {
		bootProgressUpdate(done, c->count);
	}
}

s32 romExtractAllPdarena(s32 force_rewrite)
{
	/* BYOR completion (2026-05-03): walks g_ArenaData[] from the
	 * authoring source-of-truth (port/src/arenadata_authored.c). */

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDARENA",
			"fsDataDirEnsure failed; cannot create output dir");
		return -1;
	}

	char dataDirBuf[FS_MAXPATH + 1];
	const char *dataDir = fsDataDir(dataDirBuf, sizeof(dataDirBuf));

	char arenas_dir[FS_MAXPATH];
	snprintf(arenas_dir, sizeof(arenas_dir), "%s/arenas", dataDir);
	if (!fsCreateDir(arenas_dir)) {
		sysLoudFailf("EXTRACT.PDARENA",
			"fsCreateDir(\"%s\") failed", arenas_dir);
		return -1;
	}

	char scenarios_dir[FS_MAXPATH];
	snprintf(scenarios_dir, sizeof(scenarios_dir), "%s/scenarios", dataDir);
	if (!fsCreateDir(scenarios_dir)) {
		sysLoudFailf("EXTRACT.PDARENA",
			"fsCreateDir(\"%s\") failed", scenarios_dir);
		return -1;
	}

	s_cleanupLegacyPdarenaZipSiblings(arenas_dir, scenarios_dir);

	pdscenario_stage_specs_t stage_specs;
	memset(&stage_specs, 0, sizeof(stage_specs));
	assetCatalogIterateByType(ASSET_MAP, s_collectStandaloneStageScenario,
		&stage_specs);
	s32 total_work = g_ArenaDataCount + stage_specs.count;

	if (s_pdarenaOutputsCleanForFastCache(arenas_dir, scenarios_dir,
			&stage_specs) &&
	    romExtractPdFastCacheCanSkip(ROMEXTRACT_PDARENA_FAST_CACHE_KIND, arenas_dir,
			".pdarena", force_rewrite) &&
	    romExtractPdFastCacheCanSkip(ROMEXTRACT_PDSCENARIO_FAST_CACHE_KIND, scenarios_dir,
			".pdscenario", force_rewrite)) {
		bootProgressUpdate(total_work, total_work);
		sysLogPrintf(LOG_NOTE,
			"romextract pdarena: arenas written=0 skipped=%d failed=0 total=%d (fast-cache)",
			g_ArenaDataCount, g_ArenaDataCount);
		sysLogPrintf(LOG_NOTE,
			"romextract pdscenario: written=0 skipped=%d failed=0 standalone=%d (fast-cache)",
			g_ArenaDataCount + stage_specs.count, stage_specs.count);
		return 0;
	}

	pdarena_fanout_ctx_t actx;
	memset(&actx, 0, sizeof(actx));
	actx.arenas_dir    = arenas_dir;
	actx.scenarios_dir = scenarios_dir;
	actx.force_rewrite = force_rewrite;
	actx.arena_count   = g_ArenaDataCount;
	actx.count         = total_work;
	SDL_AtomicSet(&actx.arenas_written,    0);
	SDL_AtomicSet(&actx.arenas_skipped,    0);
	SDL_AtomicSet(&actx.arenas_failed,     0);
	SDL_AtomicSet(&actx.scenarios_written, 0);
	SDL_AtomicSet(&actx.scenarios_skipped, 0);
	SDL_AtomicSet(&actx.scenarios_failed,  0);
	SDL_AtomicSet(&actx.processed,         0);

	/* Stage preprocessors share process-global scratch state; emit
	 * scenarios in order rather than running preprocess work in parallel. */
	bootProgressUpdate(0, total_work);
	for (s32 i = 0; i < g_ArenaDataCount; i++) {
		s_pdarenaWork(i, &actx);
	}
	for (s32 i = 0; i < stage_specs.count; i++) {
		s_pdscenarioStageWork(&stage_specs.specs[i], &actx);
	}
	bootProgressUpdate(total_work, total_work);

	s32 arenas_written    = SDL_AtomicGet(&actx.arenas_written);
	s32 arenas_skipped    = SDL_AtomicGet(&actx.arenas_skipped);
	s32 arenas_failed     = SDL_AtomicGet(&actx.arenas_failed);
	s32 scenarios_written = SDL_AtomicGet(&actx.scenarios_written);
	s32 scenarios_skipped = SDL_AtomicGet(&actx.scenarios_skipped);
	s32 scenarios_failed  = SDL_AtomicGet(&actx.scenarios_failed);

	sysLogPrintf(LOG_NOTE,
		"romextract pdarena: arenas written=%d skipped=%d failed=%d total=%d",
		arenas_written, arenas_skipped, arenas_failed, g_ArenaDataCount);
	sysLogPrintf(LOG_NOTE,
		"romextract pdscenario: written=%d skipped=%d failed=%d standalone=%d",
		scenarios_written, scenarios_skipped, scenarios_failed,
		stage_specs.count);

	if (arenas_failed == 0 && scenarios_failed == 0) {
		romExtractPdFastCacheWrite(ROMEXTRACT_PDARENA_FAST_CACHE_KIND,
			arenas_dir, ".pdarena");
		romExtractPdFastCacheWrite(ROMEXTRACT_PDSCENARIO_FAST_CACHE_KIND,
			scenarios_dir, ".pdscenario");
	}

	return arenas_written + scenarios_written;
}
